# Pivid play script format

A "play script" is a JSON object describing the content Pivid should display.
play script may be sent to a running [`pivid_server`](running.md#pivid_server)
using the
[`/play` POST request](protocol.md#play-post---set-play-script-to-control-video-output),
or supplied as a file to [`pivid_play`](running.md#pivid_play) with
`--script` (typically for testing).

A new play script may be sent to the server at any time, so generated play
scripts need only include short term instructions, but play scripts may
include arbitrarily long term sequencing if desired. Until a new script is
sent, `pivid_server` will continue executing the previous script
(`pivid_play` executes a single script until the program is stopped).

Play scripts may also include content preloading directives, anticipating
that updated scripts may play that content immediately (see the
[architecture overview](architecture.md)).

## JSON format

Next: [Development notes and links](notes.md)
