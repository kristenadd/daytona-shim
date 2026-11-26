# daytona-shim

Runs the original 32-bit `Daytona.exe` of Daytona Championship USA (2017 and
2019 PC editions) on Windows, and on the Steam Deck through Proton, without
TeknoParrot or the cabinet's `Shell.exe`. A `dinput8.dll` placed beside the
game stands in for the IO board, the shell and the cabinet network, and
reads modern controllers through SDL3.

Status: phase 2, a first build for 2019, not yet run. See [PLAN.md](PLAN.md) for the plan and
[NOTES.md](NOTES.md) for what the disassembly shows.

Nothing from the game is in this repository; you need your own install.

## Building (macOS or Linux)

```
tools/package.sh
```

This fetches llvm-mingw into `build/toolchains` the first time, cross-builds
the 32-bit `dinput8.dll` and makes `build/dist/daytona-shim.zip` (the DLL and
[dist/INSTALL.txt](dist/INSTALL.txt), which says how to install and run it on
Windows and the Steam Deck).

Host-side tests (the challenge answer, DYT framing):

```
cmake -S . -B build/host -G Ninja && cmake --build build/host && ctest --test-dir build/host
```

## Layout

| File | What it does |
|---|---|
| `shim/main.cpp` | `DllMain`: edition check, hooks, shell thread; the `DirectInput8Create` export |
| `shim/dinput.cpp` | `IDirectInput8A` with one device, the SEGA IO board |
| `shim/pad.cpp` | XInput pad and keyboard to the board's controls |
| `shim/shell.cpp` | the shell: TCP server on 127.0.0.1:18888 |
| `shim/dyt.cpp`, `shell_answer.cpp` | DYT packets and the security answer (also built for the tests) |
| `shim/net.cpp` | `connect`, `bind`, `gethostbyname`, `XInputGetState`, `CreateProcessA` hooks |
| `shim/iat.cpp` | import table patches in `Daytona.exe` |
| `shim/edition.cpp` | 2017 / 2019 by PE timestamp, per-edition addresses |
| `shim.log` | written beside `Daytona.exe` at run time |
