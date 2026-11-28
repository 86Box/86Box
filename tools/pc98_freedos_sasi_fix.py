#!/usr/bin/env python3
"""Copy LemonSpin's HDI and install a rebuilt FreeDOS(98) compatibility kernel."""

from __future__ import annotations

import argparse
import hashlib
import math
import os
from pathlib import Path
import shutil
import struct
import sys

from pc98_sasi_convert import ConversionError, Fat, Hdi, u16


ORIGINAL_KERNEL_SHA256 = "f7821402c5b835026e6b0859972ed1c928e244cca66f23216b245b0286b56dff"
KERNEL_NAME = b"KERNEL  SYS"


def install(source: Path, kernel: Path, output: Path) -> None:
    if output.exists() or output.resolve() in (source.resolve(), kernel.resolve()):
        raise ConversionError("output must be a new path distinct from both inputs")
    payload = kernel.read_bytes()
    if payload[2:8] != b"CONFIG" or b"FreeDOS(98)" not in payload:
        raise ConversionError("replacement is not an uncompressed FreeDOS(98) kernel")
    image = Hdi(source)
    try:
        volume = Fat(image)
        files = volume.source_files()
        item = next((entry for entry in files if entry.path == (KERNEL_NAME,)), None)
        if item is None:
            raise ConversionError("source lacks root KERNEL.SYS")
        original = b"".join(image.read(offset, length) for offset, length in item.extents)
        if hashlib.sha256(original).hexdigest() != ORIGINAL_KERNEL_SHA256:
            raise ConversionError("source kernel differs from the supplied LemonSpin release")
    finally:
        image.close()

    # Exclusive creation prevents overwriting an existing disk or partial copy.
    staging = output.with_name(output.name + ".partial")
    staging_created = False
    try:
        with staging.open("xb") as target:
            staging_created = True
            with source.open("rb") as origin:
                shutil.copyfileobj(origin, target)
        image = Hdi(staging, writable=True)
        try:
            volume = Fat(image)
            root, root_chain = volume.directory(None)
            slot = next(pos for pos in range(0, len(root), 32)
                        if root[pos:pos + 11] == KERNEL_NAME and root[pos + 11] != 0x0f)
            for cluster in volume.chain(u16(root, slot + 26)):
                volume.set(cluster, 0)
            clusters = volume.allocate(math.ceil(len(payload) / volume.cluster_size))
            for index, cluster in enumerate(clusters):
                chunk = payload[index * volume.cluster_size:(index + 1) * volume.cluster_size]
                image.write(volume.cluster_offset(cluster), chunk.ljust(volume.cluster_size, b"\0"))
            struct.pack_into("<H", root, slot + 26, clusters[0])
            struct.pack_into("<I", root, slot + 28, len(payload))
            volume.store_directory(root, root_chain)
            volume.sync()
            image.file.flush()
            os.fsync(image.file.fileno())
        finally:
            image.close()
        # On Windows rename fails if a destination appeared during creation.
        staging.rename(output)
    except Exception:
        if staging_created:
            staging.unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("kernel", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        install(args.source, args.kernel, args.output)
    except (OSError, ConversionError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    print(f"Created {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
