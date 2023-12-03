# See https://docs.conan.io/en/latest/reference/conanfile.html

import conan
import conan.tools.build
import conan.tools.meson
import os.path
import shutil

class PividConan(conan.ConanFile):
    name, version = "pivid", "0.0"
    settings = "os", "compiler", "build_type", "arch"  # boilerplate
    generators = ["PkgConfigDeps", "MesonToolchain"]
    options = {"shared": [True, False]}
    default_options = {"shared": False}  # Used by Meson build helper
    build_policy = "outdated"

    requires = [
        "cli11/2.1.1", "cpp-httplib/0.10.1",
        "ffmpeg/5.1.4+rpi@pivid", "fmt/8.0.1",
        "linux-headers-generic/[>=5.14.9]", "nlohmann_json/3.10.5",
        "spdlog/1.9.2",
    ]

    test_requires = ["doctest/2.4.8"]

    def build(self):
        meson = conan.tools.meson.Meson(self)
        meson.configure(reconfigure=True)
        meson.build()

    def configure(self):
        # ffmpeg: Trim to the things we actually use.
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

        # libdrm: Trim driver-specific support we don't use.
        self.options["libdrm"].shared = False
        for libdrm_disable in [
            "amdgpu", "etnaviv", "exynos", "freedreno", "freedreno-kgsl",
            "intel", "libkms", "nouveau", "omap", "radeon", "tegra",
            "udev", "valgrind", "vc4", "vmwgfx",
        ]:
            setattr(self.options["libdrm"], libdrm_disable, False)

    def layout(self):
        self.folders.build = "build"
        self.folders.generators = "build/meson"

    def validate(self):
        conan.tools.build.check_min_cppstd(self, 20)
