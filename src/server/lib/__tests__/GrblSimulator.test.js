/**
 * @jest-environment node
 */
// The simulated board: a port of the C++ application's tests of its own
// GrblSimulator (cppport/tests/core/test_simulator.cpp), then the board seen
// through SimulatedPort and SerialConnection as the server uses it.
process.env.GSENDER_LOG_LEVEL = process.env.GSENDER_LOG_LEVEL || "error";

const GrblSimulator = require("../Simulator/GrblSimulator").default;
const { touchPlateOnCorner } = require("../Simulator/GrblSimulator");
const SimulatedPort = require("../Simulator/SimulatedPort").default;
const {
	SIMULATOR_PORT,
	SIMULATOR_HAL_PORT,
	isSimulatorPath,
} = require("../Simulator/SimulatedPort");
const SerialConnection = require("../SerialConnection").default;

describe("GrblSimulator", () => {
	let sim;
	let output;

	// What the board posted, delivered by flush(): output is never delivered
	// from inside the call that caused it.
	let posted;
	const post = (fn) => posted.push(fn);
	const flush = () => {
		while (posted.length > 0) {
			posted.shift()();
		}
	};
	const advance = (ms) => {
		jest.advanceTimersByTime(ms);
		flush();
	};
	const newSimulator = (options = {}) => {
		const simulator = new GrblSimulator({ post, ...options });
		simulator.onData = (text) => {
			output += text;
		};
		return simulator;
	};
	const send = (text) => {
		sim.send(text);
		advance(0);
	};
	// Output since the last call.
	const take = () => {
		const out = output;
		output = "";
		return out;
	};
	const count = (text, char) => text.split(char).length - 1;

	beforeEach(() => {
		jest.useFakeTimers();
		output = "";
		posted = [];
		sim = newSimulator();
		sim.open();
		advance(100); // the banner
		take();
	});

	afterEach(() => {
		sim.close();
		jest.useRealTimers();
	});

	test("boots with the Grbl banner", () => {
		const fresh = new GrblSimulator({ post });
		let banner = "";
		fresh.onData = (text) => {
			banner += text;
		};
		fresh.open();
		advance(99);
		expect(banner).toBe("");
		advance(1);
		expect(banner).toBe("\r\nGrbl 1.1h ['$' for help]\r\n");
		fresh.close();
	});

	test("reports status, parser state and settings", () => {
		send("?");
		expect(take()).toBe(
			"<Idle|MPos:0.000,0.000,0.000|FS:0,0|Ov:100,100,100|WCO:0.000,0.000,0.000>\r\n",
		);
		send("$G\n");
		expect(take()).toBe("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]\r\nok\r\n");
		send("$I\n");
		expect(take()).toBe("[VER:1.1h.20190825:]\r\n[OPT:V,15,128]\r\nok\r\n");
		send("$$\n");
		const settings = take();
		expect(settings).toContain("$110=4000.000\r\n");
		expect(settings.endsWith("ok\r\n")).toBe(true);
		send("$110=2000\n");
		expect(take()).toBe("ok\r\n");
		send("$999=1\n");
		expect(take()).toBe("error:3\r\n");
	});

	test("moves take the time their feed gives", () => {
		send("G1 X10 F600\n"); // 10 mm/s
		expect(take()).toBe("ok\r\n");
		expect(sim.activeState).toBe("Run");
		advance(500);
		expect(sim.machinePosition[0]).toBeCloseTo(5, 0);
		advance(600);
		expect(sim.machinePosition[0]).toBe(10);
		expect(sim.activeState).toBe("Idle");

		send("G0 X110\n"); // a rapid at $110 = 4000 mm/min takes 1.5 s
		advance(1400);
		expect(sim.machinePosition[0]).toBeLessThan(110);
		advance(200);
		expect(sim.machinePosition[0]).toBe(110);
	});

	test("feed hold freezes motion until cycle start", () => {
		send("G1 X10 F600\n");
		advance(300);
		send("!");
		const held = sim.machinePosition[0];
		advance(1000);
		expect(sim.activeState).toBe("Hold:0");
		expect(sim.machinePosition[0]).toBe(held);
		send("~");
		expect(sim.activeState).toBe("Run");
		advance(1000);
		expect(sim.machinePosition[0]).toBe(10);
	});

	test("a full planner delays the ok", () => {
		let lines = "";
		for (let i = 1; i <= 20; ++i) {
			lines += `G1 X${i} F600\n`;
		}
		send(lines);
		expect(count(take(), "k")).toBe(15); // one "ok" per planned block
		advance(250); // two blocks done
		expect(count(take(), "k")).toBeGreaterThanOrEqual(2);
	});

	test("work coordinates follow G10 and G92", () => {
		send("G0 X10 Y5\n");
		advance(1000);
		send("G10 L20 P0 X0 Y0\n");
		take();
		expect(sim.workPosition[0]).toBe(0);
		send("$#\n");
		expect(take()).toContain("[G54:10.000,5.000,0.000]");
		send("G92 X1\n");
		expect(sim.workPosition[0]).toBe(1);
		send("G92.1\nG0 X0\n");
		advance(1000);
		expect(sim.machinePosition[0]).toBe(10); // G54 X0
	});

	test("errors, alarms and unlocking", () => {
		send("G1 X1\n"); // no feed rate yet
		expect(take()).toBe("error:22\r\n");
		send("G87\n");
		expect(take()).toBe("error:20\r\n");
		send("G1 X50 F600\n");
		advance(100);
		take();
		send("\x18"); // a reset while moving loses the position
		expect(take()).toBe(
			"ALARM:3\r\n\r\nGrbl 1.1h ['$' for help]\r\n[MSG:'$H'|'$X' to unlock]\r\n",
		);
		expect(sim.activeState).toBe("Alarm");
		send("G0 X0\n");
		expect(take()).toBe("error:9\r\n");
		send("$X\n");
		expect(take()).toBe("[MSG:Caution: Unlocked]\r\nok\r\n");
		expect(sim.activeState).toBe("Idle");
	});

	test("a word that is not a letter and a number is a bad number format", () => {
		send("G1 Xabc F600\n");
		expect(take()).toBe("error:2\r\n");
		send("(just a comment)\n");
		expect(take()).toBe("ok\r\n");
		send("G21 (units) G90 ; and more\n");
		expect(take()).toBe("ok\r\n");
	});

	test("homing ends at machine zero", () => {
		send("G0 X10\n");
		advance(1000);
		take();
		send("$H\n");
		expect(sim.activeState).toBe("Home");
		expect(take()).toBe(""); // the ok comes when homing is done
		advance(1500);
		expect(take()).toBe("ok\r\n");
		expect(sim.activeState).toBe("Idle");
		expect(sim.machinePosition[0]).toBe(0);
	});

	test("jogs run until cancelled", () => {
		send("$J=G21G91X10F600\n");
		expect(take()).toBe("ok\r\n");
		expect(sim.activeState).toBe("Jog");
		advance(300);
		send("\x85");
		expect(sim.activeState).toBe("Idle");
		const stopped = sim.machinePosition[0];
		expect(stopped).toBeGreaterThan(0);
		expect(stopped).toBeLessThan(10);
		advance(1000);
		expect(sim.machinePosition[0]).toBe(stopped);
		send("$J=G91 X1\n"); // a jog needs its own feed rate
		expect(take()).toBe("error:22\r\n");
	});

	test("realtime commands written as UTF-8 strings are read as the one byte", () => {
		// node-serialport UTF-8 encodes a JavaScript string: 0x91 arrives as C2 91.
		sim.send(Buffer.from("\x91", "utf8"));
		expect(sim.overrides[0]).toBe(110);
		sim.send(Buffer.from("\x85", "utf8"));
		expect(sim.activeState).toBe("Idle");
	});

	test("feed overrides change the speed", () => {
		send("\x91");
		expect(sim.overrides[0]).toBe(110);
		for (let i = 0; i < 10; ++i) {
			send("\x91");
		}
		expect(sim.overrides[0]).toBe(200); // capped
		send("G1 X10 F600\n");
		advance(520); // twice as fast
		expect(sim.machinePosition[0]).toBe(10);
	});

	test("a dwell answers once the planner empties and the time is up", () => {
		send("G1 X10 F600\n"); // 1 s
		expect(take()).toBe("ok\r\n");
		send("G4 P0.5\n");
		send("G0 X0\n"); // waits behind the dwell, like Grbl's serial buffer
		advance(1400);
		expect(take()).toBe("");
		advance(200);
		expect(take()).toBe("ok\r\nok\r\n");
	});

	test("probes stop where the bit touches the plate", () => {
		sim.setProbeSolids([{ min: [-10, -10, -20], max: [40, 40, -5] }]);
		sim.setToolRadius(3);
		send("G91 G38.2 Z-10 F600\n"); // the plate top is 5 below
		expect(take()).toBe(""); // answered when the probe completes
		advance(600);
		expect(take()).toBe("[PRB:0.000,0.000,-5.000:1]\r\nok\r\n");
		expect(sim.machinePosition[2]).toBe(-5);
		send("?");
		expect(take()).toContain("|Pn:P|");

		send("G38.2 Z-1\n"); // already touching
		advance(50);
		expect(take()).toBe("ALARM:4\r\nok\r\n");
		send("$X\n");
		take();

		// The bit's radius counts: a side probe stops 3 mm short of the face.
		send("G0 Z2\n");
		send("G0 X-20\n");
		send("G0 Z-3\n");
		advance(2000);
		take();
		send("G38.2 X15 F600\n");
		advance(1000);
		expect(take()).toBe("[PRB:-13.000,0.000,-6.000:1]\r\nok\r\n");
	});

	test("a missed probe alarms unless it only reports", () => {
		send("G91 G38.3 Z-2 F600\n");
		advance(300);
		expect(take()).toBe("[PRB:0.000,0.000,-2.000:0]\r\nok\r\n");
		send("G38.2 Z-2\n");
		advance(300);
		expect(take()).toBe("ALARM:5\r\n[PRB:0.000,0.000,-2.000:0]\r\nok\r\n");
		send("G0 X1\n");
		expect(take()).toBe("error:9\r\n");
	});

	test("a touch plate hooked over a stock corner is found by a Z probe", () => {
		sim.setProbeSolids(touchPlateOnCorner(0, 0, 0, 0, 15, 10, 50, 10));
		sim.setToolRadius(0);
		// Over the plate, 20 mm above the stock: the plate top is 15 above it.
		send("G90 G0 X-5 Y-5 Z20\n");
		advance(2000);
		take();
		send("G38.2 Z-30 F600\n");
		advance(2000);
		expect(take()).toBe("[PRB:-5.000,-5.000,15.000:1]\r\nok\r\n");
	});

	test("check mode validates without moving", () => {
		send("$C\n");
		expect(take()).toBe("[MSG:Enabled]\r\nok\r\n");
		send("G1 X10 F600\n");
		expect(take()).toBe("ok\r\n");
		advance(2000);
		expect(sim.machinePosition[0]).toBe(0);
		send("?");
		expect(take().startsWith("<Check|")).toBe(true);
	});

	test("M0 holds the job until cycle start", () => {
		send("G1 X5 F600\nM0\nG1 X10\n");
		advance(700);
		expect(sim.activeState).toBe("Hold:0");
		expect(sim.machinePosition[0]).toBe(5);
		send("~");
		advance(700);
		expect(sim.machinePosition[0]).toBe(10);
	});

	describe("grblHAL", () => {
		beforeEach(() => {
			sim.close();
			output = "";
			sim = newSimulator({ grblHal: true });
			sim.open();
			advance(100);
		});

		test("takes its settings, reports homing and reboots", () => {
			expect(output).toContain("GrblHAL 1.1f");
			// The line is read without its spaces; grblHAL takes numbered
			// settings it did not list.
			output = "";
			send("$9 = 1\n");
			expect(take()).toBe("ok\r\n");
			send("$110 = 2500\n");
			expect(take()).toBe("ok\r\n");
			send("$$\n");
			const settings = take();
			expect(settings).toContain("$110=2500\r\n");
			expect(settings).toContain("$9=1\r\n");
			send("$X9=1\n");
			expect(take()).toBe("error:3\r\n");
			// H: says whether the machine has homed.
			send("?");
			expect(take()).toContain("|H:0");
			send("$H\n");
			advance(2000);
			take();
			send("?");
			expect(take()).toContain("|H:1");
			// $REBOOT restarts it, as a reset.
			send("$REBOOT\n");
			const reboot = take();
			expect(reboot.startsWith("ok\r\n")).toBe(true);
			expect(reboot).toContain("GrblHAL 1.1f");
		});

		test("identifies itself and lists the SD card", () => {
			output = "";
			send("$I\n");
			expect(take()).toContain("[FIRMWARE:grblHAL]");
			sim.putSdFile("part.nc", "G21\nG1 X1 F600\n");
			sim.putSdFile("notes.pdf", "x");
			send("$F\n");
			expect(take()).toBe("[FILE:/part.nc|SIZE:15]\r\nok\r\n");
			send("$F+\n");
			expect(take()).toContain("notes.pdf");
			send("$F=missing.nc\n");
			expect(take()).toBe("error:61\r\n");
		});

		test("runs a file from the SD card, unanswered", () => {
			sim.putSdFile("run.nc", "G21\nG1 X5 F600\n");
			output = "";
			send("$F=run.nc\n");
			expect(take()).toBe("ok\r\n"); // only the command is answered
			advance(100);
			send("?");
			expect(take()).toContain("|SD:");
			advance(1000);
			expect(sim.machinePosition[0]).toBe(5);
			expect(sim.activeState).toBe("Idle");
		});
	});
});

describe("SimulatedPort and SerialConnection", () => {
	test("the simulated ports are recognised by name", () => {
		expect(isSimulatorPath(SIMULATOR_PORT)).toBe(true);
		expect(isSimulatorPath(SIMULATOR_HAL_PORT)).toBe(true);
		expect(isSimulatorPath("COM3")).toBe(false);
		expect(isSimulatorPath("192.168.5.1")).toBe(false);
	});

	// A real-time wait for lines to arrive (the stack above runs on real timers).
	const until = (done, ms = 3000) =>
		new Promise((resolve, reject) => {
			const start = Date.now();
			const check = () => {
				if (done()) resolve();
				else if (Date.now() - start > ms) reject(new Error("timed out"));
				else setTimeout(check, 10);
			};
			check();
		});

	test("SerialConnection opens the simulator like a serial port", async () => {
		const connection = new SerialConnection({ path: SIMULATOR_PORT, baudRate: 115200 });
		const lines = [];
		connection.on("data", (line) => lines.push(String(line).trim()));
		await new Promise((resolve, reject) => {
			connection.open((err) => (err ? reject(err) : resolve()));
		});
		expect(connection.isOpen).toBe(true);
		await until(() => lines.some((l) => l.startsWith("Grbl 1.1h")));

		connection.write("$I\n");
		await until(() => lines.includes("ok"));
		expect(lines).toContain("[VER:1.1h.20190825:]");

		// A realtime command the controller writes as a string.
		connection.writeImmediate("?");
		await until(() => lines.some((l) => l.startsWith("<Idle|")));

		await new Promise((resolve) => connection.close(resolve));
		expect(connection.isOpen).toBeFalsy();
	});

	test("the grblHAL simulator announces itself as grblHAL", async () => {
		const connection = new SerialConnection({ path: SIMULATOR_HAL_PORT, baudRate: 115200 });
		const lines = [];
		connection.on("data", (line) => lines.push(String(line).trim()));
		await new Promise((resolve, reject) => {
			connection.open((err) => (err ? reject(err) : resolve()));
		});
		await until(() => lines.some((l) => /grblhal/i.test(l)));
		await new Promise((resolve) => connection.close(resolve));
	});

	test("the port takes byte arrays, as the YMODEM sender writes them", async () => {
		const port = new SimulatedPort(SIMULATOR_HAL_PORT);
		const received = [];
		port.on("data", (chunk) => received.push(...chunk));
		await new Promise((resolve) => port.open(resolve));
		await until(() => received.length > 10);
		expect(() => port.write([0x3f])).not.toThrow();
		await until(() => Buffer.from(received).toString("latin1").includes("<Idle|"));
		await new Promise((resolve) => port.close(resolve));
		expect(port.isOpen).toBe(false);
	});
});
