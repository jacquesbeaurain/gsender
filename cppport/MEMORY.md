# Gsender project
- Goal: finish porting the whole Electron gSender app to C++, working in the cppport folder.
- Repo: https://github.com/jacquesbeaurain/gsender
- Owner: Jacques (GitHub jacquesbeaurain)
- Port lives on branch devcpp-pfpo06 (not master). Read cppport/AGENTS.md and DEV_WALKTHROUGH.md first.
- Decision 2026-09-30: upstream JS plugins do NOT need to run on the port. Keep the port's own QML/Wasm plugin system; fix its Wasm engine and bridge gaps.
- Decision 2026-09-30: no plugin sandboxing for now; plugins get full capabilities. Risks are to be documented for later review, not fixed.
- Decision 2026-09-30: the port keeps its own config file, separate from Electron's ~/.sender_rc. A one-time import from .sender_rc is planned for later, not now.
- Gamepad support: use SDL3 (decided 2026-09-30).
- Git convention: Jacques wants linear history. Update PR branches by rebasing onto devcpp-pfpo06 and force-pushing (with lease), not merge commits; he rebase-merges PRs.
- Git convention: Jacques wants small, focused commits (one logical change each), not one big commit per PR (said 2026-09-30 on the cleanup PR).
- CI (added in cleanup PR #7, 2026-09-30): a cppport GitHub workflow builds and tests on Ubuntu only; QML warnings fail the UI tests. Windows/macOS code is not built by CI.
- Memory tracking (decided 2026-10-06): this file (cppport/MEMORY.md) mirrors the project memory. Every memory change gets its own small commit here, via a PR into devcpp-pfpo06.
- gh CLI in the local checkout defaults to upstream Sienci-Labs/gsender; always pass `-R jacquesbeaurain/gsender` for PR commands.

## Where work runs (decided 2026-10-06)
- ALL threads run locally on Jacques's machines via Remote Control, never in cloud threads.
- Every machine used for this project has its own local checkout of the repo at an OS-specific folder. Threads work in that checkout with the machine's own toolchain and build cache.
- Several threads can share one checkout; a thread that commits on its own branch should use a separate git worktree so it doesn't disturb another thread's working tree.
- Known machines/folders:
  - Windows: device jb-spro, folder D:\repos\gh\gsender.
  - Other OSes (Linux/macOS): folder not recorded yet. Jacques will add these from those machines once they are connected.
- Picking a machine: use the one Jacques names or the one the task's OS needs (e.g. Windows build work goes to jb-spro); otherwise the default above.
- Commands and paths must fit that machine's OS (PowerShell and backslash paths on Windows).
