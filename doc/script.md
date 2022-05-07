# Pivid play script JSON format

A "play script" is a [JSON object](https://www.json.org/json-en.html)
describing the content Pivid should display. Play script JSON may be sent to a
running [`pivid_server`](running.md#pivid_server) using the
[`/play` request](protocol.md#play-post---set-play-script-to-control-video-output),
or supplied as a file to [`pivid_play --script`](running.md#pivid_play).

A new play script may be sent to the server at any time. You can choose
whether to send only short term "scene" instructions and update the script
as needed, or send entire long term sequences.

Play scripts may also include content preloading directives, anticipating
content updated scripts may use (see the
[architecture overview](architecture.md)).

## JSON format

Syntax notes:
* `«double angle brackets»` mark value placeholders (`𝑓(𝑡)` indicates a [time-varying value](#timing-and-time-variable-values)).
* `⟦hollow square brackets⟧` surround optional items
* `triple dots ···` indicate repeated items
* anything else is verbatim

```yaml
{
  ⟦ "zero_time": «timestamp baseline, default is server start», ⟧
  ⟦ "main_loop_hz": «output timeline update frequency, default 30», ⟧
  ⟦ "main_buffer_time": «output timeline length in seconds, default 0.2», ⟧

  ⟦
    "screens": {
      "«hardware connector, eg. HDMI-1»": {
        ⟦ "mode": [«video mode width», «height», «refresh rate»], ⟧
        ⟦ "update_hz": «content update frequency, default is refresh rate», ⟧
        ⟦
          "layers": [
            {
              "media": "«media file to show, relative to media root»",
              ⟦ "play": «𝑓(𝑡) seek position within media in seconds, default 0.0», ⟧
              ⟦ "buffer": «media readahead in seconds, default 0.2», ⟧
              ⟦
                "from_xy": [
                  «𝑓(𝑡) source media clip box left, default 0»,
                  «𝑓(𝑡) source media clip box top, default 0»
                ],
              ⟧
              ⟦
                "from_size": [
                  «𝑓(𝑡) source media clip box width, default is media width»,
                  «𝑓(𝑡) source media clip box height, default is media height»
                ],
              ⟧
              ⟦
                "to_xy": [
                  «𝑓(𝑡) screen region left, default 0»,
                  «𝑓(𝑡) screen region top, default 0»
                ],
              ⟧
              ⟦
                "to_size": [
                  «𝑓(𝑡) screen region width, default is screen width»,
                  «𝑓(𝑡) screen region height, default is screen height»
                ],
              ⟧
              ⟦ "opacity": «𝑓(𝑡) alpha value, default 1.0» ⟧
            },
            ···
          ]
        ⟧
      },
      ···
    },
  ⟧

  ⟦
    "media": {
      "«media file to configure, relative to media root»": {
        ⟦ "seek_scan_time": «threshold for seeking vs reading, default 1.0», ⟧
        ⟦ "decoder_idle_time": «retention time for unused decoders, default 1.0», ⟧
        ⟦ "preload": «preload specification, see below» ⟧
      },
      ···
    }
  ⟧
}
```

## General structure

## Time reference and `zero_time`

Pivid script timing is based on
[wall-clock Unix time](https://en.wikipedia.org/wiki/Unix_time)
on the pivid server. (It's best to make sure your
[clock is synced](https://dayne.broderson.org/2020/03/12/the_time_is_now.html).)

The top level `zero_time` value is a
[Unix timestamp](https://www.unixtimestamp.com/) as a raw number
(eg. 1651893234.4 for 2022-05-06 8:13:54.4pm PT). All other timestamps in
the script are offsets from this value. If `zero_time` is 0.0, other
timestamps are themselves absolute Unix times. If `zero_time` is not
specified, it's the time when the server (or play tool) is started.

## Time-variable values (`𝑓(𝑡)`)

Many values in pivid scripts (marked with `𝑓(𝑡)` in the syntax above)
are given as functions of time, specified in several ways.
The most general format is a JSON object:

```yaml
{
  "segments": [
    {
      ⟦ "t": [«segment begin timestamp», «segment end timestamp»], ⟧
      ⟦
        "v": [
          «value at segment begin»,
          ⟦ «spline handle at 1/3 point», ⟧
          ⟦ «spline handle at 2/3 point», ⟧
          ⟦ «value at segment end» ⟧
        ],
      ⟧
      ⟦ "rate": «», ⟧
    },
    ···
  ],
  ⟦ "repeat": «loop period, or true/false» ⟧
}
```

Next: [Development notes and links](notes.md)
