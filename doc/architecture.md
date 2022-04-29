# Pivid architecture overview

![Diagram of Pivid architecture](architecture_diagram.svg)

To accommodate gapless, nonlinear, changeable playback, Pivid has an
atypical architecture. Instead of linear (FIFO) buffers of frames between
media decoder and playback, Pivid uses nonlinear _frame caches_,
random-access maps of all frames currently loaded for each media file.

The main Pivid update thread creates a _frame loader_ for each media file
referenced in the [play script](script.md). The update thread determines
which sections from each file will be needed in the next little while
(typically ~200 milliseconds, but configurable). If a seek or loop is
coming up in the script, or multiple parts of the same video are being
played simultaneously, there may be multiple distinct sections needed.
As time advances, each frame loader is kept updated with the current sections
of interest.

Each frame loader in turn runs a thread which dynamically creates, uses,
and deletes media decoders, seeking and reading from the file to get frames
for sections needed but not yet in the cache. Media files generally have
limitations on seeking (based on "key frames") so this can be a somewhat
complicated optimization (which Pivid approximates with heuristics). The
loader also removes frames from the cache which are no longer needed.

The frame caches are available to the main update thread (with appropriate
synchronization) which constructs a _timeline_ of the next little while
(again, typically ~200 milliseconds) for each display screen.
Each timeline is a series of output frames, each of which includes all the
input media frames used in that output frame, plus layout directions.

For each active output, a _frame player_ thread follows the timeline.
The timeline may be swapped out at any moment by the main update thread
(with appropriate synchronization) without playback gaps. Collectively,
frame caches and timelines allow input media to be rearranged arbitrarily
into output frames, and allow the remixing instructions to be swapped out
seamlessly.

Pivid can predict which frames will be needed based on a given script, but
quick script changes (such as sudden starts of new media files) may require
explicit preloading in the script for gapless play.

Actual frame pixels are stored in GPU memory when possible and tracked
with reference-counted pointers, so frames can be processed through frame
caches, the update thread, output times and player threads without any actual
copies of bulk image data.

Next: [REST API protocol](protocol.md)
