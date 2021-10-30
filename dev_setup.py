#!/usr/bin/env python3

import os
import venv
from pathlib import Path
from subprocess import check_call, check_output

source_dir = Path(__file__).resolve().parent
build_dir = source_dir / "build"
venv_dir = build_dir / "python_venv"
venv_bin = venv_dir / "bin"

print("=== System packages (sudo apt install ...) ===")
check_call([
    "sudo", "apt", "install",
    "build-essential",
    "direnv",
    "libdrm-dev",
    "libdrm-tests",
    "libv4l-dev",
    "v4l-utils",
])

print()
print(f"=== Python virtualenv ({venv_dir}) ===")
build_dir.mkdir(exist_ok=True)
(build_dir / ".gitignore").open("w").write("/*\n")
venv.create(venv_dir, symlinks=True, with_pip=True)
check_call(["direnv", "allow", source_dir])

print()
print(f"=== Python packages (pip install ...) ===")
check_call([venv_bin / "pip", "install", "conan"])

print()
print(f"=== Conan packages (conan install ...) ===")
os.environ["CONAN_V2_MODE"] = "1"
conan_bin = venv_bin / "conan"
conan_profile = venv_dir / "conan_profile.txt"
check_call([conan_bin, "config", "init"])
check_call([conan_bin, "profile", "new", "--force", "--detect", conan_profile])
check_call([
    conan_bin, "profile",
    "update", "settings.compiler.libcxx=libstdc++11",
    conan_profile
])
check_call([
    conan_bin, "install",
    f"--install-folder={build_dir}/conan_install",
    "--update",
    "--build=outdated",
    f"--profile={conan_profile}",
    source_dir
])

print()
print(f"=== Meson setup (meson build) ===")
check_call([venv_bin / "meson", build_dir])

print()
print(f"::: Setup complete, build with: ninja -C build :::")
