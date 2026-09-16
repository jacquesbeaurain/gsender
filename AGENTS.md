# AGENTS.md

Notes for AI agents (and humans) working in this repo. Written to save the next
session the time it took to work these things out the first time. Everything
here was verified against the code, not assumed — but code moves, so check
before relying on a specific line.

gSender is CNC controller software: an Electron/browser front end over a Node
server that talks to a grbl or grblHAL motion controller.

## Running it

```bash
yarn dev
```

Serves the app at <http://localhost:8000>. Three watchers run concurrently:
esbuild for the server bundle, nodemon to restart the server, and tailwind for
CSS. Front-end changes appear on reload; server changes trigger a nodemon
restart, which **drops any open controller connection** — expect to reconnect.

Node >= 18 (`engines`). Use `yarn`, not `npm` — there's a `yarn.lock` and a
`bun.lock`, but the scripts assume yarn.

## Developing without a CNC attached

This is the big one. `scripts/cnc-sim/` is a grbl/grblHAL simulator that speaks
the line protocol over TCP:

```bash
yarn sim          # grbl on 127.0.0.1:2323
yarn sim:test     # 50-check protocol self-test, ~1 min
```

Then **Settings → Ethernet** → *Connect to IP* `127.0.0.1`, *Ethernet port*
`2323`, and click the **Ethernet** button in the connection dropdown (not a USB
entry). See [scripts/cnc-sim/README.md](scripts/cnc-sim/README.md) for flags and
the runtime REPL, which injects alarms, errors, holds, door states and probe
failures on demand.

It needs no changes to gSender, for the reason in the next section.

## Architecture facts that are not obvious from a quick read

**The transport already has a TCP mode.** `SerialConnection.open()`
(`src/server/lib/SerialConnection.js`) opens a `net.Socket` instead of a
`SerialPort` when `network` is set or the path matches an IPv4 regex. Both
branches feed the same `ReadlineParser`, and every layer above — `Connection`,
the controllers, `Sender`, `Feeder` — is transport-agnostic. This is what makes
the simulator possible without touching app code.

Watch the `isOpen` getter: in network mode it checks `port.writable && connected`,
otherwise `port.isOpen`. A `net.Socket` has no `isOpen`, so connecting to an IP
*without* `network: true` yields a socket that is open but reports closed.

**Firmware detection rides on the startup banner, not `$I`.** `Connection.js`
polls `$I` and regex-matches *any* incoming line for `/grbl|fluidnc/i` and
`/grblhal/i`. Real grbl answers `$I` with `[VER:...]`/`[OPT:...]`, which contains
no "grbl" — so detection actually succeeds off the `Grbl 1.1f ['$' for help]`
banner the board sends on connect. Without a banner you wait 7 × 800 ms and fall
back to `workspace.defaultFirmware`. There's a unit test at
`src/server/lib/__tests__/ConnectionFirmwareDetect.test.js`.

**There are two parallel controller stacks.** `src/server/controllers/Grbl/` and
`src/server/controllers/Grblhal/`, each with its own controller, runner and a
near-duplicate set of `*LineParserResult*.js` files. A protocol-level change
usually needs doing in both; check before assuming one edit covers it.

**One `ok` per line is load-bearing.** The sender uses character-counting
streaming. Miss an `ok` and a job stalls silently mid-file with no error.

**Port listing filters by USB vendor ID.** `CNCEngine.js` partitions discovered
ports against a valid-vendor list; anything else lands in the collapsed
"Unrecognized Ports" section. Relevant if you ever try a socat/pty virtual
serial port — it will show up, but not where you expect.

## grbl protocol gotchas (learned the hard way building the simulator)

- **`G10 L20 P0` means the *active* WCS**, not G54. gSender's Zero button sends
  exactly this. Mapping P0 to G54 works by accident until someone is in G55.
- **Jogging is `$J=`**, e.g. `$J=G21G91 X5 F3000` (built in `GrblController.js`).
  It runs in its own modal context: G90/G91 and G20/G21 on the jog line apply to
  that jog only and must not mutate the machine's modal state. Cancelled with
  realtime byte `0x85`, not a line command.
- **Some commands synchronize.** `$#`, `$G`, `G10`, `G92`, `G4`, `G38.x`,
  `M0/M2/M30` and friends wait for the planner to drain
  (`protocol_buffer_synchronize()` in the firmware). Miss this and zeroing right
  after a move captures a position that is still changing — which looks like a
  flaky off-by-a-few-mm bug, not a protocol bug.
- **`Hold:1` vs `Hold:0`** is "still decelerating" vs "stopped, resumable". A
  program pause on a drained planner is `Hold:0` immediately.
- **Realtime bytes are intercepted before line assembly**, at the ISR on real
  hardware: `?` `~` `!` `0x18` `0x84` `0x85`, grblHAL's `0x87`, and the override
  range `0x90`–`0x9B`. They can appear mid-line and must not reach the parser.
- **`Bf:15,128`** is planner blocks free, then RX bytes free. gSender derives its
  streaming buffer size as `rx - 8`.

## Conventions

- `src/` is ESM (`import`/`export`); `scripts/` is CommonJS (`require`). No
  `"type": "module"` at the root.
- Biome handles format and lint, but **`biome.json` excludes `src/server/**`** —
  server code is not formatted by it. `yarn lint` runs with the formatter
  disabled, so formatting is not a lint gate; `yarn format:check` is separate.
- Indent is 4 spaces.

## Testing

```bash
yarn test:unit     # jest, jsdom
yarn lint          # biome, formatter disabled
yarn check-types   # tsc --noEmit on the app
yarn sim:test      # simulator protocol self-test
```

Jest maps `app/*` → `src/app/src/*` and `server/*` → `src/server/*`. Tests live
both in `__tests__/` directories and beside their subjects as `*.test.ts(x)`.

Cypress specs under `cypress/e2e/` mostly target grblHAL and assume hardware or
a stand-in; the simulator is a plausible stand-in but this has not been wired up.

## Working notes for agents

- **Verify claims against the code before writing them down.** Several things in
  this file are the opposite of what a reasonable person would assume from the
  grbl docs (firmware detection, `G10 L20 P0`).
- **Drive the real app to check protocol work.** The simulator's self-test
  passed 40/40 while `$J=` jogging was entirely unimplemented — the gap only
  surfaced on clicking a jog button in the browser and getting `error:3`. Unit
  tests confirm what you thought of; the app surfaces what you didn't.
- `src/server/api/notes.json` is **generated**, not hand-edited: `yarn dev` runs
  `prebuild-dev` → `scripts/package-sync.js`, which regenerates it by parsing
  the release notes out of `README.md`. So it can show up modified in
  `git status` without you having touched it. Check what's staged before
  committing, and edit the README rather than the JSON.
