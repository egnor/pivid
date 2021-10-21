# pivid
Experimental video code for Linux / Raspberry Pi

## Building
1. Run `./dev_setup.py`. (Works On My Machine™, YMMV. Eventually we should use Docker.)
2. Make sure the `pip` installation of `meson` (in `~/.local/bin` by default) is on your `$PATH`.
3. Run `meson b` to set up a build directory (`b` is just my convention, use any name).
4. Run `ninja -C b` to run the actual build (this is the only part to repeat after edits).
5. Run binaries from `b` (like `b/pivid_list`).

## Notes and links

### Raspberry Pi
* [All about accelerated video on the Raspberry Pi](https://forums.raspberrypi.com/viewtopic.php?f=67&p=1901014) - my notes
* [kernel.org: V3D Graphics Driver](https://www.kernel.org/doc/html/v5.10/gpu/v3d.html) - RPi 4 GPU kernel driver docs
* [raspberrypi: V3D driver source](https://github.com/raspberrypi/linux/tree/rpi-5.10.y/drivers/gpu/drm/v3d) - RPi 4 GPU kernel driver source
* [raspberrypi: V3D user header](https://github.com/raspberrypi/linux/blob/rpi-5.10.y/include/uapi/drm/v3d_drm.h) - ioctl defs for RPi 4 GPU kernel driver

### Graphics output: DRM and KMS
* [Wikipedia: Direct Rendering Manager](https://en.wikipedia.org/wiki/Direct_Rendering_Manager) - a good overview
* [Man page: Direct Rendering Manager - Kernel Mode-Setting](https://manpages.debian.org/testing/libdrm-dev/drm-kms.7.en.html) - incomplete but helpful
* [Man page: Direct Rendering Manager - Memory Management](https://manpages.debian.org/testing/libdrm-dev/drm-memory.7.en.html) - incomplete but helpful
* [kernel.org: Linux GPU Driver Userland Interfaces](https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html) - kernel interface
* [kernel.org: Linux GPU Driver Developer's Guide](https://www.kernel.org/doc/html/v5.10/gpu/index.html) - kernel internals, good to know about
* [libdrm](https://gitlab.freedesktop.org/mesa/drm) - library wrapper; undocumented but simple; see [xf86drm.h](https://gitlab.freedesktop.org/mesa/drm/-/blob/main/xf86drm.h) and [xf86drmMode.h](https://gitlab.freedesktop.org/mesa/drm/-/blob/main/xf86drmMode.h) (not X-specific despite the "xf86")
* [modetest](https://cgit.freedesktop.org/drm/libdrm/tree/tests/modetest/modetest.c) - command line tool (in the [libdrm-tests](https://packages.debian.org/sid/main/libdrm-tests) Debian package)
* [libgbm](https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/gbm) - GPU allocation helper library; undocumented; see [gbm.h](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gbm/main/gbm.h)

### Video decoding: V4L2
* [kernel.org: Video for Linux API](https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/v4l2.html) - general V4L2 api
* [kernel.org: Video for Linux Memory-to-Memory Stateful Video Decoder Interface](https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/dev-decoder.html) - specifically this part
* [libv4l](https://github.com/philips/libv4l) - library wrapper; see [libv4l2.h](https://github.com/philips/libv4l/blob/master/include/libv4l2.h)
* [v4l-utils](https://linuxtv.org/wiki/index.php/V4l-utils) - command line tools; see [v4l2-ctl](https://manpages.debian.org/testing/v4l-utils/v4l2-ctl.1.en.html)

### Zero-copy buffers
* [kernel.org: Buffer Sharing and Synchronization](https://www.kernel.org/doc/html/v5.10/driver-api/dma-buf.html#userspace-interface-notes) - kernel buffer management (and user interface)
* [kernel.org: Linux GPU Memory Management - PRIME buffer sharing](https://www.kernel.org/doc/html/v5.10/gpu/drm-mm.html#prime-buffer-sharing) - exporting GPU buffers as "dma-buf" objects
* [v4l2test](https://github.com/rdkcmf/v4l2test) - test/example of V4L2 H.264 decoding feeding KMS

Of all the bits and pieces here, memory management seems to be the most complex and poorly explained. Here is what I have gleaned:
* GPUs have all kinds of memory architectures. The RPi is simple, everything is in system RAM (no dedicated GPU RAM).
* DRM/KMS requires you to create a "framebuffer" object, giving it a "buffer object" handle (opaque int32).
* How a "buffer object" is allocated and managed is dependent on the kernel GPU driver.
* Most drivers use the "Graphics Execution Manager" (GEM), their buffer object handles are "GEM handles".
* Most drivers support "dumb buffers", allowing simple creation of a buffer in system RAM.
* Many drivers have more elaborate driver-specific interfaces for allocating various kinds of memory.
* The libgbm ("Generic Buffer Manager") library tries to abstract over those driver-specific interfaces.
* "DRM-PRIME" is a fancy name for ioctl's (see libdrm `drmPrimeHandleToFD` and `drmPrimeFDToHandle`) to convert GEM handles to/from "dma-buf" descriptors.
* These "dma-buf" descriptors can be used with other systems like V4L2, of course you have to get the data format right.

Open questions:
* On the RPi (v3d driver), are "dumb buffers" all we need, or should we use the v3d-specific `DRM_V3D_CREATE_BO` ioctl?
* When using V4L2 (with MMAL underneath) for video decoding, how is hardware access arbitrated between streams?
* What's the exact dance of frames between V4L2 and KMS, esp when starting/stopping/splicing/blending?
