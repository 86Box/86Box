// ZIP and solid 7z reading adapted from rust-dos (GPL-2.0-or-later).
use sevenz_rust2::{Archive, BlockDecoder, Password};
use std::collections::BTreeMap;
use std::ffi::{CStr, CString, c_char, c_void};
use std::fs::File;
use std::io::{self, Read, Seek, SeekFrom};
use std::ptr;
use std::slice;

enum Format {
    Zip(zip::ZipArchive<File>),
    SevenZ(Archive, File),
}

#[derive(Clone, Copy)]
struct Member {
    index: usize,
    size: u64,
}

struct Entry {
    name: CString,
    member: Option<Member>,
    children: BTreeMap<String, usize>,
    next: usize,
    explicit: bool,
}

impl Entry {
    fn new(name: &str, member: Option<Member>, explicit: bool) -> Option<Self> {
        Some(Self {
            name: CString::new(name).ok()?,
            member,
            children: BTreeMap::new(),
            next: 0,
            explicit,
        })
    }
}

struct Directory {
    format: Format,
    entries: Vec<Entry>,
}

fn insert(entries: &mut Vec<Entry>, path: &str, member: Option<Member>) -> Option<()> {
    let path = if member.is_none() {
        path.trim_end_matches(['/', '\\'])
    } else {
        path
    };
    let mut components = path.split(['/', '\\']).peekable();
    let mut parent = 0;
    while let Some(name) = components.next() {
        if name.is_empty() || name == "." || name == ".." || name.contains(':') {
            return None;
        }
        let leaf = components.peek().is_none();
        let file = if leaf { member } else { None };
        if let Some(&id) = entries[parent].children.get(name) {
            let entry = &mut entries[id];
            if entry.member.is_some() || file.is_some() || (leaf && entry.explicit) {
                return None;
            }
            entry.explicit |= leaf;
            parent = id;
        } else {
            let id = entries.len();
            entries.push(Entry::new(name, file, leaf)?);
            entries[parent].children.insert(name.to_owned(), id);
            parent = id;
        }
    }
    Some(())
}

fn open(path: &str) -> Option<Directory> {
    let mut file = File::open(path).ok()?;
    let mut magic = [0; 6];
    file.read_exact(&mut magic).ok()?;
    file.seek(SeekFrom::Start(0)).ok()?;

    let mut entries = vec![Entry::new("", None, true)?];
    let format = if magic == *b"7z\xbc\xaf\x27\x1c" {
        let archive = Archive::read(&mut file, &Password::empty()).ok()?;
        for (index, entry) in archive.files.iter().enumerate() {
            if entry.is_anti_item() || entry.windows_attributes() & 0x400 != 0 {
                continue;
            }
            let member = (!entry.is_directory()).then(|| Member {
                index,
                size: entry.size(),
            });
            insert(&mut entries, entry.name(), member)?;
        }
        Format::SevenZ(archive, file)
    } else {
        let mut archive = zip::ZipArchive::new(file).ok()?;
        for index in 0..archive.len() {
            let entry = archive.by_index(index).ok()?;
            if entry.is_symlink() {
                continue;
            }
            if entry.encrypted() {
                return None;
            }
            let member = (!entry.is_dir()).then(|| Member {
                index,
                size: entry.size(),
            });
            insert(&mut entries, &entry.name().ok()?, member)?;
        }
        Format::Zip(archive)
    };

    for parent in 0..entries.len() {
        let children: Vec<_> = entries[parent].children.values().copied().collect();
        for pair in children.windows(2) {
            entries[pair[0]].next = pair[1];
        }
    }
    Some(Directory { format, entries })
}

fn read_range(reader: &mut dyn Read, offset: u64, buffer: &mut [u8]) -> io::Result<()> {
    if io::copy(&mut reader.take(offset), &mut io::sink())? != offset {
        return Err(io::ErrorKind::UnexpectedEof.into());
    }
    reader.read_exact(buffer)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn media_archive_open(path: *const c_char) -> *mut c_void {
    std::panic::catch_unwind(|| {
        let path = unsafe { CStr::from_ptr(path) }.to_str().ok()?;
        Some(Box::into_raw(Box::new(open(path)?)).cast())
    })
    .ok()
    .flatten()
    .unwrap_or(ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn media_archive_first(handle: *const c_void, parent: usize) -> usize {
    let archive = unsafe { &*handle.cast::<Directory>() };
    archive.entries[parent]
        .children
        .values()
        .next()
        .copied()
        .unwrap_or(0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn media_archive_entry(
    handle: *const c_void,
    id: usize,
    name: *mut *const c_char,
    size: *mut u64,
    is_directory: *mut i32,
) -> usize {
    let archive = unsafe { &*handle.cast::<Directory>() };
    let entry = &archive.entries[id];
    unsafe {
        *name = entry.name.as_ptr();
        *size = entry.member.map_or(0, |member| member.size);
        *is_directory = i32::from(entry.member.is_none());
    }
    entry.next
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn media_archive_read(
    handle: *mut c_void,
    id: usize,
    offset: u64,
    buffer: *mut u8,
    count: usize,
) -> i32 {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let directory = unsafe { &mut *handle.cast::<Directory>() };
        let Some(member) = directory.entries[id].member else {
            return -1;
        };
        if offset > member.size || count as u64 > member.size - offset {
            return -1;
        }
        if count == 0 {
            return 1;
        }
        let buffer = unsafe { slice::from_raw_parts_mut(buffer, count) };
        let ok = match &mut directory.format {
            Format::Zip(archive) => archive
                .by_index(member.index)
                .ok()
                .is_some_and(|mut entry| read_range(&mut entry, offset, buffer).is_ok()),
            Format::SevenZ(archive, file) => {
                let Some(block) = archive.stream_map.file_block_index[member.index] else {
                    return -1;
                };
                let mut index = archive.stream_map.block_first_file_index[block];
                let mut found = false;
                let result = BlockDecoder::new(1, block, archive, &Password::empty(), file)
                    .for_each_entries(&mut |_, reader| {
                        if index == member.index {
                            read_range(reader, offset, buffer)?;
                            found = true;
                            return Ok(false);
                        }
                        index += 1;
                        io::copy(reader, &mut io::sink())?;
                        Ok(true)
                    });
                result.is_ok() && found
            }
        };
        if ok { 1 } else { -1 }
    }))
    .unwrap_or(-1)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn media_archive_close(handle: *mut c_void) {
    if !handle.is_null() {
        unsafe { drop(Box::from_raw(handle.cast::<Directory>())) };
    }
}
