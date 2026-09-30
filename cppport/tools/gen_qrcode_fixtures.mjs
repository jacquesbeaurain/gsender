// Generates cppport/tests/data/qrcode_golden.json: the QR codes gSender
// draws (react-qr-code, which runs qrcode-generator: byte mode on the UTF-8
// bytes, the smallest version that fits, the mask with the fewest "lost
// points"). Each case is a text, a level and the symbol's rows, each row as
// hex digits of four modules (dark = 1, most significant first, the last
// digit zero-padded). gs_core_tests compares every module.
//
//   node cppport/tools/gen_qrcode_fixtures.mjs
//
// Needs the repository's node_modules (react-qr-code pulls in
// qrcode-generator), or NODE_PATH pointing at a node_modules holding it.

import { createRequire } from 'node:module';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';

import { repoRoot, seededRandom, writeCases } from './lib/bundle.mjs';

const require = createRequire(join(repoRoot, 'package.json'));
// NODE_PATH is honoured by require(); the version comes from the package
// directory (its exports hide package.json).
const qrcode = require('qrcode-generator');
const packageDir = dirname(dirname(require.resolve('qrcode-generator')));
const version = JSON.parse(readFileSync(join(packageDir, 'package.json'), 'utf8')).version;
// As react-qr-code sets it: the UTF-8 bytes.
qrcode.stringToBytes = (s) => Array.from(new TextEncoder().encode(s));

function encode(text, level) {
    const qr = qrcode(0, level);
    qr.addData(text);
    qr.make();
    const n = qr.getModuleCount();
    const rows = [];
    for (let r = 0; r < n; r++) {
        let hex = '';
        for (let c = 0; c < n; c += 4) {
            let v = 0;
            for (let k = 0; k < 4; k++) {
                v = (v << 1) | (c + k < n && qr.isDark(r, c + k) ? 1 : 0);
            }
            hex += v.toString(16);
        }
        rows.push(hex);
    }
    return { text, level, size: n, rows };
}

const texts = [
    '',
    'a',
    'http://192.168.0.10:8000/#/remote',
    'http://10.0.0.2:8000/#/remote',
    'http://172.16.254.1:65535/#/remote',
    '192.168.1.123:8080',
    'https://resources.sienci.com/view/gs-using-gsender/',
    'https://resources.sienci.com/view/gs-additional-features/#wireless-control',
    'Ünïcödé ✓ 日本',
    '0123456789',
];
const { random } = seededRandom(0x9c0de);
const alphabet = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/.-_#?=&%';
// Lengths chosen to cross version boundaries: 7+ carries the version
// information, 10+ the 16-bit length field.
for (const length of [17, 32, 53, 78, 106, 134, 154, 192, 230, 271, 321]) {
    let s = '';
    for (let i = 0; i < length; i++) {
        s += alphabet[Math.floor(random() * alphabet.length)];
    }
    texts.push(s);
}

const cases = [];
for (const text of texts) {
    cases.push(encode(text, 'L'));
}
for (const level of ['M', 'Q', 'H']) {
    cases.push(encode('http://192.168.0.10:8000/#/remote', level));
    cases.push(encode(texts[texts.length - 4], level));
}

writeCases('qrcode_golden.json', [`qrcode-generator ${version}`, 'react-qr-code 2 (level L, UTF-8 bytes)'], cases);
