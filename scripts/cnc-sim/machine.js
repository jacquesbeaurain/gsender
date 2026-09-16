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

        // Where a G38.x probe is considered to make contact. Null means the
        // probe never triggers and the move fails at its target.
        this.probeTriggerZ =
            options.probeTriggerZ === undefined ? -10 : options.probeTriggerZ;

        this.homed = false;
        this.homingEndsAt = 0;
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
     * Queue a coordinated move. `target` holds absolute machine coordinates for
     * the axes that move; omitted axes hold position.
     */
    pushMove(
        target,
        { rapid = false, feed = null, probe = null, jog = false } = {},
    ) {
        const resolved = {};
        this.axes.forEach((axis) => {
            resolved[axis] =
                target[axis] === undefined ? null : Number(target[axis]);
        });
        this.queue.push({
            target: resolved,
            rapid,
            feed: feed || this.feed || 1000,
            probe,
            jog,
            start: null,
        });
    }

    /** Drop queued and in-flight motion. Used by soft reset and jog cancel. */
    clearMotion() {
        this.queue = [];
        this.active = null;
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

        const fraction =
            stepDistance >= remaining ? 1 : stepDistance / remaining;
        this.axes.forEach((axis) => {
            this.mpos[axis] += delta[axis] * fraction;
        });

        // A probing move stops the moment it crosses the trigger plane.
        if (block.probe && this.probeTriggerZ !== null) {
            if (this.mpos.Z <= this.probeTriggerZ) {
                this.mpos.Z = this.probeTriggerZ;
                this.probeResult = { position: { ...this.mpos }, success: 1 };
                this.pinState = 'P';
                this.active = null;
                messages.push({ type: 'probe', success: true, block });
                // Probe contact is momentary as far as the status report cares.
                setTimeout(() => {
                    this.pinState = '';
                }, 200);
                return messages;
            }
        }

        if (fraction === 1) {
            messages.push(...this.finishBlock(block));
        }

        return messages;
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
