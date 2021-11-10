#!/usr/bin/env python3

import os
from pathlib import Path
from subprocess import check_call

recipe_dir = Path(__file__).resolve().parent
venv_bin = recipe_dir.parent / "build" / "python_venv" / "bin"
conan_bin = venv_bin / "conan"
conan_package_name = "ffmpeg/4.4@egnor/pi"

print("=== conan export ===")
os.environ["CONAN_UPLOAD_ONLY_RECIPE"] = "1"
check_call([conan_bin, "export", recipe_dir, conan_package_name])
print()

print("=== conan upload ===")
check_call([conan_bin, "upload", conan_package_name, "-r", "egnor-pi"])
print()
