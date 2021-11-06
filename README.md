# pivid
Experimental video code for Linux / Raspberry Pi

## Building
1. Run `./dev_setup.py`. (Works On My Machine™, YMMV)
2. Run `ninja -C build` to run the actual build (this is the only part to repeat after edits).
3. Run binaries from `build` (like `build/pivid_test_decode`).

If things get weird, `rm -rf build` and start over with `dev_setup.py`.

## Notes and links

### Raspberry Pi specifics
* [All about accelerated video on the Raspberry Pi](https://forums.raspberrypi.com/viewtopic.php?f=67&p=1901014) - my notes
* [kernel.org: V3D Graphics Driver](https://www.kernel.org/doc/html/v5.10/gpu/v3d.html) - RPi 4 GPU kernel driver docs
* [rpi kernel source: drivers/gpu/drm/v3d](https://github.com/raspberrypi/linux/tree/rpi-5.10.y/drivers/gpu/drm/v3d) - RPi 4 GPU kernel driver source
* [rpi kernel source: include/uapi/v3d_drm.h](https://github.com/raspberrypi/linux/blob/rpi-5.10.y/include/uapi/drm/v3d_drm.h) - ioctl defs for RPi 4 GPU kernel driver

### Graphics output: DRM and KMS
* [Wikipedia: Direct Rendering Manager](https://en.wikipedia.org/wiki/Direct_Rendering_Manager) - a good overview
* ["From pre-history to beyond the global thermonuclear war"](200~https://ppaalanen.blogspot.com/2014/06/from-pre-history-to-beyond-global.html) - old but good blog post on Linux graphics history
* [Atomic mode setting design overview](https://lwn.net/Articles/653071/) - old but good LWN article on the preferred KMS API
* [Man page: Direct Rendering Manager - Kernel Mode-Setting](https://manpages.debian.org/testing/libdrm-dev/drm-kms.7.en.html) - incomplete but helpful
* [Man page: Direct Rendering Manager - Memory Management](https://manpages.debian.org/testing/libdrm-dev/drm-memory.7.en.html) - incomplete but helpful
* [kernel.org: Linux GPU Driver Userland Interfaces](https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html) - basic notes on the kernel/user interface
* [kernel.org: KMS Properties](https://www.kernel.org/doc/html/v5.10/gpu/drm-kms.html#kms-properties) - exhaustive object property list
* [kernel source: include/uapi/drm/drm.h](https://github.com/torvalds/linux/blob/master/include/uapi/drm/drm.h) and [drm_mode.h](https://github.com/torvalds/linux/blob/master/include/uapi/drm/drm_mode.h) - kernel/user headers
* [ST Micro: DRM/KMS Overview](https://wiki.st.com/stm32mpu/wiki/DRM_KMS_overview) - decent general docs from a chip vendor
* [ST Micro: How to trace and debug the framework](https://wiki.st.com/stm32mpu/wiki/DRM_KMS_overview#How_to_trace_and_debug_the_framework) - an especially useful section
* [NVIDIA Jetson Linux API: Direct Rendering Manager](https://docs.nvidia.com/jetson/l4t-multimedia/group__direct__rendering__manager.html) - DRM API reference, sometimes NVIDIA-specific
* [libdrm](https://gitlab.freedesktop.org/mesa/drm) - library wrapper; see [xf86drm.h](https://gitlab.freedesktop.org/mesa/drm/-/blob/main/xf86drm.h) and [xf86drmMode.h](https://gitlab.freedesktop.org/mesa/drm/-/blob/main/xf86drmMode.h) (not X-specific despite "xf86")
* [libgbm](https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/gbm) - GPU allocation helper library; see [gbm.h](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gbm/main/gbm.h)
* [modetest](https://cgit.freedesktop.org/drm/libdrm/tree/tests/modetest/modetest.c) - command line tool (in the [libdrm-tests](https://packages.debian.org/sid/main/libdrm-tests) Debian package)
* [kmscube](https://gitlab.freedesktop.org/mesa/kmscube) - oft-referenced KMS/GL example program
* [kms++](https://android.googlesource.com/platform/external/libkmsxx/) - C++ KMS wrapper & utilities

Notes on DRM and KMS:
* These interfaces are almost completely undocumented. Learn by examples.
* But, many examples use "legacy" interfaces, prefer "atomic" update interfaces.

### Video decoding: V4L2
* [kernel.org: Video for Linux API](https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/v4l2.html) - kernel/user interface
* [kernel.org: Video for Linux - Memory-to-Memory Stateful Video Decoder Interface](https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/dev-decoder.html)
* [kernel source: include/uapi/linux/videodev2.h](https://github.com/torvalds/linux/blob/master/include/uapi/linux/videodev2.h) - kernel/user header
* [libv4l](https://github.com/philips/libv4l) - thin library wrapper with format conversion; see [libv4l2.h](https://github.com/philips/libv4l/blob/master/include/libv4l2.h)
* [v4l-utils](https://linuxtv.org/wiki/index.php/V4l-utils) - useful tools, especially [v4l2-ctl](https://manpages.debian.org/testing/v4l-utils/v4l2-ctl.1.en.html)
* [libavformat](https://github.com/FFmpeg/FFmpeg/tree/master/libavformat) - for unpacking containers (.mp4, .mkv); see [avformat.h](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/avformat.h) and [avio.h](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/avio.h)

### Zero-copy buffer sharing
* [kernel.org: Buffer Sharing and Synchronization](https://www.kernel.org/doc/html/v5.10/driver-api/dma-buf.html#userspace-interface-notes) - kernel buffer management (and user interface)
* [kernel.org: Linux GPU Memory Management - PRIME buffer sharing](https://www.kernel.org/doc/html/v5.10/gpu/drm-mm.html#prime-buffer-sharing) - exporting GPU buffers as "dma-buf" objects
* [kernel.org: Video for Linux - Streaming I/O (DMA buffer importing)](https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/dmabuf.html) - using "dma-buf" objects in V4L2
* [v4l2test](https://github.com/rdkcmf/v4l2test) - test/example of V4L2 H.264 decoding feeding KMS

Notes on memory management:
* GPUs have all kinds of memory architectures. The RPi is simple, everything is in system RAM (no dedicated GPU RAM).
* DRM/KMS requires you to create a "framebuffer" object, giving it a "buffer object" ("BO") handle (opaque int32).
* How a "buffer object" is allocated and managed is dependent on the kernel GPU driver.
* Most drivers use the "Graphics Execution Manager" (GEM), their buffer object handles are "GEM handles".
* Most drivers support "dumb buffers", allowing simple creation of a buffer in system RAM.
* Many drivers have more elaborate driver-specific interfaces for allocating various kinds of memory.
* The libgbm ("Generic Buffer Manager") library tries to abstract over those driver-specific interfaces.
* "DRM-PRIME" is a fancy name for ioctl's (see libdrm `drmPrimeHandleToFD` and `drmPrimeFDToHandle`) to convert GEM handles to/from "dma-buf" descriptors.
* These "dma-buf" descriptors can be used with other systems like V4L2, of course you have to get the data format right.
