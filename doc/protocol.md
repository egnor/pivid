# Pivid REST API protocol

The [pivid_server](running.md#pivid_server) process listens for
HTTP requests on `http://localhost:31415` (by default) with no authentication.
(Consider using an HTTP proxy to add authentication, HTTPS, and other
features as needed.) This HTTP server is not interesting to visit with a
web browser, but serves
[JSON](https://www.json.org/json-en.html) (`application/json`)
data to API clients.

> **Syntax legend:**  \
> `«angle brackets»` mark value placeholders  \
> `✳️` marks required values (other values are optional)  \
> `🔁` marks repeated items

## GET requests (information query)

### GET `/media/«file»` - fetch media file metadata

The request URL includes the path of a media file (movie or image)
relative to the server's `--media_root` (eg. `/media/kitten.rgba.png`).

Successful response:

```yaml
{
  ✳️ "filename": "«full disk filename»",
  ✳️ "container_type": "«ffmpeg format (eg. matroska,webm)»",
  ✳️ "codec_name": "«ffmpeg codec (eg. hevc)»",
  ✳️ "pixel_format": "«ffmpeg pixel format (eg. yuv420p)»",
  "size": [«frame pixel width», «frame pixel height»],
  "frame_rate": «average frames per second»,
  "bit_rate": «average compressed bits per second»,
  "duration": «runtime in seconds»,
  ✳️ "req": "/media/«file»",
  ✳️ "ok": true
}
```

### GET `/screens` - list video connectors and detected monitors

Successful response:

```yaml
{
  ✳️ "screens": {
    🔁 "«hardware connector (eg. HDMI-1)»": {
      ✳️ "detected": «monitor sensed, true/false»,
      ✳️ "modes": [ 🔁 [«video mode width», «height», «refresh rate»], ··· ],
      "active_mode": [«active mode width», «height», «refresh rate»]
    }, ···
  },
  ✳️ "req": "/screens",
  ✳️ "ok": true
}
```

## POST requests (commands)

### POST `/play` - set play script to control video output

Request body: [Play script JSON](script.md)

Successful response:

```yaml
{ ✳️ "req": "/play", ✳️ "ok": true }
```

### POST `/quit` - shut down the server process

The `/quit` request must be sent as a POST (for safety) but no request body
is required. It causes the `pivid_server` process to exit. The response is a
generic success or (unlikely) error (see below).

Successful response:

```yaml
{ ✳️ "req":  "/quit", ✳️ "ok": true }
```

## Error response format

An error (invalid request or internal processing error) use this JSON
response format:

```yaml
{
  ✳️ "req": "«request URL path (eg. /play)»",
  ✳️ "error": "«human readable message»"
}
```

Any JSON response from pivid should include either `"ok": true` or an
`"error"` message. Additionally, the HTTP status will be set appropriately
(200 for success, 500 for generic errors, etc).

Next: [Play script format](script.md)
