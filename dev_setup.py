#!/usr/bin/env python3

import argparse
import os
import shlex
import shutil
import subprocess
import venv
from pathlib import Path
from subprocess import check_call, check_output

# GENERAL BUILD / DEPENDENCY STRATEGY
# - Use Meson (mesonbuild.com) and Ninja (ninja-build.org) to build C++
# - Use Conan (conan.io) to install C++ dependencies (ffmpeg, etc)
# - Use pip in venv (pypi.org) for Python dependencies (like conan, meson, etc)
# - Reluctantly use system packages (apt) for things not covered above

parser = argparse.ArgumentParser(description="Pivid dev environment setup")
parser.add_argument("--clean", action="store_true", help="Wipe build dir first")
parser.add_argument("--no-conan", action="store_true", help="Skip conan setup")
parser.add_argument("--debug", action="store_true", help="Debug build for deps")
args = parser.parse_args()

top_dir = Path(__file__).resolve().parent
build_dir = top_dir / "build"
conan_dir = build_dir / "conan"

print("\n➡️ System packages")

def run_shell(*av, **kw):
    command = shlex.join(str(a) for a in av)
    print(f"🐚 {command}")
    subprocess.check_call(av, **kw)

apt_packages = [
    # TODO: Make libudev and libv4l into Conan dependencies
    "build-essential", "cmake", "direnv", "libudev-dev", "libv4l-dev",
    "python3", "python3-pip", "python3-venv",
]
installed = check_output(["dpkg-query", "--show", "--showformat=${Package}\\n"])
installed = installed.decode().split()
if all(p in installed for p in apt_packages):
    print("✨ (needed apt packages already installed)")
else:
    run_shell("sudo", "apt", "update")
    run_shell("sudo", "apt", "install", "--assume-yes", *apt_packages)

# Unify all pkg-config paths, to avoid issues with separate brew installs, etc.
pkg_path = {}
for p in os.environ["PATH"].split(":"):
    if (pkg_config := Path(p) / "pkg-config").is_file():
        pkg_command = [pkg_config, "--variable", "pc_path", "pkg-config"]
        pkg_output = check_output(pkg_command).decode().strip()
        pkg_path.update({pp: p for pp in pkg_output.split(":")})

os.environ["PKG_CONFIG_PATH"] = ":".join(pkg_path.keys())

print(f"\n➡️ Build dir ({build_dir})")
if args.clean and build_dir.is_dir():
    print("🗑️ ERASING build dir (per --clean)")
    shutil.rmtree(build_dir)

build_dir.mkdir(exist_ok=True)
(build_dir / ".gitignore").open("w").write("/*\n")

print(f"\n➡️ Python packages")
venv_dir = build_dir / "python_venv"
venv_bin = venv_dir / "bin"
if venv_dir.is_dir():
    print("✨ (venv already created)")
else:
    print(f"🐍 creating venv: {venv_dir}")
    venv.create(venv_dir, symlinks=True, with_pip=True)
    run_shell("direnv", "allow", top_dir)

# docutils is required by rst2man.py in the libdrm build??
python_packages = ["conan~=2.0", "docutils", "meson", "ninja", "requests"]
run_shell(venv_bin / "pip", "install", *python_packages)

print(f"\n➡️ Conan (C++ package manager) setup")

def run_conan(*av, **kw):
    command = f"conan {shlex.join(str(a) for a in av)}"
    if args.no_conan:
        print(f"⛔ SKIP: {command}")
    else:
        print(f"💪 {command}")
        subprocess.check_call(["direnv", "exec", top_dir, "conan", *av], **kw)

profile_path = conan_dir / "profiles" / "default"
run_conan("profile", "detect", "--name=detected", "--force")
print(f"⚙️ Writing: {profile_path}")
lines = ["include(detected)", "[settings]", "compiler.cppstd=23"]
profile_path.write_text("".join(f"{l}\n" for l in lines))

print(f"\n➡️ Install ffmpeg Conan recipe")
run_conan(
    "export",
    "--version=5.1.4+rpi",
    "--user=pivid",
    top_dir / "ffmpeg_rpi_recipe",
)

print(f"\n➡️ Build C++ dependencies")
build_type = "Debug" if args.debug else "Release"
run_conan(
    "install",
    f"--settings=build_type={build_type}",
    "--settings=ffmpeg:build_type=Release",  # ffmpeg ARM won't build Debug
    "--build=missing",  # Allow source builds for all packages
    top_dir,
)

# Clean up cached packages that weren't used in this process
print(f"\n➡️ Clean C++ package cache")
run_conan("remove", "--lru=1d", "--confirm", "*")
run_conan("cache", "clean", "*")

if args.no_conan:
    print(f"\n🌵 Complete (without Conan, per --no-conan)")
else:
    print(f"\n😎 Setup complete, build with: ninja -C build")
