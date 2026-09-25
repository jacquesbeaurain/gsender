#!/bin/bash
# Cloud sessions: install the C++ port's Linux build dependencies (Qt, Boost,
# GoogleTest from conda-forge into /opt/gs-deps). Idempotent: an existing
# prefix is kept. See cppport/AGENTS.md, "Linux (cloud sessions)".
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

"$CLAUDE_PROJECT_DIR/cppport/tools/setup_linux.sh"
