# Building Pivid binaries

In its current state, you are recommended to build Pivid from source.
Please let [the author](https://github.com/egnor) know if these instructions
don't work.

1. Use a Raspberry Pi (4B recommended) with a fully updated
[bullseye](https://www.raspberrypi.com/news/raspberry-pi-os-debian-bullseye/)
install and 2G+ RAM.

2. Clone this repository (you will need [git-lfs](https://git-lfs.github.com/))
and `cd` to the repository root.

3. Run `./dev_setup.py`. (It Works On My Machine™, YMMV)

4. Run `ninja -C build` to build the Pivid code.

5. Binaries (such as `pivid_server`) can be found in the `build` directory.
They are statically linked and may be copied elsewhere as desired.

6. To reset the build, `rm -rf build` and start over with `./dev_setup.py`.

# Running Pivid binaries

1. Edit `/boot/config.txt` as follows, and reboot:

```
  #dtoverlay=vc4-fkms-v3d  # Disable old dtoverlay=vc4* lines
  # Use full KMS and H.265 (HEVC) decoding, 512M CMA for frames
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

## pivid_server

In production, `pivid_server` is the program that gets run to
serve the [REST API](interface.md) and play whatever is requested.

The server takes a number of options; see `pivid_server --help`.
One required argument is `--media_root=DIR`, which points to a directory
of media files; scripts can only access media files in that directory.

For production, arrange to start `pivid_server` with the media root
and other options of your choice. (Make sure the X windows desktop
is not running.)

For testing, run this from the repository root:

```
build/pivid_server --media_root test_media
```

At the same time, run this, also from the repository root:

```
test_media/rickroll_drop.py
```

If all goes well, you should be able to press keys 0-9 to
add falling Rick Astley videos on top of a jellyfish scene.
(The `rickroll_drop.py` script sends API requests to the server
running on the default port. You will find other example scripts
in the `test_media` directory, along with media files.)

## pivid_play

The `pivid_play` utility is a self-contained player mainly used
for testing and development.

This utility takes a number of options; see `pivid_play --help`.
One of `--media=FILE` or `--script=SCRIPT` is needed to play
a video file or execute a [play script](script.md), respectively.
(Make sure the X windows desktop i snot running.)

## Other tools

The pivid build compiles a variety of programs, mostly testing and
exploration tools:

* `pivid_server` - serve the pivid web API ([see above](#pivid_server))
* `pivid_play` - play a video file or play script ([see above](#pivid_play))
* `pivid_scan_displays` - print video drivers, connectors, and available modes
* `pivid_scan_media` - print media file metadata; optionally list or dump frames
* `pivid_inspect_avformat` - print low level video file details
* `pivid_inspect_kms` - print low level KMS/DRM driver details
* `pivid_inspect_kmsg` - print kernel logs with better timestamps than dmesg
* `pivid_inspect_v4l2` - print low level V4L2 driver details

Use `--help` (and/or read the source) to see usage for each tool.

Next: [REST API reference](interface.md)
