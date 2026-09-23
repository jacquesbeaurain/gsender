// Generates cppport/tests/data/rotary_surfacing_golden.json: the programs
// gSender's rotary surfacing generator (src/app/src/features/Rotary/utils/
// Generator.ts, StockTurningGenerator) writes for a matrix of stock sizes,
// heights, stepdowns, bits, units and workspace modes. gs_core_tests replays
// every case through the C++ port and compares the lines byte for byte.
//
//   node cppport/tools/gen_rotary_fixtures.mjs
//
// Needs the repository's node_modules (for esbuild). Re-run when the
// generator changes upstream, and commit the regenerated JSON.

import { loadModule, seededRandom, stubs, writeCases } from './lib/bundle.mjs';

const source = 'src/app/src/features/Rotary/utils/Generator.ts';
const { StockTurningGenerator } = await loadModule(source, [stubs.redux, stubs.store, stubs.controller]);
const { convertToImperial } = await loadModule('src/app/src/lib/units.ts');
const { pick } = seededRandom(0x0707a7e5);

// widgets.rotary.stockTurning.options defaults (store/defaultState).
const defaults = {
    stockLength: 100,
    stepdown: 20,
    bitDiameter: 6.35,
    spindleRPM: 17000,
    feedrate: 3000,
    stepover: 15,
    startHeight: 50,
    finalHeight: 40,
    enableRehoming: false,
    shouldDwell: false,
    toolNumber: 0,
};

function generate(name, units, mode, options) {
    globalThis.__storeValues = { 'workspace.units': units, workspace: { units, mode } };
    const lines = new StockTurningGenerator(options).generate().split('\n');
    return { name, units, mode, options, lines };
}

const cases = [
    generate('defaults/mm', 'mm', 'DEFAULT', defaults),
    generate('defaults/mm/rotary', 'mm', 'ROTARY', defaults),
    generate('defaults/rehoming', 'mm', 'DEFAULT', { ...defaults, enableRehoming: true }),
    generate('defaults/tool', 'mm', 'DEFAULT', { ...defaults, toolNumber: 3, shouldDwell: true }),
    // Already at (or under) the final diameter: no layers.
    generate('nothing to cut', 'mm', 'DEFAULT', { ...defaults, startHeight: 40, finalHeight: 40 }),
];

for (let i = 0; i < 40; ++i) {
    const units = pick(['mm', 'mm', 'in']);
    const mode = pick(['DEFAULT', 'ROTARY']);
    // Diameters: start above the final one; stepdowns from one pass to many.
    const [startHeight, finalHeight] = pick([
        [50, 40],
        [60, 30],
        [45.5, 40],
        [80, 25.4],
        [30.2, 20],
        [50, 48],
    ]);
    const mm = {
        ...defaults,
        stockLength: pick([100, 250, 60.5, 150]),
        startHeight,
        finalHeight,
        stepdown: pick([20, 5, 2.5, 10, 7.3]),
        bitDiameter: pick([6.35, 3.175, 12.7, 6]),
        stepover: pick([15, 25, 40, 10]),
        feedrate: pick([3000, 1500, 2200]),
        spindleRPM: pick([17000, 12000]),
        enableRehoming: pick([false, false, true]),
        shouldDwell: pick([false, false, true]),
        toolNumber: pick([0, 0, 2]),
    };
    // The dialog converts these six for inch workspaces.
    const options =
        units === 'mm'
            ? mm
            : {
                  ...mm,
                  stockLength: convertToImperial(mm.stockLength),
                  startHeight: convertToImperial(mm.startHeight),
                  finalHeight: convertToImperial(mm.finalHeight),
                  stepdown: convertToImperial(mm.stepdown),
                  bitDiameter: convertToImperial(mm.bitDiameter),
                  feedrate: convertToImperial(mm.feedrate),
              };
    cases.push(generate(`random/${i}/${units}/${mode}`, units, mode, options));
}

writeCases('rotary_surfacing_golden.json', [source], cases);
