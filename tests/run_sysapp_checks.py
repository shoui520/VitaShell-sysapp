#!/usr/bin/env python3
"""Run host resource/decoder checks and inspect a built VitaShell Sys VPK."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build-sysapp"

def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)

with tempfile.TemporaryDirectory(prefix="vitashell-checks-") as directory:
    flags = ["-std=c11", "-Wall", "-Wextra", "-Wno-unused-parameter", "-g",
             "-fsanitize=address,undefined", "-Itests/stubs", "-I."]
    cc = os.environ.get("HOST_CC", "cc")
    policy = str(Path(directory) / "policy")
    run(cc, *flags, "sysapp.c", "tests/sysapp_policy.c", "-o", policy)
    run(policy)
    images = str(Path(directory) / "images")
    run(cc, *flags, "-include", "tests/stubs/image_environment.h",
        "sysapp_image.c", "tests/sysapp_images.c", "-lpng", "-ljpeg", "-o", images)
    run(images)

with zipfile.ZipFile(BUILD / "VitaShellSys.vpk") as package:
    assert package.testzip() is None
    sfo = package.read("sce_sys/param.sfo")
    magic, version, keys, data, count = struct.unpack_from("<5I", sfo)
    assert magic == 0x46535000
    params = {}
    for i in range(count):
        key, fmt, length, capacity, offset = struct.unpack_from("<HHIII", sfo, 20 + 16 * i)
        name = sfo[keys + key:].split(b"\0", 1)[0].decode()
        params[name] = sfo[data + offset:data + offset + length].rstrip(b"\0")
    assert params["TITLE_ID"] == b"VTSYS0001", params
    assert params["TITLE"] == b"VitaShell Sys", params
    eboot = package.read("eboot.bin")
    assert eboot[:4] == b"SCE\0"
    control, size = struct.unpack_from("<QQ", eboot, 0x68)
    end = control + size
    found = False
    while control < end:
        kind, length = struct.unpack_from("<II", eboot, control)
        assert length >= 16 and control + length <= end
        if kind == 6:
            used, attr, phycont, total = struct.unpack_from("<4I", eboot, control + 16)
            assert (used, attr, phycont, total) == (1, 0x0E, 0x6800, 0x12800)
            found = True
        control += length
    assert found

# Read the actual ELF symbol values, not just their presence in source code.
elf = (BUILD / "VitaShell").read_bytes()
assert elf[:6] == b"\x7fELF\x01\x01"
section_offset = struct.unpack_from("<I", elf, 32)[0]
entry_size, section_count = struct.unpack_from("<HH", elf, 46)
sections = [struct.unpack_from("<10I", elf, section_offset + i * entry_size)
            for i in range(section_count)]
expected = {"sceUserMainThreadCpuAffinityMask": 0x80000,
            "sceUserMainThreadPriority": 0x10000100,
            "sceUserMainThreadStackSize": 256 * 1024,
            "_newlib_heap_size_user": 16 * 1024 * 1024}
values = {}
for section in sections:
    if section[1] != 2:
        continue
    strings = sections[section[6]]
    strings = elf[strings[4]:strings[4] + strings[5]]
    for offset in range(section[4], section[4] + section[5], section[9]):
        name, value, size, info, other, index = struct.unpack_from("<IIIBBH", elf, offset)
        name = strings[name:].split(b"\0", 1)[0].decode()
        if name in expected:
            target = sections[index]
            values[name] = struct.unpack_from("<I", elf, target[4] + value - target[3])[0]
assert values == expected, values
print("VPK metadata: VTSYS0001, sysapp 0x0E, 74 MiB total / 26 MiB PHYCONT")
print("ELF runtime: CPU3, system priority, 256 KiB main stack, 16 MiB fixed heap")
