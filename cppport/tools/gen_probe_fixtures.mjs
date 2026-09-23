// Generates cppport/tests/data/probing_golden.json: the probe routines that
// gSender's own src/app/src/lib/Probing.ts produces for a matrix of plates,
// axes, corners, units and machine settings. gs_core_tests replays every case
// through the C++ port and compares the G-code byte for byte.
//
//   node cppport/tools/gen_probe_fixtures.mjs
//
// Needs the repository's node_modules (for esbuild). Re-run when Probing.ts
// (or the probe widget's option building) changes upstream, and commit the
// regenerated JSON.

import { loadModule, seededRandom, stubs, writeCases } from './lib/bundle.mjs';

const probing = await loadModule('src/app/src/lib/Probing.ts', [stubs.redux]);
const { convertToImperial } = await loadModule('src/app/src/lib/units.ts');
const { pick } = seededRandom(0x5eed1234);

// Mirrors generateProbeCommands() in src/app/src/features/Probe/index.tsx:
// settings are stored in mm and converted for imperial workspaces.
function buildOptions(s) {
    const metric = s.units === 'mm';
    const probeDistances = metric
        ? { x: 30, y: 30, z: s.zProbeDistance ? s.zProbeDistance : 30 }
        : { x: 1.2, y: 1.2, z: s.zProbeDistance ? convertToImperial(s.zProbeDistance) : 1.2 };
    let zThickness, xyThickness, feedrate, fastFeedrate, retractDistance, zRetractNormal, tipDiameter, xyRetract,
        movementSpeed;
    const modal = metric ? '21' : '20';
    if (metric) {
        zThickness = s.profile.zThickness;
        xyThickness = s.profile.xyThickness;
        feedrate = s.probeFeedrate;
        fastFeedrate = s.probeFastFeedrate;
        retractDistance = s.retractionDistance;
        zRetractNormal = s.zRetractNormal;
        tipDiameter = s.tipDiameter3D;
        xyRetract = s.xyRetract3D;
        movementSpeed = s.probeMovementSpeed;
    } else {
        zThickness = {
            autoZero: s.profile.zThickness.autoZero,
            standardBlock: convertToImperial(s.profile.zThickness.standardBlock),
            zProbe: convertToImperial(s.profile.zThickness.zProbe),
            probe3D: convertToImperial(s.profile.zThickness.probe3D),
        };
        xyThickness = convertToImperial(s.profile.xyThickness);
        feedrate = convertToImperial(s.probeFeedrate);
        fastFeedrate = convertToImperial(s.probeFastFeedrate);
        retractDistance = convertToImperial(s.retractionDistance);
        zRetractNormal = convertToImperial(s.zRetractNormal);
        tipDiameter = convertToImperial(s.tipDiameter3D);
        xyRetract = convertToImperial(s.xyRetract3D);
        movementSpeed = s.probeMovementSpeed ? convertToImperial(s.probeMovementSpeed) : 0;
    }
    return {
        axes: s.axes,
        modal,
        probeFast: fastFeedrate,
        probeSlow: feedrate,
        units: s.units,
        retract: retractDistance,
        zRetractNormal,
        zRetractAuto: s.zRetractAuto,
        toolDiameter: s.toolDiameter,
        zThickness,
        xyThickness,
        plateType: s.profile.touchplateType,
        $13: s.$13,
        probeDistances,
        probeType: s.probeType,
        homingEnabled: s.$22 !== '0',
        tipDiameter3D: tipDiameter,
        xyRetract3D: xyRetract,
        probeMovementSpeed: movementSpeed,
        probeMovementSpeedAuto: s.probeMovementSpeed,
        firmware: s.firmware,
    };
}

const tools = [
    { metricDiameter: 6.35, imperialDiameter: 0.25 },
    { metricDiameter: 3.175, imperialDiameter: 0.125 },
    { metricDiameter: 12.7, imperialDiameter: 0.5 },
    { metricDiameter: 1.5, imperialDiameter: 0.059 },
];
const axesSets = {
    XYZ: { x: true, y: true, z: true },
    XY: { x: true, y: true, z: false },
    X: { x: true, y: false, z: false },
    Y: { x: false, y: true, z: false },
    Z: { x: false, y: false, z: true },
    YZ: { x: false, y: true, z: true }, // takes the AutoZero diameter routine's `axes.z && axes.y && axes.z` branch
};
const plates = [
    ['Standard Block', 'Diameter'],
    ['Z Probe', 'Diameter'],
    ['3D Probe', 'Diameter'],
    ['BitZero', 'Diameter'],
    ['AutoZero', 'Auto'],
    ['AutoZero', 'Tip'],
    ['AutoZero', 'Diameter'],
];

const cases = [];
for (const [plateType, probeType] of plates) {
    for (const [axesName, axes] of Object.entries(axesSets)) {
        for (const direction of [0, 1, 2, 3]) {
            const unitsName = pick(['mm', 'in']);
            const tool = pick(tools);
            const settings = {
                units: unitsName,
                axes,
                profile: {
                    xyThickness: pick([10, 10, 9.5]),
                    zThickness: {
                        standardBlock: pick([15, 15, 12.7]),
                        autoZero: pick([5, 5, 5.2]),
                        zProbe: pick([15, 19.05]),
                        probe3D: pick([0, 0, 1.5]),
                        bitZero: pick([13, 13, 12.5]),
                        bitZeroZOnly: pick([15.5, 15.5, 0]),
                    },
                    touchplateType: plateType,
                },
                probeFeedrate: pick([75, 75, 60]),
                probeFastFeedrate: pick([150, 150, 250]),
                retractionDistance: pick([2, 2, 1.5, 3.175]),
                zRetractNormal: pick([2, 2, 5]),
                zRetractAuto: pick([1, 1, 4]),
                zProbeDistance: pick([30, 30, 25.4, 0]),
                tipDiameter3D: pick([2, 2, 1.5]),
                xyRetract3D: pick([10, 10, 6]),
                probeMovementSpeed: pick([0, 0, 500, 1000]),
                toolDiameter:
                    probeType === 'Auto' || probeType === 'Tip'
                        ? 0
                        : unitsName === 'mm'
                          ? tool.metricDiameter
                          : tool.imperialDiameter,
                probeType,
                $13: pick(['0', '0', '1']),
                $22: pick(['0', '1']),
                firmware: pick(['Grbl', 'grblHAL']),
            };
            // Soft limits: $132 and the machine Z the store would hold.
            const machine = { $132: pick(['170.000', '100.000', '-80.5']), mposZ: pick([0, -12.3, -80.123, -95.75]) };
            globalThis.__reduxState = {
                controller: { settings: { settings: { $132: machine.$132 } }, mpos: { z: machine.mposZ } },
            };
            const options = buildOptions(settings);
            const recorded = JSON.parse(JSON.stringify(options)); // getProbeCode mutates its argument
            const code = probing.getProbeCode(options, direction);
            cases.push({
                name: `${plateType}/${probeType}/${axesName}/${direction}/${unitsName}`,
                settings,
                machine,
                direction,
                options: recorded,
                code,
            });
        }
    }
}

writeCases('probing_golden.json', ['src/app/src/lib/Probing.ts', 'src/app/src/features/Probe/index.tsx'], cases);
