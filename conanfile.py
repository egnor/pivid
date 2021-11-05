import conans
import os.path

class PividConan(conans.ConanFile):
    name, version = "pivid", "0.0"

    settings = "os", "compiler", "build_type", "arch"  # boilerplate
    requires = "ffmpeg/4.4", "abseil/20210324.2"
    generators = "pkg_config"  # Used by the Meson build helper (below)

    default_options = {
        # Omit as much of ffmpeg as possible to minimize dependency build time
        "ffmpeg:postproc": False, **{
            f"ffmpeg:with_{lib}": False for lib in [
                "bzip2", "freetype", "libalsa", "libfdk_aac", "libiconv",
                "libmp3lame", "libvpx", "libwebp", "libx264", "libx265",
                "lzma", "openh264", "openjpeg", "opus", "programs", "pulse",
                "vaapi", "vdpau", "vorbis", "xcb", "zlib",
            ]
        }
    }

    def build(self):
        meson = conans.Meson(self)  # Uses the "pkg_config" generator (above)
        dat = os.path.join(self.build_folder, "meson-private", "coredata.dat")
        meson.configure(args=["--reconfigure"] if os.path.isfile(dat) else [])
        meson.build()
