/**
 * grbl / grblHAL line protocol: parses what gSender sends and builds the replies
 * its line parsers expect (see src/server/controllers/Grbl/GrblLineParser*.js).
 */

const { PLANNER_BUFFER_BLOCKS, RX_BUFFER_BYTES } = require('./machine');

const fmt = (n) => (Number.isFinite(n) ? n : 0).toFixed(3);

const WCS_CODES = ['G54', 'G55', 'G56', 'G57', 'G58', 'G59'];

/** Split a G-code line into {letter, value} words, ignoring comments. */
function parseWords(line) {
    const stripped = line
        .replace(/\([^)]*\)/g, '') // ( inline comment )
        .replace(/;.*$/, '') // ; trailing comment
        .trim();

    const words = [];
    const pattern = /([A-Za-z])\s*([-+]?[0-9]*\.?[0-9]+)/g;
    let match;
    while ((match = pattern.exec(stripped)) !== null) {
        words.push({ letter: match[1].toUpperCase(), value: Number(match[2]) });
    }
    return words;
}

/** G-codes that select a motion mode, in the form gSender's parser reports them. */
function motionCodeFor(value) {
    const normalized = Number(value);
    const map = {
        0: 'G0',
        1: 'G1',
        2: 'G2',
        3: 'G3',
        4: 'G4',
        80: 'G80',
        38.2: 'G38.2',
        38.3: 'G38.3',
        38.4: 'G38.4',
        38.5: 'G38.5',
    };
    return map[normalized] || null;
}

// ---------------------------------------------------------------------------
// Reports
// ---------------------------------------------------------------------------

function statusReport(machine, { includeWCO = false, complete = false } = {}) {
    const fields = [];

    let state = machine.activeState;
    if (state === 'Hold') {
        state = machine.isDecelerating() ? 'Hold:1' : 'Hold:0';
    } else if (state === 'Door') {
        state = 'Door:1';
    } else if (state === 'Alarm' && machine.alarmCode) {
        state = 'Alarm';
    }
    fields.push(state);

    const mpos = machine.axes.map((axis) => fmt(machine.mpos[axis])).join(',');
    fields.push(`MPos:${mpos}`);

    fields.push(
        `Bf:${machine.plannerAvailable()},${machine.rxAvailable === undefined ? RX_BUFFER_BYTES : machine.rxAvailable}`,
    );

    // Report the live block's rate, not the modal F - a jog carries its own feed
    // and never touches modal state.
    let feed = 0;
    if (machine.active) {
        feed = machine.active.rapid ? machine.rapidRate() : machine.active.feed;
    }
    fields.push(
        `FS:${Math.round(feed)},${Math.round(machine.isMoving() ? machine.spindleSpeed : machine.modal.spindle === 'M5' ? 0 : machine.spindleSpeed)}`,
    );

    if (machine.pinState) {
        fields.push(`Pn:${machine.pinState}`);
    }

    // grbl sends WCO periodically rather than every report; gSender caches it.
    if (includeWCO || complete) {
        const offset = machine.activeOffset();
        const wco = machine.axes.map((axis) => fmt(offset[axis])).join(',');
        fields.push(`WCO:${wco}`);
    }

    fields.push(
        `Ov:${machine.overrides.feed},${machine.overrides.rapid},${machine.overrides.spindle}`,
    );

    const accessory = [];
    if (machine.modal.spindle === 'M3') accessory.push('S');
    if (machine.modal.spindle === 'M4') accessory.push('C');
    if (machine.modal.coolant === 'M8') accessory.push('F');
    if (machine.modal.coolant === 'M7') accessory.push('M');
    if (accessory.length) {
        fields.push(`A:${accessory.join('')}`);
    }

    return `<${fields.join('|')}>`;
}

function parserState(machine) {
    const m = machine.modal;
    const words = [m.motion, m.wcs, m.plane, m.units, m.distance, m.feedrate];
    // grbl only reports a program word while one is actually latched.
    if (m.program && m.program !== 'M0') {
        words.push(m.program);
    }
    words.push(m.spindle, m.coolant);
    words.push(`T${machine.tool}`);
    words.push(`F${fmt(machine.feed).replace(/\.000$/, '')}`);
    words.push(`S${machine.spindleSpeed}`);
    return `[GC:${words.join(' ')}]`;
}

/**
 * Commands that force grbl to finish all queued motion before running
 * (protocol_buffer_synchronize in the firmware). Reading or writing offsets
 * mid-move would otherwise capture a position that is still changing.
 */
function isSynchronizing(rawLine) {
    const line = rawLine.trim().toUpperCase();
    if (!line) {
        return false;
    }
    if (/^\$(#|\$|G|H|X|I|C|N|SLP)/.test(line)) {
        return true;
    }
    // G10 offsets, G92 offsets, G4 dwell, probing, program pause/end, tool change.
    return /(^|\s)(G10|G28\.1|G30\.1|G92|G4(\s|$)|G38\.[2-5]|M0|M1|M2|M6|M30)(\s|$|[^0-9.])/.test(
        line,
    );
}

function parameters(machine) {
    const lines = [];
    WCS_CODES.forEach((wcs) => {
        const offset = machine.offsets[wcs];
        lines.push(
            `[${wcs}:${machine.axes.map((axis) => fmt(offset[axis])).join(',')}]`,
        );
    });
    lines.push(`[G28:${machine.axes.map(() => fmt(0)).join(',')}]`);
    lines.push(`[G30:${machine.axes.map(() => fmt(0)).join(',')}]`);
    lines.push(
        `[G92:${machine.axes.map((axis) => fmt(machine.g92[axis])).join(',')}]`,
    );
    lines.push(`[TLO:${fmt(machine.tlo)}]`);
    lines.push(
        `[PRB:${machine.axes.map((axis) => fmt(machine.probeResult.position[axis])).join(',')}:${machine.probeResult.success}]`,
    );
    return lines;
}

function buildInfo(machine, firmware) {
    if (firmware === 'grblhal') {
        return [
            '[VER:1.1f.20230625:gSender Sim]',
            '[OPT:VNSL,35,1024,3]',
            `[AXS:${machine.axes.length}:${machine.axes.join('')}]`,
            '[NEWOPT:ENUMS,RT+,HOME,TC,SED]',
            '[FIRMWARE:grblHAL]',
            '[SIGNALS:]',
            '[PLUGIN:SDCARD v1.08]',
        ];
    }
    return ['[VER:1.1f.20170801:gSender Sim]', '[OPT:V,15,128]'];
}

function startupBanner(firmware) {
    return firmware === 'grblhal'
        ? "GrblHAL 1.1f ['$' or '$HELP' for help]"
        : "Grbl 1.1f ['$' for help]";
}

// ---------------------------------------------------------------------------
// Line execution
// ---------------------------------------------------------------------------

/**
 * Execute one line. Returns an array of response lines; the caller appends the
 * terminating `ok` unless a response already carries an `error:`.
 */
function executeLine(machine, rawLine, firmware) {
    const line = rawLine.trim();

    if (!line) {
        return [];
    }

    if (line.startsWith('$')) {
        return executeSystemCommand(machine, line, firmware);
    }

    if (machine.isAlarm()) {
        // grbl rejects G-code while alarm-locked.
        return ['error:9'];
    }

    return executeGcode(machine, line);
}

function executeSystemCommand(machine, line, firmware) {
    const upper = line.toUpperCase();

    // $J=<gcode> - jog. This is how gSender drives every jog button
    // (GrblController sends `$J=<units>G91 X.. F..`), so it has to come before
    // the generic `$` handling.
    const jog = line.match(/^\$J\s*=\s*(.+)$/i);
    if (jog) {
        if (machine.isAlarm()) {
            return ['error:9'];
        }
        return executeJog(machine, jog[1]);
    }

    // $<n>=<value> - write a setting.
    const assignment = line.match(/^\$(\d+)\s*=\s*(.+)$/);
    if (assignment) {
        machine.settings[`$${assignment[1]}`] = assignment[2].trim();
        return [];
    }

    if (upper === '$$') {
        return Object.keys(machine.settings).map(
            (key) => `${key}=${machine.settings[key]}`,
        );
    }

    if (upper === '$I' || upper === '$I+') {
        return buildInfo(machine, firmware);
    }

    if (upper === '$G') {
        return [parserState(machine)];
    }

    if (upper === '$#') {
        return parameters(machine);
    }

    if (upper === '$H' || /^\$H[XYZABC]$/.test(upper)) {
        machine.startHoming();
        // `ok` is withheld until homing finishes - see the 'homed' message.
        return [{ defer: 'homing' }];
    }

    if (upper === '$X') {
        machine.clearAlarm();
        return ['[MSG:Caution: Unlocked]'];
    }

    if (upper === '$C') {
        machine.activeState =
            machine.activeState === 'Check' ? 'Idle' : 'Check';
        return [
            machine.activeState === 'Check'
                ? '[MSG:Enabled]'
                : '[MSG:Disabled]',
        ];
    }

    if (upper === '$SLP') {
        machine.activeState = 'Sleep';
        return ['[MSG:Sleeping]'];
    }

    if (upper.startsWith('$N')) {
        return ['$N0=', '$N1='];
    }

    if (upper.startsWith('$RST')) {
        return ['[MSG:Reset to defaults]'];
    }

    // grblHAL extras that gSender probes for. Empty-but-ok is the honest answer
    // for a simulator that has no setting-description database.
    if (
        upper === '$ES' ||
        upper === '$EG' ||
        upper === '$EA' ||
        upper === '$FM' ||
        upper === '$F' ||
        upper === '$F+' ||
        upper === '$+' ||
        upper === '$'
    ) {
        return [];
    }

    // Unknown system command.
    return ['error:3'];
}

/**
 * A jog move. grbl runs these in their own modal context: G90/G91 and G20/G21
 * given on the jog line apply to that jog only, and the machine's own modal
 * state is left untouched. Distance mode defaults to G90 when the line is
 * silent, and F is required.
 */
function executeJog(machine, body) {
    const words = parseWords(body);
    if (!words.length) {
        return ['error:24']; // no axis words / invalid jog
    }

    const upper = body.toUpperCase();
    const relative = /G91(?![0-9.])/.test(upper);
    const machineCoords = /G53(?![0-9.])/.test(upper);
    const inches = /G20(?![0-9.])/.test(upper);
    const scale = inches ? 25.4 : 1;

    const axisWords = {};
    let feed = 0;

    for (const { letter, value } of words) {
        if (letter === 'F') {
            feed = value * scale;
        } else if (machine.axes.includes(letter)) {
            axisWords[letter] = value * scale;
        }
    }

    if (!Object.keys(axisWords).length) {
        return ['error:24'];
    }
    if (!feed) {
        return ['error:22']; // feed rate undefined
    }

    const offset = machineCoords
        ? machine.zeroVector()
        : machine.activeOffset();
    const target = {};
    machine.axes.forEach((axis) => {
        if (axisWords[axis] === undefined) {
            return;
        }
        target[axis] = relative
            ? machine.mpos[axis] + axisWords[axis]
            : axisWords[axis] + offset[axis];
    });

    machine.pushMove(target, { feed, jog: true });
    return [];
}

function executeGcode(machine, line) {
    const words = parseWords(line);
    if (!words.length) {
        return [];
    }

    const scale = machine.modal.units === 'G20' ? 25.4 : 1;
    const axisWords = {};
    let motion = null;
    let nonModalWcsOverride = false; // G53
    let g10Mode = null;
    let g10Pvalue = null;
    let dwellSeconds = null;
    let setG92 = false;
    let clearG92 = false;
    let pauseProgram = false;

    for (const word of words) {
        const { letter, value } = word;

        if (letter === 'G') {
            const code = motionCodeFor(value);
            if (code === 'G4') {
                dwellSeconds = 0;
            } else if (code) {
                motion = code;
                machine.modal.motion = code === 'G80' ? 'G80' : code;
            } else if (value === 10) {
                g10Mode = 'pending';
            } else if (value === 17 || value === 18 || value === 19) {
                machine.modal.plane = `G${value}`;
            } else if (value === 20 || value === 21) {
                machine.modal.units = `G${value}`;
            } else if (value === 28 || value === 30) {
                motion = 'G0';
                // Park at machine zero; close enough for a simulator.
                machine.axes.forEach((axis) => {
                    axisWords[axis] = 0;
                });
                nonModalWcsOverride = true;
            } else if (value === 53) {
                nonModalWcsOverride = true;
            } else if (value >= 54 && value <= 59) {
                machine.modal.wcs = `G${value}`;
            } else if (value === 90) {
                machine.modal.distance = 'G90';
            } else if (value === 91) {
                machine.modal.distance = 'G91';
            } else if (value === 92) {
                setG92 = true;
            } else if (value === 92.1) {
                clearG92 = true;
            } else if (value === 93 || value === 94) {
                machine.modal.feedrate = `G${value}`;
            }
            continue;
        }

        if (letter === 'M') {
            if (value === 0 || value === 1) {
                machine.modal.program = `M${value}`;
                // A program pause parks grbl in Hold until cycle start.
                pauseProgram = true;
            } else if (value === 2 || value === 30) {
                machine.modal.program = `M${value}`;
                machine.modal.spindle = 'M5';
                machine.modal.coolant = 'M9';
            } else if (value === 3 || value === 4) {
                machine.modal.spindle = `M${value}`;
            } else if (value === 5) {
                machine.modal.spindle = 'M5';
            } else if (value === 7 || value === 8) {
                machine.modal.coolant = `M${value}`;
            } else if (value === 9) {
                machine.modal.coolant = 'M9';
            }
            continue;
        }

        if (letter === 'F') {
            machine.feed = value * scale;
            continue;
        }
        if (letter === 'S') {
            machine.spindleSpeed = value;
            continue;
        }
        if (letter === 'T') {
            machine.tool = value;
            continue;
        }
        if (letter === 'L' && g10Mode === 'pending') {
            g10Mode = value; // 2 or 20
            continue;
        }
        if (letter === 'P' && g10Mode !== null) {
            g10Pvalue = value;
            continue;
        }
        if (letter === 'R' && dwellSeconds !== null) {
            dwellSeconds = value;
            continue;
        }
        if (letter === 'P' && dwellSeconds !== null) {
            dwellSeconds = value;
            continue;
        }

        if (machine.axes.includes(letter)) {
            axisWords[letter] = value * scale;
        }
    }

    if (pauseProgram) {
        // The drain gate has already emptied the planner, so this parks a
        // stationary machine - resumed by cycle start (~), as on real hardware.
        machine.hold();
    }

    if (clearG92) {
        machine.g92 = machine.zeroVector();
        return [];
    }

    if (setG92) {
        // G92 sets the current work position to the given values.
        const offset = machine.offsets[machine.modal.wcs];
        machine.axes.forEach((axis) => {
            if (axisWords[axis] !== undefined) {
                machine.g92[axis] =
                    machine.mpos[axis] - offset[axis] - axisWords[axis];
            }
        });
        return [];
    }

    if (g10Mode === 2 || g10Mode === 20) {
        // P0 (and an absent P) mean "whichever WCS is active" - gSender zeroes
        // with `G10 L20 P0 ...`, so mapping P0 to G54 would silently write the
        // wrong offset for anyone working in G55-G59.
        const wcs = !g10Pvalue
            ? machine.modal.wcs
            : WCS_CODES[g10Pvalue - 1] || machine.modal.wcs;
        machine.axes.forEach((axis) => {
            if (axisWords[axis] === undefined) {
                return;
            }
            if (g10Mode === 2) {
                machine.offsets[wcs][axis] = axisWords[axis];
            } else {
                // L20: set offset so the current position reads as the value given.
                machine.offsets[wcs][axis] =
                    machine.mpos[axis] - axisWords[axis] - machine.g92[axis];
            }
        });
        return [];
    }

    if (dwellSeconds !== null) {
        return [{ defer: 'dwell', seconds: dwellSeconds }];
    }

    if (!Object.keys(axisWords).length) {
        return [];
    }

    // Resolve the target into absolute machine coordinates.
    const offset = nonModalWcsOverride
        ? machine.zeroVector()
        : machine.activeOffset();
    const target = {};
    machine.axes.forEach((axis) => {
        if (axisWords[axis] === undefined) {
            return;
        }
        if (machine.modal.distance === 'G91' && !nonModalWcsOverride) {
            target[axis] = machine.mpos[axis] + axisWords[axis];
        } else {
            target[axis] = axisWords[axis] + offset[axis];
        }
    });

    const effectiveMotion = motion || machine.modal.motion;
    const isProbe = effectiveMotion && effectiveMotion.startsWith('G38');

    if (effectiveMotion === 'G80') {
        return [];
    }

    machine.pushMove(target, {
        rapid: effectiveMotion === 'G0',
        feed: machine.feed,
        probe: isProbe ? effectiveMotion : null,
    });

    return [];
}

module.exports = {
    parseWords,
    isSynchronizing,
    statusReport,
    parserState,
    parameters,
    buildInfo,
    startupBanner,
    executeLine,
    fmt,
    PLANNER_BUFFER_BLOCKS,
    RX_BUFFER_BYTES,
};
