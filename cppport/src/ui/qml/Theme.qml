pragma Singleton

import QtQuick
import GSender

// Upstream's design tokens (src/app/src/index.css, tailwind.config.ts): the
// semantic surface / content / outline colours, light and the Workshop dark
// theme, and the palettes upstream overrides (its blue, green, red, orange,
// purple are not Tailwind's). Components use the semantic names; the raw
// palettes are for the few places upstream names a shade.
QtObject {
    id: theme

    readonly property bool dark: Backend.darkMode

    // Tailwind's gray (upstream keeps the default scale).
    readonly property var gray: ({
        50: "#f9fafb", 100: "#f3f4f6", 200: "#e5e7eb", 300: "#d1d5db", 400: "#9ca3af",
        500: "#6b7280", 600: "#4b5563", 700: "#374151", 800: "#1f2937", 900: "#111827"
    })
    readonly property var blue: ({
        50: "#9fc2e3", 100: "#8cb6dd", 200: "#79aad8", 300: "#659dd2", 400: "#5291cd",
        500: "#3F85C7", 600: "#3978b3", 700: "#2c5d8b", 800: "#265077", 900: "#204364"
    })
    readonly property var green: ({
        50: "#82cbb4", 100: "#69c0a5", 200: "#50b696", 300: "#37ab87", 400: "#1ea178",
        500: "#059669", 600: "#05875f", 700: "#047854", 800: "#04694a", 900: "#035a3f"
    })
    readonly property var red: ({
        50: "#ee9393", 100: "#ea7d7d", 200: "#e76767", 300: "#e35151", 400: "#e03c3c",
        500: "#dc2626", 600: "#c62222", 700: "#b01e1e", 800: "#9a1b1b", 900: "#841717"
    })
    readonly property var orange: ({
        500: "#bb6a0c", 600: "#a85f0b", 700: "#96550a"
    })
    readonly property var purple: ({
        50: "#EEEDFE", 100: "#CECBF6", 200: "#AFA9EC", 400: "#7F77DD", 600: "#534AB7"
    })
    readonly property color yellow600: "#ca8a04"   // Tailwind's (not overridden)

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
    readonly property color contentMuted: dark ? "#A0AABA" : gray[500]
    readonly property color contentDisabled: dark ? "#778291" : gray[400]
    readonly property color contentInverse: dark ? "#151B23" : "#ffffff"
    // Outlines.
    readonly property color outlineSubtle: dark ? "#3F4B59" : gray[200]
    readonly property color outline: dark ? "#59687B" : gray[300]
    readonly property color outlineStrong: dark ? "#72849D" : gray[400]
    readonly property color outlineDisabled: dark ? "#3A444F" : gray[200]
    // shadcn primitives.
    readonly property color background: dark ? surfaceBase : "#ffffff"
    readonly property color card: dark ? surfaceRaised : "#ffffff"
    readonly property color primary: blue[500]
    readonly property color primaryForeground: "#ffffff"
    readonly property color destructive: red[500]
    readonly property color ring: blue[500]

    // The top bar (bg-gray-50 / surface-base) and the widget cards
    // (Widget.Content: bg-gray-100 border-gray-300 / surface-raised outline).
    readonly property color topBar: dark ? surfaceBase : gray[50]
    readonly property color widget: dark ? surfaceRaised : gray[100]
    readonly property color widgetBorder: dark ? outline : gray[300]

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
        case "Home": return blue[500]
        case "Hold": case "Door": return yellow600
        case "Alarm": return red[500]
        case "Tool": return purple[600]
        default: return gray[800]   // disconnected
        }
    }

    // An icon of resources/icons in a colour.
    function icon(name, color) {
        return "image://icon/" + name + "/" + String(color).replace("#", "")
    }
}
