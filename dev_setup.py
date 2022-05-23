#!/usr/bin/env python3

import os
import venv
from pathlib import Path
from subprocess import check_call, check_output

# GENERAL BUILD / DEPENDENCY STRATEGY
# - Use Meson (mesonbuild.com) and Ninja (ninja-build.org) to build C++
# - Use Conan (conan.io) to install C++ dependencies (ffmpeg, etc)
# - Use pip in venv (pypi.org) for Python dependencies (like conan, meson, etc)
# - Reluctantly use system packages (apt) for things not covered above

source_dir = Path(__file__).resolve().parent
build_dir = source_dir / "build"

print("=== System packages (sudo apt install ...) ===")
apt_packages = [
    # TODO: Make libudev and libv4l into Conan dependencies
    "build-essential", "cmake", "direnv", "libudev-dev", "libv4l-dev",
    "python3", "python3-pip"
]
installed = check_output(["dpkg-query", "--show", "--showformat=${Package}\\n"])
installed = installed.decode().split()
if not all(p in installed for p in apt_packages):
    check_call(["sudo", "apt", "update"])
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

python_packages = ["conan", "meson", "ninja", "requests"]
if not all(
    any(venv_dir.glob(f"lib/python*/site-packages/{p}-*.dist-info"))
    for p in python_packages
):
    check_call([venv_bin / "pip", "install"] + python_packages)

print()
print(f"=== C++ package manager (conan init) ===")
run_conan = lambda *av: check_call(["direnv", "exec", source_dir, "conan", *av])
conan_profile = build_dir / "conan-profile.txt"
conan_install = build_dir / "conan-install"

run_conan("config", "init")
run_conan("config", "set", "general.revisions_enabled=1")
if not conan_profile.is_file():
    run_conan("profile", "new", "--detect", "--force", conan_profile)
    run_conan(
        "profile", "update", "settings.compiler.libcxx=libstdc++11",
        conan_profile
    )

for dir, ref in [
    ("ffmpeg_rpi_recipe", "ffmpeg/4.3+rpi@pivid/specific"),
]:
    print()
    print(f"=== {ref} recipe (conan export) ===")
    run_conan("export", source_dir / dir, ref)

print()
print(f"=== C++ dependencies (conan install) ===")
run_conan(
    "install",
    f"--profile={conan_profile}",
    # "--settings=build_type=Debug",  # Uncomment & re-run to build debug
    "--settings=ffmpeg:build_type=Release",  # ffmpeg ARM won't build Debug
    f"--install-folder={conan_install}",
    "--build=outdated",
    source_dir
)

print()
print(f"=== Prepare build (meson/ninja via conan) ===")
run_conan(
    "build",
    f"--build-folder={build_dir}",
    f"--install-folder={conan_install}",
    "--configure",  # Only configure, not build (yet)
    source_dir
)

# Save this to the end, to preserve conan cache for debugging if things fail
print()
print(f"=== Clean C++ package cache (conan remove) ===")
run_conan("remove", "--src", "--builds", "--force", "*")

print()
print(f"::: Setup complete, build with: ninja -C build :::")
