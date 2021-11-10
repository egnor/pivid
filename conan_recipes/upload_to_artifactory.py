#!/usr/bin/env python3

import os
from pathlib import Path
from subprocess import check_call

recipes_dir = Path(__file__).resolve().parent
venv_bin = recipes_dir.parent / "build" / "python_venv" / "bin"
conan_bin = venv_bin / "conan"

conan_packages = {
    "ffmpeg/4.3+rpi@egnor/pi": recipes_dir / "ffmpeg"
}

print("=== conan export ===")
os.environ["CONAN_UPLOAD_ONLY_RECIPE"] = "1"
for name, dir in sorted(conan_packages.items()):
    print(f"{dir} => {name}")
    check_call([conan_bin, "export", dir, name])
print()

print("=== conan upload ===")
for name, dir in sorted(conan_packages.items()):
    check_call([conan_bin, "upload", name, "-r", "egnor-pi"])
print()
