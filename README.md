# pivid
Experimental video code for Linux / Raspberry Pi

## Building
1. Use a Raspberry Pi with a fully updated [bullseye](https://www.raspberrypi.com/news/raspberry-pi-os-debian-bullseye/) install and 2G+ RAM.
2. Edit `/boot/config.txt` as follows, and reboot:
```
#dtoverlay=vc4-fkms-v3d  # Comment/remove old dtoverlay=vc4* lines
# Use full KMS and H.265 (HEVC) decoding, 512M CMA for frames
dtoverlay=vc4-kms-v3d,cma-512
dtoverlay=rpivid-v4l2
```
3. Run `./dev_setup.py`. (Works On My Machine™, YMMV)
4. Run `ninja -C build` to run the actual build (this is the only part to repeat after edits).
5. Run binaries from `build` (like `build/pivid_test_playback`).
6. If things get weird, `rm -rf build` and start over with `dev_setup.py`.

Notable programs:

* `pivid_play_media` - play a video file via KMS/DRM (stop X first)
* `pivid_save_frames` - split a video file into .tiff images
* `pivid_inspect_avformat` - print a summary of streams in a video file
* `pivid_inspect_kms` - print capabilities & properties of KMS/DRM devices
* `pivid_inspect_v4l2` - print capabilities & properties of V4L2 devices

Use `--help` to see usage (and/or read the source).

## Notes and links

### Raspberry Pi specifics
* [All about accelerated video on the Raspberry Pi](https://forums.raspberrypi.com/viewtopic.php?f=67&p=1901014) - my notes
* [kernel.org: V3D Graphics Driver](https://www.kernel.org/doc/html/v5.10/gpu/v3d.html) - RPi 4 GPU kernel driver docs
* [rpi kernel source: drivers/gpu/drm/v3d](https://github.com/raspberrypi/linux/tree/rpi-5.10.y/drivers/gpu/drm/v3d) - RPi 4 GPU kernel driver source
* [rpi kernel source: include/uapi/v3d_drm.h](https://github.com/raspberrypi/linux/blob/rpi-5.10.y/include/uapi/drm/v3d_drm.h) - ioctl defs for RPi 4 GPU kernel driver

### Graphics output: DRM and KMS
* [Wikipedia: Direct Rendering Manager](https://en.wikipedia.org/wiki/Direct_Rendering_Manager) - a decent overview
* [Blog post: "From pre-history to beyond the global thermonuclear war"](https://ppaalanen.blogspot.com/2014/06/from-pre-history-to-beyond-global.html) - Linux graphics history
* [LWN article: Atomic mode setting design overview](https://lwn.net/Articles/653071/) - the current KMS API
* [Man page: Direct Rendering Manager - Kernel Mode-Setting](https://manpages.debian.org/testing/libdrm-dev/drm-kms.7.en.html) - incomplete but helpful
* [Man page: Direct Rendering Manager - Memory Management](https://manpages.debian.org/testing/libdrm-dev/drm-memory.7.en.html) - incomplete but helpful
* [kernel.org: Linux GPU Driver Userland Interfaces](https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html) - basic notes on the kernel/user interface
* [kernel.org: KMS Properties](https://www.kernel.org/doc/html/v5.10/gpu/drm-kms.html#kms-properties) - exhaustive object property list
* [kernel source: include/uapi/drm/drm.h](https://github.com/torvalds/linux/blob/master/include/uapi/drm/drm.h) and [drm_mode.h](https://github.com/torvalds/linux/blob/master/include/uapi/drm/drm_mode.h) - kernel/user headers
* [kernel source: include/drm/drm_print.h](https://github.com/torvalds/linux/blob/master/include/drm/drm_print.h#L253) - debugging definitions 
* [ST Micro: DRM/KMS Overview](https://wiki.st.com/stm32mpu/wiki/DRM_KMS_overview) - decent general docs from a chip vendor
* [ST Micro: How to trace and debug the framework](https://wiki.st.com/stm32mpu/wiki/DRM_KMS_overview#How_to_trace_and_debug_the_framework) - an especially useful section
* [libdrm](https://gitlab.freedesktop.org/mesa/drm) - library wrapper; see [xf86drm.h](https://gitlab.freedesktop.org/mesa/drm/-/blob/main/xf86drm.h) and [xf86drmMode.h](https://gitlab.freedesktop.org/mesa/drm/-/blob/main/xf86drmMode.h) (not X-specific despite "xf86")
* [libgbm](https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/gbm) - GPU allocation helper library; see [gbm.h](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gbm/main/gbm.h)
* [modetest](https://cgit.freedesktop.org/drm/libdrm/tree/tests/modetest/modetest.c) - command line tool (in the [libdrm-tests](https://packages.debian.org/sid/main/libdrm-tests) Debian package)
* [kmscube](https://gitlab.freedesktop.org/mesa/kmscube) - oft-referenced KMS/GL example program
* [kms++](https://android.googlesource.com/platform/external/libkmsxx/) - C++ KMS wrapper & utilities

### Video decoding: V4L2
* [kernel.org: Video for Linux API](https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/v4l2.html) - kernel/user interface
* [kernel.org: Video for Linux - Memory-to-Memory Stateful Video Decoder Interface](https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/dev-decoder.html)
* [kernel source: include/uapi/linux/videodev2.h](https://github.com/torvalds/linux/blob/master/include/uapi/linux/videodev2.h) - kernel/user header
* [libv4l](https://github.com/philips/libv4l) - thin library wrapper with format conversion; see [libv4l2.h](https://github.com/philips/libv4l/blob/master/include/libv4l2.h)
* [v4l-utils](https://linuxtv.org/wiki/index.php/V4l-utils) - useful tools, especially [v4l2-ctl](https://manpages.debian.org/testing/v4l-utils/v4l2-ctl.1.en.html)
* [libavformat](https://github.com/FFmpeg/FFmpeg/tree/master/libavformat) - for unpacking containers (.mp4, .mkv); see [avformat.h](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/avformat.h) and [avio.h](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/avio.h)

### Zero-copy buffer sharing
* [kernel.org: Buffer Sharing and Synchronization](https://www.kernel.org/doc/html/v5.10/driver-api/dma-buf.html#userspace-interface-notes) - kernel buffer management (and user interface)
* [kernel.org: Linux GPU Memory Management - PRIME buffer sharing](https://www.kernel.org/doc/html/v5.10/gpu/drm-mm.html#prime-buffer-sharing) - exporting GPU buffers as "dma-bufs"
* [kernel.org: Video for Linux - Streaming I/O (DMA buffer importing)](https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/dmabuf.html) - using "dma-buf" objects in V4L2
* [hello_drmprime](https://github.com/jc-kynesim/hello_drmprime) - nice example of hardware H.264/H.265 sending to DRM/KMS with zero copy
