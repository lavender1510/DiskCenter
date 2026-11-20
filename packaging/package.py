"""Create and verify DiscCenter's architecture-specific Windows packages."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text(encoding="ascii").strip()
MACHINES = {"x86": 0x014C, "x64": 0x8664}
SHIPPED_DOCS = ["README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "licenses/llvm-mingw.txt", "licenses/nsis.txt"]


def machine(payload):
    if payload[:2] != b"MZ" or len(payload) < 64:
        raise ValueError("Missing DOS header")
    offset = struct.unpack_from("<I", payload, 60)[0]
    if payload[offset:offset + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    return struct.unpack_from("<H", payload, offset + 4)[0]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def names(arch):
    stem = f"DiscCenter-{VERSION}"
    return f"{stem}-Setup-{arch}.exe", f"{stem}-Portable-{arch}.zip"


def verify(folder, arch):
    setup, portable = (folder / name for name in names(arch))
    if machine(setup.read_bytes()) != MACHINES[arch]:
        raise ValueError(f"Wrong setup architecture: {setup.name}")
    with ZipFile(portable) as archive:
        if archive.testzip() is not None:
            raise ValueError(f"Damaged ZIP: {portable.name}")
        if set(archive.namelist()) != {"DiscCenter.exe", "portable.flag", *SHIPPED_DOCS}:
            raise ValueError(f"Unexpected ZIP content: {portable.name}")
        if machine(archive.read("DiscCenter.exe")) != MACHINES[arch]:
            raise ValueError(f"Wrong architecture: {portable.name}")


def build(executable, arch, folder, makensis):
    if machine(executable.read_bytes()) != MACHINES[arch]:
        raise ValueError(f"Wrong architecture: {executable}")
    numeric_version = re.fullmatch(r"(\d+\.\d+\.\d+)(?:-[0-9A-Za-z.-]+)?", VERSION)
    if numeric_version is None:
        raise ValueError("Invalid VERSION")
    folder.mkdir(parents=True, exist_ok=True)
    setup_name, portable_name = names(arch)
    with tempfile.TemporaryDirectory(dir=folder) as staging:
        temporary = Path(staging)
        with ZipFile(temporary / portable_name, "w", ZIP_DEFLATED) as archive:
            archive.write(executable, "DiscCenter.exe")
            archive.writestr("portable.flag", "")
            for item in SHIPPED_DOCS:
                archive.write(ROOT / item, item)
        defines = {"ROOT": ROOT.as_posix(), "ARCH": arch, "VERSION": VERSION,
                   "PE_VERSION": numeric_version[1] + ".0", "APP_EXE": executable.as_posix(),
                   "OUT_EXE": (temporary / setup_name).as_posix()}
        flag = "/D" if os.name == "nt" else "-D"
        subprocess.run([makensis, *(f"{flag}{key}={value}" for key, value in defines.items()),
                        str(ROOT / "installer" / "DiscCenter.nsi")], check=True, cwd=ROOT)
        verify(temporary, arch)
        for name in (setup_name, portable_name):
            (temporary / name).replace(folder / name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    children = parser.add_subparsers(dest="command", required=True)
    make = children.add_parser("build")
    make.add_argument("--arch", choices=MACHINES, required=True)
    make.add_argument("--exe", type=Path, required=True)
    make.add_argument("--out", type=Path, required=True)
    make.add_argument("--makensis", default="makensis")
    check = children.add_parser("checksums")
    check.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "build":
        build(args.exe.resolve(), args.arch, args.out.resolve(), args.makensis)
    else:
        folder = args.out.resolve()
        artifacts = []
        for arch in MACHINES:
            verify(folder, arch)
            artifacts.extend(folder / name for name in names(arch))
        (folder / "SHA256SUMS.txt").write_text(
            "".join(f"{sha256(file)}  {file.name}\n" for file in sorted(artifacts)), encoding="ascii", newline="\n"
        )


if __name__ == "__main__":
    main()
