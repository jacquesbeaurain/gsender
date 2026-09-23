// Generates cppport/tests/data/surfacing_golden.json: the programs gSender's
// own Surfacing generator (src/app/src/features/Surfacing/utils/
// surfacingGcodeGenerator.js) writes for a matrix of patterns, start
// positions, cut directions, units and sizes. gs_core_tests replays every case
// through the C++ port and compares the lines byte for byte.
//
//   node cppport/tools/gen_surfacing_fixtures.mjs
//
// Needs the repository's node_modules (for esbuild). Re-run when the
// generator changes upstream, and commit the regenerated JSON.

import { loadModule, seededRandom, stubs, writeCases } from './lib/bundle.mjs';

const source = 'src/app/src/features/Surfacing/utils/surfacingGcodeGenerator.js';
const { default: Generator } = await loadModule(source, [stubs.redux, stubs.store, stubs.controller]);
const { convertToImperial } = await loadModule('src/app/src/lib/units.ts');
const { pick } = seededRandom(0x5f0acade);

const types = ['SPIRAL_MOVEMENT', 'ZIG_ZAG_MOVEMENT'];
const starts = [
    'START_POSITION_BACK_LEFT',
    'START_POSITION_BACK_RIGHT',
    'START_POSITION_FRONT_LEFT',
    'START_POSITION_FRONT_RIGHT',
    'START_POSITION_CENTER',
];

// widgets.surfacing defaults (store/defaultState).
const defaults = {
    bitDiameter: 22,
    stepover: 40,
    feedrate: 2500,
    length: 100,
    width: 100,
    skimDepth: 1,
    maxDepth: 1,
    spindleRPM: 17000,
    type: 'SPIRAL_MOVEMENT',
    startPosition: 'START_POSITION_BACK_LEFT',
    spindle: 'M3',
    cutDirectionFlipped: false,
    shouldDwell: false,
    flood: false,
    mist: false,
    toolNumber: 0,
};

function generate(name, units, surfacing) {
    globalThis.__storeValues = { 'workspace.units': units };
    const lines = new Generator({ surfacing, units }).generate({ returnArray: true });
    return { name, units, surfacing, lines };
}

const cases = [generate('defaults/mm', 'mm', defaults)];
for (const type of types) {
    for (const startPosition of starts) {
        for (const cutDirectionFlipped of [false, true]) {
            for (let variant = 0; variant < 2; ++variant) {
                const units = pick(['mm', 'mm', 'in']);
                const [skimDepth, maxDepth] = pick([
                    [1, 1],
                    [0.5, 1.5],
                    [1, 2.5],
                    [0.3, 0.3],
                    [2, 1],
                ]);
                // The widget keeps mm and converts these six for inch workspaces.
                const mm = {
                    ...defaults,
                    type,
                    startPosition,
                    cutDirectionFlipped,
                    width: pick([100, 150, 60, 220, 90.5]),
                    length: pick([100, 80, 200, 120, 45.25]),
                    bitDiameter: pick([22, 12.7, 25.4, 31.75]),
                    stepover: pick([40, 40, 60, 85, 25]),
                    skimDepth,
                    maxDepth,
                    feedrate: pick([2500, 1500]),
                    spindleRPM: pick([17000, 12000]),
                    spindle: pick(['M3', 'M3', 'M4']),
                    shouldDwell: pick([false, false, true]),
                    mist: pick([false, false, true]),
                    flood: pick([false, false, true]),
                    toolNumber: pick([0, 0, 3]),
                };
                const surfacing =
                    units === 'mm'
                        ? mm
                        : {
                              ...mm,
                              bitDiameter: convertToImperial(mm.bitDiameter),
                              feedrate: convertToImperial(mm.feedrate),
                              length: convertToImperial(mm.length),
                              width: convertToImperial(mm.width),
                              skimDepth: convertToImperial(mm.skimDepth),
                              maxDepth: convertToImperial(mm.maxDepth),
                          };
                const name = `${type}/${startPosition}/${cutDirectionFlipped ? 'flipped' : 'normal'}/${variant}/${units}`;
                cases.push(generate(name, units, surfacing));
            }
        }
    }
}

writeCases('surfacing_golden.json', [source], cases);
