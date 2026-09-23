// Generates cppport/tests/data/outline_golden.json: the outline programs
// gSender's own src/app/src/workers/Outline.worker.ts writes - Detailed
// (hull of the toolpath vertices), Square and Rapidless Square - for a
// matrix of point clouds, boxes and programs. gs_core_tests replays every
// case through the C++ port and compares the lines.
//
//   node cppport/tools/gen_outline_fixtures.mjs

import { loadModule, seededRandom, stubs, writeCases } from './lib/bundle.mjs';

// The worker assigns self.onmessage and answers with postMessage.
let answer = null;
globalThis.self = {};
globalThis.postMessage = (message) => {
    answer = message;
};
const source = 'src/app/src/workers/Outline.worker.ts';
await loadModule(source, [stubs.redux, stubs.store, stubs.controller]);
const { random, pick } = seededRandom(0x0a71e5);

function run(data) {
    answer = null;
    try {
        globalThis.self.onmessage({ data });
        return { lines: answer.outlineGcode };
    } catch (error) {
        return { error: String(error.message ?? error) };
    }
}

const round3 = (v) => Math.round(v * 1000) / 1000;
// Flat x,y,z vertices, stored as the visualizer does (Float32Array).
function vertices(points) {
    const flat = new Float32Array(points.length * 3);
    points.forEach(([x, y, z = 0], i) => {
        flat[i * 3] = x;
        flat[i * 3 + 1] = y;
        flat[i * 3 + 2] = z;
    });
    return flat;
}

const clouds = {
    scatter: () =>
        Array.from({ length: 120 }, () => [round3(random() * 200 - 50), round3(random() * 150 - 30), -1]),
    rectangleEdges: () => {
        // Collinear points along the edges of a pocket, with repeats.
        const pts = [];
        for (let x = 0; x <= 80; x += 2.5) pts.push([x, 0], [x, 40]);
        for (let y = 0; y <= 40; y += 2.5) pts.push([0, y], [80, y]);
        return pts.concat(pts.slice(0, 10));
    },
    circle: () =>
        Array.from({ length: 90 }, (_, i) => {
            const a = (i / 90) * 2 * Math.PI;
            return [25 + 20 * Math.cos(a), 30 + 20 * Math.sin(a), -2];
        }),
    centredSquare: () => [
        [-10, -10],
        [10, -10],
        [10, 10],
        [-10, 10],
        [0, 0],
        [5, 5],
    ],
    nearDuplicates: () =>
        Array.from({ length: 60 }, (_, i) => [10 + (i % 6) * 0.1, 20 + Math.floor(i / 6) * 0.2, 0]),
    withRapids: () => [
        [0, 0, 5],
        [12.3456, 7.891, 5],
        [12.3456, 7.891, -1],
        [60.5, 7.891, -1],
        [60.5, 45.25, -1],
        [12.3456, 45.25, -1],
        [12.3456, 7.891, -1],
        [0, 0, 5],
    ],
    triangle: () => [
        [0, 0],
        [30, 0],
        [15, 26],
    ],
    collinear: () => [
        [0, 0],
        [10, 0],
        [20, 0],
    ],
    single: () => [[5, 5]],
};

const programs = {
    lines: 'G21 G90\nG0 X-5 Y-5 Z5\nG1 Z-1 F500\nG1 X40\nG1 Y25\nG0 X100 Y100\nG1 X41 Y26\n',
    arcs: 'G21 G90\nG0 X10 Y0\nG1 Z-1 F300\nG2 X-10 Y0 I-10 J0\nG3 X10 Y0 I10 J0\nG0 Z5\n',
    quarterArc: 'G21 G90\nG0 X0 Y-20\nG1 Z-1 F300\nG3 X20 Y0 R20\nG1 X20.5\n',
    inches: 'G20 G90\nG0 X0 Y0\nG1 Z-0.05 F20\nG1 X2.5 Y1.25\nG2 X3.5 Y0.25 I0 J-1\n',
    rapidsOnly: 'G21 G90\nG0 X10 Y10\nG0 X50 Y60\n',
    relative: 'G21 G91\nG1 X10 F400\nG1 Y10\nG1 X-4 Y-3\n',
};

const variants = () => ({
    isLaser: pick([false, false, true]),
    zTravel: pick([5, 5, 2, -1]),
    outlineSpeed: pick([null, 0, 1500, '2000', 'abc']),
});

const cases = [];
for (const [name, make] of Object.entries(clouds)) {
    for (let i = 0; i < 2; ++i) {
        const v = variants();
        const data = { ...v, parsedData: vertices(make()), mode: 'Detailed' };
        cases.push({
            name: `Detailed/${name}/${i}`,
            input: { ...v, mode: 'Detailed', parsedData: Array.from(data.parsedData) },
            ...run(data),
        });
    }
}
for (const withVertices of [true, false]) {
    const v = variants();
    const bbox = { min: { x: -12.5, y: 0.25, z: -3 }, max: { x: 88.125, y: 40, z: 5 } };
    const parsedData = withVertices ? vertices(clouds.triangle()) : new Float32Array(0);
    cases.push({
        name: `Square/${withVertices ? 'vertices' : 'bbox'}`,
        input: { ...v, mode: 'Square', bbox, parsedData: Array.from(parsedData) },
        ...run({ ...v, mode: 'Square', bbox, parsedData }),
    });
}
for (const [name, content] of Object.entries(programs)) {
    const v = variants();
    const bbox = { min: { x: 0, y: 0, z: 0 }, max: { x: 10, y: 10, z: 0 } };
    cases.push({
        name: `Rapidless/${name}`,
        input: { ...v, mode: 'Rapidless Square', bbox, content },
        ...run({ ...v, mode: 'Rapidless Square', bbox, parsedData: [], content }),
    });
}

writeCases('outline_golden.json', [source], cases);
