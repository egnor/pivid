# Pivid play script JSON format

A "play script" is a [JSON object](https://www.json.org/json-en.html)
describing the content Pivid should display. Play script JSON may be sent to a
running [`pivid_server`](running.md#pivid_server) using the
[`/play` POST request](protocol.md#play-post---set-play-script-to-control-video-output),
or supplied as a file to [`pivid_play --script`](running.md#pivid_play)
(typically for testing).

A new play script may be sent to the server at any time, so generated play
scripts need only include short term instructions, but play scripts may
include arbitrarily long term sequencing if desired. Until a new script is
sent, `pivid_server` will continue executing the previous script
(`pivid_play` executes a single script until the program is stopped).

Play scripts may also include content preloading directives, anticipating
that updated scripts may play that content immediately (see the
[architecture overview](architecture.md)).

Syntax note: In this guide, `«double chevrons»` mark a value placeholder,
`⟦hollow brackets⟧` mark an optional item, and `three dots ⋯` indicate
possible repetition.

## JSON format

```yaml
{
  ⟦ "zero_time": «zero_time», ⟧
  ⟦ "main_loop_hz": «main_loop_hz», ⟧
  ⟦ "main_buffer_time": «main_buffer_time», ⟧

  ⟦
    "screens": {
      "«connector»": {
        ⟦ "mode": [«width», «height», «hz»], ⟧
        ⟦ "update_hz": «update_hz», ⟧
        ⟦
          "layers": [
            {
              "media": "«media»",
              ⟦ "play": «play_var», ⟧
              ⟦ "buffer": «buffer», ⟧
              ⟦ "from_xy": [«from_x_var», «from_y_var»], ⟧
              ⟦ "from_size": [«from_width_var», «from_height_var»], ⟧
              ⟦ "to_xy": [«to_x_var», «to_y_var»], ⟧
              ⟦ "to_size": [«to_width_var», «to_height_var»], ⟧
              ⟦ "opacity": «opacity_var» ⟧
            },
            ⋯
          ]
        ⟧
      },
      ⋯
    },
  ⟧

  ⟦
    "media": {
      "«media»": {
        ⟦ "seek_scan_time": «seek_scan_time», ⟧
        ⟦ "decoder_idle_time": «decoder_idle_time», ⟧
        ⟦ "preload": «preload» ⟧
      },
      ⋯
    }
  ⟧
}
```

Next: [Development notes and links](notes.md)
