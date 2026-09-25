// Generates cppport/tests/data/gcode_highlight_golden.json: how gSender's
// G-code views colour each line - react-syntax-highlighter's `gcode`
// language, which is highlight.js 10.7's grammar run by lowlight with
// ignoreIllegals, one line at a time (GCodeSourcePanel, GcodeEditor). Each
// case is a line and its colour runs, [class, length]: the innermost class
// the a11y-light/a11y-dark themes colour ("" for the base colour). Lines
// come from the repository's example programs, edge cases for the grammar's
// quirks and seeded random token soup. gs_core_tests compares every run.
//
//   node cppport/tools/gen_highlight_fixtures.mjs

import { createRequire } from 'node:module';
import { readFileSync } from 'node:fs';
import { execSync } from 'node:child_process';
import { join } from 'node:path';

import { repoRoot, seededRandom, writeCases } from './lib/bundle.mjs';

const require = createRequire(join(repoRoot, 'package.json'));
const hljs = require('highlight.js/lib/core');
hljs.registerLanguage('gcode', require('highlight.js/lib/languages/gcode'));
const version = require('highlight.js/package.json').version;

// The classes the a11y themes give a colour (both themes colour the same set).
const theme = require('react-syntax-highlighter/dist/cjs/styles/hljs/a11y-light').default;
const colored = (cls) => Object.hasOwn(theme, `hljs-${cls}`) && theme[`hljs-${cls}`].color !== undefined;

const entities = { amp: '&', lt: '<', gt: '>', quot: '"', '#x27': "'", '#39': "'" };
function runs(line) {
    const html = hljs.highlight(line, { language: 'gcode', ignoreIllegals: true }).value;
    const out = [];
    const stack = [''];
    const push = (cls, text) => {
        if (!text) return;
        const last = out[out.length - 1];
        if (last && last[0] === cls) last[1] += text.length;
        else out.push([cls, text.length]);
    };
    const re = /<span class="hljs-([^"]+)">|<\/span>|&([a-z#0-9]+);|([^<&]+)/g;
    for (let m; (m = re.exec(html)); ) {
        if (m[1] !== undefined) stack.push(colored(m[1]) ? m[1] : stack[stack.length - 1]);
        else if (m[0] === '</span>') stack.pop();
        else push(stack[stack.length - 1], m[2] !== undefined ? entities[m[2]] : m[3]);
    }
    return out;
}

const { random, pick } = seededRandom(0x6c0de);
const lines = new Set();

// Example programs: a seeded sample of each file's distinct lines.
const files = execSync('git ls-files', { cwd: repoRoot, encoding: 'utf8' })
    .split('\n')
    .filter((f) => /\.(nc|gcode|ngc|tap)$/i.test(f) && !f.startsWith('cppport/'))
    .sort();
for (const file of files) {
    const distinct = [...new Set(readFileSync(join(repoRoot, file), 'utf8').split(/\r?\n/))];
    for (let i = 0; i < 8 && distinct.length; i++) {
        lines.add(distinct.splice(Math.floor(random() * distinct.length), 1)[0].slice(0, 160));
    }
}

// The grammar's corners.
[
    '', ' ', '%', '%wait', 'O1000', 'o12 (sub)', 'N10 G1 X1', 'n5g0x1', 'G38.2 Z-10 F100', 'G1.25', 'G1.',
    'M3 S12000', 'm30', 'T1 M6', 'G0 X-1.5 Y+2 Z.5', 'X1e5 Y0x1F', '(comment) G1 X1 (another)', '(unclosed G1 X1',
    '; End of program', '; Notes', 'G1 X1 ; move', '// line comment G1', '/* block */ G1 X1', '/* open',
    "'single' G1", '"double" X1', "'esc\\' still' X2", '"open string X1', 'IF [#1 EQ 2] GOTO 5',
    'WHILE [#2 LT 10] DO1', 'END1', 'ENDWHILE', 'endif', 'CALL SUB1', 'REPEAT 3', 'x or y xor z',
    '#100=5', '#<_x>+1', 'G0 X[#<_x>+1]', 'VC1=2', 'VS10', 'VZOFX', 'vzofz 5', 'ATAN[1]/[2]', 'SIN[30.5]',
    'ASIN[0.5]', 'ROUND[1.5', 'ln[2]', 'FUP[1.2]+FIX[3]', 'G1 X10 Y10 (TODO: check this)', '(NOTE: the bit is just fine)',
    '$H', '$J=G21G91X10F3000', '$$', '$130=800', '?', '!', '~', '[MSG:Info]', 'ok', 'error:20',
    'G2 X10 Y0 I5 J0', 'G10 L20 P1 X0 Y0 Z0', 'G53 G0 Z-1', 'F1000.', 'S+100', 'X--1', '1..2', '.5.5',
    '\tG1\tX1', 'G1 X1   ', 'Movement complete', 'Tool change', 'Engraving N-letters',
].forEach((line) => lines.add(line));

// Token soup: words, numbers, punctuation and the grammar's trigger letters.
const pieces = [
    'G', 'g', 'M', 'm', 'N', 'n', 'O', 'o', 'X', 'Y', 'Z', 'A', 'F', 'S', 'T', 'I', 'J', 'P', 'Q', 'R', 'E', 'V', 'C',
    '1', '12', '0.5', '.25', '-3', '+4', '38.2', '1.', '#', '#1', '[', ']', '(', ')', ';', '%', "'", '"', '\\', '/',
    '*', '//', '/*', '*/', '=', '<', '>', '&', ' ', ' ', ' ', 'IF', 'EQ', 'GOTO', 'WHILE', 'END', 'SIN[', 'ABS[',
    'VC', 'VS', 'VZOFY', 'TODO:', 'just', 'the', '_', '$', 'e5', 'x1F',
];
for (let i = 0; i < 400; i++) {
    let line = '';
    const count = 1 + Math.floor(random() * 12);
    for (let j = 0; j < count; j++) line += pick(pieces);
    lines.add(line);
}

writeCases(
    'gcode_highlight_golden.json',
    [`highlight.js ${version} (lib/languages/gcode.js)`, 'react-syntax-highlighter styles/hljs/a11y-light'],
    [...lines].map((line) => ({ line, runs: runs(line) })),
);
