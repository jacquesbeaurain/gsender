# Parser Demo Plugin

Reference plugin for manifest-declared response parsers. gSender runs each
firmware line through the parsers below and sends every match to this plugin
alone, on the `parser` topic; the plugin counts them and keeps the last one.

## Parsers
- `probe`: `^\[PRB:(?<x>[-\d.]+),(?<y>[-\d.]+),(?<z>[-\d.]+):(?<success>[01])\]`, a probe result.
- `modal-state`: `^\[GC:(?<modes>[^\]]+)\]`, the modal state `$G` reports (only while no job runs).

The UI's button sends `$G`, so a `modal-state` match follows.
