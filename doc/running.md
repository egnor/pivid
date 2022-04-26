# Building Pivid binaries

You are currently recommended to build Pivid from source. Please
[open an issue](https://github.com/egnor/pivid/issues) if these
instructions don't work.

1. Use a Raspberry Pi (4B recommended) with a fully updated
[bullseye](https://www.raspberrypi.com/news/raspberry-pi-os-debian-bullseye/)
install and 2G+ RAM.

2. Clone [this repository](https://github.com/egnor/pivid) (you will need
[git-lfs](https://git-lfs.github.com/)), and `cd` to the repository root.

3. Run `./dev_setup.py`.

4. Run `ninja -C build` to build the Pivid code.

5. Binaries (such as `pivid_server`) can be found in the `build` directory.
They are statically linked and may be copied elsewhere as desired.

6. To reset the build, `rm -rf build` and start over with `./dev_setup.py`.

# Running Pivid binaries

1. Edit `/boot/config.txt` (as root), comment out existing
`dtoverlay=vc4`... lines, and add these options:

```
  # Use full KMS and H.265 (HEVC) decoding, reserve 512M CMA for frames
  dtoverlay=vc4-kms-v3d,cma-512
  dtoverlay=rpivid-v4l2
```

2. Run `sudo servicectl stop lightdm` to stop the X windows desktop.

   (You may want to log in to the Pi from another computer now, unless you
   like using the text mode Linux console.)

3. Test playing a video file:

```
build/pivid_play --media test_media/jellyfish-3-mbps-hd-hevc.mkv
```

If all goes well, 1080p video of blorping jellyfish should play on the
first HDMI output for 30 seconds.

## pivid_server

The main `pivid_server` program listens for HTTP requests, serves the
[REST API](interface.md), and plays content on screen as requested.

The server takes a number of arguments; see `pivid_server --help`.
One required argument is `--media_root=DIR`, which points to a directory
of media files; the server will only load media files from that tree.

By default, the server listens on port 31415 and only accepts connections
from localhost; use `--port=PORT` and/or `--trust_network` to change that.

For a quick test, run the server from the repository root:

```
build/pivid_server --media_root test_media
```

At the same time, run this script, also from the repository root:

```
test_media/rickroll_drop.py
```

If all goes well, you should be able to press keys 0-9 to
add falling Rick Astley videos on top of a jellyfish scene.
(The `rickroll_drop.py` script sends API requests to the server
running on the default port. You will find other example scripts
in the `test_media` directory, along with media files.)

A production setup typically starts `pivid_server` on boot with
appropriate media root and other options, and disables X windows
desktop autostart. Doing so is left as an exercise for the reader
(the means will depend on local needs and preferences).

## pivid_play

The `pivid_play` utility is a self-contained player mainly used
for testing and development.

This utility takes a number of arguments; see `pivid_play --help`.
One of `--media=FILE` or `--script=SCRIPT` is needed to play
a video file or execute a [play script](script.md), respectively.
(Make sure the X windows desktop is not running.)

## Other tools

In addition to `pivid_server` and `pivid_play`, the pivid build includes
various testing and exploration tools:

* `pivid_scan_displays` - lists video drivers, connectors, and available modes
* `pivid_scan_media` - lists media file metadata; optionally list or dump frames
* `pivid_inspect_avformat` - lists low level video file details
* `pivid_inspect_kms` - lists low level KMS/DRM driver details
* `pivid_inspect_kmsg` - lists kernel logs with better timestamps than dmesg
* `pivid_inspect_v4l2` - lists low level V4L2 driver details

Use `--help` (and/or read the source) to see usage for each tool.

Next: [REST API reference](interface.md)
