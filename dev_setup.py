#!/usr/bin/env python3

import os
import venv
from pathlib import Path
from subprocess import check_call, check_output

top_dir = Path(__file__).resolve().parent
build_dir = top_dir / "build"
venv_dir = build_dir / "python_venv"
venv_bin = venv_dir / "bin"

pip_packages = ["meson"]  # Add "conan" once we're using it

apt_packages = [
    "build-essential",
    "direnv",
    "libdrm-dev",
    "libdrm-tests",
    "libfmt-dev",
    "libgflags-dev",
    "libavformat-dev",
    "libv4l-dev",
    "ninja-build",
    "v4l-utils",
]

print("=== Update system packages (sudo apt install ...) ===")
check_call(["sudo", "apt", "install"] + apt_packages)
print()

print(f"=== Set up {venv_dir} ===")
build_dir.mkdir(exist_ok=True)
(build_dir / ".gitignore").open("w").write("/*\n")
venv.create(venv_dir, symlinks=True, with_pip=True)
print()

print(f"=== Update python packages (pip install ...) ===")
check_call([venv_bin / "pip", "install"] + pip_packages)
print()

print(f"=== Set up meson (meson build) ===")
check_call([venv_bin / "meson", build_dir])
print()

print(f"::: Setup complete, build with: ninja -C build :::")
