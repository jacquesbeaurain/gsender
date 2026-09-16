/**
 * $$ setting dumps.
 *
 * Values are a plausible 3-axis belt-driven router (LongMill-ish), not a copy of
 * any particular board. The simulator reads a few of these back - $110/$111/$112
 * for rapid rates, $130/$131/$132 for travel limits - so editing them here
 * actually changes how the simulated machine moves.
 */

const GRBL_SETTINGS = {
    $0: '10', // Step pulse, microseconds
    $1: '255', // Step idle delay, milliseconds
    $2: '0', // Step port invert, mask
    $3: '2', // Direction port invert, mask
    $4: '0', // Step enable invert, boolean
    $5: '0', // Limit pins invert, boolean
    $6: '0', // Probe pin invert, boolean
    $10: '1', // Status report, mask
    $11: '0.010', // Junction deviation, millimeters
    $12: '0.002', // Arc tolerance, millimeters
    $13: '0', // Report inches, boolean
    $20: '0', // Soft limits, boolean
    $21: '0', // Hard limits, boolean
    $22: '1', // Homing cycle, boolean
    $23: '3', // Homing dir invert, mask
    $24: '100.000', // Homing feed, mm/min
    $25: '1000.000', // Homing seek, mm/min
    $26: '250', // Homing debounce, milliseconds
    $27: '2.000', // Homing pull-off, millimeters
    $30: '30000', // Max spindle speed, RPM
    $31: '0', // Min spindle speed, RPM
    $32: '0', // Laser mode, boolean
    $100: '200.000', // X steps/mm
    $101: '200.000', // Y steps/mm
    $102: '200.000', // Z steps/mm
    $110: '4000.000', // X max rate, mm/min
    $111: '4000.000', // Y max rate, mm/min
    $112: '3000.000', // Z max rate, mm/min
    $120: '750.000', // X acceleration, mm/sec^2
    $121: '750.000', // Y acceleration, mm/sec^2
    $122: '500.000', // Z acceleration, mm/sec^2
    $130: '790.000', // X max travel, millimeters
    $131: '845.000', // Y max travel, millimeters
    $132: '115.000', // Z max travel, millimeters
};

/**
 * grblHAL keeps the classic numbers and adds its own. Only the extras that
 * gSender actually reads are worth carrying here.
 */
const GRBLHAL_EXTRA_SETTINGS = {
    $8: '0', // Ganged axes direction invert, mask
    $9: '1', // PWM spindle, mask
    $14: '0', // Invert control pins, mask
    $15: '0', // Invert coolant pins, mask
    $16: '0', // Invert spindle signals, mask
    $17: '0', // Pullup disable control pins, mask
    $18: '0', // Pullup disable limit pins, mask
    $19: '0', // Pullup disable probe pin, boolean
    $28: '0.100', // G73 retract distance, millimeters
    $29: '0.0', // Step pulse delay, microseconds
    $33: '5000.0', // Spindle PWM frequency, Hz
    $34: '0.0', // Spindle PWM off value, percent
    $35: '0.0', // Spindle PWM min value, percent
    $36: '100.0', // Spindle PWM max value, percent
    $37: '0', // Steppers deenergize, mask
    $39: '1', // Enable legacy RT commands, boolean
    $40: '0', // Limit jog commands, boolean
    $43: '1', // Homing passes
    $44: '4', // Homing cycle 1, mask
    $45: '3', // Homing cycle 2, mask
    $46: '0', // Homing cycle 3, mask
    $62: '0', // Sleep enable, boolean
    $63: '2', // Feed hold actions, mask
    $64: '0', // Force init alarm, boolean
    $65: '0', // Probing feed override, boolean
    $341: '0', // Tool change mode
    $342: '30.0', // Tool change probing distance, millimeters
    $343: '25.0', // Tool change locate feed rate, mm/min
    $344: '200.0', // Tool change search seek rate, mm/min
    $345: '200.0', // Tool change probe pull-off rate, mm/min
    $384: '0', // Disable G92 persistence, boolean
    $398: '100', // Planner buffer blocks
    $481: '0', // Autoreport interval, milliseconds
};

function buildSettings(firmware) {
    if (firmware === 'grblhal') {
        // Numeric order, the way a real board dumps them.
        const merged = { ...GRBL_SETTINGS, ...GRBLHAL_EXTRA_SETTINGS };
        const sorted = {};
        Object.keys(merged)
            .sort((a, b) => Number(a.slice(1)) - Number(b.slice(1)))
            .forEach((key) => {
                sorted[key] = merged[key];
            });
        return sorted;
    }
    return { ...GRBL_SETTINGS };
}

module.exports = { buildSettings };
