pragma Singleton

import QtQuick
import GSender

// Upstream's design tokens (src/app/src/index.css, tailwind.config.ts): the
// semantic surface / content / outline colours, light and the Workshop dark
// theme, and the palettes upstream overrides (its blue, green, red, orange,
// purple are not Tailwind's; 50-300 of blue, green, red and orange are pale
// tints, 500 mixed with white). Components use the semantic names; the raw
// palettes are for the few places upstream names a shade.
QtObject {
    id: theme

    readonly property bool dark: Backend.darkMode
    // Accessibility: focus rings shown (upstream draws none otherwise).
    readonly property bool focusRings: Backend.focusRings
    // Accessibility: animations off (upstream's body.reduced-motion).
    readonly property bool reducedMotion: Backend.reducedMotion

    // A 1 CSS px border is one device pixel in Chromium; Qt would draw it
    // 1.5 px wide and blurred at a 150% scale: borders use one device pixel.
    readonly property real hairline: 1 / Math.max(1, Qt.application.screens.length > 0
                                                  ? Qt.application.screens[0].devicePixelRatio : 1)

    // Tailwind's gray (upstream keeps the default scale).
    readonly property var gray: ({
        50: "#f9fafb", 100: "#f3f4f6", 200: "#e5e7eb", 300: "#d1d5db", 400: "#9ca3af",
        500: "#6b7280", 600: "#4b5563", 700: "#374151", 800: "#1f2937", 900: "#111827"
    })
    readonly property var blue: ({
        50: "#f5f9fc", 100: "#e6eff8", 200: "#b8d2ea", 300: "#88b3dc", 400: "#5291cd",
        500: "#3F85C7", 600: "#3978b3", 700: "#2c5d8b", 800: "#265077", 900: "#204364",
        950: "#193550"
    })
    readonly property var green: ({
        50: "#f3faf8", 100: "#dff1ec", 200: "#a3d8c8", 300: "#64bea2", 400: "#1ea178",
        500: "#059669", 600: "#05875f", 700: "#047854", 800: "#04694a", 900: "#035a3f"
    })
    readonly property var red: ({
        50: "#fdf4f4", 100: "#fae3e3", 200: "#f2afaf", 300: "#e97878", 400: "#e03c3c",
        500: "#dc2626", 600: "#c62222", 700: "#b01e1e", 800: "#9a1b1b", 900: "#841717"
    })
    readonly property var orange: ({
        50: "#fcf8f3", 100: "#f6ecdf", 200: "#e6c8a5", 300: "#d5a368", 400: "#c27924",
        500: "#bb6a0c", 600: "#a85f0b", 700: "#96550a", 800: "#834a08", 900: "#704007",
        950: "#5e3506"
    })
    readonly property var purple: ({
        50: "#f7f6fc", 100: "#eae9f8", 200: "#c3bfec", 300: "#9a94df", 400: "#6d64d0",
        500: "#5c52cb", 600: "#534ab7", 700: "#4a42a3", 800: "#413a8e", 900: "#37317a",
        950: "#2e2966"
    })
    // Upstream's robin (the secondary buttons' border, the active glow).
    readonly property var robin: ({
        100: "#ecf2f8", 200: "#c7d9eb", 300: "#a1c0dd", 400: "#7ca7d0", 500: "#689AC9",
        600: "#568ec3", 700: "#3c74a9"
    })
    // Tailwind's own scales, for the shades upstream takes from them: the
    // families it does not override (yellow, emerald, ...) and the default
    // green, red and blue some components keep (a check's green-500).
    readonly property var tw: ({
        blue: { 50: "#eff6ff", 100: "#dbeafe", 200: "#bfdbfe", 300: "#93c5fd", 400: "#60a5fa", 500: "#3b82f6",
                600: "#2563eb", 700: "#1d4ed8", 800: "#1e40af", 900: "#1e3a8a", 950: "#172554" },
        green: { 50: "#f0fdf4", 100: "#dcfce7", 200: "#bbf7d0", 300: "#86efac", 400: "#4ade80", 500: "#22c55e",
                 600: "#16a34a", 700: "#15803d", 800: "#166534", 900: "#14532d", 950: "#052e16" },
        red: { 50: "#fef2f2", 100: "#fee2e2", 200: "#fecaca", 300: "#fca5a5", 400: "#f87171", 500: "#ef4444",
               600: "#dc2626", 700: "#b91c1c", 800: "#991b1b", 900: "#7f1d1d", 950: "#450a0a" },
        orange: { 50: "#fff7ed", 100: "#ffedd5", 200: "#fed7aa", 300: "#fdba74", 400: "#fb923c", 500: "#f97316",
                  600: "#ea580c", 700: "#c2410c", 800: "#9a3412", 900: "#7c2d12", 950: "#431407" },
        yellow: { 50: "#fefce8", 100: "#fef9c3", 200: "#fef08a", 300: "#fde047", 400: "#facc15", 500: "#eab308",
                  600: "#ca8a04", 700: "#a16207", 800: "#854d0e", 900: "#713f12", 950: "#422006" },
        amber: { 50: "#fffbeb", 100: "#fef3c7", 200: "#fde68a", 300: "#fcd34d", 400: "#fbbf24", 500: "#f59e0b",
                 600: "#d97706", 700: "#b45309", 800: "#92400e", 900: "#78350f", 950: "#451a03" },
        emerald: { 50: "#ecfdf5", 100: "#d1fae5", 200: "#a7f3d0", 300: "#6ee7b7", 400: "#34d399", 500: "#10b981",
                   600: "#059669", 700: "#047857", 800: "#065f46", 900: "#064e3b", 950: "#022c22" },
        slate: { 50: "#f8fafc", 100: "#f1f5f9", 200: "#e2e8f0", 300: "#cbd5e1", 400: "#94a3b8", 500: "#64748b",
                 600: "#475569", 700: "#334155", 800: "#1e293b", 900: "#0f172a", 950: "#020617" },
        violet: { 50: "#f5f3ff", 100: "#ede9fe", 200: "#ddd6fe", 300: "#c4b5fd", 400: "#a78bfa", 500: "#8b5cf6",
                  600: "#7c3aed", 700: "#6d28d9", 800: "#5b21b6", 900: "#4c1d95", 950: "#2e1065" },
        rose: { 50: "#fff1f2", 100: "#ffe4e6", 200: "#fecdd3", 300: "#fda4af", 400: "#fb7185", 500: "#f43f5e",
                600: "#e11d48", 700: "#be123c", 800: "#9f1239", 900: "#881337", 950: "#4c0519" },
        cyan: { 50: "#ecfeff", 100: "#cffafe", 200: "#a5f3fc", 300: "#67e8f9", 400: "#22d3ee", 500: "#06b6d4",
                600: "#0891b2", 700: "#0e7490", 800: "#155e75", 900: "#164e63", 950: "#083344" }
    })

    // The console's content: always dark, whatever the mode (MessageIcon,
    // ConsoleList). Sent reads bright, responses sit back, colour marks what
    // needs attention.
    readonly property var consoleColors: ({
        surface: "#090D12", border: Qt.rgba(1, 1, 1, 0.1), rowBorder: Qt.rgba(1, 1, 1, 0.03),
        gcode: "#F4F7FA", gcodeIcon: "#659dd2", response: "#A0AABA", system: "#37ab87",
        warning: "#fdba74", error: "#facc15", alarm: "#dc2626"
    })

    // Surfaces.
    readonly property color surfaceBase: dark ? "#151B23" : gray[100]
    readonly property color surfaceSunken: dark ? "#090D12" : gray[200]
    readonly property color surfaceRaised: dark ? "#202832" : "#ffffff"
    readonly property color surfaceElevated: dark ? "#2D3946" : "#ffffff"
    readonly property color surfaceHover: dark ? "#3A4857" : gray[200]
    readonly property color surfaceActive: dark ? "#445261" : gray[300]
    readonly property color surfaceDisabled: dark ? "#252D36" : gray[100]
    // Content (text and icons).
    readonly property color contentPrimary: dark ? "#F4F7FA" : gray[900]
    readonly property color contentSecondary: dark ? "#CFD6DF" : gray[700]
    readonly property color contentMuted: dark ? "#A0AABA" : gray[600]
    readonly property color contentDisabled: dark ? "#778291" : gray[400]
    readonly property color contentInverse: dark ? "#151B23" : "#ffffff"
    // Outlines.
    readonly property color outlineSubtle: dark ? "#3F4B59" : gray[200]
    readonly property color outline: dark ? "#59687B" : gray[300]
    readonly property color outlineStrong: dark ? "#72849D" : gray[400]
    readonly property color outlineDisabled: dark ? "#3A444F" : gray[200]
    // shadcn primitives (index.css: the same names, light and dark).
    readonly property color background: dark ? surfaceBase : "#ffffff"
    readonly property color card: dark ? surfaceRaised : "#ffffff"
    readonly property color secondary: dark ? surfaceRaised : gray[100]   // also the widget cards
    readonly property color muted: dark ? surfaceElevated : gray[100]
    readonly property color border: dark ? outline : gray[200]
    // A hovered ghost's fill (its text is accent-foreground: contentPrimary).
    readonly property color accent: dark ? surfaceHover : gray[100]
    readonly property color primary: blue[600]
    readonly property color primaryForeground: "#ffffff"
    // Blue text and icons on a surface (text-blue-600 dark:text-blue-400).
    readonly property color primaryText: dark ? blue[400] : blue[600]
    readonly property color destructive: red[500]
    readonly property color ring: blue[500]

    // The pairs upstream's components spell out (bg-gray-50 dark:bg-dark...),
    // named for where they recur.
    // A text box's fill: white, sunken in dark.
    readonly property color field: dark ? surfaceSunken : "#ffffff"
    // Bars and page grounds a shade off white: the top bar, the wizards'
    // header and footer, the Stats page.
    readonly property color surfaceBar: dark ? surfaceBase : gray[50]
    // Rows and list items a shade off white (striped rows, notifications).
    readonly property color surfaceSubtle: dark ? surfaceRaised : gray[50]
    // A disabled button's fill.
    readonly property color buttonDisabled: dark ? surfaceRaised : gray[300]
    // Grey text that is white in dark: menus and secondary buttons
    // (text-gray-600), Stats and Config body text (text-gray-700).
    readonly property color contentSoft: dark ? contentPrimary : gray[600]
    readonly property color contentBody: dark ? contentPrimary : gray[700]

    // Sizes: upstream's rem scale (1rem = 16px) and the touch targets it
    // keeps on its buttons (min-h-11 = 44px).
    readonly property int radius: 8        // rounded-lg
    readonly property int radiusSmall: 6   // rounded-md
    readonly property int touchTarget: 44
    readonly property int fontXs: 12
    readonly property int fontSm: 14
    readonly property int fontBase: 16
    readonly property int fontLg: 18
    readonly property int font3xl: 30
    readonly property int fontXl: 20
    readonly property string monoFont: Qt.platform.os === "windows" ? "Consolas" : "monospace"

    // The status pill's colours by Grbl state (MachineStatus).
    function stateColor(state) {
        switch (state) {
        case "Idle": return gray[500]
        case "Run": case "Jog": case "Check": return green[600]
        case "Home": return blue[600]
        case "Hold": case "Door": return tw.yellow[600]
        case "Alarm": return red[500]
        case "Tool": return purple[600]
        case "Sleep": return blue[300]
        default: return gray[800]   // disconnected
        }
    }
    // The text on the status pill: white, but dark blue on Sleep's pale blue.
    function stateTextColor(state) {
        return state === "Sleep" ? blue[950] : "white"
    }

    // An icon of resources/icons in a colour.
    function icon(name, color) {
        return "image://icon/" + name + "/" + String(color).replace("#", "")
    }
}
