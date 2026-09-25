// Writes the icons the QML UI uses into cppport/resources/icons as SVG files:
// react-icons components rendered as upstream renders them (react-dom/
// server), plus the SVG assets of gSender's own. Their colour stays
// `currentColor`; the UI's icon provider paints them in the colour asked for.
// Re-run after adding an icon to the list below.
//
//   node cppport/tools/extract_icons.mjs

import { createRequire } from 'node:module';
import { copyFileSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '..', '..');
const require = createRequire(join(repoRoot, 'package.json'));
const React = require('react');
const { renderToStaticMarkup } = require('react-dom/server');

const out = resolve(here, '..', 'resources', 'icons');
// react-icons does not export its package.json; find it beside the entry.
const iconsVersion = JSON.parse(
    readFileSync(join(dirname(require.resolve('react-icons')), 'package.json'), 'utf8'),
).version;
mkdirSync(out, { recursive: true });

// [react-icons set, component] - where upstream uses them.
const icons = [
    // The navigation rail (features/navbar).
    ['io5', 'IoSpeedometerOutline'],
    ['ri', 'RiToolsFill'],
    ['fa', 'FaTasks'],
    // The top bar: connection, notifications, status icons.
    ['bs', 'BsUsbPlug'],
    ['bs', 'BsEthernet'],
    ['bs', 'BsCheckCircleFill'],
    ['pi', 'PiPlugLight'],
    ['gr', 'GrSatellite'],
    ['fa', 'FaArrowAltCircleRight'],
    ['lu', 'LuBell'],
    ['fa6', 'FaRegKeyboard'],
    ['lu', 'LuGamepad2'],
    // The machine status (MachineStatus, UnlockButton).
    ['fa', 'FaUnlock'],
    ['fa', 'FaHome'],
    ['fa', 'FaLock'],
    // The DRO: Go To, Park, Zero.
    ['fa6', 'FaPaperPlane'],
    ['ri', 'RiParkingFill'],
    ['vsc', 'VscTarget'],
    // File control, job control, the overrides.
    ['fa', 'FaFolderOpen'],
    ['md', 'MdKeyboardArrowDown'],
    ['md', 'MdKeyboardArrowLeft'],
    ['md', 'MdKeyboardArrowRight'],
    ['md', 'MdClose'],
    ['fa', 'FaRedo'],
    ['lia', 'LiaFileUploadSolid'],
    ['lu', 'LuFileCode2'],
    ['md', 'MdInfoOutline'],
    ['fi', 'FiClock'],
    ['lu', 'LuPencil'],
    ['lu', 'LuFootprints'],
    // The SD card (features/SDCard, lucide-react).
    ['lu', 'LuRefreshCw'],
    ['lu', 'LuUpload'],
    ['lu', 'LuHardDrive'],
    ['lu', 'LuFile'],
    ['io5', 'IoPlayOutline'],
    ['pi', 'PiPause'],
    ['fi', 'FiOctagon'],
    ['tb', 'TbVector'],
    ['md', 'MdFormatListNumbered'],
    ['fa', 'FaPlay'],
    ['fa', 'FaUndo'],
    ['fa', 'FaMinus'],
    ['fa', 'FaPlus'],
    // The tool area (features/Tools): the tabs, the console (lucide, as
    // react-icons' lu set), probe, macros, spindle/laser, coolant, rotary.
    ['lu', 'LuCopy'],
    ['lu', 'LuEraser'],
    ['lu', 'LuExternalLink'],
    ['lu', 'LuUnplug'],
    ['lu', 'LuArrowDown'],
    ['lu', 'LuChevronRight'],
    ['lu', 'LuChevronLeft'],
    ['lu', 'LuChevronDown'],
    ['lu', 'LuChevronsLeft'],
    ['lu', 'LuChevronsRight'],
    ['lu', 'LuDot'],
    ['lu', 'LuCog'],
    ['lu', 'LuTriangleAlert'],
    ['lu', 'LuCircleAlert'],
    ['lu', 'LuOctagonAlert'],
    ['lu', 'LuPause'],
    ['lu', 'LuPlay'],
    ['lu', 'LuPlus'],
    ['lu', 'LuRotateCcw'],
    ['lu', 'LuEye'],
    ['lu', 'LuEyeOff'],
    ['lu', 'LuSearch'],
    ['lu', 'LuWrench'],
    ['lu', 'LuX'],
    ['lu', 'LuTrash'],
    ['fa', 'FaBan'],
    ['fa', 'FaCheck'],
    ['fa', 'FaEdit'],
    ['fa', 'FaEllipsisH'],
    ['fa', 'FaExclamation'],
    ['fa', 'FaFileExport'],
    ['fa', 'FaFileImport'],
    ['fa', 'FaLightbulb'],
    ['fa', 'FaRedoAlt'],
    ['fa', 'FaRegLightbulb'],
    ['fa', 'FaSatelliteDish'],
    ['fa', 'FaTimes'],
    ['fa', 'FaTrashAlt'],
    ['fa', 'FaUndoAlt'],
    ['fa', 'FaWater'],
    ['fa6', 'FaShower'],
    // The G-code editor.
    ['pi', 'PiMouseScroll'],
    ['lu', 'LuCopyCheck'],
    ['lu', 'LuTrash2'],
    ['lu', 'LuSave'],
    ['lu', 'LuChevronUp'],
    ['lu', 'LuArrowUp'],
    ['lu', 'LuCheck'],
    ['vsc', 'VscDebugStart'],
    // The Stats page.
    ['fa', 'FaTrash'],
    ['fa', 'FaGithub'],
    ['fa6', 'FaBookBookmark'],
    ['im', 'ImBubbles4'],
    ['lu', 'LuCircleCheck'],
    ['lu', 'LuCircleX'],
    ['lu', 'LuPen'],
    ['fa', 'FaExternalLinkAlt'],
    ['go', 'GoArrowUpRight'],
    ['fa', 'FaCheckCircle'],
    ['fa6', 'FaCircleXmark'],
    ['fa', 'FaDownload'],
    ['io', 'IoIosWarning'],
    ['pi', 'PiMaskHappyBold'],
    ['fa', 'FaCircle'],
    // The Tools page.
    ['gi', 'GiFlatPlatform'],
    ['bi', 'BiSolidCylinder'],
    ['tb', 'TbRulerMeasure'],
    ['md', 'MdSquareFoot'],
    ['fa', 'FaKeyboard'],
    ['fa', 'FaGamepad'],
    ['fa', 'FaSdCard'],
    ['lu', 'LuDrill'],
    ['pi', 'PiPuzzlePiece'],
    ['lu', 'LuArrowLeft'],
];

for (const [set, name] of icons) {
    const component = require(`react-icons/${set}`)[name];
    if (!component) throw new Error(`react-icons/${set} has no ${name}`);
    // Sized by the viewBox rather than "1em", so a renderer draws it at its size.
    const svg = renderToStaticMarkup(React.createElement(component)).replace(/ (height|width)="1em"/g, '');
    writeFileSync(join(out, `${name}.svg`), `${svg}\n`, 'utf8');
}

// gSender's own: the navigation's Carve picture, the jog controls' labels.
const own = [
    ['src/app/src/features/navbar/assets/Carve.svg', 'Carve.svg'],
    ['src/app/src/features/Jogging/assets/labels.svg', 'JogWheelLabels.svg'],
    ['src/app/src/features/Jogging/assets/zLabels.svg', 'JogZLabels.svg'],
    ['src/app/src/features/Jogging/assets/aLabels.svg', 'JogALabels.svg'],
];
for (const [from, to] of own) {
    copyFileSync(join(repoRoot, from), join(out, to));
}
// The Surfacing tool's pattern pictures (features/Surfacing/SVG), their
// paths as the components draw them.
const patterns = [
    ['SurfacingSpiral.svg', '3 3 25 24',
     '<path d="M4 3L3 3 3 4 3 26 3 27 4 27 23 27 24 27 24 26 24 8 24 7 23 7 8 7 7 7 7 8 7 22 7 23 8 23 19 23 20 23 20 22 20 12 20 11 19 11 12 11 11 11 11 12 11 18 11 19 12 19 15 19 16 19 16 18 16 15 15 15 14 15 14 16 15 16 15 18 12 18 12 12 19 12 19 22 8 22 8 8 23 8 23 26 4 26 4 4 27 4 27 27 28 27 28 3 27 3z"/>'],
    ['SurfacingZigZag.svg', '14.27 6.1 57.6 76.83',
     '<path transform="rotate(90.227 43.07 44.514)" d="M81.37 61.864l-11.3 11.3-11.3-11.3 2.8-2.8 6.5 6.5v-39.7c0-3.3-2.7-6-6-6h-11c-3.3 0-6 2.7-6 6v37c0 5.5-4.5 10-10 10h-11c-5.5 0-10-4.5-10-10v-39.2l-6.5 6.5-2.8-2.8 11.3-11.3 11.3 11.3-2.8 2.8-6.5-6.5v39.2c0 3.3 2.7 6 6 6h11c3.3 0 6-2.7 6-6v-37c0-5.5 4.5-10 10-10h11c5.5 0 10 4.5 10 10v39.6l6.5-6.5 2.8 2.9z"/>'],
];
for (const [name, viewBox, body] of patterns) {
    writeFileSync(join(out, name),
        `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}" fill="currentColor">${body}</svg>\n`, 'utf8');
}

writeFileSync(
    join(out, 'README.md'),
    `# Icons

Generated by \`tools/extract_icons.mjs\` - do not edit. The icons come from
the icon sets react-icons ${iconsVersion} bundles, under their own
licences: Font Awesome Free (fa, fa6; CC BY 4.0), Lucide (lu; ISC), Ionicons
(io5; MIT), Remix Icon (ri; Apache 2.0), Bootstrap Icons (bs; MIT), Phosphor
(pi; MIT), Grommet (gr; Apache 2.0), VS Code Codicons (vsc; CC BY 4.0),
Material Design icons (md; Apache 2.0), Feather (fi; MIT), Line Awesome (lia;
MIT/CC BY 4.0), Tabler (tb; MIT), Game Icons (gi; CC BY 3.0), BoxIcons (bi;
MIT), Github Octicons (go; MIT), IcoMoon Free (im; CC BY 4.0), Ionicons 4
(io; MIT).
Carve.svg, the Jog*Labels.svg files and the Surfacing*.svg patterns are
gSender's own.
`,
    'utf8',
);
console.log(`wrote ${icons.length + own.length + patterns.length} icons to ${out}`);
