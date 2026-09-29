# Example Hello Plugin

Demonstrates the minimal structure of a gSender plugin using WebAssembly for business logic and Qt QML for user interface integration.

## Structure
- `gsender-plugin.json`: Manifest declaring metadata, capabilities, and the `tools-page` contribution slot.
- `src/main.c`: C logic compiled to `bin/plugin.wasm`.
- `ui/Main.qml`: Declarative UI component mounted into gSender's Tools page.
