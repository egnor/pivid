#!/usr/bin/env python3

import os
from pathlib import Path
from subprocess import check_call, check_output

os.chdir(str(Path(__file__).resolve().parent))

print("=== Update system packages (sudo apt) ===")
apt_packages = ["libabsl-dev", "libdrm-dev", "libdrm-tests", "ninja-build"]
check_call(["sudo", "apt", "install"] + apt_packages)
print()

print("=== Update meson (pip3) ===")
check_call(["pip3", "install", "--user", "meson"])
