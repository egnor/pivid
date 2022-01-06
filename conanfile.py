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
        "cli11/2.1.1", "fmt/8.0.1", "libdrm/2.4.109",
        "linux-headers-generic/5.14.9",
        "ffmpeg/4.3+rpi@pivid/specific",
    ]

    def configure(self):
        # Trim things we don't use from ffmpeg to simplify the build.
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

        # Likewise, trim driver-specific support we don't use from libdrm.
        self.options["libdrm"].shared = False
        for libdrm_disable in [
            "amdgpu", "etnaviv", "exynos", "freedreno", "freedreno-kgsl",
            "intel", "libkms", "nouveau", "omap", "radeon", "tegra",
            "udev", "valgrind", "vc4", "vmwgfx",
        ]:
            setattr(self.options["libdrm"], libdrm_disable, False)

    def build(self):
        meson = conans.Meson(self)  # Uses the "pkg_config" generator (above)
        meson_private_dir = os.path.join(self.build_folder, "meson-private")
        shutil.rmtree(meson_private_dir, ignore_errors=True)  # Force reconfig
        meson.configure()
        meson.build()
