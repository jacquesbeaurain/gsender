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

import { createRequire } from 'node:module';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '..', '..');
const outFile = resolve(here, '..', 'tests', 'data', 'probing_golden.json');
const require = createRequire(join(repoRoot, 'package.json'));
const esbuild = require('esbuild');

// SoftLimits.js reads $132 and the machine position from the Redux store;
// the stub serves them from globalThis.__probeState per case.
const stubRedux = {
    name: 'stub-redux',
    setup(build) {
        build.onResolve({ filter: /(^|[\\/])store[\\/]redux$/ }, () => ({ path: 'redux', namespace: 'stub' }));
        build.onLoad({ filter: /.*/, namespace: 'stub' }, () => ({
            contents: 'export default { getState: () => globalThis.__probeState };',
            loader: 'js',
        }));
    },
};

async function load(entry) {
    const result = await esbuild.build({
        entryPoints: [join(repoRoot, entry)],
        bundle: true,
        write: false,
        format: 'cjs',
        platform: 'node',
        logLevel: 'silent',
        plugins: [stubRedux],
        alias: { app: join(repoRoot, 'src/app/src') },
        define: { 'import.meta.env': '{}' }, // Vite's, read by modules the constants pull in
        loader: { '.ts': 'ts', '.tsx': 'tsx', '.js': 'jsx' },
    });
    const module = { exports: {} };
    new Function('module', 'exports', 'require', result.outputFiles[0].text)(module, module.exports, require);
    return module.exports;
}

// Some modules the constants pull in read browser storage at import time.
globalThis.localStorage = { getItem: () => null, setItem() {}, removeItem() {} };

const probing = await load('src/app/src/lib/Probing.ts');
const units = await load('src/app/src/lib/units.ts');
const { convertToImperial } = units;

// Deterministic choices (mulberry32), so the fixture only changes when
// Probing.ts does.
let seed = 0x5eed1234;
function random() {
    seed = (seed + 0x6d2b79f5) | 0;
    let t = seed;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
const pick = (values) => values[Math.floor(random() * values.length)];

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
            globalThis.__probeState = {
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

mkdirSync(dirname(outFile), { recursive: true });
const payload = { _source: ['src/app/src/lib/Probing.ts', 'src/app/src/features/Probe/index.tsx'], cases };
writeFileSync(outFile, `${JSON.stringify(payload)}\n`, 'utf8');
console.log(`wrote ${relative(repoRoot, outFile)} (${cases.length} cases)`);
