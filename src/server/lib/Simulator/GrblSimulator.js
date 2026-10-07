/*
 * Copyright (C) 2021 Sienci Labs Inc.
 *
 * This file is part of gSender.
 *
 * gSender is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, under version 3 of the License.
 *
 * gSender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gSender.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Contact for information regarding this program and its license
 * can be sent through gSender@sienci.com or mailed to the main office
 * of Sienci Labs Inc. in Waterloo, Ontario, Canada.
 *
 */

// A simulated Grbl 1.1 or grblHAL board: enough of the real firmware's
// behaviour to connect, stream jobs, jog, hold/resume, reset, home, probe and
// zero work coordinates without hardware. Port of the C++ application's
// GrblSimulator (cppport/src/core/src/sim/grbl_simulator.cpp), so both
// applications can be compared against the same board.
//
// Fidelity notes: motion is timed from distance and feed (no acceleration),
// arcs travel their chord, and the RX buffer is not modelled (lines wait for
// planner space instead). G4 and G38.x hold the input until they complete, as
// Grbl's buffer synchronisation does; probes stop where the bit touches one of
// the configured solids (a touch plate), else fail as Grbl does.
//
// Bytes go in through send() and come out through onData(text); output is
// always delivered asynchronously, never from inside send().

const INCH = 25.4;
const PLANNER_SIZE = 15;
const TICK_MS = 20;
const TOUCH_EPSILON = 1e-6; // mm

// YMODEM control bytes (grblHAL's ymodem.c).
const SOH = 0x01;
const STX = 0x02;
const EOT = 0x04;
const ACK = 0x06;
const NAK = 0x15;
const CAN = 0x18;
const CRC = 0x43; // 'C'

const STATE = Object.freeze({
	IDLE: "Idle",
	RUN: "Run",
	HOLD: "Hold:0",
	JOG: "Jog",
	ALARM: "Alarm",
	HOME: "Home",
	CHECK: "Check",
});

// Grbl 1.1 defaults for a small router; $22=1 enables homing (the simulated
// board starts unlocked, as if built without HOMING_INIT_LOCK).
const defaultSettings = () => [
	["$0", "10"],
	["$1", "25"],
	["$2", "0"],
	["$3", "0"],
	["$4", "0"],
	["$5", "0"],
	["$6", "0"],
	["$10", "1"],
	["$11", "0.010"],
	["$12", "0.002"],
	["$13", "0"],
	["$20", "0"],
	["$21", "0"],
	["$22", "1"],
	["$23", "0"],
	["$24", "25.000"],
	["$25", "500.000"],
	["$26", "250"],
	["$27", "1.000"],
	["$30", "1000"],
	["$31", "0"],
	["$32", "0"],
	["$100", "200.000"],
	["$101", "200.000"],
	["$102", "200.000"],
	["$110", "4000.000"],
	["$111", "4000.000"],
	["$112", "3000.000"],
	["$120", "750.000"],
	["$121", "750.000"],
	["$122", "500.000"],
	["$130", "800.000"],
	["$131", "800.000"],
	["$132", "100.000"],
];

const zeros = () => [0, 0, 0, 0];
const fixed3 = (value) => value.toFixed(3);
const axesText = (axes, scale = 1) =>
	`${fixed3(axes[0] / scale)},${fixed3(axes[1] / scale)},${fixed3(axes[2] / scale)}`;

// C++'s std::round: halves go away from zero.
const roundHalfAway = (value) => Math.sign(value) * Math.round(Math.abs(value));

// Number(...) of a settings value; NaN for anything not numeric.
const toNumber = (text) => {
	const trimmed = String(text ?? "").trim();
	return trimmed === "" ? NaN : Number(trimmed);
};

const distance = (a, b) => {
	let sum = 0;
	for (let i = 0; i < 4; ++i) {
		sum += (a[i] - b[i]) * (a[i] - b[i]);
	}
	return Math.sqrt(sum);
};

// The part [t0, t1] of the move from a to b (fractions of it) inside `solid`
// grown by the bit radius in XY; null when the move misses it.
const clip = (solid, radius, a, b) => {
	let t0 = 0;
	let t1 = 1;
	for (let i = 0; i < 3; ++i) {
		const grow = i < 2 ? radius : 0;
		const lo = solid.min[i] - grow;
		const hi = solid.max[i] + grow;
		const d = b[i] - a[i];
		if (Math.abs(d) < 1e-12) {
			if (a[i] < lo - TOUCH_EPSILON || a[i] > hi + TOUCH_EPSILON) {
				return null;
			}
			continue;
		}
		let ta = (lo - a[i]) / d;
		let tb = (hi - a[i]) / d;
		if (ta > tb) {
			[ta, tb] = [tb, ta];
		}
		t0 = Math.max(t0, ta);
		t1 = Math.min(t1, tb);
		if (t0 > t1) {
			return null;
		}
	}
	return [t0, t1];
};

// A standard touch plate hooked over a stock corner (corner numbers: 0 bottom
// left, clockwise). The stock corner is at (cornerX, cornerY) with its top at
// stockTop; the plate's outer faces sit `wall` beyond the stock edges, its top
// `thickness` above the stock and its lips `depth` below it, and it spans
// `size`.
export const touchPlateOnCorner = (
	corner,
	cornerX,
	cornerY,
	stockTop,
	thickness = 15,
	wall = 10,
	size = 50,
	depth = 10,
) => {
	const sx = corner === 0 || corner === 1 ? 1 : -1;
	const sy = corner === 0 || corner === 3 ? 1 : -1;
	const outerX = cornerX - wall * sx;
	const outerY = cornerY - wall * sy;
	return [
		{
			min: [
				Math.min(outerX, outerX + size * sx),
				Math.min(outerY, outerY + size * sy),
				stockTop - depth,
			],
			max: [
				Math.max(outerX, outerX + size * sx),
				Math.max(outerY, outerY + size * sy),
				stockTop + thickness,
			],
		},
	];
};

// The SD card plugin's filename_valid().
const usableSdName = (name) => name.length <= 40 && !/[?~!]/.test(name);

// $F lists the CNC files only.
const CNC_EXTENSIONS = [
	".nc",
	".ncc",
	".ngc",
	".cnc",
	".gcode",
	".txt",
	".text",
	".tap",
	".macro",
];
const isCncFile = (name) => {
	const dot = name.lastIndexOf(".");
	return dot >= 0 && CNC_EXTENSIONS.includes(name.slice(dot).toLowerCase());
};

const sdName = (path) => {
	const name = String(path).trim();
	return name.startsWith("/") ? name.slice(1) : name;
};

// CRC-16/XMODEM, as YMODEM packets carry.
const crc16Xmodem = (bytes) => {
	let crc = 0;
	for (const byte of bytes) {
		crc ^= byte << 8;
		for (let bit = 0; bit < 8; ++bit) {
			crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
		}
	}
	return crc;
};

// "G1 X10 (note) Y5" -> [["G", 1], ["X", 10], ["Y", 5]]; null for a word that
// is not a letter and a number (error:2, bad number format).
const parseWords = (line) => {
	const text = line
		.replace(/\([^)]*\)?/g, "")
		.replace(/;.*$/, "")
		.replace(/\s+/g, "")
		.toUpperCase();
	const words = [];
	let rest = text;
	while (rest.length > 0) {
		const match = /^([A-Z])([-+]?(?:\d+\.?\d*|\.\d+))/.exec(rest);
		if (!match) {
			return null;
		}
		words.push([match[1], Number(match[2])]);
		rest = rest.slice(match[0].length);
	}
	return words;
};

class GrblSimulator {
	// Bytes the board sends to the host, as text (binary-safe: one char per byte).
	onData = null;

	// options.post(fn) delivers output later, never from inside the call that
	// caused it; setImmediate by default, a queue the test flushes in tests.
	constructor(options = {}) {
		this.post = options.post ?? ((fn) => setImmediate(fn));
		this.grblHal = !!options.grblHal;
		this.speed = options.speed > 0 ? options.speed : 1;
		this.reportAfterDwell = false;
		this.toolRadius = 0;
		this.solids = [];
		this.settings = defaultSettings();
		this.wcsOffsets = [zeros(), zeros(), zeros(), zeros(), zeros(), zeros()];
		this.g28 = zeros();
		this.g30 = zeros();
		this.probe = zeros();
		this.probeSuccess = false;
		this.sdFiles = new Map();
		this.received = [];
		this.timers = new Set();
		this.isOpen = false;
		this.reset();
	}

	// ---- configuration -----------------------------------------------------

	setSpeed(factor) {
		this.speed = factor > 0 ? factor : 1;
	}

	setProbeSolids(solids) {
		this.solids = solids;
	}

	setToolRadius(radius) {
		this.toolRadius = radius;
	}

	putSdFile(name, data) {
		this.sdFiles.set(name, String(data));
	}

	get probeTriggered() {
		return this.touching(this.mpos);
	}

	get machinePosition() {
		return [...this.mpos];
	}

	get workOffset() {
		const wcs = this.wcsOffsets[this.wcs];
		return wcs.map((value, i) => value + this.g92[i]);
	}

	get workPosition() {
		const offset = this.workOffset;
		return this.mpos.map((value, i) => value - offset[i]);
	}

	get activeState() {
		return this.state;
	}

	// ---- timers ------------------------------------------------------------

	timeout(ms, fn) {
		const handle = setTimeout(() => {
			this.timers.delete(handle);
			fn();
		}, ms);
		this.timers.add(handle);
		return handle;
	}

	interval(ms, fn) {
		const handle = setInterval(fn, ms);
		this.timers.add(handle);
		return handle;
	}

	clearTimer(handle) {
		if (handle) {
			clearTimeout(handle);
			clearInterval(handle);
			this.timers.delete(handle);
		}
	}

	clearAll() {
		for (const handle of this.timers) {
			clearTimeout(handle);
			clearInterval(handle);
		}
		this.timers.clear();
		this.tickTimer = null;
		this.ymodemTimer = null;
	}

	// ---- power -------------------------------------------------------------

	// The modes a power-on (or DTR reset) starts with.
	reset() {
		this.homed = false;
		this.ymodem = null;
		this.sdRun = null;
		this.muted = false;
		this.planner = [];
		this.waiting = [];
		this.syncing = false;
		this.input = "";
		this.mpos = zeros();
		this.g92 = zeros();
		this.state = STATE.IDLE;
		this.beforeHold = STATE.IDLE;
		this.moveElapsed = 0;
		this.moveStart = zeros();
		this.lastTick = 0;
		this.tickTimer = null;
		this.ymodemTimer = null;
		this.motion = 0; // 0, 1, 2, 3, 38 (probe), 80
		this.relative = false;
		this.inches = false;
		this.plane = 17;
		this.wcs = 0;
		this.feed = 0; // mm/min
		this.spindle = 5; // 3, 4, 5
		this.spindleSpeed = 0;
		this.mist = false;
		this.flood = false;
		this.tool = 0;
		this.overrides = [100, 100, 100];
	}

	// Power on (or DTR reset): the banner follows shortly. A reboot starts
	// position and modes over; the coordinate offsets (EEPROM) survive.
	open() {
		this.clearAll();
		this.isOpen = true;
		this.reset();
		this.timeout(100, () => this.banner());
	}

	close() {
		this.isOpen = false;
		this.clearAll();
		this.ymodem = null;
		this.sdRun = null;
		this.planner = [];
		this.waiting = [];
		this.syncing = false;
	}

	// Stops where it is and raises ALARM:<code>, as a tripped limit switch or a
	// failed homing cycle would.
	triggerAlarm(code) {
		this.sdRun = null;
		this.homed = false;
		this.flushMotion();
		this.waiting = [];
		this.syncing = false;
		this.state = STATE.ALARM;
		this.emitText(`ALARM:${code}\r\n`);
	}

	banner() {
		this.emitText(
			this.grblHal
				? "\r\nGrblHAL 1.1f ['$' or '$HELP' for help]\r\n"
				: "\r\nGrbl 1.1h ['$' for help]\r\n",
		);
		if (this.state === STATE.ALARM) {
			this.emitText("[MSG:'$H'|'$X' to unlock]\r\n");
		}
	}

	emitText(text) {
		if (this.muted) {
			if (text === "ok\r\n") {
				return;
			}
			if (text.startsWith("error:")) {
				this.sdRun = null; // an error ends the file's run
			}
		}
		this.post(() => {
			if (this.isOpen && this.onData) {
				this.onData(text);
			}
		});
	}

	// ---- input -------------------------------------------------------------

	// `bytes` is a Buffer or a string (one char per byte for realtime bytes).
	// A realtime command that the JavaScript controller wrote as a UTF-8
	// encoded string (0xC2 0x91) is read as the single byte Grbl makes of it:
	// the 0xC2 is an unknown character the board discards.
	send(bytes) {
		if (!this.isOpen) {
			return;
		}
		const data = Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes, "latin1");
		for (const byte of data) {
			// A YMODEM transfer takes every byte; a packet's SOH at the start of a
			// line begins one (grblHAL's protocol layer does the same).
			if (this.ymodem) {
				this.receiveYmodem(byte);
				continue;
			}
			if (this.grblHal && byte === SOH && this.input === "") {
				this.ymodem = { packet: [], open: false, name: "", size: 0, data: [], expected: 1 };
				this.receiveYmodem(byte);
				continue;
			}
			if (byte === 0xc2 || byte === 0xc3) {
				continue;
			}
			// Realtime commands are picked out of the stream wherever they are.
			if (
				byte === 0x3f /* ? */ ||
				byte === 0x21 /* ! */ ||
				byte === 0x7e /* ~ */ ||
				byte === 0x18 ||
				byte >= 0x80
			) {
				this.realtime(byte);
				continue;
			}
			if (byte === 0x0a) {
				const line = this.input;
				this.input = "";
				this.handleLine(line);
			} else if (byte !== 0x0d) {
				this.input += String.fromCharCode(byte);
			}
		}
	}

	realtime(byte) {
		switch (byte) {
			case 0x3f: // ?
				this.emitText(`${this.statusReport()}\r\n`);
				return;
			case 0x21: // !
				if (this.state === STATE.JOG) {
					this.flushMotion(); // a hold ends a jog
					this.state = STATE.IDLE;
				} else if (this.state === STATE.RUN) {
					this.beforeHold = STATE.RUN;
					this.state = STATE.HOLD;
				}
				return;
			case 0x7e: // ~
				if (this.state === STATE.HOLD) {
					this.state = this.planner.length === 0 ? STATE.IDLE : this.beforeHold;
					if (this.planner.length > 0) {
						this.startMotion();
					}
					if (this.sdRun) {
						this.feedSdRun();
					}
				}
				return;
			case 0x18: {
				// Soft reset. A reset after a completed feed hold keeps the position
				// without an alarm (why senders hold first); the simulated hold
				// completes at once.
				const moving =
					this.state === STATE.RUN || this.state === STATE.JOG || this.state === STATE.HOME;
				this.sdRun = null;
				this.flushMotion();
				this.waiting = [];
				this.syncing = false;
				this.input = "";
				this.clearAll();
				this.g92 = zeros();
				this.motion = 0;
				this.relative = false;
				this.inches = false;
				this.wcs = 0;
				this.spindle = 5;
				this.mist = false;
				this.flood = false;
				if (moving) {
					this.state = STATE.ALARM;
					this.homed = false; // the position is lost
					this.emitText("ALARM:3\r\n");
				} else if (this.state !== STATE.ALARM) {
					this.state = STATE.IDLE;
				}
				this.banner();
				return;
			}
			case 0x87: // grblHAL: a complete report
				if (this.grblHal) {
					this.emitText(`${this.completeStatusReport()}\r\n`);
				}
				return;
			case 0x85: // jog cancel
				if (this.state === STATE.JOG) {
					this.flushMotion();
					this.state = STATE.IDLE;
				}
				return;
			case 0x90: this.overrides[0] = 100; return;
			case 0x91: this.overrides[0] = Math.min(this.overrides[0] + 10, 200); return;
			case 0x92: this.overrides[0] = Math.max(this.overrides[0] - 10, 10); return;
			case 0x93: this.overrides[0] = Math.min(this.overrides[0] + 1, 200); return;
			case 0x94: this.overrides[0] = Math.max(this.overrides[0] - 1, 10); return;
			case 0x95: this.overrides[1] = 100; return;
			case 0x96: this.overrides[1] = 50; return;
			case 0x97: this.overrides[1] = 25; return;
			case 0x99: this.overrides[2] = 100; return;
			case 0x9a: this.overrides[2] = Math.min(this.overrides[2] + 10, 200); return;
			case 0x9b: this.overrides[2] = Math.max(this.overrides[2] - 10, 10); return;
			case 0x9c: this.overrides[2] = Math.min(this.overrides[2] + 1, 200); return;
			case 0x9d: this.overrides[2] = Math.max(this.overrides[2] - 1, 10); return;
			default: // door, coolant toggles, unknown bytes
		}
	}

	handleLine(line) {
		this.received.push(line);
		// Lines queue behind one that waits for planner space or a sync (the
		// serial FIFO).
		if (
			this.waiting.length > 0 ||
			this.syncing ||
			(this.planner.length >= PLANNER_SIZE && line !== "" && line[0] !== "$")
		) {
			this.waiting.push(line);
			return;
		}
		this.execute(line);
	}

	execute(line) {
		const text = line.trim();
		if (text === "") {
			this.emitText("ok\r\n");
		} else if (text.startsWith("$J=")) {
			this.executeGcode(text.slice(3), true);
		} else if (text[0] === "$") {
			this.executeSystem(text);
		} else {
			this.executeGcode(text, false);
		}
	}

	acceptPending() {
		while (this.waiting.length > 0 && !this.syncing && this.planner.length < PLANNER_SIZE) {
			this.execute(this.waiting.shift());
		}
		if (this.sdRun) {
			this.feedSdRun();
		}
	}

	// ---- system commands ---------------------------------------------------

	getSetting(key) {
		const entry = this.settings.find(([name]) => name === key);
		return entry ? entry[1] : "";
	}

	executeSystem(line) {
		const command = line.toUpperCase();
		if (command === "$") {
			this.emitText("[HLP:$$ $# $G $I $N $x=val $Nx=line $J=line $SLP $C $X $H ~ ! ? ctrl-x]\r\nok\r\n");
			return;
		}
		if (command === "$$") {
			this.emitText(`${this.settings.map(([k, v]) => `${k}=${v}\r\n`).join("")}ok\r\n`);
			return;
		}
		if (command === "$#") {
			let out = "";
			for (let i = 0; i < 6; ++i) {
				out += `[G${54 + i}:${axesText(this.wcsOffsets[i])}]\r\n`;
			}
			out += `[G28:${axesText(this.g28)}]\r\n[G30:${axesText(this.g30)}]\r\n[G92:${axesText(this.g92)}]\r\n`;
			out += `[TLO:0.000]\r\n[PRB:${axesText(this.probe)}:${this.probeSuccess ? "1" : "0"}]\r\n`;
			this.emitText(`${out}ok\r\n`);
			return;
		}
		if (command === "$G") {
			this.emitText(`${this.parserState()}\r\nok\r\n`);
			return;
		}
		if (command === "$I") {
			if (this.grblHal) {
				this.emitText(
					"[VER:1.1f.20240417:]\r\n[OPT:VNMSL,35,1024,3,0]\r\n[AXS:3:XYZ]\r\n" +
						"[NEWOPT:ENUMS,RT+,SD,YM]\r\n[FIRMWARE:grblHAL]\r\n[BOARD:Simulator]\r\nok\r\n",
				);
			} else {
				this.emitText("[VER:1.1h.20190825:]\r\n[OPT:V,15,128]\r\nok\r\n");
			}
			return;
		}
		if (this.grblHal && this.executeGrblHal(command, line)) {
			return;
		}
		if (command === "$N") {
			this.emitText("$N0=\r\n$N1=\r\nok\r\n");
			return;
		}
		if (command === "$X") {
			if (this.state === STATE.ALARM) {
				this.state = STATE.IDLE;
				this.emitText("[MSG:Caution: Unlocked]\r\n");
			}
			this.emitText("ok\r\n");
			return;
		}
		// $H, or $HX/$HY/$HZ/$HA (single-axis homing, grblHAL's $22 bit 1).
		const singleAxis = command.length === 3 && command.startsWith("$H") && "XYZA".includes(command[2]);
		if (command === "$H" || singleAxis) {
			const homing = toNumber(this.getSetting("$22"));
			const flags = Number.isFinite(homing) ? Math.trunc(homing) : 0;
			if ((flags & 1) === 0) {
				this.emitText("error:5\r\n");
				return;
			}
			const axis = singleAxis ? "XYZA".indexOf(command[2]) : -1;
			if (axis >= 0 && (flags & 2) === 0) {
				this.emitText("error:3\r\n");
				return;
			}
			if (this.state !== STATE.IDLE && this.state !== STATE.ALARM) {
				this.emitText("error:8\r\n");
				return;
			}
			this.state = STATE.HOME;
			const seconds = axis >= 0 ? 0.5 : 1.5;
			this.timeout(Math.trunc((seconds * 1000) / this.speed), () => {
				if (axis >= 0) {
					this.mpos[axis] = 0;
				} else {
					this.mpos = zeros();
					this.homed = true;
				}
				this.state = STATE.IDLE;
				this.emitText("ok\r\n");
			});
			return;
		}
		if (command === "$C") {
			if (this.state === STATE.CHECK) {
				this.state = STATE.IDLE;
				this.emitText("[MSG:Disabled]\r\nok\r\n");
				this.banner(); // leaving check mode resets
			} else if (this.state === STATE.IDLE) {
				this.state = STATE.CHECK;
				this.emitText("[MSG:Enabled]\r\nok\r\n");
			} else {
				this.emitText("error:8\r\n");
			}
			return;
		}
		if (command === "$SLP") {
			this.emitText("ok\r\n[MSG:Sleeping]\r\n");
			return;
		}
		// $n=value - the firmware reads the line without its spaces ("$9 = 1" is "$9=1").
		const eq = command.indexOf("=");
		if (eq > 1) {
			const key = command.slice(0, eq).replace(/[ \t]/g, "");
			const value = line.slice(eq + 1).trim();
			if (!Number.isFinite(toNumber(value))) {
				this.emitText("error:2\r\n");
				return;
			}
			const entry = this.settings.find(([name]) => name === key);
			if (entry) {
				entry[1] = value;
				this.emitText("ok\r\n");
				return;
			}
			// grblHAL has hundreds: a numbered one it does not list yet is taken.
			if (this.grblHal && /^\$\d+$/.test(key)) {
				this.settings.push([key, value]);
				this.emitText("ok\r\n");
				return;
			}
		}
		this.emitText("error:3\r\n");
	}

	// ---- G-code ------------------------------------------------------------

	plannerEnd() {
		return this.planner.length === 0 ? [...this.mpos] : [...this.planner[this.planner.length - 1].target];
	}

	maxRate(axis) {
		const key = ["$110", "$111", "$112", "$112"][axis];
		const rate = toNumber(this.getSetting(key));
		return Number.isFinite(rate) && rate > 0 ? rate : 500;
	}

	executeGcode(line, jog) {
		if (this.state === STATE.ALARM) {
			this.emitText("error:9\r\n"); // G-code locked out
			return;
		}
		if (jog && this.state !== STATE.IDLE && this.state !== STATE.JOG) {
			this.emitText("error:8\r\n");
			return;
		}

		const words = parseWords(line);
		if (words === null) {
			this.emitText("error:2\r\n");
			return;
		}
		let motion = null;
		let probeKind = 382; // G38.2 .. G38.5
		let dwell = false;
		let g53 = false;
		let g10 = false;
		let home = null; // 28 / 30
		let storeHome = false;
		let g92 = false;
		let g92Clear = false;
		let pause = false;
		let programEnd = false;
		// Jogs carry their own distance and unit modes.
		let relative = this.relative;
		let inches = this.inches;
		const axes = [null, null, null, null];
		let feed = null;
		let speed = null;
		let p = null;
		let l = null;
		let tool = null;

		for (const [letter, v] of words) {
			switch (letter) {
				case "G": {
					const code = Math.round(v * 10); // 38.2 -> 382
					if (code === 0 || code === 10 || code === 20 || code === 30) motion = code / 10;
					else if (code >= 382 && code <= 385) {
						motion = 38;
						probeKind = code;
					} else if (code === 40) dwell = true;
					else if (code === 100) g10 = true;
					else if (code === 170 || code === 180 || code === 190) this.plane = code / 10;
					else if (code === 200) inches = true;
					else if (code === 210) inches = false;
					else if (code === 280 || code === 300) home = code / 10;
					else if (code === 281 || code === 301) {
						home = Math.trunc(code / 10);
						storeHome = true;
					} else if (code === 530) g53 = true;
					else if (code >= 540 && code <= 590 && code % 10 === 0) this.wcs = (code - 540) / 10;
					else if (code === 800) motion = 80;
					else if (code === 900) relative = false;
					else if (code === 910) relative = true;
					else if (code === 920) g92 = true;
					else if (code === 921) g92Clear = true;
					else if ([911, 901, 930, 940, 400, 431, 490, 610].includes(code)) {
						// accepted, no effect in the simulation
					} else {
						this.emitText("error:20\r\n");
						return;
					}
					break;
				}
				case "M": {
					const code = Math.round(v);
					if (code === 0 || code === 1) pause = true;
					else if (code === 2 || code === 30) programEnd = true;
					else if (code === 3 || code === 4 || code === 5) this.spindle = code;
					else if (code === 6) {
						// tool change: nothing to do
					} else if (code === 7) this.mist = true;
					else if (code === 8) this.flood = true;
					else if (code === 9) {
						this.mist = false;
						this.flood = false;
					} else {
						this.emitText("error:20\r\n");
						return;
					}
					break;
				}
				case "X": axes[0] = v; break;
				case "Y": axes[1] = v; break;
				case "Z": axes[2] = v; break;
				case "A": axes[3] = v; break;
				case "F": feed = v; break;
				case "S": speed = v; break;
				case "P": p = v; break;
				case "L": l = v; break;
				case "T": tool = v; break;
				case "I": case "J": case "K": case "R": case "N": break;
				default:
					this.emitText("error:20\r\n");
					return;
			}
		}

		if (!jog) {
			this.relative = relative;
			this.inches = inches;
			if (motion !== null) {
				this.motion = motion;
			}
		}
		const scale = inches ? INCH : 1;
		if (feed !== null && !jog) {
			this.feed = feed * scale;
		}
		if (speed !== null) {
			this.spindleSpeed = speed;
		}
		if (tool !== null) {
			this.tool = Math.trunc(tool);
		}

		const end = this.plannerEnd();
		const targetFor = (machine) => {
			const target = [...end];
			const offset = this.workOffset;
			for (let i = 0; i < 4; ++i) {
				if (axes[i] === null) {
					continue;
				}
				const value = i < 3 ? axes[i] * scale : axes[i];
				if (machine) {
					target[i] = value;
				} else if (relative) {
					target[i] = end[i] + value;
				} else {
					target[i] = value + offset[i];
				}
			}
			return target;
		};
		const anyAxis = axes.some((a) => a !== null);
		const check = this.state === STATE.CHECK;

		let deferOk = false; // answered once a synchronous command completes
		if (dwell && !check) {
			// Grbl empties the planner, dwells, then answers.
			this.enqueue({
				target: end,
				seconds: Math.max(p ?? 0, 0),
				feed: 0,
				dwell: true,
				sync: true,
				after: "ok\r\n",
			});
			deferOk = true;
		}
		if (g10) {
			const index = Math.trunc(p ?? 0);
			const slot = index === 0 ? this.wcs : index - 1;
			if (slot >= 0 && slot < 6 && (l === 2 || l === 20)) {
				for (let i = 0; i < 4; ++i) {
					if (axes[i] === null) {
						continue;
					}
					const value = i < 3 ? axes[i] * scale : axes[i];
					// L2: the offset itself; L20: make the current position read `value`.
					this.wcsOffsets[slot][i] = l === 2 ? value : end[i] - this.g92[i] - value;
				}
			} else {
				this.emitText("error:20\r\n");
				return;
			}
		} else if (home !== null) {
			if (storeHome) {
				if (home === 28) this.g28 = [...end];
				else this.g30 = [...end];
			} else if (!check) {
				const stored = home === 28 ? this.g28 : this.g30;
				if (anyAxis) {
					const via = targetFor(false);
					this.enqueue({
						target: via,
						seconds: distance(end, via) / (this.maxRate(0) / 60),
						feed: this.maxRate(0),
						rapid: true,
					});
				}
				const from = this.plannerEnd();
				this.enqueue({
					target: [...stored],
					seconds: distance(from, stored) / (this.maxRate(0) / 60),
					feed: this.maxRate(0),
					rapid: true,
				});
			}
		} else if (g92) {
			for (let i = 0; i < 4; ++i) {
				if (axes[i] !== null) {
					const value = i < 3 ? axes[i] * scale : axes[i];
					this.g92[i] = end[i] - this.wcsOffsets[this.wcs][i] - value;
				}
			}
		} else if (g92Clear) {
			this.g92 = zeros();
		} else if (anyAxis) {
			const mode = motion ?? (jog ? 1 : this.motion);
			if (mode === 80) {
				this.emitText("error:31\r\n"); // axis words with no motion mode
				return;
			}
			const target = targetFor(g53);
			const length = distance(end, target);
			if (mode === 0) {
				let rate = Number.POSITIVE_INFINITY;
				for (let i = 0; i < 4; ++i) {
					if (target[i] !== end[i]) {
						rate = Math.min(rate, this.maxRate(i));
					}
				}
				if (!Number.isFinite(rate)) {
					rate = this.maxRate(0);
				}
				// Grbl's planner drops a move that goes nowhere (PLAN_EMPTY_BLOCK):
				// the machine stays idle.
				if (!check && length > 0) {
					this.enqueue({ target, seconds: length / (rate / 60), feed: rate, rapid: true });
				}
			} else {
				const rate = jog ? (feed ?? 0) * scale : this.feed;
				if (!(rate > 0)) {
					this.emitText("error:22\r\n"); // undefined feed rate
					return;
				}
				if (!check && mode === 38 && !jog) {
					this.enqueueProbe(end, target, rate, probeKind);
					deferOk = true;
				} else if (!check && length > 0) {
					this.enqueue({ target, seconds: length / (rate / 60), feed: rate, jog });
				}
			}
		}
		if (pause && !check) {
			this.enqueue({ target: this.plannerEnd(), seconds: 0, feed: 0, dwell: true, pause: true });
		}
		if (programEnd) {
			// Program end restores the default modes.
			this.motion = 1;
			this.relative = false;
			this.wcs = 0;
			this.spindle = 5;
			this.mist = false;
			this.flood = false;
			this.g92 = zeros();
			if (!check) {
				this.enqueue({
					target: this.plannerEnd(),
					seconds: 0,
					feed: 0,
					dwell: true,
					after: "[MSG:Pgm End]\r\n",
				});
			}
		}
		if (!deferOk) {
			this.emitText("ok\r\n");
		}
	}

	// Grbl's mc_probe_cycle(): after the planner empties, a probe that starts in
	// the wrong pin state alarms at once; otherwise the bit moves until the pin
	// changes. G38.2/G38.4 alarm when it never does; G38.3/G38.5 just report.
	enqueueProbe(from, target, rate, kind) {
		const away = kind >= 384;
		const reportOnly = kind === 383 || kind === 385;
		const move = { target: [...from], seconds: 0, feed: rate, sync: true, alarm: 0, after: "" };
		if (this.touching(from) !== away) {
			move.alarm = 4;
			move.after = "ok\r\n";
			this.enqueue(move);
			return;
		}
		const t = this.probeContact(from, target, away);
		let stop = [...target];
		if (t !== null) {
			stop = stop.map((_, i) => from[i] + (target[i] - from[i]) * t);
		}
		move.target = stop;
		move.seconds = distance(from, stop) / (rate / 60);
		if (t !== null || reportOnly) {
			this.probe = [...stop];
			this.probeSuccess = t !== null;
		} else {
			this.probeSuccess = false;
			move.alarm = 5;
		}
		move.after = `[PRB:${axesText(this.probe)}:${this.probeSuccess ? "1" : "0"}]\r\nok\r\n`;
		this.enqueue(move);
	}

	touching(at) {
		return this.solids.some((solid) => clip(solid, this.toolRadius, at, at) !== null);
	}

	probeContact(from, to, away) {
		let best = null;
		for (const solid of this.solids) {
			const inside = clip(solid, this.toolRadius, from, to);
			if (!inside) {
				continue;
			}
			// Towards: where the bit first enters a solid. Away: where it leaves
			// the one it started in.
			const t = away ? inside[1] : inside[0];
			if (away && (inside[0] > TOUCH_EPSILON || t >= 1)) {
				continue;
			}
			if (best === null || (away ? t > best : t < best)) {
				best = t;
			}
		}
		return best;
	}

	// ---- motion ------------------------------------------------------------

	enqueue(move) {
		if (this.planner.length === 0) {
			this.moveStart = [...this.mpos];
			this.moveElapsed = 0;
		}
		move.muted = !!move.muted || this.muted;
		this.syncing = this.syncing || !!move.sync;
		this.planner.push(move);
		if (this.state === STATE.IDLE) {
			this.state = move.jog ? STATE.JOG : STATE.RUN;
		}
		this.startMotion();
	}

	startMotion() {
		if (this.tickTimer === null) {
			this.lastTick = Date.now();
			this.tickTimer = this.interval(TICK_MS, () => this.tick());
		}
	}

	flushMotion() {
		this.planner = [];
		this.moveElapsed = 0;
		this.moveStart = [...this.mpos];
		this.clearTimer(this.tickTimer);
		this.tickTimer = null;
	}

	tick() {
		const now = Date.now();
		let dt = ((now - this.lastTick) / 1000) * this.speed;
		this.lastTick = now;
		if (this.state === STATE.HOLD || this.state === STATE.ALARM || this.state === STATE.HOME) {
			return;
		}
		while (dt > 0 && this.planner.length > 0) {
			const move = this.planner[0];
			const percent = move.dwell || move.jog ? 100 : move.rapid ? this.overrides[1] : this.overrides[0];
			const rate = percent / 100;
			const left = (move.seconds - this.moveElapsed) / rate; // wall seconds
			if (dt < left) {
				this.moveElapsed += dt * rate;
				const t = move.seconds > 0 ? this.moveElapsed / move.seconds : 1;
				for (let i = 0; i < 4; ++i) {
					this.mpos[i] = this.moveStart[i] + (move.target[i] - this.moveStart[i]) * t;
				}
				dt = 0;
				break;
			}
			dt -= Math.max(left, 0);
			this.mpos = [...move.target];
			const { pause } = move;
			if (move.alarm) {
				this.state = STATE.ALARM;
				this.sdRun = null;
				this.emitText(`ALARM:${move.alarm}\r\n`);
			}
			if (this.reportAfterDwell && move.dwell && move.sync) {
				this.emitText(`${this.statusReport()}\r\n`);
			}
			let after = move.after || "";
			if (move.muted && after.endsWith("ok\r\n")) {
				after = after.slice(0, -4);
			}
			if (after) {
				this.emitText(after);
			}
			if (move.sync) {
				this.syncing = false;
			}
			this.planner.shift();
			this.moveElapsed = 0;
			this.moveStart = [...this.mpos];
			this.acceptPending();
			if (pause) {
				this.beforeHold = STATE.RUN;
				this.state = STATE.HOLD;
				break;
			}
		}
		if (this.planner.length === 0 && (this.state === STATE.RUN || this.state === STATE.JOG)) {
			this.state = STATE.IDLE;
			this.clearTimer(this.tickTimer);
			this.tickTimer = null;
		}
	}

	// ---- reports -----------------------------------------------------------

	statusReport() {
		const mask = toNumber(this.getSetting("$10"));
		const flags = Number.isFinite(mask) ? Math.trunc(mask) : 1;
		const units = this.getSetting("$13") === "1" ? INCH : 1;
		let report = `<${this.state}`;
		report += flags & 1 ? `|MPos:${axesText(this.mpos, units)}` : `|WPos:${axesText(this.workPosition, units)}`;
		if (flags & 2) {
			report += `|Bf:${PLANNER_SIZE - Math.min(this.planner.length, PLANNER_SIZE)},128`;
		}
		const moving = this.planner.length > 0 && (this.state === STATE.RUN || this.state === STATE.JOG);
		const feed = moving ? this.planner[0].feed : 0;
		const spindle = this.spindle === 5 ? 0 : this.spindleSpeed;
		report += `|FS:${roundHalfAway(feed / units)},${spindle}`;
		if (this.probeTriggered) {
			report += "|Pn:P";
		}
		report += `|Ov:${this.overrides.join(",")}`;
		report += `|WCO:${axesText(this.workOffset, units)}`;
		if (this.grblHal) {
			report += this.homed ? "|H:1" : "|H:0";
		}
		if (this.sdRun) {
			// grblHAL: the file's progress while it runs.
			const done = this.sdRun.size === 0 ? 100 : (100 * this.sdRun.consumed) / this.sdRun.size;
			report += `|SD:${Math.min(done, 100).toFixed(1)},${this.sdRun.name}`;
		}
		return `${report}>`;
	}

	// Every field, the card's presence among them.
	completeStatusReport() {
		let report = this.statusReport().slice(0, -1);
		if (!this.sdRun) {
			report += "|SD:1";
		}
		return `${report}|FW:grblHAL>`;
	}

	parserState() {
		let coolant = "";
		if (this.mist) coolant += " M7";
		if (this.flood) coolant += " M8";
		if (coolant === "") coolant = " M9";
		return (
			`[GC:G${this.motion === 38 ? 1 : this.motion} G${54 + this.wcs} G${this.plane}` +
			`${this.inches ? " G20" : " G21"}${this.relative ? " G91" : " G90"} G94 M${this.spindle}${coolant}` +
			` T${this.tool} F${roundHalfAway(this.feed / (this.inches ? INCH : 1))} S${this.spindleSpeed}]`
		);
	}

	// ---- grblHAL: the SD card ---------------------------------------------

	executeGrblHal(command, line) {
		// The extended queries: nothing to describe.
		if (["$ES", "$ESH", "$EG", "$EA", "$EE", "$SPINDLES", "$SPINDLESH", "$FM"].includes(command)) {
			this.emitText("ok\r\n");
			return true;
		}
		if (command === "$REBOOT") {
			this.emitText("ok\r\n");
			this.realtime(0x18);
			return true;
		}
		if (command === "$F" || command === "$F+") {
			let out = "";
			for (const [name, data] of this.sdFiles) {
				if (command === "$F+" || isCncFile(name)) {
					out += `[FILE:/${name}|SIZE:${data.length}${usableSdName(name) ? "" : "|UNUSABLE"}]\r\n`;
				}
			}
			this.emitText(`${out}ok\r\n`);
			return true;
		}
		if (command.startsWith("$FD=")) {
			this.emitText(this.sdFiles.delete(sdName(line.slice(4))) ? "ok\r\n" : "error:61\r\n");
			return true;
		}
		if (command.startsWith("$F=")) {
			const name = sdName(line.slice(3));
			if (!this.sdFiles.has(name)) {
				this.emitText("error:61\r\n"); // file open failed
				return true;
			}
			if (this.state !== STATE.IDLE || this.sdRun) {
				this.emitText("error:8\r\n");
				return true;
			}
			const data = this.sdFiles.get(name);
			this.sdRun = { name, lines: data.split("\n"), size: data.length, consumed: 0 };
			// split() keeps a trailing empty piece the C++ port does not produce.
			if (this.sdRun.lines[this.sdRun.lines.length - 1] === "") {
				this.sdRun.lines.pop();
			}
			this.emitText("ok\r\n");
			this.feedSdRun();
			return true;
		}
		return false;
	}

	// The run's lines go in as the planner takes them, unanswered.
	feedSdRun() {
		while (
			this.sdRun &&
			this.waiting.length === 0 &&
			!this.syncing &&
			this.planner.length < PLANNER_SIZE &&
			this.state !== STATE.HOLD &&
			this.state !== STATE.ALARM
		) {
			if (this.sdRun.lines.length === 0) {
				if (this.planner.length === 0) {
					this.sdRun = null; // the last move is done
				}
				return;
			}
			const line = this.sdRun.lines.shift();
			this.sdRun.consumed += line.length + 1;
			if (line.trim() === "") {
				continue;
			}
			this.muted = true;
			this.execute(line);
			this.muted = false;
		}
	}

	// ---- grblHAL: YMODEM (ymodem.c, receiving) ----------------------------

	receiveYmodem(byte) {
		// A transfer that goes quiet is given up.
		this.clearTimer(this.ymodemTimer);
		this.ymodemTimer = this.timeout(10000, () => {
			this.ymodemTimer = null;
			this.ymodem = null;
		});
		const rx = this.ymodem;
		if (rx.packet.length === 0) {
			if (byte === EOT) {
				// The file is complete: its padding is cut off at its size.
				if (rx.open) {
					this.sdFiles.set(
						rx.name,
						Buffer.from(rx.data).subarray(0, Math.min(rx.size, rx.data.length)).toString("latin1"),
					);
				}
				this.emitText(String.fromCharCode(ACK));
				this.ymodem = null;
				this.clearTimer(this.ymodemTimer);
				return;
			}
			if (byte === CAN) {
				this.ymodem = null;
				this.clearTimer(this.ymodemTimer);
				return;
			}
			if (byte !== SOH && byte !== STX) {
				return; // noise between packets
			}
		}
		rx.packet.push(byte);
		const size = (rx.packet[0] === SOH ? 128 : 1024) + 5;
		if (rx.packet.length === size) {
			this.ymodemPacket();
		}
	}

	ymodemPacket() {
		const rx = this.ymodem;
		const packet = rx.packet;
		rx.packet = [];
		const seq = packet[1];
		const complement = packet[2];
		const block = packet.slice(3, packet.length - 2);
		const crc = (packet[packet.length - 2] << 8) | packet[packet.length - 1];
		const nak = String.fromCharCode(NAK);
		if (seq + complement !== 0xff || crc16Xmodem(block) !== crc) {
			this.emitText(nak);
			return;
		}
		if (!rx.open) {
			// The header: "/name", NUL, the size; an empty name ends the batch.
			if (seq !== 0) {
				this.emitText(nak);
				return;
			}
			const nul = block.indexOf(0);
			const name = sdName(Buffer.from(nul < 0 ? block : block.slice(0, nul)).toString("latin1"));
			if (name === "") {
				this.emitText(String.fromCharCode(ACK));
				this.ymodem = null;
				this.clearTimer(this.ymodemTimer);
				return;
			}
			const rest = nul < 0 ? "" : Buffer.from(block.slice(nul + 1)).toString("latin1");
			const size = toNumber(rest.split(/[ \0]/)[0]);
			rx.open = true;
			rx.name = name;
			rx.size = Number.isFinite(size) && size > 0 ? Math.trunc(size) : 0;
			rx.expected = 1;
			this.emitText(String.fromCharCode(ACK, CRC));
			return;
		}
		if (seq === rx.expected) {
			rx.data.push(...block);
			rx.expected = (rx.expected + 1) & 0xff;
			this.emitText(String.fromCharCode(ACK));
		} else if (seq === ((rx.expected - 1) & 0xff)) {
			this.emitText(String.fromCharCode(ACK)); // the last one again: its ACK was lost
		} else {
			this.emitText(nak);
		}
	}
}

export { GrblSimulator, STATE as SIMULATOR_STATE };
export default GrblSimulator;
