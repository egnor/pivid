# Pivid play script JSON format

A "play script" is a [JSON object](https://www.json.org/json-en.html)
describing the content Pivid should display. Play script JSON may be sent to a
running [`pivid_server`](running.md#pivid_server) using the
[`/play` request](protocol.md#play-post---set-play-script-to-control-video-output),
or supplied as a file to [`pivid_play --script`](running.md#pivid_play).

A new play script may be sent to the server at any time, and playback will
switch seamlessly. You choose whether to send only short term instructions
and revise the script periodically, or send long sequences and only revise
if necessary.

Play scripts may also include content preloading directives, anticipating
content you might use in upcoming scripts (see the
[architecture overview](architecture.md)).

## Play script JSON format

> **Syntax legend:**  \
> `«angle brackets»` mark value placeholders (`⏱️` indicates a
> [time-varying value](#time-varying-values))  \
> `✳️` marks required values (other values are optional)  \
> `🔁` marks repeated items  \
> `▶️ 🔘 radio 🔘 buttons ◀️` describe alternative forms

```yaml
{
  "zero_time": «timestamp baseline (default=server start)»,
  "main_loop_hz": «output timeline update frequency (default=30)»,

  "screens": {
    🔁 "«hardware connector, eg. HDMI-1»": {
      ✳️ "mode": ▶️ 🔘 [«video mode width», «height», «refresh rate»] 🔘 null ◀️,
      "update_hz": «content update frequency (default=mode refresh rate)», 
      "layers": [
        🔁 {
          ✳️ "media": "«media file, relative to media root»",
          "play": ⏱️ «seek position within media in seconds (default=0.0)», 
          "buffer": «media readahead in seconds (default=0.2)», 
          "from_xy": [
            ⏱️ «source media clip box left (default=0)»,
            ⏱️ «source media clip box top (default=0)»
          ],
                
          "from_size": [
            ⏱️ «source media clip box width (default=media width)»,
            ⏱️ «source media clip box height (default=media height)»
          ],
                
          "to_xy": [
            ⏱️ «screen region left (default=0)»,
            ⏱️ «screen region top (default=0)»
          ],
                
          "to_size": [
            ⏱️ «screen region width (default=screen width)»,
            ⏱️ «screen region height (default=screen height)»
          ],
                
          "opacity": ⏱️ «alpha value (default=1.0)» 
        }, ···
      ]
    }, ···
  }
  
  "buffer_tuning": {
    🔁 "«media file to configure, relative to media root»": {
      "seek_scan_time": «threshold for seeking vs reading (default=1.0)», 
      "decoder_idle_time": «retention time for unused decoders (default=1.0)», 
      🔽
      🔘 "pin": «seconds to always keep loaded from start of media»
      🔘 "pin": [«begin time within media», «end time within media»]
      🔘 "pin": [🔁 [«begin time within media», «end time within media»], ···]
      🔼
    }, ···
  }
}
```

## General structure

Play scripts list all active outputs in `"screens"`.
(Use the [`pivid_scan_displays`](running.md#other-tools) tool to show
screen names.) For each screen, the script gives the video mode to use
([see below](#video-modes)), and a stack of layers to display.

Each layer references a single media file (still image or video), describing
which part of the source image should be clipped out, and where it should be
placed on screen. The clipped image will be resized as necessary to fit the
destination region.

Play scripts may supply further options for specific media files in
`"media"`, independent of screens and layers which use the file. (If the
default options are satisfactory, a media file need not be listed here.)
These options include `"preload"` definitions which instruct pivid to
cache portions of the media, anticipating script updates.

## Time reference and `zero_time`

Pivid script timing is based on
[wall-clock Unix time](https://en.wikipedia.org/wiki/Unix_time).
(It's best to make sure your server's
[clock is synced](https://dayne.broderson.org/2020/03/12/the_time_is_now.html).)

The top level `zero_time` value is a
[Unix timestamp](https://www.unixtimestamp.com/) as a raw number
(eg. 1651893234.4 for 2022-05-06 8:13:54.4pm PT). Other time values in
the script are offsets from this value. If `zero_time` is 0.0, other
timestamps are absolute Unix times. If not set, `zero_time` defaults to the
time when the server was started.

## Time-varying values

Many values in pivid scripts (marked with `⏱️` in the syntax above)
may be set to change over time. This is the basis of all animation,
including basic video playback (a time-varying `"play"` position).

If the value should not actually vary with time, a simple number may
be used (the third format below).

```yaml
🔽
🔘 {
     "segments": [
       🔁 {
         🔽
         🔘 "t": [«begin timestamp», «end timestamp»],
         🔘 "t": «begin timestamp (default=0.0)», "length": «seconds (default=infinite)»,
         🔼

         🔽
         🔘 "v": [«value at begin», «control point», «control point», «value at end»],
         🔘 "v": [«value at begin», «value at end»],
         🔘 "v": «value at begin», "rate": «units per second (default=0.0)»,
         🔼
       }, ···
     ],

     "repeat": ▶️ 🔘 «loop period» 🔘 true ◀️
   }

🔘 {
     "t": «same as "t" above»,
     "v": «same as "v" above»,
     "repeat": «same as "repeat" above»
   }

🔘 «constant value for all time»
🔼
```

In the most general case (the first format above), the value is
[defined piecewise](https://en.wikipedia.org/wiki/Piecewise) as
a collection of segments with begin and end times. Segments must be
listed in time order and may not overlap. Before, between, and after defined
segments, the value is undefined and reverts to its default.

Within each segment, the value is described by a 1-dimensional
[cubic Bézier curve](https://en.wikipedia.org/wiki/B%C3%A9zier_curve#Cubic_B%C3%A9zier_curves).
The Bézier curve may be described by the value at the ends of the segment, plus
optional control points at 1/3 and 2/3 from begin to end,
or by the starting point and a slope.

If `"repeat"` is set, the value loops with the given period (if provided)
or after the last segment ends (if `true`).

If there is only one segment, a simplified format (the second format above)
lists the segment by itself without a top-level object; the `repeat` value
(if present) is now in the single segment. An even more simplified format
(the third format above) gives a single value which never changes.

## Video modes

For each screen, play scripts list the video mode resolution and refresh
rate to use (`null` to disable the display). Pivid attempts to find a matching
mode defined by these standards:

* [CTA-861](https://www.cta.tech/Resources/i3-Magazine/i3-Issues/2019/November-December/cta-861-ctas-most-popular-standard)
(consumer TV modes)
* [VESA DMT](https://vesa.org/vesa-standards/) (list of computer monitor modes)
* [VESA CVT](https://en.wikipedia.org/wiki/Coordinated_Video_Timings)
(general formula for computer monitors)

If no mode timings can be found, the script is rejected.

The connected display's capabilities are not checked in this process.
For pivid's use cases, consistent and direct video mode control is normally
preferred. To adapt to display capabilities, clients may use the
[`/screens` request](protocol.md#screens-get---list-video-connectors-and-detected-monitors) and choose modes as desired.

Next: [Development notes and links](notes.md)
