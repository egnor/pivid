# See https://docs.conan.io/en/latest/reference/conanfile.html

import conans
import os.path
import shutil

class PividConan(conans.ConanFile):
    name, version = "pivid", "0.0"
    settings = "os", "compiler", "build_type", "arch"  # boilerplate
    generators = "pkg_config"  # Used by the Meson build helper (below)
    options = {"shared": [True, False]}
    default_options = {"shared": False}  # Used by Meson build helper

    requires = [
        "cli11/2.1.1", "cpp-httplib/0.10.1",
        "ffmpeg/4.3+rpi@pivid/specific", "fmt/8.0.1",
        "linux-headers-generic/5.14.9", "nlohmann_json/3.10.5",
        "openssl/1.1.1n", "spdlog/1.9.2",
    ]

    def configure(self):
        # Trim ffmpeg to the things we actually use.
        self.options["ffmpeg"].for_pivid = True
        self.options["ffmpeg"].postproc = False
        self.options["ffmpeg"].shared = False
        for ffmpeg_without in [
            "bzip2", "freetype", "libalsa", "libfdk_aac", "libiconv",
            "libmp3lame", "libvpx", "libwebp", "libx264", "libx265",
            "lzma", "openh264", "openjpeg", "opus", "programs",
            "pulse", "vaapi", "vdpau", "vorbis", "xcb"
        ]:
            setattr(self.options["ffmpeg"], f"with_{ffmpeg_without}", False)

        # Also trim driver-specific support we don't use from libdrm.
        self.options["libdrm"].shared = False
        for libdrm_disable in [
            "amdgpu", "etnaviv", "exynos", "freedreno", "freedreno-kgsl",
            "intel", "libkms", "nouveau", "omap", "radeon", "tegra",
            "udev", "valgrind", "vc4", "vmwgfx",
        ]:
            setattr(self.options["libdrm"], libdrm_disable, False)

    def build_requirements(self):
        self.test_requires("doctest/2.4.8")

    def build(self):
        meson = conans.Meson(self)  # Uses the "pkg_config" generator (above)
        meson_private_dir = os.path.join(self.build_folder, "meson-private")
        shutil.rmtree(meson_private_dir, ignore_errors=True)  # Force reconfig
        meson.configure()
        meson.build()
