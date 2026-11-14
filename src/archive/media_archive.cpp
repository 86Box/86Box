/*
 * 86Box    Read-only archive file trees for virtual removable media.
 *
 *          This file is part of the 86Box distribution.
 */

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <7z.h>
#include <7zCrc.h>
#include <Lzma2Dec.h>
#include <LzmaDec.h>
#if __has_include(<minizip/unzip.h>)
#    include <minizip/unzip.h>
#else
#    include <unzip.h>
#endif

#include <86box/media_archive.h>
#include <86box/plat.h>

namespace {

constexpr size_t chunk_size = 64 * 1024;
/* Filtered and PPMd folders use the SDK's whole-folder decoder. Limit both
   that output and individual SDK allocations (including LZMA dictionaries). */
constexpr size_t sdk_memory_limit = 64 * 1024 * 1024;
constexpr UInt32 no_folder = UINT32_MAX;

void *sdk_alloc(ISzAllocPtr, size_t size)
{
    return size <= sdk_memory_limit ? std::malloc(size) : nullptr;
}

void sdk_free(ISzAllocPtr, void *buffer)
{
    std::free(buffer);
}

const ISzAlloc sdk_allocator = {sdk_alloc, sdk_free};

struct ArchiveStream {
    ISeekInStream vt;
    FILE *file;
};

SRes stream_read(ISeekInStreamPtr stream, void *buffer, size_t *size)
{
    auto file = reinterpret_cast<const ArchiveStream *>(stream)->file;
    *size = std::fread(buffer, 1, *size, file);
    return std::ferror(file) ? SZ_ERROR_READ : SZ_OK;
}

SRes stream_seek(ISeekInStreamPtr stream, Int64 *position, ESzSeek origin)
{
    auto file = reinterpret_cast<const ArchiveStream *>(stream)->file;
    const int whence = origin == SZ_SEEK_SET ? SEEK_SET : origin == SZ_SEEK_CUR ? SEEK_CUR : SEEK_END;
    if (fseeko64(file, *position, whence) != 0)
        return SZ_ERROR_READ;
    *position = ftello64(file);
    return *position < 0 ? SZ_ERROR_READ : SZ_OK;
}

size_t read_at(FILE *file, uint64_t offset, void *buffer, size_t count)
{
    if (offset > INT64_MAX || fseeko64(file, offset, SEEK_SET) != 0)
        return 0;
    return std::fread(buffer, 1, count, file);
}

/* Reuse the emulator's UTF-8-aware file open and keep ownership of the
   FILE in Archive when minizip opens or closes its stream. */
voidpf ZCALLBACK zip_open(voidpf file, const void *, int)
{
    return fseeko64(static_cast<FILE *>(file), 0, SEEK_SET) == 0 ? file : nullptr;
}

uLong ZCALLBACK zip_read(voidpf, voidpf file, void *buffer, uLong count)
{
    return std::fread(buffer, 1, count, static_cast<FILE *>(file));
}

ZPOS64_T ZCALLBACK zip_tell(voidpf, voidpf file)
{
    return ftello64(static_cast<FILE *>(file));
}

long ZCALLBACK zip_seek(voidpf, voidpf file, ZPOS64_T offset, int origin)
{
    switch (origin) {
        case ZLIB_FILEFUNC_SEEK_SET: return offset > INT64_MAX ? -1 : fseeko64(static_cast<FILE *>(file), offset, SEEK_SET);
        case ZLIB_FILEFUNC_SEEK_CUR: return fseeko64(static_cast<FILE *>(file), static_cast<int64_t>(offset), SEEK_CUR);
        case ZLIB_FILEFUNC_SEEK_END: return fseeko64(static_cast<FILE *>(file), static_cast<int64_t>(offset), SEEK_END);
        default: return -1;
    }
}

int ZCALLBACK zip_close(voidpf, voidpf)
{
    return 0;
}

int ZCALLBACK zip_error(voidpf, voidpf file)
{
    return std::ferror(static_cast<FILE *>(file));
}

bool valid_utf8(std::string_view name)
{
    for (size_t offset = 0; offset < name.size();) {
        const auto first = static_cast<unsigned char>(name[offset++]);
        if (first < 0x80)
            continue;
        size_t count;
        unsigned codepoint;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1;
            codepoint = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2;
            codepoint = first & 0xf;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3;
            codepoint = first & 7;
        } else {
            return false;
        }
        if (count > name.size() - offset)
            return false;
        for (size_t i = 0; i < count; i++) {
            const auto next = static_cast<unsigned char>(name[offset++]);
            if ((next & 0xc0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((count == 2 && codepoint < 0x800) || (count == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff)
            return false;
    }
    return true;
}

void append_utf8(std::string &name, unsigned codepoint)
{
    if (codepoint < 0x80) {
        name += static_cast<char>(codepoint);
        return;
    }
    if (codepoint >= 0x10000) {
        name += static_cast<char>(0xf0 | (codepoint >> 18));
        name += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
    } else if (codepoint >= 0x800) {
        name += static_cast<char>(0xe0 | (codepoint >> 12));
    } else {
        name += static_cast<char>(0xc0 | (codepoint >> 6));
    }
    if (codepoint >= 0x800)
        name += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
    name += static_cast<char>(0x80 | (codepoint & 0x3f));
}

std::string zip_name(std::string_view bytes, bool utf8)
{
    if (utf8)
        return std::string(bytes);
    static constexpr uint16_t cp437[] = {
        0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
        0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
        0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
        0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
        0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
        0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
        0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
        0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
        0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
        0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
        0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
        0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
        0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
        0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0
    };
    std::string name;
    for (unsigned char byte : bytes)
        append_utf8(name, byte < 128 ? byte : cp437[byte - 128]);
    return name;
}

bool seven_name(const CSzArEx &archive, UInt32 index, std::string &name)
{
    const size_t length = SzArEx_GetFileNameUtf16(&archive, index, nullptr);
    if (!length)
        return false;
    std::vector<UInt16> utf16(length);
    SzArEx_GetFileNameUtf16(&archive, index, utf16.data());
    for (size_t i = 0; i < length - 1; i++) {
        unsigned codepoint = utf16[i];
        if (!codepoint)
            return false;
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (++i >= length - 1 || utf16[i] < 0xdc00 || utf16[i] > 0xdfff)
                return false;
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (utf16[i] - 0xdc00);
        } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            return false;
        }
        append_utf8(name, codepoint);
    }
    return true;
}

struct Entry {
    std::string name;
    std::map<std::string, size_t, std::less<>> children;
    uint64_t size = 0;
    size_t next = 0;
    UInt32 index = 0;
    bool directory = true;
    bool explicit_entry = false;
};

enum class Method { none, copy, lzma, lzma2, buffered };

class Archive {
public:
    explicit Archive(FILE *file) : stream{{stream_read, stream_seek}, file}
    {
        SzArEx_Init(&seven);
        LzmaDec_Construct(&lzma);
        Lzma2Dec_Construct(&lzma2);
    }

    ~Archive()
    {
        if (zip_member_open)
            unzCloseCurrentFile(zip);
        if (zip)
            unzClose(zip);
        reset_seven();
        SzArEx_Free(&seven, &sdk_allocator);
        std::fclose(stream.file);
    }

    bool open();
    bool read(const Entry &entry, uint64_t offset, uint8_t *buffer, size_t count);
    std::vector<Entry> entries{1};

private:
    bool insert(std::string_view path, UInt32 index, uint64_t size, bool directory);
    bool list_zip();
    bool list_seven();
    bool read_zip(const Entry &entry, uint64_t offset, uint8_t *buffer, size_t count);
    bool read_seven(const Entry &entry, uint64_t offset, uint8_t *buffer, size_t count);
    bool start_seven(UInt32 folder);
    bool read_seven_bytes(uint8_t *buffer, size_t count);
    void reset_seven();

    ArchiveStream stream;
    unzFile zip = nullptr;
    std::vector<unz64_file_pos> zip_members;
    bool zip_member_open = false;
    UInt32 zip_index = 0;
    uint64_t zip_position = 0;

    CSzArEx seven;
    CLookToRead2 look{};
    std::array<Byte, chunk_size> look_buffer;
    std::array<Byte, chunk_size> discard;
    CLzmaDec lzma;
    CLzma2Dec lzma2;
    std::vector<Byte> folder_buffer;
    Method method = Method::none;
    UInt32 active_folder = no_folder;
    uint64_t unpack_position = 0;
    uint64_t packed_remaining = 0;
    uint64_t packed_start = 0;
};

bool Archive::insert(std::string_view path, UInt32 index, uint64_t size, bool directory)
{
    if (directory && !path.empty() && (path.back() == '/' || path.back() == '\\'))
        path.remove_suffix(1);
    size_t parent = 0;
    for (;;) {
        const size_t separator = path.find_first_of("/\\");
        const auto name = path.substr(0, separator);
        if (name.empty() || name == "." || name == ".." ||
            name.find(':') != std::string_view::npos || name.find('\0') != std::string_view::npos)
            return false;

        const bool leaf = separator == std::string_view::npos;
        const bool is_dir = !leaf || directory;
        auto &children = entries[parent].children;
        auto it = children.find(name);
        if (it == children.end()) {
            it = children.emplace(std::string(name), entries.size()).first;
            parent = it->second;
            Entry entry;
            entry.name = name;
            entry.directory = is_dir;
            entry.explicit_entry = leaf;
            if (leaf) {
                entry.index = index;
                entry.size = size;
            }
            entries.push_back(std::move(entry));
        } else {
            parent = it->second;
            auto &entry = entries[parent];
            if (!entry.directory || !is_dir || (leaf && entry.explicit_entry))
                return false;
            entry.explicit_entry |= leaf;
        }
        if (leaf)
            return true;
        path.remove_prefix(separator + 1);
    }
}

bool Archive::list_zip()
{
    zlib_filefunc64_def callbacks{};
    callbacks.zopen64_file = zip_open;
    callbacks.zread_file = zip_read;
    callbacks.ztell64_file = zip_tell;
    callbacks.zseek64_file = zip_seek;
    callbacks.zclose_file = zip_close;
    callbacks.zerror_file = zip_error;
    callbacks.opaque = stream.file;
    zip = unzOpen2_64(nullptr, &callbacks);
    if (!zip)
        return false;

    int result = unzGoToFirstFile(zip);
    for (; result == UNZ_OK; result = unzGoToNextFile(zip)) {
        unz_file_info64 info{};
        if (unzGetCurrentFileInfo64(zip, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK)
            return false;
        const auto mode = info.external_fa >> 16;
        if ((mode & 0170000) == 0120000)
            continue;
        if ((info.flag & 1) || (info.compression_method != 0 && info.compression_method != Z_DEFLATED))
            return false;
        std::string bytes(info.size_filename, '\0');
        if (unzGetCurrentFileInfo64(zip, nullptr, bytes.data(), info.size_filename,
                                    nullptr, 0, nullptr, 0) != UNZ_OK)
            return false;
        const bool directory = (info.external_fa & 0x10) || (mode & 0170000) == 0040000 ||
                               (!bytes.empty() && (bytes.back() == '/' || bytes.back() == '\\'));
        const std::string name = zip_name(bytes, (info.flag & (1 << 11)) != 0);
        unz64_file_pos position;
        if (zip_members.size() >= UINT32_MAX || unzGetFilePos64(zip, &position) != UNZ_OK ||
            !valid_utf8(name) || !insert(name, static_cast<UInt32>(zip_members.size()),
                                         directory ? 0 : info.uncompressed_size, directory))
            return false;
        zip_members.push_back(position);
    }
    return result == UNZ_END_OF_LIST_OF_FILE;
}

bool Archive::list_seven()
{
    /* Initialization of the shared SDK CRC tables is thread-safe. */
    static const bool crc_ready = [] { CrcGenerateTable(); return true; }();
    (void) crc_ready;
    LookToRead2_CreateVTable(&look, False);
    look.realStream = &stream.vt;
    look.buf = look_buffer.data();
    look.bufSize = look_buffer.size();
    LookToRead2_INIT(&look)
    if (fseeko64(stream.file, 0, SEEK_SET) != 0 ||
        SzArEx_Open(&seven, &look.vt, &sdk_allocator, &sdk_allocator) != SZ_OK)
        return false;

    for (UInt32 index = 0; index < seven.NumFiles; index++) {
        if (SzBitWithVals_Check(&seven.Attribs, index)) {
            const UInt32 attributes = seven.Attribs.Vals[index];
            if ((attributes & 0x400) || ((attributes >> 16) & 0170000) == 0120000)
                continue;
        }
        std::string name;
        const bool directory = SzArEx_IsDir(&seven, index);
        if (!seven_name(seven, index, name) ||
            !insert(name, index, directory ? 0 : SzArEx_GetFileSize(&seven, index), directory))
            return false;
    }
    return true;
}

bool Archive::open()
{
    Byte magic[k7zSignatureSize];
    if (std::fread(magic, 1, sizeof(magic), stream.file) != sizeof(magic))
        return false;
    if (!(std::memcmp(magic, k7zSignature, sizeof(magic)) == 0 ? list_seven() : list_zip()))
        return false;
    for (auto &entry : entries) {
        size_t previous = 0;
        for (const auto &child : entry.children) {
            if (previous)
                entries[previous].next = child.second;
            previous = child.second;
        }
    }
    return true;
}

bool Archive::read_zip(const Entry &entry, uint64_t offset, uint8_t *buffer, size_t count)
{
    if (!zip_member_open || zip_index != entry.index || offset < zip_position) {
        if (zip_member_open)
            unzCloseCurrentFile(zip);
        zip_member_open = unzGoToFilePos64(zip, &zip_members[entry.index]) == UNZ_OK &&
                          unzOpenCurrentFile(zip) == UNZ_OK;
        if (!zip_member_open)
            return false;
        zip_index = entry.index;
        zip_position = 0;
    }
    while (zip_position < offset) {
        const size_t skip = std::min<uint64_t>(discard.size(), offset - zip_position);
        if (unzReadCurrentFile(zip, discard.data(), static_cast<unsigned>(skip)) != static_cast<int>(skip))
            break;
        zip_position += skip;
    }
    size_t remaining = count;
    while (zip_position >= offset && remaining) {
        const auto chunk = static_cast<int>(std::min(remaining, chunk_size));
        const int received = unzReadCurrentFile(zip, buffer, chunk);
        if (received <= 0)
            break;
        zip_position += received;
        remaining -= received;
        buffer += received;
    }
    if (remaining || zip_position == entry.size) {
        const int result = unzCloseCurrentFile(zip);
        zip_member_open = false;
        return !remaining && result == UNZ_OK;
    }
    return true;
}

void Archive::reset_seven()
{
    LzmaDec_Free(&lzma, &sdk_allocator);
    Lzma2Dec_Free(&lzma2, &sdk_allocator);
    std::vector<Byte>().swap(folder_buffer);
    active_folder = no_folder;
    method = Method::none;
}

bool Archive::start_seven(UInt32 folder_id)
{
    reset_seven();

    const auto &db = seven.db;
    const size_t start = db.FoCodersOffsets[folder_id];
    const size_t end = db.FoCodersOffsets[folder_id + 1];
    CSzData data{db.CodersData + start, end - start};
    CSzFolder folder;
    if (SzGetNextFolderItem(&folder, &data) != SZ_OK || !folder.NumCoders)
        return false;
    const auto &coder = folder.Coders[0];
    const Byte *props = db.CodersData + start + coder.PropsOffset;
    if (folder.NumCoders == 1 && folder.NumPackStreams == 1) {
        switch (coder.MethodID) {
            case 0:
                method = Method::copy;
                break;
            case 0x30101:
                if (LzmaDec_Allocate(&lzma, props, coder.PropsSize, &sdk_allocator) != SZ_OK)
                    return false;
                LzmaDec_Init(&lzma);
                method = Method::lzma;
                break;
            case 0x21:
                if (coder.PropsSize != 1 || Lzma2Dec_Allocate(&lzma2, *props, &sdk_allocator) != SZ_OK)
                    return false;
                Lzma2Dec_Init(&lzma2);
                method = Method::lzma2;
                break;
        }
    }
    if (method == Method::none) {
        const uint64_t size = SzAr_GetFolderUnpackSize(&db, folder_id);
        if (size > sdk_memory_limit)
            return false;
        folder_buffer.resize(static_cast<size_t>(size));
        if (SzAr_DecodeFolder(&db, folder_id, &look.vt, seven.dataPos,
                              folder_buffer.data(), folder_buffer.size(), &sdk_allocator) != SZ_OK) {
            reset_seven();
            return false;
        }
        method = Method::buffered;
    } else {
        const UInt32 pack = db.FoStartPackStreamIndex[folder_id];
        const uint64_t start_offset = db.PackPositions[pack];
        packed_remaining = db.PackPositions[pack + 1] - start_offset;
        if (start_offset > db.PackPositions[pack + 1] || start_offset > INT64_MAX ||
            seven.dataPos > INT64_MAX - start_offset)
            return false;
        packed_start = seven.dataPos + start_offset;
        if (method == Method::copy && packed_remaining != SzAr_GetFolderUnpackSize(&db, folder_id))
            return false;
        Int64 position = packed_start;
        if (ILookInStream_Seek(&look.vt, &position, SZ_SEEK_SET) != SZ_OK)
            return false;
    }

    unpack_position = 0;
    active_folder = folder_id;
    return true;
}

bool Archive::read_seven_bytes(uint8_t *buffer, size_t count)
{
    while (count) {
        size_t input_size = std::min<uint64_t>(packed_remaining, chunk_size);
        const void *input;
        if (ILookInStream_Look(&look.vt, &input, &input_size) != SZ_OK)
            return false;
        size_t output_size = count;
        ELzmaStatus status;
        SRes result = SZ_OK;
        switch (method) {
            case Method::lzma:
                result = LzmaDec_DecodeToBuf(&lzma, buffer, &output_size,
                    static_cast<const Byte *>(input), &input_size, LZMA_FINISH_ANY, &status);
                break;
            case Method::lzma2:
                result = Lzma2Dec_DecodeToBuf(&lzma2, buffer, &output_size,
                    static_cast<const Byte *>(input), &input_size, LZMA_FINISH_ANY, &status);
                break;
            default:
                return false;
        }
        if (result != SZ_OK || (!input_size && !output_size) ||
            ILookInStream_Skip(&look.vt, input_size) != SZ_OK)
            return false;
        packed_remaining -= input_size;
        unpack_position += output_size;
        count -= output_size;
        buffer += output_size;
    }
    return true;
}

bool Archive::read_seven(const Entry &entry, uint64_t offset, uint8_t *buffer, size_t count)
{
    const UInt32 folder = seven.FileToFolder[entry.index];
    if (folder == no_folder || folder >= seven.db.NumFolders)
        return false;
    const UInt32 first = seven.FolderToFile[folder];
    const uint64_t start = seven.UnpackPositions[entry.index] - seven.UnpackPositions[first];
    const uint64_t size = SzAr_GetFolderUnpackSize(&seven.db, folder);
    if (start > size || offset > size - start || count > size - start - offset)
        return false;
    const uint64_t target = start + offset;
    if ((active_folder != folder || (method != Method::buffered && target < unpack_position)) &&
        !start_seven(folder))
        return false;
    if (method == Method::buffered) {
        std::memcpy(buffer, folder_buffer.data() + target, count);
        return true;
    }
    if (method == Method::copy) {
        return target <= INT64_MAX && packed_start <= INT64_MAX - target &&
               read_at(stream.file, packed_start + target, buffer, count) == count;
    }
    while (unpack_position < target) {
        if (!read_seven_bytes(discard.data(), std::min<uint64_t>(discard.size(), target - unpack_position))) {
            reset_seven();
            return false;
        }
    }
    if (!read_seven_bytes(buffer, count)) {
        reset_seven();
        return false;
    }
    return true;
}

bool Archive::read(const Entry &entry, uint64_t offset, uint8_t *buffer, size_t count)
{
    if (entry.directory || offset > entry.size || count > entry.size - offset)
        return false;
    return !count || (zip ? read_zip(entry, offset, buffer, count)
                                  : read_seven(entry, offset, buffer, count));
}

} // namespace

extern "C" void *
media_archive_open(const char *path)
{
    try {
        std::unique_ptr<FILE, decltype(&std::fclose)> file(plat_fopen64(path, "rb"), &std::fclose);
        if (!file)
            return nullptr;
        auto archive = std::make_unique<Archive>(file.get());
        file.release();
        return archive->open() ? archive.release() : nullptr;
    } catch (const std::exception &) {
        return nullptr;
    }
}

extern "C" size_t
media_archive_first(const void *handle, size_t parent)
{
    const auto &children = static_cast<const Archive *>(handle)->entries[parent].children;
    return children.empty() ? 0 : children.begin()->second;
}

extern "C" size_t
media_archive_entry(const void *handle, size_t id, const char **name, uint64_t *size, int *is_directory)
{
    const auto &entry = static_cast<const Archive *>(handle)->entries[id];
    *name = entry.name.c_str();
    *size = entry.size;
    *is_directory = entry.directory;
    return entry.next;
}

extern "C" int
media_archive_read(void *handle, size_t id, uint64_t offset, uint8_t *buffer, size_t count)
{
    try {
        auto &archive = *static_cast<Archive *>(handle);
        return archive.read(archive.entries[id], offset, buffer, count) ? 1 : -1;
    } catch (const std::exception &) {
        return -1;
    }
}

extern "C" void
media_archive_close(void *handle)
{
    delete static_cast<Archive *>(handle);
}
