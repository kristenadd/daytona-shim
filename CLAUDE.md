# Working on this repository

Read [PLAN.md](PLAN.md) and [NOTES.md](NOTES.md) first. The companion
static recompilation is `../daytona-3-nse-decomp` (its PHASE0 / PHASE4 and
`src/recomp/win32.cpp`, `shell.cpp`, `game_addresses.hpp` describe the same
cabinet).

- Nothing from the game folder is committed (executables, ini files, data).
- Never modify the player's install; the 2017 build uses the pristine
  `Daytona.exe.original`.
- Addresses in notes are virtual addresses (image base 0x400000), 2019
  unless marked 2017.
