// Generates cppport/tests/data/toolchange_golden.json: gSender's tool change
// wizards (src/app/src/wizards: Standard Re-zero, Flexible Re-zero, Fixed
// Tool Sensor and its probe-length-only variant) - their start-up G-code,
// steps, instructions and action G-code - for a matrix of plates, machine
// settings, tool change counts and sensor/manual positions. gs_core_tests
// rebuilds every wizard with the C++ port and compares them.
//
//   node cppport/tools/gen_toolchange_fixtures.mjs

import { createRequire } from 'node:module';
import { join } from 'node:path';
import { loadModule, repoRoot, seededRandom, stubs, writeCases } from './lib/bundle.mjs';

const require = createRequire(join(repoRoot, 'package.json'));
const { renderToStaticMarkup } = require('react-dom/server');

// The controller stub records gcode commands (Fixed Tool Sensor starts that way).
let commands = [];
const controllerStub = [
    stubs.controller[0],
    'export default { command: (...args) => globalThis.__commands.push(args) };',
];
globalThis.__commands = commands;

const load = (entry) => loadModule(entry, [stubs.redux, stubs.store, controllerStub]);
const sources = {
    manual: 'src/app/src/wizards/manualToolchange.tsx',
    semiauto: 'src/app/src/wizards/semiautoToolchange.tsx',
    automatic: 'src/app/src/wizards/automaticToolchange.tsx',
    probeToolLength: 'src/app/src/wizards/probeToolLength.tsx',
};
const modules = {};
for (const [name, entry] of Object.entries(sources)) {
    modules[name] = (await load(entry)).default;
}
const { random, pick } = seededRandom(0x7001c4);

// Descriptions are strings, or functions returning a string or JSX.
function text(description) {
    const value = typeof description === 'function' ? description() : description;
    if (typeof value === 'string') {
        return value;
    }
    return renderToStaticMarkup(value)
        .replace(/<[^>]+>/g, '')
        .replace(/&#x27;/g, "'")
        .replace(/&quot;/g, '"')
        .replace(/&amp;/g, '&');
}

function snapshot(instructions) {
    commands.length = 0;
    const started = instructions.onStart ? instructions.onStart() : undefined;
    return {
        intro: instructions.intro.description,
        // Returned lines go through wizard:start; the Fixed Tool Sensor
        // wizards send theirs with a gcode command instead.
        start: Array.isArray(started) ? started : commands.length ? commands[0][1] : [],
        startDirect: !Array.isArray(started),
        steps: instructions.steps.map((step) => ({
            title: step.title,
            substeps: step.substeps.map((substep) => ({
                title: substep.title,
                description: text(substep.description),
                toolBanner: Boolean(substep.toolBanner),
                actions: (substep.actions ?? []).map((action) => ({
                    label: action.label,
                    lines: action.gcodeLines ?? [],
                })),
            })),
        })),
    };
}

const plates = ['Standard Block', 'AutoZero', 'Z Probe', '3D Probe', 'BitZero'];
const cases = [];
for (let i = 0; i < 40; ++i) {
    const plateType = pick(plates);
    const input = {
        wizard: ['manual', 'semiauto', 'automatic', 'probeToolLength'][i % 4],
        count: pick([1, 1, 2, 3]),
        plateType,
        zThickness: {
            standardBlock: pick([15, 12.7]),
            autoZero: pick([5, 5.2]),
            zProbe: pick([15, 19.05]),
            probe3D: pick([0, 1.5]),
            bitZero: 13,
            bitZeroZOnly: pick([15.5, 16]),
        },
        probe: {
            probeFeedrate: pick([75, 60]),
            probeFastFeedrate: pick([150, 250]),
            retractionDistance: pick([2, 1.5, 4]),
            zProbeDistance: pick([30, 25.4, 40]),
        },
        settings: { $13: pick(['0', '0', '1']), $20: pick(['0', '1']), $132: pick(['100.000', '80', '170.5']) },
        mposZ: pick([0, -12.5, -70.25, -95]),
        tool: pick(['2', '5', '12']),
        toolChangePosition: { x: pick([-10, -500.25]), y: pick([-20, -3.5]), z: pick([-5, -15.75]) },
        moveToManualPosition: pick([false, true]),
        manualPosition: { x: pick([0, -300]), y: pick([-100, -7.5]), z: pick([-1, 0]) },
    };
    globalThis.__storeValues = {
        'workspace.probeProfile': { zThickness: input.zThickness, touchplateType: plateType },
        'workspace.probeProfile.touchplateType': plateType,
        'widgets.probe': input.probe,
        'workspace.toolChangePosition': input.toolChangePosition,
        'workspace.toolChange.moveToManualPosition': input.moveToManualPosition,
        'workspace.toolChange.manualPosition': input.manualPosition,
    };
    globalThis.__reduxState = {
        controller: {
            settings: { settings: input.settings },
            mpos: { z: input.mposZ },
            state: { parserstate: { modal: { tool: input.tool } } },
        },
    };
    const factory = modules[input.wizard];
    const instructions = typeof factory === 'function' ? factory(input.count) : factory;
    cases.push({ name: `${input.wizard}/${i}`, input, ...snapshot(instructions) });
}

writeCases('toolchange_golden.json', Object.values(sources), cases);
