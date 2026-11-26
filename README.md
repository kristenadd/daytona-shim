# daytona-shim

Runs the original 32-bit `Daytona.exe` of Daytona Championship USA (2017 and
2019 PC editions) on Windows, and on the Steam Deck through Proton, without
TeknoParrot or the cabinet's `Shell.exe`. A `dinput8.dll` placed beside the
game stands in for the IO board, the shell and the cabinet network, and
reads modern controllers through SDL3.

Status: phase 1 (investigation). See [PLAN.md](PLAN.md) for the plan and
[NOTES.md](NOTES.md) for what the disassembly shows.

Nothing from the game is in this repository; you need your own install.
