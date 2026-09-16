# CNC simulator

A grbl / grblHAL simulator that speaks the line protocol over TCP, so you can
develop against a "connected" machine without hardware.

It needs no changes to gSender. `SerialConnection.open()` already opens a
`net.Socket` instead of a `SerialPort` when the target looks like an IPv4
address ([`src/server/lib/SerialConnection.js`](../../src/server/lib/SerialConnection.js)),
and everything above that layer — firmware detection, the controllers, the
sender and feeder — is transport-agnostic.

## Running it

```bash
yarn sim
```

Then in gSender:

1. **Settings → Ethernet** → *Connect to IP* `127.0.0.1`, *Ethernet port* `2323`
2. Open the connection dropdown and click the **Ethernet** button (below the USB
   port list, not one of the USB entries — the USB path would try to open a real
   serial port).

Port 2323 rather than grbl's usual 23, because ports below 1024 need root on
macOS and Linux.

## Self-test

```bash
yarn sim:test
```

Starts a simulator per scenario and drives it with the exact command forms
gSender's controllers emit, asserting the protocol invariants (one `ok` per
line, planner back-pressure, deferred `ok` on `$H`, `$J=` jogging, probe and
zeroing behaviour). Takes about a minute — the behaviour worth testing here is
all timing behaviour, so it runs real timers.

## Options

| Flag | Meaning |
| --- | --- |
| `--host=<ip>` | Listen address (default `127.0.0.1`) |
| `--port=<n>` | Listen port (default `2323`) |
| `--firmware=grbl\|grblhal` | Which firmware to impersonate (default `grbl`) |
| `--axes=XYZ` | Axis letters, e.g. `XYZA` for a 4-axis machine |
| `--no-banner` | Suppress the startup banner, to exercise gSender's `$I` detection fallback |
| `--alarm-on-connect[=n]` | Come up alarm-locked with `ALARM:n` (default 1) |
| `--error-every=<n>` | Reject every nth G-code line |
| `--error-code=<n>` | Code used by `--error-every` (default 20) |
| `--latency=<ms>` | Delay every outgoing write |
| `--plate-z=<mm\|never>` | Machine Z of the touch plate's top face (default `-10`); `never` makes probes miss |
| `--plate-x=<mm>` | Machine X of the plate's bottom-left corner (default `0`) |
| `--plate-y=<mm>` | Machine Y of the plate's bottom-left corner (default `0`) |
| `--plate-size=<mm>` | Plate width and length (default `50`) |
| `--probe-touched` | Hold the probe pin asserted, so gSender's connectivity test passes |
| `--quiet` | Do not log traffic |

```bash
yarn sim --firmware=grblhal --axes=XYZA   # 4-axis grblHAL board
yarn sim --alarm-on-connect               # starts alarm-locked, needs $X
yarn sim --error-every=25                 # reject a line mid-job
yarn sim --latency=150                    # a sluggish link
yarn sim --probe-touched                  # ready for the probe dialog
```

## Probing

The simulator carries a touch plate: a square block in **machine** coordinates
with its bottom-left corner at `(plate-x, plate-y)` and its top face at
`plate-z`. The defaults are a 50 mm plate (gSender's own default plate size)
with its corner at machine origin and its top 10 mm below Z0, so the stock
bottom-left-corner routines work from a tool parked at machine zero.

A `G38.x` move triggers on the face it is travelling toward — down onto the top,
+X onto the left face, −X onto the right face, and the same for Y — so single
axis, XY and XYZ routines all complete.

To use gSender's probe dialog, start with:

```bash
yarn sim --probe-touched
```

The dialog will not enable **Start Probe** until it has seen the probe circuit
close (`Pn:P`). `--probe-touched` holds the pin asserted so the check passes
immediately; `touch` / `untouch` in the REPL do the same at runtime. Failing
that, the circuit also closes for real whenever the tool is inside the plate, so
jogging into it works — but then the tool is below the plate top and has nothing
left to probe down onto.

To probe somewhere other than machine zero, jog to where you want the plate and
run `plate here`, which treats the tool's position as parked above the corner.

Worked example, with the defaults and a 15 mm plate thickness: a Z probe stops
at machine −10, gSender writes the offset with `G10 L20 P0 Z15`, and work zero
lands at machine −25. Retracted 2 mm off the plate, the DRO reads Z = 17.000.

A 3D probe works the same way against the same plate — its routines are the
Standard Block ones with a different thickness. Set **Touch plate type** to
*3D Probe* in Config → Probe; with the default `Z offset` of 0 the probe zeroes
on the plate's top face, so the same Z routine ends with the DRO reading
Z = 2.000 at machine −8 rather than 17.000.

**The tool is a point.** There is no tool-radius compensation on contact, so an
X or Y probe stops when the tool *centre* reaches the face. gSender compensates
for the tool radius when it writes the offset, so simulated XY zeros are off by
one tool radius from what real hardware would produce. Z probing is unaffected.

## Runtime fault injection

With a TTY attached, the simulator takes commands while it runs — this is the
part that's hard to reproduce on real hardware:

```
sim> alarm 1        Raise ALARM:<code> and lock the machine
sim> unlock         Clear the alarm, as $X would
sim> hold           Feed hold
sim> resume         Cycle start
sim> door           Open the safety door (Door:1)
sim> error 9        Reject the next line with error:9
sim> reset          Soft reset, as Ctrl-X would
sim> plate          Show the touch plate's faces (machine coords)
sim> plate here     Treat the tool's spot as parked above the plate corner
sim> plate z -5     Move the plate's top face ('never' so probes miss)
sim> touch          Assert the probe pin, for gSender's connectivity test
sim> untouch        Release the probe pin
sim> say [MSG:hi]   Push a raw line to the client
sim> status         Print machine state
sim> drop           Drop the client connection mid-job
sim> quit           Stop the simulator
```

Under `yarn sim &`, nohup or CI there is no TTY, so the REPL is skipped and the
server just runs.

## What it models

- Startup banner, `$I`, `$$`, `$G`, `$#`, `$H`, `$X`, `$C`, `$N`, and `$<n>=<v>`
  setting writes
- Status reports on `?` (and grblHAL's `0x87` complete report), with `MPos`,
  `Bf`, `FS`, `WCO`, `Pn`, `Ov` and `A` fields; `WCO` every 10th report, as real
  grbl does, plus a forced `WCO` on the next report after any offset change
  (`G10`, `G92`, `G92.1`, a `G54`–`G59` switch) — grbl's
  `system_flag_wco_change()`. gSender derives the work position as
  `MPos - WCO` from the last `WCO` it saw, so without the forced refresh the
  DRO keeps showing the pre-probe zero for seconds after a routine finishes,
  which reads as "probing did nothing"
- Realtime bytes: `?` `~` `!` `0x18` `0x84` `0x85` `0x87`, and the feed/rapid/
  spindle override bytes `0x90`–`0x9B`
- A 15-block planner buffer, so `ok` carries real back-pressure — exactly one
  `ok` per line, delayed when the buffer is full. This is what gSender's
  character-counting sender depends on.
- Synchronizing commands (`$#`, `$G`, `G10`, `G92`, `G4`, `G38.x`, `M0`/`M2`/
  `M30`, …) wait for the planner to drain, matching
  `protocol_buffer_synchronize()` in the firmware. Without this, zeroing right
  after a move captures a position that is still changing.
- Motion at the programmed feed rate, `Run`/`Idle`/`Hold`/`Jog`/`Home`/`Alarm`
  states, `Hold:1` while decelerating then `Hold:0`
- `G0/G1`, `G90/G91`, `G20/G21`, `G53`, `G54`–`G59`, `G10 L2/L20`, `G92`,
  `G92.1`, `G28/G30`, `G4` dwell, `M0/M1/M2/M30`, `M3/M4/M5`, `M7/M8/M9`, `F`,
  `S`, `T`
- Probing: `G38.2`–`G38.5` stop at the touch plate and report `[PRB:…:1]`; a
  probe that reaches its target without contact reports `[PRB:…:0]` and raises
  `ALARM:5`. `Pn:P` asserts while the tool is in contact. See **Probing** above.
- Relative moves plan from the end of the previous queued block, not the live
  position, so a burst of buffered `G91` moves lands where it should. Nothing
  is planned past a probe, since where it stops is not knowable in advance.

## What it does not model

- **Acceleration.** A move runs at its target feed for its whole length, so
  timings are optimistic and `Bf` drains more evenly than on real hardware.
- **Arcs.** `G2`/`G3` move in a straight line to the endpoint. Fine for state
  and streaming, wrong for anything reading the toolpath back.
- **Soft and hard limits.** `$130`–`$132` are reported but never enforced, and
  there are no limit switches, so you cannot trigger a real limit alarm (use
  `alarm 1` from the REPL instead).
- **Tool radius on probe contact** — the tool is a point, so XY probe zeros are
  off by one tool radius from real hardware. See **Probing**.
- **The plate is planes, not a solid.** Probing X triggers at the X face
  wherever the tool happens to be in Y, so a badly mispositioned probe still
  succeeds. The `Pn:P` contact test does check the full footprint.
- **Setting-description queries.** grblHAL's `$ES`/`$EG`/`$EA` return an empty
  `ok`, so any gSender UI driven by the firmware's own setting metadata will
  come up blank.
- **Tool changes.** `M6` synchronizes but does nothing else.
- **EEPROM persistence.** Setting writes live for the life of the connection.
