/**
 * The simulated machine: position, modal state, and a planner queue that drains
 * on a timer.
 *
 * This models grbl's behaviour at the level gSender observes it - blocks are
 * accepted into a finite planner buffer, motion interpolates at the programmed
 * feed rate, and the active state follows the queue. It is not a stepper-accurate
 * model: there is no acceleration ramp, so a move runs at its target feed for its
 * whole length.
 */

const { buildSettings } = require('./settings');

const PLANNER_BUFFER_BLOCKS = 15;

// How long a hold reports Hold:1 before settling to Hold:0.
const HOLD_DECEL_MS = 250;
const RX_BUFFER_BYTES = 128;

// Anything at or below this is "arrived" - avoids creeping on float error.
const EPSILON = 0.0001;

const STATE_IDLE = 'Idle';
const STATE_RUN = 'Run';
const STATE_HOLD = 'Hold';
const STATE_JOG = 'Jog';
const STATE_ALARM = 'Alarm';
const STATE_HOME = 'Home';
const STATE_CHECK = 'Check';
const STATE_DOOR = 'Door';

class Machine {
    constructor(options = {}) {
        this.axes = options.axes || ['X', 'Y', 'Z'];
        this.settings = buildSettings(options.firmware);

        this.mpos = this.zeroVector();
        // G54..G59 work offsets, plus G92 applied on top of whichever is active.
        this.offsets = {
            G54: this.zeroVector(),
            G55: this.zeroVector(),
            G56: this.zeroVector(),
            G57: this.zeroVector(),
            G58: this.zeroVector(),
            G59: this.zeroVector(),
        };
        this.g92 = this.zeroVector();
        this.tlo = 0;
        this.probeResult = { position: this.zeroVector(), success: 0 };

        this.modal = {
            motion: 'G0',
            wcs: 'G54',
            plane: 'G17',
            units: 'G21',
            distance: 'G90',
            feedrate: 'G94',
            program: 'M0',
            spindle: 'M5',
            coolant: 'M9',
        };
        this.tool = 0;
        this.feed = 0; // programmed F
        this.spindleSpeed = 0; // programmed S

        this.activeState = STATE_IDLE;
        this.alarmCode = null;
        this.holdStartedAt = 0;
        this.queue = [];
        this.active = null; // block currently executing
        this.pinState = ''; // Pn: field, e.g. 'P' for probe triggered
        // Free bytes in the simulated RX buffer; the server keeps this current.
        this.rxAvailable = RX_BUFFER_BYTES;

        this.overrides = { feed: 100, rapid: 100, spindle: 100 };

        // The touch plate, as planes in MACHINE coordinates: a square block with
        // its bottom-left corner at (plateX, plateY) and its top face at `z`.
        // A probing move triggers when it crosses the face it is travelling
        // toward - down onto `z`, +X onto `xMin`, -X onto `xMax`, likewise Y.
        // Set `z` to null for a plate the probe never reaches (ALARM:5).
        //
        // The defaults put a 50mm plate (gSender's own default plate size) with
        // its corner at machine origin and its top 10mm below Z0, so the stock
        // bottom-left-corner routine works from a tool parked at machine zero.
        //
        // This is a plane model, not a solid: probing X triggers at the X face
        // wherever the tool happens to be in Y. That keeps the routines
        // predictable at the cost of not catching a genuinely mispositioned
        // probe - see README.
        const plateX = options.plateX === undefined ? 0 : options.plateX;
        const plateY = options.plateY === undefined ? 0 : options.plateY;
        const plateSize =
            options.plateSize === undefined ? 50 : options.plateSize;
        this.plate = {
            z: options.plateZ === undefined ? -10 : options.plateZ,
            xMin: plateX,
            xMax: plateX + plateSize,
            yMin: plateY,
            yMax: plateY + plateSize,
        };

        // True while the tool is resting on the plate, which is what drives the
        // Pn:P field. gSender's probe dialog will not enable its Start button
        // until it has seen this at least once (the "connectivity test").
        this.probeTouched = false;
        this.probeTouchHold = false; // forced on from the REPL
        this.probeFace = null; // which plate face the tool is resting on

        // Position after everything queued has run; null means "use mpos".
        this.plannerEnd = null;

        this.homed = false;
        this.homingEndsAt = 0;

        // Real grbl calls system_flag_wco_change() whenever a coordinate
        // offset changes, which forces WCO into the *next* status report
        // rather than waiting for the periodic one. gSender derives the work
        // position as MPos - cached WCO, so without this the DRO keeps showing
        // the pre-probe zero for up to ten reports after a routine finishes.
        this.wcoPending = true;
    }

    /** Force WCO into the next status report, as grbl does on an offset change. */
    flagWcoChange() {
        this.wcoPending = true;
    }

    /** Read and clear the pending-WCO flag. */
    consumeWcoPending() {
        const pending = this.wcoPending;
        this.wcoPending = false;
        return pending;
    }

    zeroVector() {
        const v = {};
        this.axes.forEach((axis) => {
            v[axis] = 0;
        });
        return v;
    }

    setting(key, fallback) {
        const value = Number(this.settings[key]);
        return Number.isFinite(value) ? value : fallback;
    }

    rapidRate() {
        // Slowest axis max rate, which is what a coordinated rapid is limited to.
        return Math.min(
            this.setting('$110', 4000),
            this.setting('$111', 4000),
            this.setting('$112', 3000),
        );
    }

    activeOffset() {
        const base = this.offsets[this.modal.wcs] || this.zeroVector();
        const combined = {};
        this.axes.forEach((axis) => {
            combined[axis] = (base[axis] || 0) + (this.g92[axis] || 0);
        });
        return combined;
    }

    wpos() {
        const offset = this.activeOffset();
        const pos = {};
        this.axes.forEach((axis) => {
            pos[axis] = this.mpos[axis] - offset[axis];
        });
        return pos;
    }

    isAlarm() {
        return this.activeState === STATE_ALARM;
    }

    isMoving() {
        return this.active !== null || this.queue.length > 0;
    }

    /** Blocks held by the planner, including the one currently executing. */
    plannerDepth() {
        return this.queue.length + (this.active ? 1 : 0);
    }

    hasBufferSpace() {
        return this.plannerDepth() < PLANNER_BUFFER_BLOCKS;
    }

    plannerAvailable() {
        return Math.max(0, PLANNER_BUFFER_BLOCKS - this.plannerDepth());
    }

    /**
     * Where the tool will be once everything already queued has run.
     *
     * Lines are parsed into the planner ahead of execution, so a G91 move must
     * be resolved against the end of the previous block rather than the live
     * position - otherwise a burst of queued relative moves all resolve against
     * the same stale spot and the machine ends up short.
     */
    planFrom() {
        return this.plannerEnd || this.mpos;
    }

    /**
     * Queue a coordinated move. `target` holds absolute machine coordinates for
     * the axes that move; omitted axes hold position.
     */
    pushMove(
        target,
        { rapid = false, feed = null, probe = null, jog = false } = {},
    ) {
        const from = this.planFrom();
        const resolved = {};
        const end = {};
        this.axes.forEach((axis) => {
            resolved[axis] =
                target[axis] === undefined ? null : Number(target[axis]);
            end[axis] = resolved[axis] === null ? from[axis] : resolved[axis];
        });
        this.queue.push({
            target: resolved,
            rapid,
            feed: feed || this.feed || 1000,
            probe,
            jog,
            start: null,
        });
        // A probe stops wherever it touches, so its end position is unknowable
        // here. Planning is suspended until it completes (see hasPendingProbe).
        this.plannerEnd = probe ? null : end;
    }

    /** True while a probing move is queued or running. */
    hasPendingProbe() {
        return Boolean(
            (this.active && this.active.probe) ||
                this.queue.some((block) => block.probe),
        );
    }

    /** Re-anchor planning to the live position. */
    resyncPlanner() {
        this.plannerEnd = null;
    }

    /** Drop queued and in-flight motion. Used by soft reset and jog cancel. */
    clearMotion() {
        this.queue = [];
        this.active = null;
        this.resyncPlanner();
    }

    alarm(code) {
        this.clearMotion();
        this.activeState = STATE_ALARM;
        this.alarmCode = code;
        return code;
    }

    clearAlarm() {
        if (this.activeState === STATE_ALARM) {
            this.activeState = STATE_IDLE;
            this.alarmCode = null;
        }
    }

    hold() {
        if (
            this.activeState !== STATE_RUN &&
            this.activeState !== STATE_JOG &&
            this.activeState !== STATE_IDLE
        ) {
            return;
        }
        // Hold:1 means "still decelerating", Hold:0 means "stopped, resumable".
        // There is no acceleration model here, so a hold that interrupts motion
        // gets a short synthetic deceleration window and anything else is
        // resumable straight away - which is what M0 on a drained planner is.
        const wasMoving =
            this.activeState === STATE_RUN || this.activeState === STATE_JOG;
        this.activeState = STATE_HOLD;
        this.holdStartedAt = wasMoving ? Date.now() : 0;
    }

    /** True while the synthetic deceleration window is still open. */
    isDecelerating() {
        return (
            this.holdStartedAt > 0 &&
            Date.now() - this.holdStartedAt < HOLD_DECEL_MS
        );
    }

    resume() {
        if (
            this.activeState === STATE_HOLD ||
            this.activeState === STATE_DOOR
        ) {
            this.activeState = this.isMoving() ? STATE_RUN : STATE_IDLE;
            this.holdStartedAt = 0;
        }
    }

    startHoming(durationMs = 2500) {
        this.clearMotion();
        this.activeState = STATE_HOME;
        this.homingEndsAt = Date.now() + durationMs;
    }

    /**
     * Advance the simulation by `dtMs`. Returns a list of asynchronous messages
     * the machine wants to emit (probe results, homing completion).
     */
    tick(dtMs) {
        const messages = [];

        if (this.activeState === STATE_HOME) {
            if (Date.now() >= this.homingEndsAt) {
                // Home to max travel, then pull off - matches $23=3 / $27.
                const pulloff = this.setting('$27', 2);
                this.mpos.X = -pulloff;
                this.mpos.Y = -pulloff;
                if (this.axes.includes('Z')) {
                    this.mpos.Z = -pulloff;
                }
                this.homed = true;
                this.activeState = STATE_IDLE;
                messages.push({ type: 'homed' });
            }
            return messages;
        }

        if (
            this.activeState === STATE_ALARM ||
            this.activeState === STATE_HOLD ||
            this.activeState === STATE_DOOR ||
            this.activeState === STATE_CHECK
        ) {
            return messages;
        }

        if (!this.active) {
            this.active = this.queue.shift() || null;
            if (this.active) {
                this.active.start = { ...this.mpos };
            }
        }

        if (!this.active) {
            if (
                this.activeState === STATE_RUN ||
                this.activeState === STATE_JOG
            ) {
                this.activeState = STATE_IDLE;
            }
            return messages;
        }

        this.activeState = this.active.jog ? STATE_JOG : STATE_RUN;

        const block = this.active;
        const rateOverride = block.rapid
            ? this.overrides.rapid / 100
            : this.overrides.feed / 100;
        const mmPerMin =
            (block.rapid ? this.rapidRate() : block.feed) * rateOverride;
        const stepDistance = Math.max(0, (mmPerMin / 60) * (dtMs / 1000));

        // Remaining vector to the block target.
        const delta = {};
        let remaining = 0;
        this.axes.forEach((axis) => {
            const target =
                block.target[axis] === null
                    ? this.mpos[axis]
                    : block.target[axis];
            delta[axis] = target - this.mpos[axis];
            remaining += delta[axis] * delta[axis];
        });
        remaining = Math.sqrt(remaining);

        if (remaining <= EPSILON) {
            messages.push(...this.finishBlock(block));
            return messages;
        }

        const before = { ...this.mpos };
        const fraction =
            stepDistance >= remaining ? 1 : stepDistance / remaining;
        this.axes.forEach((axis) => {
            this.mpos[axis] += delta[axis] * fraction;
        });

        // A probing move stops the moment it touches the plate.
        if (block.probe) {
            const contact = this.probeContact(before);
            if (contact) {
                this.mpos[contact.axis] = contact.at;
                this.probeResult = { position: { ...this.mpos }, success: 1 };
                this.probeFace = contact;
                this.setProbeTouched(true);
                this.active = null;
                this.resyncPlanner();
                messages.push({ type: 'probe', success: true, block });
                return messages;
            }
        }

        // Contact is positional, so re-derive it after every step: the pin
        // stays asserted while the tool rests on the plate and releases as soon
        // as it retracts clear.
        this.refreshProbePin();

        if (fraction === 1) {
            messages.push(...this.finishBlock(block));
        }

        return messages;
    }

    /**
     * Did the step from `before` to the current position cross a plate face in
     * the direction of travel? Returns {axis, at} for the first face crossed.
     */
    probeContact(before) {
        const faces = [
            { axis: 'Z', at: this.plate.z, dir: -1 },
            { axis: 'X', at: this.plate.xMin, dir: 1 },
            { axis: 'X', at: this.plate.xMax, dir: -1 },
            { axis: 'Y', at: this.plate.yMin, dir: 1 },
            { axis: 'Y', at: this.plate.yMax, dir: -1 },
        ];

        for (const face of faces) {
            if (face.at === null || !this.axes.includes(face.axis)) {
                continue;
            }
            const from = before[face.axis];
            const to = this.mpos[face.axis];
            if (to === from) {
                continue;
            }
            // Travelling toward the face, and this step reached or passed it.
            if (face.dir < 0 && to < from && to <= face.at && from > face.at) {
                return face;
            }
            if (face.dir > 0 && to > from && to >= face.at && from < face.at) {
                return face;
            }
        }
        return null;
    }

    /**
     * Is the tool touching the plate? True anywhere inside the block's XY
     * footprint at or below its top face. The probe circuit closes on contact
     * however it happens - jogging into the plate, or lifting the plate up to
     * the tool as gSender's connectivity check asks you to - not only during a
     * G38.x move.
     */
    toolInPlate() {
        if (this.plate.z === null) {
            return false;
        }
        if (this.mpos.Z > this.plate.z + EPSILON) {
            return false;
        }
        const inX =
            this.mpos.X >= this.plate.xMin - EPSILON &&
            this.mpos.X <= this.plate.xMax + EPSILON;
        const inY =
            this.mpos.Y >= this.plate.yMin - EPSILON &&
            this.mpos.Y <= this.plate.yMax + EPSILON;
        return inX && inY;
    }

    /** Recompute Pn:P from contact plus any manual override. */
    refreshProbePin() {
        this.probeTouched = this.toolInPlate();
        this.pinState = this.probeTouched || this.probeTouchHold ? 'P' : '';
    }

    setProbeTouched(touched) {
        this.probeTouched = touched;
        this.pinState = touched || this.probeTouchHold ? 'P' : '';
    }

    /** REPL/flag override: hold the probe pin asserted regardless of position. */
    setProbeTouchHold(hold) {
        this.probeTouchHold = hold;
        this.pinState = hold || this.probeTouched ? 'P' : '';
    }

    finishBlock(block) {
        const messages = [];
        this.axes.forEach((axis) => {
            if (block.target[axis] !== null) {
                this.mpos[axis] = block.target[axis];
            }
        });
        this.active = null;

        if (block.probe) {
            // Reached the target without contact: grbl raises ALARM:5 for G38.2.
            this.probeResult = { position: { ...this.mpos }, success: 0 };
            messages.push({ type: 'probe', success: false, block });
        }

        if (!this.queue.length) {
            this.activeState = STATE_IDLE;
            this.resyncPlanner();
        }
        return messages;
    }
}

module.exports = {
    Machine,
    PLANNER_BUFFER_BLOCKS,
    RX_BUFFER_BYTES,
    STATE_IDLE,
    STATE_RUN,
    STATE_HOLD,
    STATE_JOG,
    STATE_ALARM,
    STATE_HOME,
    STATE_CHECK,
    STATE_DOOR,
};
