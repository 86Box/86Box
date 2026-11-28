#!/usr/bin/env python3
"""Move a PC-98 MS-DOS installation and its files onto a 256-byte SASI HDI.

The destination uses the SASI layout and IPL from a supplied 256-byte image,
but its FAT boot code, DOS system files, and contents come from the source.
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


class ConversionError(Exception):
    pass


def u16(buf: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", buf, offset)[0]


def u32(buf: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", buf, offset)[0]


@dataclass
class FileEntry:
    path: tuple[bytes, ...]
    entry: bytes
    extents: list[tuple[int, int]]


class Hdi:
    def __init__(self, path: Path, writable: bool = False):
        self.path = path
        self.file = path.open("r+b" if writable else "rb")
        header = self.file.read(32)
        if len(header) != 32:
            raise ConversionError(f"{path}: HDI header is truncated")
        self.base, self.data_size, self.bps, self.spt, self.heads, self.cylinders = (
            u32(header, offset) for offset in (8, 12, 16, 20, 24, 28)
        )
        size = os.fstat(self.file.fileno()).st_size
        if (self.base < 32 or self.base + self.data_size != size or
                self.bps not in (256, 512) or not all((self.spt, self.heads, self.cylinders)) or
                self.spt * self.heads * self.cylinders * self.bps != self.data_size):
            raise ConversionError(f"{path}: inconsistent or unsupported HDI geometry")

    def close(self) -> None:
        self.file.close()

    def read(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset + size > self.data_size:
            raise ConversionError(f"{self.path}: read past image boundary at {offset}")
        self.file.seek(self.base + offset)
        data = self.file.read(size)
        if len(data) != size:
            raise ConversionError(f"{self.path}: truncated image at {offset}")
        return data

    def write(self, offset: int, data: bytes | bytearray) -> None:
        if offset < 0 or offset + len(data) > self.data_size:
            raise ConversionError(f"{self.path}: write past image boundary at {offset}")
        self.file.seek(self.base + offset)
        self.file.write(data)

    def partition_start(self) -> int:
        ipl = self.read(0, self.bps)
        if ipl[4:8] != b"IPL1":
            raise ConversionError(f"{self.path}: no PC-98 IPL1 partition table")
        table = self.read(self.bps, self.bps)
        for i in range(0, min(len(table), 512), 32):
            entry = table[i:i + 32]
            if len(entry) < 32 or not (entry[0] & 0x80):
                continue
            sector, head, cylinder = entry[8], entry[9], u16(entry, 10)
            if sector >= self.spt or head >= self.heads or cylinder >= self.cylinders:
                continue
            return ((cylinder * self.heads + head) * self.spt + sector) * self.bps
        raise ConversionError(f"{self.path}: no active PC-98 partition")


class Fat:
    def __init__(self, image: Hdi):
        self.image = image
        self.start = image.partition_start()
        boot = image.read(self.start, 64)
        self.bps = u16(boot, 11)
        self.spc = boot[13]
        self.reserved = u16(boot, 14)
        self.copies = boot[16]
        self.root_entries = u16(boot, 17)
        self.total = u16(boot, 19) or u32(boot, 32)
        self.spf = u16(boot, 22)
        if (self.bps not in (256, 512, 1024, 2048, 4096) or
                self.spc == 0 or self.spc & (self.spc - 1) or
                self.reserved == 0 or self.copies != 2 or self.root_entries == 0 or
                self.spf == 0 or self.total == 0):
            raise ConversionError(f"{image.path}: unsupported FAT BPB")
        self.cluster_size = self.bps * self.spc
        self.root_sectors = math.ceil(self.root_entries * 32 / self.bps)
        self.fat_start = self.start + self.reserved * self.bps
        self.root_start = self.fat_start + self.copies * self.spf * self.bps
        self.data_start = self.root_start + self.root_sectors * self.bps
        self.clusters = (self.total - self.reserved - self.copies * self.spf -
                         self.root_sectors) // self.spc
        self.bits = 12 if self.clusters < 4085 else 16
        self.eoc = 0xFF8 if self.bits == 12 else 0xFFF8
        self.fat = bytearray(image.read(self.fat_start, self.spf * self.bps))
        if (self.start + self.total * self.bps > image.data_size or
                self.clusters < 1 or
                len(self.fat) * 8 // self.bits < self.clusters + 2):
            raise ConversionError(f"{image.path}: FAT volume exceeds image or FAT table")

    def cluster_offset(self, cluster: int) -> int:
        return self.data_start + (cluster - 2) * self.cluster_size

    def get(self, cluster: int) -> int:
        if self.bits == 16:
            return u16(self.fat, cluster * 2)
        pos = cluster + cluster // 2
        value = self.fat[pos] | self.fat[pos + 1] << 8
        return (value >> 4) & 0xFFF if cluster & 1 else value & 0xFFF

    def set(self, cluster: int, value: int) -> None:
        if self.bits == 16:
            struct.pack_into("<H", self.fat, cluster * 2, value)
            return
        pos = cluster + cluster // 2
        pair = self.fat[pos] | self.fat[pos + 1] << 8
        pair = ((pair & 0x000F) | ((value & 0xFFF) << 4) if cluster & 1
                else (pair & 0xF000) | (value & 0xFFF))
        self.fat[pos:pos + 2] = pair.to_bytes(2, "little")

    def chain(self, first: int) -> list[int]:
        if first < 2 or first >= self.clusters + 2:
            raise ConversionError("invalid FAT start cluster")
        result: list[int] = []
        visited: set[int] = set()
        current = first
        while current < self.eoc:
            if current < 2 or current >= self.clusters + 2 or current in visited:
                raise ConversionError("FAT chain loops or leaves the volume")
            visited.add(current)
            result.append(current)
            current = self.get(current)
            if current == 0 or current == (0xFF7 if self.bits == 12 else 0xFFF7):
                raise ConversionError("FAT chain reaches a free or bad cluster")
        return result

    def allocate(self, count: int) -> list[int]:
        free = [c for c in range(2, self.clusters + 2) if self.get(c) == 0]
        if len(free) < count:
            raise ConversionError(f"destination has {len(free)} free clusters; needs {count}")
        selected = free[:count]
        for index, cluster in enumerate(selected):
            self.set(cluster, selected[index + 1] if index + 1 < count else
                     (0xFFF if self.bits == 12 else 0xFFFF))
        return selected

    def directory(self, first: int | None) -> tuple[bytearray, list[int]]:
        if first is None:
            return bytearray(self.image.read(self.root_start, self.root_entries * 32)), []
        chain = self.chain(first)
        return bytearray(b"".join(self.image.read(self.cluster_offset(c), self.cluster_size)
                                  for c in chain)), chain

    def store_directory(self, content: bytearray, chain: list[int]) -> None:
        if not chain:
            self.image.write(self.root_start, content)
        else:
            for index, cluster in enumerate(chain):
                begin = index * self.cluster_size
                self.image.write(self.cluster_offset(cluster), content[begin:begin + self.cluster_size])

    def source_files(self) -> list[FileEntry]:
        files: list[FileEntry] = []
        visited: set[int] = set()

        def visit(path: tuple[bytes, ...], first: int | None, depth: int) -> None:
            if depth > 64:
                raise ConversionError("directory nesting exceeds 64")
            if first is not None:
                if first in visited:
                    raise ConversionError("directory cluster referenced twice")
                visited.add(first)
            directory, _ = self.directory(first)
            for pos in range(0, len(directory), 32):
                entry = bytes(directory[pos:pos + 32])
                if entry[0] == 0:
                    break
                if entry[0] == 0xE5 or entry[11] == 0x0F or entry[11] & 0x08:
                    continue
                name = entry[:11]
                if name in (b".          ", b"..         "):
                    continue
                cluster = u16(entry, 26)
                if entry[11] & 0x10:
                    if cluster:
                        visit(path + (name,), cluster, depth + 1)
                    continue
                size = u32(entry, 28)
                chain = self.chain(cluster) if size else []
                if len(chain) * self.cluster_size < size:
                    raise ConversionError("file chain is shorter than its directory size")
                remaining = size
                extents: list[tuple[int, int]] = []
                for source_cluster in chain:
                    if not remaining:
                        break
                    length = min(remaining, self.cluster_size)
                    extents.append((self.cluster_offset(source_cluster), length))
                    remaining -= length
                files.append(FileEntry(path + (name,), entry, extents))

        visit((), None, 0)
        return files

    def volume_labels(self) -> list[bytes]:
        root = self.image.read(self.root_start, self.root_entries * 32)
        labels = []
        for pos in range(0, len(root), 32):
            entry = root[pos:pos + 32]
            if entry[0] == 0:
                break
            if entry[0] != 0xE5 and entry[11] & 0x08 and entry[11] != 0x0F:
                labels.append(entry)
        return labels

    def find_directory(self, path: tuple[bytes, ...]) -> tuple[bytearray, list[int], int | None]:
        parent: int | None = None
        for name in path:
            content, _ = self.directory(parent)
            match = next((content[pos:pos + 32] for pos in range(0, len(content), 32)
                          if content[pos] not in (0, 0xE5) and
                          content[pos:pos + 11] == name and content[pos + 11] & 0x10), None)
            if match is None:
                cluster = self.allocate(1)[0]
                new_dir = bytearray(self.cluster_size)
                new_dir[:32] = self.make_entry(b".          ", 0x10, cluster)
                new_dir[32:64] = self.make_entry(b"..         ", 0x10, parent or 0)
                self.image.write(self.cluster_offset(cluster), new_dir)
                self.add_entry(parent, self.make_entry(name, 0x10, cluster))
                parent = cluster
            else:
                parent = u16(match, 26)
        content, chain = self.directory(parent)
        return content, chain, parent

    @staticmethod
    def make_entry(name: bytes, attr: int, cluster: int, size: int = 0,
                   original: bytes | None = None) -> bytes:
        entry = bytearray(original if original is not None else bytes(32))
        entry[:11] = name
        entry[11] = attr
        struct.pack_into("<H", entry, 26, cluster)
        struct.pack_into("<I", entry, 28, size)
        return bytes(entry)

    def add_entry(self, parent: int | None, entry: bytes) -> None:
        content, chain = self.directory(parent)
        name = entry[:11]
        for pos in range(0, len(content), 32):
            if content[pos] not in (0, 0xE5) and content[pos:pos + 11] == name:
                raise ConversionError(f"destination already contains {name!r}")
        slot = next((pos for pos in range(0, len(content), 32)
                     if content[pos] in (0, 0xE5)), None)
        if slot is None:
            if parent is None:
                raise ConversionError("destination root directory is full")
            new_cluster = self.allocate(1)[0]
            self.set(chain[-1], new_cluster)
            chain.append(new_cluster)
            content.extend(bytes(self.cluster_size))
            slot = len(content) - self.cluster_size
        content[slot:slot + 32] = entry
        self.store_directory(content, chain)

    def write_file(self, source: Hdi | None, item: FileEntry, payload: bytes | None = None) -> None:
        size = len(payload) if payload is not None else u32(item.entry, 28)
        content, chain, parent = self.find_directory(item.path[:-1])
        for pos in range(0, len(content), 32):
            if content[pos] not in (0, 0xE5) and content[pos:pos + 11] == item.path[-1]:
                raise ConversionError(f"destination already contains {item.path[-1]!r}")
        clusters = self.allocate(math.ceil(size / self.cluster_size))
        data = payload if payload is not None else b"".join(source.read(off, length)
                                                               for off, length in item.extents)
        if len(data) != size:
            raise ConversionError("source file length changed during conversion")
        for index, cluster in enumerate(clusters):
            chunk = data[index * self.cluster_size:(index + 1) * self.cluster_size]
            self.image.write(self.cluster_offset(cluster), chunk.ljust(self.cluster_size, b"\0"))
        self.add_entry(parent, self.make_entry(item.path[-1], item.entry[11] & 0x27,
                                               clusters[0] if clusters else 0, size, item.entry))

    def sync(self) -> None:
        for copy in range(self.copies):
            self.image.write(self.fat_start + copy * len(self.fat), self.fat)

    def clear_for_install(self) -> None:
        """Clear the old FAT volume while retaining its BPB-derived layout."""
        if self.reserved > 1:
            self.image.write(self.start + self.bps,
                             bytes((self.reserved - 1) * self.bps))

        media = self.image.read(self.start + 21, 1)[0]
        self.fat = bytearray(len(self.fat))
        reserved_fat_bytes = 3 if self.bits == 12 else 4
        self.fat[:reserved_fat_bytes] = bytes((media,)) + b"\xff" * (reserved_fat_bytes - 1)

        # Clear the old root and file data in bounded writes. The boot sector
        # and FAT area are rewritten separately.
        offset = self.root_start
        end = self.start + self.total * self.bps
        zeroes = bytes(1024 * 1024)
        while offset < end:
            length = min(len(zeroes), end - offset)
            self.image.write(offset, zeroes[:length])
            offset += length

    def replace_root_file(self, name: bytes, payload: bytes) -> None:
        if len(payload) > self.cluster_size:
            raise ConversionError("startup script exceeds one cluster")
        root, root_chain = self.directory(None)
        pos = next((p for p in range(0, len(root), 32)
                    if root[p] not in (0, 0xE5) and root[p:p + 11] == name), None)
        if pos is None:
            item = FileEntry((name,), self.make_entry(name, 0x20, 0), [])
            self.write_file(None, item, payload=payload)
            return

        first = u16(root, pos + 26)
        chain = self.chain(first) if first else []
        if chain:
            cluster = chain[0]
            for unused in chain[1:]:
                self.set(unused, 0)
        else:
            cluster = self.allocate(1)[0]
        self.set(cluster, 0xFFF if self.bits == 12 else 0xFFFF)
        self.image.write(self.cluster_offset(cluster), payload.ljust(self.cluster_size, b"\0"))
        struct.pack_into("<H", root, pos + 26, cluster)
        struct.pack_into("<I", root, pos + 28, len(payload))
        self.store_directory(root, root_chain)


def build_launch_autoexec(source_script: bytes, launch: bytes) -> bytes:
    """Keep source startup setup and add a launch unless it already runs it."""
    body = source_script[:-1] if source_script.endswith(b"\x1a") else source_script

    def target_name(command: bytes) -> bytes | None:
        command = command.strip()
        if command.startswith(b"@"):
            command = command[1:].lstrip()
        if not command or any(char in command for char in b" \t\r\n"):
            return None
        command = command.strip(b"\"'").replace(b"/", b"\\").rsplit(b"\\", 1)[-1].upper()
        if b"." in command:
            stem, extension = command.rsplit(b".", 1)
            if extension not in (b"BAT", b"COM", b"EXE"):
                return command
            return stem
        return command

    last_command = next((line.strip() for line in reversed(body.splitlines())
                         if line.strip() and not line.lstrip(b"@ ").upper().startswith(
                             (b"REM ", b"REM\t", b"::", b":"))), b"")
    existing_target = target_name(last_command)
    requested_target = target_name(launch)
    if (existing_target is None or requested_target is None or
            existing_target != requested_target):
        if body and not body.endswith((b"\n", b"\r")):
            body += b"\r\n"
        body += launch + b"\r\n"
    return body + b"\x1a"


MSDOS_CORE = (b"IO      SYS", b"MSDOS   SYS", b"COMMAND COM")


def convert(source_path: Path, template_path: Path, output_path: Path,
            launch: bytes | None) -> int:
    source = Hdi(source_path)
    template = Hdi(template_path)
    try:
        if source.bps != 512 or source.spt != 17:
            raise ConversionError("source must be a 17-sector, 512-byte PC-98 HDI")
        if template.bps != 256 or template.spt != 33:
            raise ConversionError("template must be a 33-sector, 256-byte SASI HDI")
        src_volume = Fat(source)
        template_volume = Fat(template)
        files = src_volume.source_files()
        root_files = {f.path[0] for f in files if len(f.path) == 1}
        missing = [name for name in MSDOS_CORE if name not in root_files]
        if missing:
            names = ", ".join(name.decode("ascii").strip() for name in missing)
            raise ConversionError(f"source lacks required root MS-DOS files: {names}")
        if src_volume.bps != template_volume.bps:
            raise ConversionError("source and SASI layout must use the same FAT sector size")
        template_boot = template.read(template_volume.start, template_volume.bps)
        if u16(template_boot, 19) != template_volume.total:
            raise ConversionError("SASI layout must use a 16-bit FAT volume size")

        core_paths = {(name,) for name in MSDOS_CORE}
        ordered_files = ([next(f for f in files if f.path == (name,)) for name in MSDOS_CORE] +
                         [f for f in files if f.path not in core_paths])
        if output_path.exists():
            raise ConversionError(f"output already exists: {output_path}")
        if source_path.resolve() == template_path.resolve():
            raise ConversionError("source and template must be different images")
        staging = output_path.with_name(output_path.name + ".partial")
        if staging.exists():
            raise ConversionError(f"partial output already exists: {staging}")
        try:
            shutil.copyfile(template_path, staging)
            destination = Hdi(staging, writable=True)
            try:
                dst_volume = Fat(destination)
                dst_volume.clear_for_install()
                for label_entry in src_volume.volume_labels():
                    dst_volume.add_entry(None, label_entry)

                # Keep the source DOS jump/loader and the target's compatible
                # FAT parameters. This PC-98 boot sector stores absolute
                # partition LBA, data-area offset, and physical sector size at
                # 24, 28, and 30; those are not the standard FAT geometry fields.
                source_boot = bytearray(source.read(src_volume.start, src_volume.bps))
                target_boot = destination.read(dst_volume.start, dst_volume.bps)
                source_boot[11:24] = target_boot[11:24]
                if dst_volume.start % destination.bps or \
                        (dst_volume.data_start - dst_volume.start) % destination.bps:
                    raise ConversionError("SASI FAT layout is not aligned to physical sectors")

                if src_volume.start % source.bps or \
                        (src_volume.data_start - src_volume.start) % source.bps:
                    raise ConversionError("source FAT layout is not aligned to physical sectors")
                source_partition_lba = src_volume.start // source.bps
                source_data_area_lba = (src_volume.data_start - src_volume.start) // source.bps
                source_metadata = struct.pack("<IHH", source_partition_lba,
                                              source_data_area_lba, source.bps)
                metadata_limit = min(128, len(source_boot)) - len(source_metadata) + 1
                metadata_offsets = [offset for offset in range(24, metadata_limit)
                                    if source_boot[offset:offset + len(source_metadata)] ==
                                    source_metadata]
                if len(metadata_offsets) != 1:
                    raise ConversionError(
                        "could not identify a supported PC-98 MS-DOS boot-sector layout")
                metadata_offset = metadata_offsets[0]
                # Older PC-98 MS-DOS boot sectors place these loader fields at
                # 24. Newer NEC 6.x sectors keep a standard FAT BPB there and
                # place the same loader fields after the extended BPB.
                if metadata_offset > 24:
                    source_boot[24:32] = target_boot[24:32]
                    source_boot[32:36] = target_boot[32:36]

                partition_lba = dst_volume.start // destination.bps
                data_area_lba = (dst_volume.data_start - dst_volume.start) // destination.bps
                if data_area_lba > 0xFFFF:
                    raise ConversionError("SASI data area exceeds the MS-DOS boot loader range")
                struct.pack_into("<IHH", source_boot, metadata_offset, partition_lba,
                                 data_area_lba, destination.bps)
                destination.write(dst_volume.start, source_boot)

                for item in ordered_files:
                    dst_volume.write_file(source, item)
                if launch is not None:
                    source_autoexec = next((item for item in files
                                            if item.path == (b"AUTOEXECBAT",)), None)
                    original_script = (b"" if source_autoexec is None else
                                       b"".join(source.read(offset, length)
                                                for offset, length in source_autoexec.extents))
                    script = build_launch_autoexec(original_script, launch)
                    dst_volume.replace_root_file(b"AUTOEXECBAT", script)
                dst_volume.sync()
                destination.file.flush()
                os.fsync(destination.file.fileno())
            finally:
                destination.close()
            staging.replace(output_path)
        except Exception:
            staging.unlink(missing_ok=True)
            raise
        return len(files)
    finally:
        source.close()
        template.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="17x512 PC-98 HDI")
    parser.add_argument("output", type=Path, help="new 33x256 SASI HDI with source MS-DOS")
    parser.add_argument("--template", type=Path, required=True,
                        help="33x256 SASI HDI providing the IPL, partition and FAT layout")
    parser.add_argument("--launch", metavar="COMMAND", default=None,
                        help="append a DOS launch command after source startup setup, e.g. GAME.BAT")
    args = parser.parse_args()
    try:
        command = args.launch.encode("ascii") if args.launch else None
        count = convert(args.source, args.template, args.output, command)
    except (ConversionError, OSError, UnicodeEncodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(f"Created {args.output} with the source MS-DOS installation and "
          f"{count} migrated files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
