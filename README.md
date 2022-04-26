# Pivid

[![Example video](doc/example_thumbnail.png)](https://photos.app.goo.gl/aU5KSJmNnLvrw1gc8)

* [Building and running Pivid](doc/running.md)
* [Architecture overview](doc/architecture.md)
* [REST API reference](doc/interface.md)
* [Play script format](doc/script.md)
* [Development notes and links](doc/notes.md)

## Introduction

Pivid is a nonlinear, gapless, direct-rendering, multi-head video playback and
compositing engine based on [libav](https://libav.org/), optimized for the
[Raspberry Pi](https://www.raspberrypi.org/) and controlled with a JSON/REST API.
Pivid is intended for escape rooms, immersive experiences, video performances
and similar applications.

Unlike media player apps like [VLC](https://www.videolan.org/vlc/) or
[https://kodi.tv](Kodi), Pivid has no user interface of its own and is only
useful driven by other software.

Pivid can splice, overlay, scale, transition, seek and loop videos
with no gaps and frame-perfect rendering. The [play script](doc/script.md)
(playback instructions) may be updated at any time; preload hints may
be provided to ensure gapless changes.

Playback position, media layout onscreen, and layer opacity are specified as
(1-D) cubic Bezier functions of time, enabling flexible animation and
transition effects. Still images are considered one-frame videos and may be
layered with moving video content, including alpha-channel transparency support.

Pivid does not run under the Raspberry Pi desktop (X windows). Instead,
Pivid uses the Linux
[Direct Rendering Manager](https://en.wikipedia.org/wiki/Direct_Rendering_Manager)
(with [Atomic Display](https://en.wikipedia.org/wiki/Direct_Rendering_Manager#Atomic_Display))
for high performance full screen playback. When playing 
[H.264/AVC](https://en.wikipedia.org/wiki/Advanced_Video_Coding) or
[H.265/HEVC](https://en.wikipedia.org/wiki/High_Efficiency_Video_Coding)
video content on the
[Raspberry Pi 4B](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/),
Pivid uses the GPU and
[DMA-BUF/DRM-PRIME](https://en.wikipedia.org/wiki/Direct_Rendering_Manager#DMA_Buffer_Sharing_and_PRIME)
for zero-copy decoding, buffering, compositing and display.

Pivid is "early alpha" status and you are invited to
[contact the author](https://github.com/egnor) if you're planning to use it
seriously. Pivid currently has no audio support, which is obviously a major
limitation and a likely area of upcoming work.

Pivid is available under the [MIT license](LICENSE.md).

Next: [Building and running Pivid](doc/running.md)
