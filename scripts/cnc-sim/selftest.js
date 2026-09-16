#!/usr/bin/env node

/**
 * Self-test for the CNC simulator.
 *
 * Each scenario starts a simulator on its own port, connects a raw socket, and
 * drives it the way gSender's controllers do - including the exact command
 * forms from src/server/controllers/Grbl/GrblController.js, such as
 * `$J=<units>G91 X.. F..` for jogs and `G10 L20 P0 ...` for zeroing.
 *
 *   yarn sim:test
 *
 * Takes about a minute; it runs real timers rather than faking them, because
 * the things worth testing here (planner back-pressure, deferred `ok`, hold
 * deceleration) are all timing behaviour.
 */

const net = require('net');
const { spawn } = require('child_process');
const path = require('path');

const SIM = path.join(__dirname, 'index.js');
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

let pass = 0;
let fail = 0;
function check(name, cond, detail) {
    if (cond) {
        pass++;
        console.log(`  PASS  ${name}`);
    } else {
        fail++;
        console.log(`  FAIL  ${name}${detail ? ` -- ${detail}` : ''}`);
    }
}

function startSim(args) {
    const proc = spawn(process.execPath, [SIM, '--quiet', ...args], {
        stdio: ['ignore', 'pipe', 'pipe'],
    });
    return proc;
}

function connect(port) {
    return new Promise((resolve, reject) => {
        const sock = net.connect(port, '127.0.0.1');
        const lines = [];
        let buf = '';
        sock.on('data', (d) => {
            buf += d.toString();
            let i;
            while ((i = buf.indexOf('\n')) >= 0) {
                const line = buf.slice(0, i).replace(/\r$/, '');
                buf = buf.slice(i + 1);
                if (line) lines.push(line);
            }
        });
        sock.on('connect', () =>
            resolve({
                sock,
                lines,
                send: (s) => sock.write(s),
                last: (pred) => [...lines].reverse().find(pred),
                clear: () => (lines.length = 0),
            }),
        );
        sock.on('error', reject);
    });
}

async function scenario(name, args, port, fn) {
    console.log(`\n${name}`);
    const proc = startSim([...args, `--port=${port}`]);
    await wait(700);
    try {
        const c = await connect(port);
        await fn(c);
        c.sock.destroy();
    } catch (err) {
        console.log(`  FAIL  scenario threw: ${err.message}`);
        fail++;
    }
    proc.kill();
    await wait(200);
}

(async () => {
    // ---------------------------------------------------------------
    await scenario('grbl handshake', [], 2501, async (c) => {
        await wait(500);
        check(
            'startup banner matches GrblLineParserResultStartup',
            /^Grbl\s+1\.1f\s+\['\$' for help\]$/.test(c.lines[0]),
            c.lines[0],
        );
        check(
            'banner satisfies Connection.js firmware detect (/grbl/i)',
            /.*(grbl|fluidnc).*/i.test(c.lines[0]) &&
                !/.*(grblhal).*/i.test(c.lines[0]),
        );

        c.clear();
        c.send('?');
        await wait(150);
        const status = c.last((l) => l.startsWith('<'));
        check('status report parses as grbl 1.1', /^<Idle\|MPos:[-0-9.,]+\|Bf:\d+,\d+/.test(status), status);
    });

    // ---------------------------------------------------------------
    await scenario('grblHAL handshake', ['--firmware=grblhal', '--axes=XYZA'], 2502, async (c) => {
        await wait(500);
        check(
            'grblHAL banner detected as GRBLHAL not GRBL',
            /.*(grblhal).*/i.test(c.lines[0]),
            c.lines[0],
        );
        c.clear();
        c.send('$I\n');
        await wait(200);
        const axs = c.last((l) => l.startsWith('[AXS:'));
        check('AXS reports 4 axes with letters', axs === '[AXS:4:XYZA]', axs);
        c.clear();
        c.send('?');
        await wait(150);
        const status = c.last((l) => l.startsWith('<'));
        check('4-axis MPos has 4 values', (status.match(/MPos:([-0-9.,]+)/)[1].split(',').length) === 4, status);
        c.clear();
        // grblHAL complete-status realtime byte 0x87
        c.sock.write(Buffer.from([0x87]));
        await wait(150);
        check('0x87 returns a complete report with WCO', /WCO:/.test(c.last((l) => l.startsWith('<')) || ''));
    });

    // ---------------------------------------------------------------
    await scenario('alarm on connect + unlock', ['--alarm-on-connect=1'], 2503, async (c) => {
        await wait(700);
        check('ALARM:1 emitted', c.lines.includes('ALARM:1'), c.lines.join(' | '));
        c.clear();
        c.send('?');
        await wait(150);
        check('state is Alarm', /^<Alarm\|/.test(c.last((l) => l.startsWith('<')) || ''));

        c.clear();
        c.send('G0 X10\n');
        await wait(200);
        check('G-code rejected while alarm-locked (error:9)', c.lines.includes('error:9'), c.lines.join(' | '));

        c.clear();
        c.send('$X\n');
        await wait(300);
        check('$X acked with ok', c.lines.includes('ok'), c.lines.join(' | '));
        c.clear();
        c.send('?');
        await wait(150);
        check('state returns to Idle', /^<Idle\|/.test(c.last((l) => l.startsWith('<')) || ''));
    });

    // ---------------------------------------------------------------
    await scenario('homing defers ok until complete', [], 2504, async (c) => {
        await wait(500);
        c.clear();
        c.send('$H\n');
        await wait(300);
        check('no ok yet while homing', !c.lines.includes('ok'), c.lines.join(' | '));
        c.send('?');
        await wait(150);
        check('state is Home', /^<Home\|/.test(c.last((l) => l.startsWith('<')) || ''));
        await wait(2800);
        check('ok arrives after homing completes', c.lines.includes('ok'), c.lines.join(' | '));
        c.clear();
        c.send('?');
        await wait(150);
        const status = c.last((l) => l.startsWith('<'));
        check('homed to -pulloff (-2.000)', /MPos:-2\.000,-2\.000,-2\.000/.test(status), status);
    });

    // ---------------------------------------------------------------
    await scenario('probe + zero workflow', [], 2505, async (c) => {
        await wait(500);
        c.clear();
        c.send('G21\nG90\nG38.2 Z-20 F600\n');
        await wait(3000);
        const prb = c.last((l) => l.startsWith('[PRB:'));
        check('PRB reported with success flag 1', prb === '[PRB:0.000,0.000,-10.000:1]', prb);
        check('probe move got exactly 3 oks', c.lines.filter((l) => l === 'ok').length === 3, String(c.lines.filter((l) => l === 'ok').length));

        c.clear();
        c.send('G10 L20 P1 Z0\n');
        await wait(300);
        c.send('$#\n');
        await wait(300);
        const g54 = c.last((l) => l.startsWith('[G54:'));
        check('G54 Z offset set from probe stop position', g54 === '[G54:0.000,0.000,-10.000]', g54);
        c.clear();
        c.send('?');
        await wait(200);
        c.send('?');
        await wait(200);
        // WPos = MPos - WCO should now read Z0
        const status = c.last((l) => /WCO:/.test(l));
        check('WCO reflects the new offset', /WCO:0\.000,0\.000,-10\.000/.test(status || ''), status);
    });

    // ---------------------------------------------------------------
    await scenario('probe failure raises ALARM:5', ['--probe-z=never'], 2506, async (c) => {
        await wait(500);
        c.clear();
        c.send('G21\nG90\nG38.2 Z-5 F3000\n');
        await wait(2500);
        check('ALARM:5 on no contact', c.lines.includes('ALARM:5'), c.lines.join(' | '));
        const prb = c.last((l) => l.startsWith('[PRB:'));
        check('PRB success flag is 0', /:0\]$/.test(prb || ''), prb);
    });

    // ---------------------------------------------------------------
    await scenario('planner back-pressure + ok accounting', [], 2507, async (c) => {
        await wait(500);
        c.clear();
        c.send('G21\nG90\nF600\n');
        await wait(200);
        c.clear();
        let gcode = '';
        for (let i = 1; i <= 40; i++) gcode += `G1 X${i} Y0 F600\n`;
        c.send(gcode);
        await wait(400);
        const early = c.lines.filter((l) => l === 'ok').length;
        check('planner throttles: fewer than 40 oks immediately', early < 40 && early >= 14, String(early));
        c.send('?');
        await wait(150);
        const status = c.last((l) => l.startsWith('<'));
        check('Bf shows a full planner (0 free)', /Bf:0,/.test(status), status);
        // let it drain
        await wait(9000);
        const total = c.lines.filter((l) => l === 'ok').length;
        check('exactly one ok per line (40)', total === 40, String(total));
    });

    // ---------------------------------------------------------------
    await scenario('feed hold / resume / soft reset', [], 2508, async (c) => {
        await wait(500);
        c.clear();
        c.send('G21\nG90\nG1 X100 F600\n');
        await wait(600);
        c.send('!');
        await wait(200);
        c.send('?');
        await wait(150);
        const held = c.last((l) => l.startsWith('<'));
        check('feed hold gives Hold:1 while a block is live', /^<Hold:1\|/.test(held), held);
        const posAtHold = Number(held.match(/MPos:([-0-9.]+)/)[1]);
        await wait(600);
        c.send('?');
        await wait(150);
        const stillHeld = c.last((l) => l.startsWith('<'));
        const posLater = Number(stillHeld.match(/MPos:([-0-9.]+)/)[1]);
        check('position frozen during hold', Math.abs(posLater - posAtHold) < 0.001, `${posAtHold} -> ${posLater}`);
        check('hold settles to Hold:0 once decelerated', /^<Hold:0\|/.test(stillHeld), stillHeld);

        c.send('~');
        await wait(400);
        c.send('?');
        await wait(150);
        check('cycle start resumes Run', /^<Run\|/.test(c.last((l) => l.startsWith('<')) || ''));

        c.clear();
        c.sock.write(Buffer.from([0x18]));
        await wait(400);
        check('soft reset re-emits banner', c.lines.some((l) => /^Grbl 1\.1f/.test(l)), c.lines.join(' | '));
        c.send('?');
        await wait(150);
        check('soft reset stops motion (Idle)', /^<Idle\|/.test(c.last((l) => l.startsWith('<')) || ''));
    });

    // ---------------------------------------------------------------
    await scenario('M0 pause parks in Hold', [], 2509, async (c) => {
        await wait(500);
        c.clear();
        c.send('G21\nG90\nG1 X5 F3000\nM0\nG1 X10 F3000\n');
        await wait(1500);
        c.send('?');
        await wait(150);
        const status = c.last((l) => l.startsWith('<'));
        check('M0 leaves machine in Hold', /^<Hold:0\|/.test(status), status);
        check('X stopped at 5 (line after M0 not run)', /MPos:5\.000/.test(status), status);
        c.send('~');
        await wait(600);
        c.send('?');
        await wait(150);
        check('cycle start continues past M0', /MPos:(10\.000|[6-9]\.)/.test(c.last((l) => l.startsWith('<')) || ''), c.last((l) => l.startsWith('<')));
    });

    // ---------------------------------------------------------------
    await scenario('error injection', ['--error-every=3', '--error-code=20'], 2510, async (c) => {
        await wait(500);
        c.clear();
        c.send('G1 X1 F100\nG1 X2 F100\nG1 X3 F100\nG1 X4 F100\n');
        await wait(600);
        check('every 3rd line errors', c.lines.filter((l) => l === 'error:20').length >= 1, c.lines.join(' | '));
        check('errors replace ok, never accompany it', c.lines.filter((l) => l === 'ok').length + c.lines.filter((l) => l.startsWith('error:')).length === 4, c.lines.join(' | '));
    });

    // ---------------------------------------------------------------
    await scenario('no-banner exercises $I fallback', ['--no-banner'], 2511, async (c) => {
        await wait(600);
        check('no banner sent', c.lines.length === 0, c.lines.join(' | '));
        c.send('$I\n');
        await wait(200);
        check(
            '$I reply alone does NOT match /grbl/i (matches real grbl)',
            !c.lines.some((l) => /grbl/i.test(l)),
            c.lines.join(' | '),
        );
    });

    // ---------------------------------------------------------------
    await scenario('G91 relative + G20 inches', [], 2512, async (c) => {
        await wait(500);
        c.send('G21\nG90\nG0 X10 Y10\n');
        await wait(1500);
        c.send('G91\nG0 X5\n');
        await wait(1500);
        c.clear();
        c.send('?');
        await wait(150);
        check('G91 move is relative (X=15)', /MPos:15\.000,10\.000/.test(c.last((l) => l.startsWith('<')) || ''), c.last((l) => l.startsWith('<')));

        c.send('G90\nG20\nG0 X1 Y0\n');
        await wait(1500);
        c.clear();
        c.send('?');
        await wait(150);
        check('G20 inch move converts (X1in = 25.4mm)', /MPos:25\.400/.test(c.last((l) => l.startsWith('<')) || ''), c.last((l) => l.startsWith('<')));
    });

    // ---------------------------------------------------------------
    await scenario('$J= jogging (the form GrblController sends)', [], 2513, async (c) => {
        await wait(500);
        c.clear();
        // Exactly what src/server/controllers/Grbl/GrblController.js builds.
        c.send('$J=G21G91 X5 F3000\n');
        await wait(300);
        check('$J= is accepted, not error:3', c.lines.includes('ok') && !c.lines.some((l) => l.startsWith('error:')), c.lines.join(' | '));
        await wait(600);
        c.clear();
        c.send('?');
        await wait(150);
        check('jog moved X by 5mm', /MPos:5\.000/.test(c.last((l) => l.startsWith('<')) || ''), c.last((l) => l.startsWith('<')));

        // A long, slow jog so the Jog state is observable.
        c.clear();
        c.send('$J=G21G91 X50 F300\n');
        await wait(300);
        c.send('?');
        await wait(150);
        check('state is Jog while jogging', /^<Jog\|/.test(c.last((l) => l.startsWith('<')) || ''), c.last((l) => l.startsWith('<')));
        c.sock.write(Buffer.from([0x85]));
        await wait(200);

        c.clear();
        c.send('$G\n');
        await wait(200);
        const gc = c.last((l) => l.startsWith('[GC:'));
        check('jog did NOT change modal F (stays F0)', / F0 /.test(gc), gc);

        // Jog cancel (0x85) mid-move.
        c.clear();
        c.send('$J=G21G91 X100 F600\n');
        await wait(400);
        c.sock.write(Buffer.from([0x85]));
        await wait(200);
        c.send('?');
        await wait(150);
        const after = c.last((l) => l.startsWith('<'));
        check('0x85 cancels the jog back to Idle', /^<Idle\|/.test(after), after);
        const x = Number(after.match(/MPos:([-0-9.]+)/)[1]);
        check('jog cancel stopped short of the 105mm target', x > 5 && x < 105, String(x));

        c.clear();
        c.send('$J=G21G91 X5\n');
        await wait(250);
        check('jog without F is rejected (error:22)', c.lines.includes('error:22'), c.lines.join(' | '));
    });

    // ---------------------------------------------------------------
    await scenario('jog rejected while alarm-locked', ['--alarm-on-connect=1'], 2514, async (c) => {
        await wait(700);
        c.clear();
        c.send('$J=G21G91 X5 F3000\n');
        await wait(250);
        check('$J= returns error:9 in alarm', c.lines.includes('error:9'), c.lines.join(' | '));
    });

    // ---------------------------------------------------------------
    await scenario('G10 L20 P0 targets the active WCS', [], 2515, async (c) => {
        await wait(500);
        c.send('G21\nG90\nG55\n');
        await wait(300);
        c.send('$J=G21G91 X7 F3000\n');
        await wait(900);
        c.clear();
        // This is the exact form gSender's Zero button sends.
        c.send('G10 L20 P0 X0 Y0 Z0\n');
        await wait(300);
        c.send('$#\n');
        await wait(300);
        const g54 = c.last((l) => l.startsWith('[G54:'));
        const g55 = c.last((l) => l.startsWith('[G55:'));
        check('P0 wrote the active WCS (G55)', /^\[G55:7\.000/.test(g55), g55);
        check('P0 left G54 untouched', g54 === '[G54:0.000,0.000,0.000]', g54);
    });

    console.log(`\n${pass} passed, ${fail} failed`);
    process.exit(fail ? 1 : 0);
})();
