// Extracts data tables from the gSender JavaScript/TypeScript sources into
// cppport/resources/data/*.json, which gs_core embeds at build time, and
// copies the images the app shows into cppport/resources/images.
//
//   node cppport/tools/extract_data.mjs
//
// Run it from anywhere after `npm install`/`yarn` in the repository root (it
// uses the repository's esbuild to bundle each module). Re-run whenever the
// upstream tables change and commit the regenerated JSON.

import { createRequire } from 'node:module';
import { copyFileSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '..', '..');
const outDir = resolve(here, '..', 'resources', 'data');
const require = createRequire(join(repoRoot, 'package.json'));
const esbuild = require('esbuild');

// Bundles one module and returns its exports.
function load(entry) {
    const result = esbuild.buildSync({
        entryPoints: [join(repoRoot, entry)],
        bundle: true,
        write: false,
        format: 'cjs',
        platform: 'node',
        logLevel: 'silent',
        alias: {
            app: join(repoRoot, 'src/app/src'),
            server: join(repoRoot, 'src/server'),
        },
        loader: { '.ts': 'ts', '.tsx': 'tsx', '.js': 'jsx' },
    });
    const code = result.outputFiles[0].text;
    const module = { exports: {} };
    new Function('module', 'exports', 'require', code)(module, module.exports, require);
    return module.exports;
}

function pick(exports, names, entry) {
    const out = {};
    for (const [key, name] of Object.entries(names)) {
        if (!(name in exports)) {
            throw new Error(`${entry} does not export ${name}`);
        }
        out[key] = exports[name];
    }
    return out;
}

function write(name, sources, data) {
    const payload = { _source: sources, ...data };
    const file = join(outDir, name);
    writeFileSync(file, `${JSON.stringify(payload, null, 2)}\n`, 'utf8');
    console.log(`wrote ${relative(repoRoot, file)}`);
}

mkdirSync(outDir, { recursive: true });

// Firmware tables used by the controllers (console annotations, errors, alarms).
{
    const entry = 'src/server/controllers/Grbl/constants.js';
    write('grbl.json', [entry], pick(load(entry), {
        errors: 'GRBL_ERRORS',
        alarms: 'GRBL_ALARMS',
        settings: 'GRBL_SETTINGS',
        modalGroups: 'GRBL_MODAL_GROUPS',
        realtimeCommands: 'GRBL_REALTIME_COMMANDS',
    }, entry));
}
{
    const entry = 'src/server/controllers/Grblhal/constants.js';
    write('grblhal.json', [entry], pick(load(entry), {
        errors: 'GRBL_HAL_ERRORS',
        alarms: 'GRBL_HAL_ALARMS',
        settings: 'GRBL_HAL_SETTINGS',
        modalGroups: 'GRBL_HAL_MODAL_GROUPS',
        realtimeCommands: 'GRBLHAL_REALTIME_COMMANDS',
        atciSupportedVersion: 'ATCI_SUPPORTED_VERSION',
    }, entry));
}

// Setting metadata the configuration UI uses (input types, ranges, groups).
{
    const grblEntry = 'src/app/src/constants/firmware/grbl.ts';
    const halEntry = 'src/app/src/constants/firmware/grblHAL.ts';
    write('firmware_settings_ui.json', [grblEntry, halEntry], {
        inputTypes: load(grblEntry).GRBL_SETTINGS_INPUT_TYPES,
        grbl: load(grblEntry).GRBL_SETTINGS,
        grblhal: load(halEntry).GRBL_HAL_SETTINGS,
    });
}

// Machine profiles with their default EEPROM values.
{
    const profilesEntry = 'src/app/src/features/Config/assets/MachineDefaults/defaultMachineProfiles.ts';
    const boardEntry = 'src/app/src/features/Config/assets/MachineDefaults/boardProfiles.ts';
    const coreEntry = 'src/app/src/features/Config/assets/MachineDefaults/grblCore.ts';
    const core = load(coreEntry);
    const coreData = {};
    for (const [key, value] of Object.entries(core)) {
        if (typeof value !== 'function') {
            coreData[key] = value;
        }
    }
    // orderedSettings are Maps (settings written in that order, after the
    // rest): kept as [key, value] pairs, which JSON.stringify would lose.
    const profiles = load(profilesEntry).default.map((profile) =>
        profile.orderedSettings instanceof Map
            ? { ...profile, orderedSettings: [...profile.orderedSettings] }
            : profile,
    );
    write('machine_profiles.json', [profilesEntry, boardEntry, coreEntry], {
        profiles,
        boardProfiles: load(boardEntry).BOARD_PROFILES,
        grblCore: coreData,
    });
}

// Rotary mounting setup: the ready-made programs that bore the rotary
// track's mounting holes, by hole layout and end mill.
{
    const entry = 'src/app/src/features/Rotary/utils/mountingSetupMacros.ts';
    write('rotary_mounting.json', [entry], { holeTypes: load(entry).HOLE_TYPES });

    // The track illustrations the mounting setup dialog shows.
    const assets = join(repoRoot, 'src/app/src/features/Rotary/assets');
    const images = resolve(here, '..', 'resources', 'images', 'rotary');
    mkdirSync(images, { recursive: true });
    for (const name of [
        'custom-boring-track-top-view.png',
        'extension-track-top-view.png',
        'standard-track-top-view.png',
    ]) {
        copyFileSync(join(assets, name), join(images, name));
        console.log(`copied ${relative(repoRoot, join(images, name))}`);
    }
}

// Defaults the config store backfills into ~/.sender_rc. They are not
// exported, so the block declaring them is evaluated on its own; fail loudly
// if upstream reshapes it.
{
    const entry = 'src/server/services/configstore/index.js';
    const source = readFileSync(join(repoRoot, entry), 'utf8');
    const start = source.indexOf('const defaultJobStats');
    const end = source.indexOf('const writeFileAtomicSync');
    if (start < 0 || end < start) {
        throw new Error(`${entry}: the default declarations moved; update extract_data.mjs`);
    }
    const defaults = new Function(
        `${source.slice(start, end)}\nreturn { defaultJobStats, defaultState, defaultMaintenance };`,
    )();
    write('config_defaults.json', [entry], {
        state: defaults.defaultState,
        jobStats: defaults.defaultJobStats,
        maintenance: defaults.defaultMaintenance,
    });
}
