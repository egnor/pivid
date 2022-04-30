# Pivid REST API protocol

When [pivid_server](running.md#pivid_server) is running, it listens for
requests on `http://localhost:31415` (the port is configurable) without
authentication. (Consider using an HTTP proxy to add authentication,
HTTPS, and other features as needed.)

It serves the following request types, accepting and returning JSON
(content-type `application/json`) data.

Syntax note: In this guide, `«double chevrons»` mark a value placeholder,
`⟦hollow brackets⟧` mark an optional item, and `⋯` indicate
possible repetition.

## `/media/«path»` (GET) - query media file metadata

`«path»` - The pathname of a media file (movie or image) relative to
the server's `--media_root`

### Successful response

```yaml
{
  "filename": "«filename»",
  "container_type": "«container_type»",
  "codec_name": "«codec_name»",
  "pixel_format": "«pixel_format»",
  ⟦ "size": [«width», «height»], ⟧
  ⟦ "frame_rate": «frame_rate», ⟧
  ⟦ "bit_rate": «bit_rate», ⟧
  ⟦ "duration": «duration», ⟧
  "req": "/media/«path»",
  "ok": true
}
```

`«filename»` - the full canonical disk filename of the media (including
`--media_root`)

`«container_type»` - the
[libav (ffmpeg) file format](https://ffmpeg.org/ffmpeg-formats.html#matroska)
as a comma-separated list of names, e.g. `matroska,webm`

`«codec_name»` - the
[libav (ffmpeg) codec](https://www.ffmpeg.org/ffmpeg-codecs.html) used to
decode this media, e.g. `hevc`

`«pixel_format»` - the
[libav (ffmpeg) pixel format](https://github.com/FFmpeg/FFmpeg/blob/master/libavutil/pixfmt.h)
of the decoded frames, e.g. `yuv420p`

`«width», «height»` (int pair, optional) - the frame pixel size

`«frame_rate»` (float, optional) - the average frames per second of the media

`«bit_rate»` (float, optional) - the average bits per second of the
compessed media file

`«duration»` (float, optional) - the runtime of the media in seconds

`«path»` - the media path from the request URL

## `/screens` (GET) - list video connectors and detected monitors

### Successful response

```yaml
{
  "screens": {
    "«connector»": {
      "detected": «detected»,
      "modes": [ [«width», «height», «hz»], ⋯ ],
      ⟦ "active_mode": [«width», «height», «hz»] ⟧ 
    },
    ⋯
  },
  "req": "/screens",
  "ok": true
}
```

`«connector»` - the name and index number of a video connector,
e.g. `HDMI-1`

`«detected»` (bool) - `true` if a monitor is sensed, `false` otherwise

`«width», «height», «hz»` (int triple) - the resolution and refresh rate of
a video mode, either currently active (`"active_mode"`) or supported by the
monitor (`"modes"`)

## `/play` (POST) - set play script to control video output

The request body must be a [play script](script.js) which becomes the
server's operating play script.

### Successful response

```yaml
{ "req": "/play", "ok": true }
```

## `/quit` (POST)

The `/quit` request must be sent as a POST (for safety) but no request body
is required. It causes the `pivid_server` process to exit.

### Successful response

```yaml
{ "req": "/quit", "ok": true }
```

## Generic error response

An error (invalid request or internal processing error) will return a JSON
response in this format:

```yaml
{ "req": "«urlpath»", "error": "«message»" }
```

`«urlpath»` - the relative URL of the error request, e.g. `/play`

`«message»` - a human readable description of the error

Additionally, the HTTP status for an error will be an appropriate code
(e.g. 400 or 500).

Next: [Play script format](script.md)
