# Parser Demo Plugin

Mirrored reference plugin demonstrating manifest-declared response parsers and regex patterns.

## Parsers
- `^\[PRB:([-\d.]+),([-\d.]+),([-\d.]+):([01])\]` (Probe result line parser)
- `^;TOOL:(\d+)` (G-code tool change comment parser)
