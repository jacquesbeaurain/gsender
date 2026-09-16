#!/usr/bin/env node

/**
 * A grbl / grblHAL simulator that speaks the line protocol over TCP.
 *
 * gSender's SerialConnection already opens a net.Socket instead of a SerialPort
 * when the target looks like an IPv4 address (src/server/lib/SerialConnection.js),
 * so pointing the app's Ethernet connection at 127.0.0.1 reaches this process
 * with no changes to gSender itself.
 *
 *   node scripts/cnc-sim            # grbl on 127.0.0.1:2323
 *   node scripts/cnc-sim --firmware=grblhal --port=2323
 *
 * Then in gSender: Settings -> Ethernet -> Connect to IP 127.0.0.1, port 2323,
 * and use the Ethernet button in the connection dropdown.
 *
 * Type `help` in this process's terminal for runtime fault injection.
 */

const net = require('net');
const readline = require('readline');

const { Machine, RX_BUFFER_BYTES } = require('./machine');
const {
    statusReport,
    startupBanner,
    executeLine,
    isSynchronizing,
} = require('./protocol');

const TICK_MS = 50;
const WCO_EVERY_N_REPORTS = 10;

// Realtime bytes, intercepted before line assembly exactly as grbl's ISR does.
const RT_STATUS = 0x3f; // ?
const RT_CYCLE_START = 0x7e; // ~
const RT_FEED_HOLD = 0x21; // !
const RT_RESET = 0x18; // Ctrl-X
const RT_SAFETY_DOOR = 0x84;
const RT_JOG_CANCEL = 0x85;
const RT_COMPLETE_REPORT = 0x87; // grblHAL
const RT_FEED_100 = 0x90;
const RT_FEED_PLUS_10 = 0x91;
const RT_FEED_MINUS_10 = 0x92;
const RT_FEED_PLUS_1 = 0x93;
const RT_FEED_MINUS_1 = 0x94;
const RT_RAPID_100 = 0x95;
const RT_RAPID_50 = 0x96;
const RT_RAPID_25 = 0x97;
const RT_SPINDLE_100 = 0x99;
const RT_SPINDLE_PLUS_10 = 0x9a;
const RT_SPINDLE_MINUS_10 = 0x9b;

function parseArgs(argv) {
    const options = {
        host: '127.0.0.1',
        port: 2323,
        firmware: 'grbl',
        axes: 'XYZ',
        banner: true,
        alarmOnConnect: null,
        errorEvery: 0,
        errorCode: 20,
        latency: 0,
        probeZ: -10,
        quiet: false,
    };

    argv.forEach((arg) => {
        const [rawKey, rawValue] = arg.split('=');
        const key = rawKey.replace(/^--/, '');
        const value = rawValue;

        switch (key) {
            case 'host':
                options.host = value;
                break;
            case 'port':
                options.port = Number(value);
                break;
            case 'firmware':
                options.firmware = String(value).toLowerCase();
                break;
            case 'axes':
                options.axes = String(value).toUpperCase();
                break;
            case 'no-banner':
                options.banner = false;
                break;
            case 'alarm-on-connect':
                options.alarmOnConnect = value === undefined ? 1 : Number(value);
                break;
            case 'error-every':
                options.errorEvery = Number(value);
                break;
            case 'error-code':
                options.errorCode = Number(value);
                break;
            case 'latency':
                options.latency = Number(value);
                break;
            case 'probe-z':
                options.probeZ = value === 'never' ? null : Number(value);
                break;
            case 'quiet':
                options.quiet = true;
                break;
            case 'help':
                printUsage();
                process.exit(0);
                break;
            default:
                console.error(`Unknown option: ${arg}`);
                printUsage();
                process.exit(1);
        }
    });

    if (!['grbl', 'grblhal'].includes(options.firmware)) {
        console.error(`--firmware must be grbl or grblhal`);
        process.exit(1);
    }

    return options;
}

function printUsage() {
    console.log(`
gSender CNC simulator

  node scripts/cnc-sim [options]

Options
  --host=<ip>            Listen address (default 127.0.0.1)
  --port=<n>             Listen port (default 2323; avoid 23, privileged on macOS)
  --firmware=grbl|grblhal
  --axes=XYZ             Axis letters, e.g. XYZA
  --no-banner            Suppress the startup banner, to exercise gSender's
                         $I firmware-detection fallback
  --alarm-on-connect[=n] Come up alarm-locked with ALARM:n (default 1)
  --error-every=<n>      Reject every nth G-code line
  --error-code=<n>       Code used by --error-every (default 20)
  --latency=<ms>         Delay every outgoing write
  --probe-z=<mm|never>   Z where a G38.x probe makes contact (default -10)
  --quiet                Do not log traffic

Runtime commands (type into this terminal): help
`);
}

class Session {
    constructor(socket, options) {
        this.socket = socket;
        this.options = options;
        this.machine = new Machine({
            axes: options.axes.split(''),
            firmware: options.firmware,
            probeTriggerZ: options.probeZ,
        });

        this.rxBuffer = '';
        this.pendingLines = [];
        this.deferred = null; // blocks the drain loop until resolved
        this.reportCount = 0;
        this.lineCount = 0;
        this.injectError = null;
        this.closed = false;

        socket.setNoDelay(true);
        socket.on('data', (chunk) => this.onData(chunk));
        socket.on('error', (err) => {
            this.log(`socket error: ${err.message}`);
        });
        socket.on('close', () => {
            this.closed = true;
            clearInterval(this.timer);
        });

        this.timer = setInterval(() => this.tick(), TICK_MS);

        if (options.banner) {
            // Real boards take a moment to come up after the port opens.
            setTimeout(() => this.send(startupBanner(options.firmware)), 250);
        }

        if (options.alarmOnConnect !== null) {
            setTimeout(() => {
                this.machine.alarm(options.alarmOnConnect);
                this.send(`ALARM:${options.alarmOnConnect}`);
            }, 400);
        }
    }

    log(message) {
        if (!this.options.quiet) {
            console.log(message);
        }
    }

    send(line) {
        if (this.closed || this.socket.destroyed) {
            return;
        }
        const write = () => {
            if (this.closed || this.socket.destroyed) {
                return;
            }
            this.socket.write(`${line}\r\n`);
            if (!this.options.quiet && !line.startsWith('<')) {
                this.log(`  < ${line}`);
            }
        };
        if (this.options.latency > 0) {
            setTimeout(write, this.options.latency);
        } else {
            write();
        }
    }

    onData(chunk) {
        let text = '';

        for (const byte of chunk) {
            if (this.handleRealtime(byte)) {
                continue;
            }
            text += String.fromCharCode(byte);
        }

        this.rxBuffer += text;

        let index;
        while ((index = this.rxBuffer.indexOf('\n')) >= 0) {
            const line = this.rxBuffer.slice(0, index).replace(/\r$/, '');
            this.rxBuffer = this.rxBuffer.slice(index + 1);
            this.pendingLines.push(line);
        }
    }

    /** Returns true if the byte was a realtime command and should not be buffered. */
    handleRealtime(byte) {
        const machine = this.machine;

        switch (byte) {
            case RT_STATUS:
                this.sendStatusReport();
                return true;
            case RT_COMPLETE_REPORT:
                this.sendStatusReport({ complete: true });
                return true;
            case RT_FEED_HOLD:
                machine.hold();
                this.log('  ! feed hold');
                return true;
            case RT_CYCLE_START:
                machine.resume();
                this.log('  ~ cycle start');
                return true;
            case RT_RESET:
                machine.clearMotion();
                machine.clearAlarm();
                machine.activeState = 'Idle';
                this.pendingLines = [];
                this.rxBuffer = '';
                this.deferred = null;
                this.log('  ^X soft reset');
                if (this.options.banner) {
                    this.send(startupBanner(this.options.firmware));
                }
                return true;
            case RT_SAFETY_DOOR:
                machine.activeState = 'Door';
                this.log('  door opened');
                return true;
            case RT_JOG_CANCEL:
                machine.clearMotion();
                if (machine.activeState === 'Jog') {
                    machine.activeState = 'Idle';
                }
                return true;
            case RT_FEED_100:
                machine.overrides.feed = 100;
                return true;
            case RT_FEED_PLUS_10:
                machine.overrides.feed = Math.min(200, machine.overrides.feed + 10);
                return true;
            case RT_FEED_MINUS_10:
                machine.overrides.feed = Math.max(10, machine.overrides.feed - 10);
                return true;
            case RT_FEED_PLUS_1:
                machine.overrides.feed = Math.min(200, machine.overrides.feed + 1);
                return true;
            case RT_FEED_MINUS_1:
                machine.overrides.feed = Math.max(10, machine.overrides.feed - 1);
                return true;
            case RT_RAPID_100:
                machine.overrides.rapid = 100;
                return true;
            case RT_RAPID_50:
                machine.overrides.rapid = 50;
                return true;
            case RT_RAPID_25:
                machine.overrides.rapid = 25;
                return true;
            case RT_SPINDLE_100:
                machine.overrides.spindle = 100;
                return true;
            case RT_SPINDLE_PLUS_10:
                machine.overrides.spindle = Math.min(
                    200,
                    machine.overrides.spindle + 10,
                );
                return true;
            case RT_SPINDLE_MINUS_10:
                machine.overrides.spindle = Math.max(
                    10,
                    machine.overrides.spindle - 10,
                );
                return true;
            default:
                return false;
        }
    }

    sendStatusReport(opts = {}) {
        this.reportCount += 1;
        const includeWCO = this.reportCount % WCO_EVERY_N_REPORTS === 1;
        this.machine.rxAvailable = Math.max(
            0,
            RX_BUFFER_BYTES - this.pendingLines.join('\n').length,
        );
        this.socket.write(
            `${statusReport(this.machine, { includeWCO, ...opts })}\r\n`,
        );
    }

    tick() {
        const messages = this.machine.tick(TICK_MS);

        messages.forEach((message) => {
            if (message.type === 'homed') {
                if (this.deferred === 'homing') {
                    this.deferred = null;
                    this.send('ok');
                }
            } else if (message.type === 'probe') {
                const { position, success } = this.machine.probeResult;
                this.send(
                    `[PRB:${this.machine.axes.map((axis) => position[axis].toFixed(3)).join(',')}:${success}]`,
                );
                if (!success) {
                    // G38.2 that never made contact is an alarm on real grbl.
                    this.machine.alarm(5);
                    this.send('ALARM:5');
                }
            }
        });

        this.drain();
    }

    /**
     * Feed queued lines into the planner while it has room. This is what gives
     * gSender's character-counting sender realistic back-pressure: `ok` only
     * comes once the block is accepted.
     */
    drain() {
        while (
            !this.deferred &&
            this.pendingLines.length &&
            this.machine.hasBufferSpace()
        ) {
            // A synchronizing command has to wait for the planner to run dry,
            // or it would read/write offsets against a position still in motion.
            if (isSynchronizing(this.pendingLines[0]) && this.machine.isMoving()) {
                return;
            }
            const line = this.pendingLines.shift();
            this.processLine(line);
        }
    }

    processLine(line) {
        if (line.trim()) {
            this.log(`  > ${line}`);
        }

        this.lineCount += 1;

        if (this.injectError !== null) {
            const code = this.injectError;
            this.injectError = null;
            this.send(`error:${code}`);
            return;
        }

        if (
            this.options.errorEvery > 0 &&
            !line.trim().startsWith('$') &&
            line.trim() &&
            this.lineCount % this.options.errorEvery === 0
        ) {
            this.send(`error:${this.options.errorCode}`);
            return;
        }

        let responses;
        try {
            responses = executeLine(this.machine, line, this.options.firmware);
        } catch (err) {
            this.log(`  !! ${err.message}`);
            this.send('error:2');
            return;
        }

        let deferredOk = false;

        for (const response of responses) {
            if (typeof response === 'object' && response.defer) {
                if (response.defer === 'homing') {
                    this.deferred = 'homing';
                    deferredOk = true;
                } else if (response.defer === 'dwell') {
                    this.deferred = 'dwell';
                    deferredOk = true;
                    setTimeout(
                        () => {
                            this.deferred = null;
                            this.send('ok');
                        },
                        (response.seconds || 0) * 1000,
                    );
                }
                continue;
            }

            this.send(response);
            if (String(response).startsWith('error:')) {
                return;
            }
        }

        if (!deferredOk) {
            this.send('ok');
        }
    }
}

function startRepl(server, getSession, options) {
    // Without a TTY there is nobody to type commands, and wiring up readline
    // would stop the server the moment stdin closed - which is exactly what
    // happens under `yarn sim &`, nohup, or CI.
    if (!process.stdin.isTTY) {
        console.log('(stdin is not a TTY - runtime commands disabled)');
        return;
    }

    const rl = readline.createInterface({
        input: process.stdin,
        output: process.stdout,
        prompt: 'sim> ',
    });

    rl.on('line', (input) => {
        const [command, ...rest] = input.trim().split(/\s+/);
        const session = getSession();

        const needsSession = () => {
            if (!session) {
                console.log('No client connected.');
                return false;
            }
            return true;
        };

        switch (command) {
            case '':
                break;
            case 'help':
                console.log(`
  alarm [code]   Raise ALARM:<code> (default 1) and lock the machine
  unlock         Clear the alarm, as $X would
  hold           Feed hold
  resume         Cycle start
  door           Open the safety door (Door:1)
  error <code>   Reject the next line with error:<code>
  reset          Soft reset, as Ctrl-X would
  probe-z <mm>   Move the probe trigger plane ('never' to disable)
  say <text>     Push a raw line to the client
  status         Print machine state
  drop           Drop the client connection
  quit           Stop the simulator
`);
                break;
            case 'alarm':
                if (needsSession()) {
                    const code = rest[0] ? Number(rest[0]) : 1;
                    session.machine.alarm(code);
                    session.send(`ALARM:${code}`);
                }
                break;
            case 'unlock':
                if (needsSession()) {
                    session.machine.clearAlarm();
                    session.send('[MSG:Caution: Unlocked]');
                }
                break;
            case 'hold':
                if (needsSession()) session.machine.hold();
                break;
            case 'resume':
                if (needsSession()) session.machine.resume();
                break;
            case 'door':
                if (needsSession()) session.machine.activeState = 'Door';
                break;
            case 'error':
                if (needsSession()) {
                    session.injectError = rest[0] ? Number(rest[0]) : 20;
                    console.log(`Next line will return error:${session.injectError}`);
                }
                break;
            case 'reset':
                if (needsSession()) session.handleRealtime(RT_RESET);
                break;
            case 'probe-z':
                if (needsSession()) {
                    session.machine.probeTriggerZ =
                        rest[0] === 'never' ? null : Number(rest[0]);
                    console.log(`Probe trigger Z: ${session.machine.probeTriggerZ}`);
                }
                break;
            case 'say':
                if (needsSession()) session.send(rest.join(' '));
                break;
            case 'status':
                if (needsSession()) {
                    const m = session.machine;
                    console.log({
                        state: m.activeState,
                        mpos: m.mpos,
                        wpos: m.wpos(),
                        wcs: m.modal.wcs,
                        queued: m.queue.length,
                        pendingLines: session.pendingLines.length,
                        overrides: m.overrides,
                    });
                }
                break;
            case 'drop':
                if (needsSession()) session.socket.destroy();
                break;
            case 'quit':
            case 'exit':
                server.close();
                process.exit(0);
                break;
            default:
                console.log(`Unknown command: ${command} (try 'help')`);
        }

        rl.prompt();
    });

    rl.on('close', () => {
        server.close();
        process.exit(0);
    });

    console.log(`Type 'help' for runtime commands.`);
    rl.prompt();
}

function main() {
    const options = parseArgs(process.argv.slice(2));
    let session = null;

    const server = net.createServer((socket) => {
        const peer = `${socket.remoteAddress}:${socket.remotePort}`;
        console.log(`\nClient connected from ${peer}`);

        if (session && !session.closed) {
            // Real boards accept one connection; make the conflict obvious rather
            // than quietly interleaving two clients.
            console.log('Replacing the existing connection.');
            session.socket.destroy();
        }

        session = new Session(socket, options);

        socket.on('close', () => {
            console.log(`Client ${peer} disconnected`);
        });
    });

    server.on('error', (err) => {
        if (err.code === 'EADDRINUSE') {
            console.error(
                `Port ${options.port} is already in use - is another simulator running?`,
            );
        } else {
            console.error(err.message);
        }
        process.exit(1);
    });

    server.listen(options.port, options.host, () => {
        console.log(
            `gSender CNC simulator - ${options.firmware} on ${options.axes} axes`,
        );
        console.log(`Listening on ${options.host}:${options.port}`);
        console.log(
            `\nIn gSender: Settings -> Ethernet -> Connect to IP ${options.host}, Ethernet port ${options.port},` +
                `\nthen use the Ethernet button in the connection dropdown.`,
        );
        startRepl(server, () => session, options);
    });
}

main();
