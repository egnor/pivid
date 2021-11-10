#!/usr/bin/env python3

import os
import venv
from pathlib import Path
from subprocess import check_call, check_output

# GENERAL BUILD / DEPENDENCY STRATEGY
# - Use Meson (mesonbuild.com) and Ninja (ninja-build.org) to build C++
# - Use Conan (conan.io) to install C++ dependencies (ffmpeg, etc)
# - Use pip / pypi (pypi.org) for Python dependencies (like conan, meson, etc)
# - Reluctantly use system packages (apt) for things not covered above

source_dir = Path(__file__).resolve().parent
build_dir = source_dir / "build"

print("=== System packages (sudo apt install ...) ===")
apt_packages = ["build-essential", "direnv", "python3"]
installed = check_output(["dpkg-query", "--show", "--showformat=${Package}\\n"])
installed = installed.decode().split()
if not all(p in installed for p in apt_packages):
    check_call(["sudo", "apt", "install"] + apt_packages)

print()
print(f"=== Build dir ({build_dir}) ===")
build_dir.mkdir(exist_ok=True)
(build_dir / ".gitignore").open("w").write("/*\n")

print()
print(f"=== Python packages (pip install ...) ===")
venv_dir = build_dir / "python_venv"
venv_bin = venv_dir / "bin"

if not venv_dir.is_dir():
    venv.create(venv_dir, symlinks=True, with_pip=True)
    check_call(["direnv", "allow", source_dir])

python_packages = ["conan", "meson", "ninja"]
if not all(
    any(venv_dir.glob(f"lib/python*/site-packages/{p}-*.dist-info"))
    for p in python_packages
):
    check_call([venv_bin / "pip", "install"] + python_packages)

print()
print(f"=== Conan (C++) packages (conan install ...) ===")
conan_bin = venv_bin / "conan"
conan_home = Path.home() / ".conan"
conan_profile = build_dir / "conan-profile.txt"
conan_install = build_dir / "conan-install"
os.environ["CONAN_V2_MODE"] = "1"

check_call([conan_bin, "config", "init"])
check_call([conan_bin, "config", "set", "general.revisions_enabled=1"])

bc_url = "https://bincrafters.jfrog.io/artifactory/api/conan/conan-legacy-bincrafters"
check_call([conan_bin, "remote", "add", "--force", "bincrafters", bc_url])
check_call([conan_bin, "profile", "new", "--detect", "--force", conan_profile])
check_call([
    conan_bin, "profile", "update", "settings.compiler.libcxx=libstdc++11",
    conan_profile
])

check_call([
    conan_bin, "install",
    f"--profile={conan_profile}",
    # "--settings=build_type=Debug",
    "--settings=ffmpeg:build_type=Release",  # ffmpeg ARM won't build Debug
    f"--install-folder={conan_install}",
    "--update",
    "--build=outdated",
    source_dir
])

print()
print(f"=== Configure build (Meson/Ninja via Conan) ===")
check_call([
    conan_bin, "build",
    f"--build-folder={build_dir}",
    f"--install-folder={conan_install}",
    "--configure",  # Only configure, not build (yet)
    source_dir
])

print()
print(f"::: Setup complete, build with: ninja -C build :::")
