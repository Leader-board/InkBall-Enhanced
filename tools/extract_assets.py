#!/usr/bin/env python3
"""Re-extract InkBall assets from the user's installer or an extracted game EXE.

Uses only Python's standard library. An NSIS installer additionally needs 7-Zip.
The supplied installer and game are read as data; neither is executed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


class PE:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError("Not a Windows PE executable")
        header = self.u32(0x3C)
        if self.data[header:header + 4] != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        sections = self.u16(header + 6)
        optional_size = self.u16(header + 20)
        optional = header + 24
        magic = self.u16(optional)
        if magic not in (0x10B, 0x20B):
            raise ValueError("Unsupported PE optional header")
        directories = optional + (96 if magic == 0x10B else 112)
        self.resource_rva = self.u32(directories + 16)
        self.sections = []
        for i in range(sections):
            at = optional + optional_size + 40 * i
            virtual_size, virtual_address, raw_size, raw_address = struct.unpack_from("<IIII", self.data, at + 8)
            self.sections.append((virtual_address, max(virtual_size, raw_size), raw_address))
        self.resource_offset = self.offset(self.resource_rva)

    def u16(self, at):
        return struct.unpack_from("<H", self.data, at)[0]

    def u32(self, at):
        return struct.unpack_from("<I", self.data, at)[0]

    def offset(self, rva):
        for address, size, raw in self.sections:
            if address <= rva < address + size:
                return raw + rva - address
        raise ValueError(f"RVA {rva:#x} is outside the PE sections")

    def resources(self):
        result = {}

        def walk(relative, path):
            if len(path) > 3:
                raise ValueError("Invalid resource directory depth")
            at = self.resource_offset + relative
            count = self.u16(at + 12) + self.u16(at + 14)
            for i in range(count):
                name, child = struct.unpack_from("<II", self.data, at + 16 + i * 8)
                if name & 0x80000000:
                    text_at = self.resource_offset + (name & 0x7FFFFFFF)
                    length = self.u16(text_at)
                    key = self.data[text_at + 2:text_at + 2 + length * 2].decode("utf-16-le")
                else:
                    key = name
                if child & 0x80000000:
                    walk(child & 0x7FFFFFFF, path + (key,))
                else:
                    data_at = self.resource_offset + child
                    rva, size = struct.unpack_from("<II", self.data, data_at)
                    raw = self.offset(rva)
                    blob = self.data[raw:raw + size]
                    if len(blob) != size:
                        raise ValueError("Truncated PE resource")
                    result[path + (key,)] = blob

        walk(0, ())
        return result


def resource(resources, kind, identifier):
    matches = [(language, blob) for (typ, name, language), blob in resources.items()
               if typ == kind and name == identifier]
    if not matches:
        raise ValueError(f"Missing resource {kind!r}/{identifier!r}")
    return sorted(matches, key=lambda item: item[0] != 1033)[0][1]


def make_bmp(dib):
    header_size = struct.unpack_from("<I", dib)[0]
    depth = struct.unpack_from("<H", dib, 14)[0]
    compression = struct.unpack_from("<I", dib, 16)[0]
    colors = struct.unpack_from("<I", dib, 32)[0]
    if not colors and depth <= 8:
        colors = 1 << depth
    offset = 14 + header_size + colors * 4 + (12 if header_size == 40 and compression == 3 else 0)
    return struct.pack("<2sIHHI", b"BM", len(dib) + 14, 0, 0, offset) + dib


def make_icon(resources):
    group = resource(resources, 14, 101)
    reserved, kind, count = struct.unpack_from("<HHH", group)
    header = bytearray(struct.pack("<HHH", reserved, kind, count))
    payload = bytearray()
    offset = 6 + count * 16
    for i in range(count):
        w, h, colors, reserved, planes, bits, _, identifier = struct.unpack_from("<BBBBHHIH", group, 6 + 14 * i)
        blob = resource(resources, 3, identifier)
        header.extend(struct.pack("<BBBBHHII", w, h, colors, reserved, planes, bits, len(blob), offset))
        payload.extend(blob)
        offset += len(blob)
    return bytes(header + payload)


def extract(executable, destination):
    pe = PE(executable)
    resources = pe.resources()
    assets = {
        "levels.ikl": resource(resources, "IKL", 20001),
        "sprites.bmp": make_bmp(resource(resources, 2, 501)),
        "inkball.ico": make_icon(resources),
    }
    destination.mkdir(parents=True, exist_ok=True)
    manifest = {"source": executable.name, "source_sha256": hashlib.sha256(pe.data).hexdigest(), "assets": []}
    for name, data in assets.items():
        (destination / name).write_bytes(data)
        manifest["assets"].append({"name": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    print(json.dumps(manifest, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="inkball.exe, inkball_1.exe, or the supplied NSIS installer")
    parser.add_argument("--installer", action="store_true", help="Use 7-Zip to unpack source first; never run it")
    parser.add_argument("--7zip", dest="seven_zip", help="Explicit path to 7z/7zz executable")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "assets")
    args = parser.parse_args()
    if not args.installer:
        extract(args.source, args.output)
        return
    seven_zip = args.seven_zip or shutil.which("7z") or shutil.which("7zz")
    if not seven_zip:
        raise SystemExit("Install 7-Zip and supply --7zip with its executable path, or pass an already extracted inkball.exe.")
    with tempfile.TemporaryDirectory(prefix="inkball-assets-") as temporary:
        subprocess.run([seven_zip, "x", str(args.source.resolve()), "-o" + temporary, "-y"], check=True, stdout=subprocess.DEVNULL)
        candidates = sorted(Path(temporary).rglob("inkball*.exe"))
        for candidate in candidates:
            try:
                extract(candidate, args.output)
                return
            except (ValueError, struct.error):
                continue
        raise SystemExit("No InkBall executable with the expected resources was found in the installer.")


if __name__ == "__main__":
    main()
