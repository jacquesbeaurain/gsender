// Loads gSender's own modules into node for the golden-fixture generators:
// bundles one entry with the repository's esbuild (TypeScript/JSX, `app/`
// imports) and returns its exports. Modules that need the browser or the
// running application are replaced by stubs.

import { createRequire } from 'node:module';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
export const repoRoot = resolve(here, '..', '..', '..');
const require = createRequire(join(repoRoot, 'package.json'));
const esbuild = require('esbuild');

// Some modules read browser storage at import time.
globalThis.localStorage ??= { getItem: () => null, setItem() {}, removeItem() {} };

// Stubs: [regex over the import path, module source]. Paths are matched as
// written and after the `app` alias, so patterns should allow both.
export const stubs = {
    // SoftLimits and others read the Redux store; serve globalThis.__reduxState.
    redux: [/(^|[\\/])store[\\/]redux$/, 'export default { getState: () => globalThis.__reduxState ?? {} };'],
    // The UI's settings store; serve globalThis.__storeValues by key.
    store: [
        /(^app|[\\/]src)[\\/]store$/,
        'export default { get: (key, fallback) => (globalThis.__storeValues ?? {})[key] ?? fallback, set() {}, on() {} };',
    ],
    // The socket.io controller client.
    controller: [/(^app|[\\/]src)[\\/]lib[\\/]controller$/, 'export default {};'],
};

export async function loadModule(entry, useStubs = []) {
    const plugin = {
        name: 'stubs',
        setup(build) {
            useStubs.forEach(([filter], i) => {
                build.onResolve({ filter }, () => ({ path: String(i), namespace: 'stub' }));
            });
            build.onLoad({ filter: /.*/, namespace: 'stub' }, (args) => ({
                contents: useStubs[Number(args.path)][1],
                loader: 'js',
            }));
        },
    };
    const result = await esbuild.build({
        entryPoints: [join(repoRoot, entry)],
        bundle: true,
        write: false,
        format: 'cjs',
        platform: 'node',
        logLevel: 'silent',
        plugins: [plugin],
        alias: { app: join(repoRoot, 'src/app/src') },
        define: { 'import.meta.env': '{}' }, // Vite's, read by modules the constants pull in
        loader: { '.ts': 'ts', '.tsx': 'tsx', '.js': 'jsx' },
    });
    const module = { exports: {} };
    new Function('module', 'exports', 'require', result.outputFiles[0].text)(module, module.exports, require);
    return module.exports;
}

// Deterministic choices (mulberry32), so fixtures only change with upstream.
export function seededRandom(seed) {
    let state = seed | 0;
    const random = () => {
        state = (state + 0x6d2b79f5) | 0;
        let t = state;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
    return { random, pick: (values) => values[Math.floor(random() * values.length)] };
}

// Writes cppport/tests/data/<name> as {"_source": [...], "cases": [...]},
// one case per line so regenerated fixtures diff well.
export function writeCases(name, sources, cases) {
    const file = resolve(here, '..', '..', 'tests', 'data', name);
    mkdirSync(dirname(file), { recursive: true });
    const lines = cases.map((c) => JSON.stringify(c)).join(',\n');
    writeFileSync(file, `{"_source":${JSON.stringify(sources)},"cases":[\n${lines}\n]}\n`, 'utf8');
    console.log(`wrote ${relative(repoRoot, file)} (${cases.length} cases)`);
}
