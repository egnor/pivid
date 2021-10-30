#!/usr/bin/env python3

import os
import venv
from pathlib import Path
from subprocess import check_call, check_output

# GENERAL BUILD / DEPENDENCY STRATEGY
# - Use Meson (mesonbuild.com) / Ninja (ninja-build.org) to build the C++ code
# - Use Conan (conan.io) to install C++ dependencies (ffmpeg, etc)
# - Use pip / pypi (pypi.org) for Python dependencies (like conan, meson, etc)
# - Reluctantly use system packages (apt) for things not covered above

source_dir = Path(__file__).resolve().parent
build_dir = source_dir / "build"
venv_dir = build_dir / "python_venv"
venv_bin = venv_dir / "bin"
conan_bin = venv_bin / "conan"
conan_profile = build_dir / "conan_profile.txt"

print("=== System packages (sudo apt install ...) ===")
check_call([
    "sudo", "apt", "install",
    "build-essential",  # conan requires build tools to be systemwide
    "cmake",            # needed by fmt build in conan (hermeticity bug)
    "direnv",           # only useful if installed systemwide
    "libdrm-dev",       # TODO package for conan?
    "libdrm-tests",     # TODO package for conan?
    "libv4l-dev",       # TODO package for conan?
    "v4l-utils",        # TODO package for conan? (not required but handy)
])

print()
print(f"=== Python virtualenv ({venv_dir}) ===")
build_dir.mkdir(exist_ok=True)
(build_dir / ".gitignore").open("w").write("/*\n")
venv.create(venv_dir, symlinks=True, with_pip=True)
check_call(["direnv", "allow", source_dir])

print()
print(f"=== Python packages (pip install ...) ===")
check_call([venv_bin / "pip", "install", "conan", "meson", "ninja"])

print()
print(f"=== Conan (C++) packages (conan install ...) ===")
os.environ["CONAN_V2_MODE"] = "1"
check_call([conan_bin, "config", "init"])
check_call([conan_bin, "profile", "new", "--force", "--detect", conan_profile])
check_call([
    conan_bin, "profile",
    "update", "settings.compiler.libcxx=libstdc++11",
    conan_profile
])

check_call([
    conan_bin, "install",
    f"--install-folder={build_dir}",
    "--update",
    "--build=outdated",
    f"--profile={conan_profile}",
    source_dir
])

print()
print(f"=== Configure build (Meson/Ninja via Conan) ===")
check_call([
    conan_bin, "build",
    f"--build-folder={build_dir}",
    "--configure",  # Only configure, not build (yet)
    source_dir
])

print()
print(f"::: Setup complete, build with: ninja -C build :::")
