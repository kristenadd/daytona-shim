# daytona-shim

Runs the original 32-bit `Daytona.exe` of Daytona Championship USA (2017 and
2019 PC editions) on Windows, and on the Steam Deck through Proton, without
TeknoParrot or the cabinet's `Shell.exe`. A `dinput8.dll` placed beside the
game stands in for the IO board, the shell and the cabinet network, and
reads modern controllers through SDL3.

Status: 2019 runs on the Steam Deck; so does 2017 (the patched `Daytona.exe` restored in memory from `Daytona.exe.original`). Esc or Start + Back (held a second) quits. New, not yet run: SDL3 pads (PlayStation, Switch), wheels and force feedback. `DaytonaLauncher.exe` is full screen and controller-driven on Windows and the Deck (`--desktop` for a window with tabs). See [PLAN.md](PLAN.md) for the plan and
[NOTES.md](NOTES.md) for what the disassembly shows.

Nothing from the game is in this repository; you need your own install.

## Building (macOS or Linux)

```
tools/package.sh
```

This fetches llvm-mingw into `build/toolchains`, and SDL3's official MinGW
release, stb_truetype / stb_image and the launcher's two OFL fonts (Russo
One, Titillium Web) into `build/third_party`, the first time; cross-builds the 32-bit
`dinput8.dll` and makes `build/dist/daytona-shim.zip`: the DLL, SDL3.dll and
its licence, [dist/shim.ini](dist/shim.ini) (settings, all optional) and
[dist/INSTALL.txt](dist/INSTALL.txt), which says how to install and run it on
Windows and the Steam Deck.

The launcher's logo and icon are generic (a chequered flag). To build with
your own, make them with `tools/branding/make_branding.py` and copy
[branding.example.ini](branding.example.ini) to `branding.ini` (not
committed) pointing at them: [tools/branding/README.md](tools/branding/README.md).

Host-side tests (the challenge answer, DYT framing; the launcher's sound
bank reader when `DAYTONA_SE_CSB` names a game's `rom/sound/csb/SE.csb`):

```
cmake -S . -B build/host -G Ninja && cmake --build build/host && ctest --test-dir build/host
```

## Layout

| File | What it does |
|---|---|
| `shim/main.cpp` | `DllMain`: edition check, hooks, shell thread; the `DirectInput8Create` export |
| `shim/dinput.cpp` | `IDirectInput8A` with one device, the SEGA IO board |
| `shim/shell.cpp` | the shell: TCP server on 127.0.0.1:18888 |
| `shim/dyt.cpp`, `shell_answer.cpp` | DYT packets and the security answer (also built for the tests) |
| `shim/net.cpp` | `connect`, `bind`, `gethostbyname`, `XInputGetState`, `CreateProcessA` hooks |
| `shim/iat.cpp` | import table patches in `Daytona.exe` |
| `shim/edition.cpp` | 2017 / 2019 by PE timestamp, per-edition addresses |
| `shim/pad.cpp` | input thread: SDL3 pads and wheels (or XInput), rumble and wheel force, quit |
| `shim/ffb.cpp` | the force-feedback helper's part: the game's commands by `WM_COPYDATA` |
| `launcher/main.cpp` | `DaytonaLauncher.exe`: the full-screen launcher, or the window with `--desktop` |
| `launcher/big.cpp` | full-screen, controller-driven launcher (SDL3 renderer, stb_truetype, the logo from `branding.ini` or `launcher/logo.png`) |
| `launcher/desktop.cpp` | the window (`--desktop`): tabs for settings, controls, wheel, and the edition |
| `launcher/sound_bank.cpp`, `launcher/sounds.cpp` | the game's SE.csb (CRI @UTF tables, ADX) decoded, played through SDL3 |
| `launcher/settings.cpp` | the install, ShellData.ini / GameSettings.ini, starting the game |
| `shim/bindings.hpp` | pad buttons and axes by name (shim.ini `[Pad]`) |
| `shim/config.cpp` | `shim.ini` (sample in `dist/shim.ini`) |
| `shim/restore.cpp`, `shim/diff_runs.cpp` | 2017: TeknoParrot's edits undone in memory from `Daytona.exe.original` |
| `shim.log` | written beside `Daytona.exe` at run time |

## License

The code in this repository is under the [MIT License](LICENSE). What the build
fetches keeps its own licence: SDL3 (zlib), stb (MIT or public domain) and the
launcher's fonts, Russo One and Titillium Web (SIL Open Font License). The
licence does not cover Daytona Championship USA itself: the game, its code,
art, sound and names belong to Sega, and you need your own copy to run it.
