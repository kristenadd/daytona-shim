# Daytona Championship USA on PC without TeknoParrot: a shim

A plan for a new repository. It runs the original 32-bit `Daytona.exe` (2017
and 2019 editions) on Windows, and on the Steam Deck through Proton. A small
DLL replaces the cabinet around the game: the shell, the IO board, the
network and the dongle check. Modern controllers come through SDL3.

The game's own code and DXVK do the work, so the frame rate should match
TeknoParrot's. Everything below was learned in the static recompilation
(`daytona-3-nse-decomp`), whose host code emulates the same cabinet. The
addresses are 2019's unless marked otherwise.

## Goals

- Start the unpatched `Daytona.exe` of either edition from a launcher, with
  no TeknoParrot and no `Shell.exe`.
- Controllers: Xbox, PlayStation, Switch, the Deck's own controls and Steam
  Input, through SDL3, with the same default layout as the recompilation.
- Free play or credits, and the operator settings, through `ShellData.ini`.
- Nothing from the game folder in the repository. The player points the
  launcher at their own install.

Not in the first version: force feedback (the game sends it to a
parallel-port motor through the shell; SDL rumble later), the Test / Service
menu (it belongs to `Shell.exe`), and network play.

## What the cabinet gives the game, and what replaces it

| On the cabinet | What the game does | The shim |
|---|---|---|
| USB IO board | DirectInput 8: `0x5DEBA0` keeps a device only if its instance name is `SEGA IO Controller`; axes -1000..1000; `lX` steering, `lRx` gas, `lZ` brake; buttons through the profile at `0x116D490` | `dinput8.dll` proxy that reports one `SEGA IO Controller`, fed from SDL3 |
| `Shell.exe` link | TCP client to 127.0.0.1:18888 (`0x5847A0`), non-blocking, 500 ms timeouts; DYT packets: `'D' 'Y' 'T'`, id (2 bytes, big-endian), length (2), four zero bytes, payload | A thread in the DLL that listens on 127.0.0.1:18888 and answers as the shell (the recompilation's `ws_send` / `shell_inbox` in `src/recomp/win32.cpp`) |
| Dongle and board unlock | Message `0x2A` every 10 s with a 4-byte challenge; after five unanswered it asks for a reboot and exits | Answer with `0x2E`, computed by `shell_answer` (`src/recomp/shell.cpp`, the exact inverse of the game's check at `0x584600`) |
| Shell greeting | Waits for `0x2C` (`SHELL_MSG_SET_SKU`), which sets the ready byte at `+0x38` | Send `0x2C` once after the game's first packet |
| Coin mech | `0x2D` `SHELL_MSG_SETCREDIT` carries the credit count (one byte) | Coin button: the count the game holds now plus one (2019 `0x15278F8 + 7`, `0xFF` before the first) |
| Operator menu | Reads `ShellData/ShellData.ini` (`[Credit] Freeplay`, `[Audio]`, `[CalibrationData]`) and `GameSettings.ini` (2019) | The launcher writes them (an equivalent of `--shell-setting Section.Key=Value`) |
| Cabinet network | 2019: `[Network] Enabled=0` skips it. 2017, as CabinetID 1, is the master: threads at `0x576AD0` connect to 192.168.1.2 .. .8:26000 | Hook `connect`: anything but loopback fails at once with `WSAECONNREFUSED` (on a PC these would otherwise wait for timeouts) |
| Cabinet address check | `0x5775A0`: its own address; 127.0.0.1 means no cabinet network, anything else starts a master or slave by `[Network] CabinetID` | Hook `gethostbyname` so the game's own name resolves to 127.0.0.1 |
| Link-play listener | `0x529E80` binds 192.168.1.<own octet>:27000; on a PC the bind fails and sets an error byte | Hook `bind`: 192.168.x.x becomes 127.0.0.1, same port |
| Motion telemetry | UDP to 127.0.0.1:20777 (`0x541225`) | Nothing; SimHub and motion rigs can read it |
| Force-feedback helper | `CreateProcessA` of `c:\sega\shell\InpWrapper.exe`; failure is handled | Nothing (let it fail), or hook it to fail cleanly |
| TeknoParrot's 2017 edits | `0x47D087` / `0x47D0E8` input, `0x4DD697` frame-update gate, `0x52958F` shell window message | None needed (NOTES.md section 2) |

The DYT protocol, the challenge answer and the credit rules are all in
`src/recomp/win32.cpp` and `src/recomp/shell.cpp`. They can be copied over,
since it is the same author's code. The ports and addresses above were
checked in the disassembly in phase 1; [NOTES.md](NOTES.md) has the details.

### The controller layout (from `src/runtime/pad_mapping.cpp`)

| Game action | Controller | IO board |
|---|---|---|
| Steering | Left stick X, 10% dead zone, rescaled to full lock | `lX` |
| Gas / brake | Right / left trigger, analog | `lRx` / `lZ` (-1000 released, +1000 full) |
| Start | Start / Options | button 0 |
| Shift up / down | Right / left shoulder | buttons 3 / 4 (edge-triggered by the game) |
| View 1..4 | Y / X / B / A | buttons 8..11 |
| Gear 1..4 (H-shifter) | Unassigned | buttons 1 / 2 / 6 / 7 |
| Coin | Back / Share | not on the board: `0x2D` on the shell link |

Steering goes through the test-menu calibration (`[CalibrationData]`,
defaults 0 / 255 / 127), so the shim should report the full -1000..1000
range and leave calibration at its defaults.

`XInputGetState` (`XINPUT9_1_0.dll`) is probed for users 0..3 at
`0x6A027A`. The recompilation reports no pad there: if the game also reads
XInput pads, a real Xbox pad would reach it twice. The shim should return
`ERROR_DEVICE_NOT_CONNECTED` from XInput, so that every controller arrives
once, through the board.

## Design

```
daytona-shim/
  launcher/      dcusa-launch.exe: finds the install, checks the edition,
                 writes settings, starts Daytona.exe in its folder
  shim/          dinput8.dll proxy, placed next to Daytona.exe
    dinput.cpp   IDirectInput8 / IDirectInputDevice8 with one SEGA IO Controller
    pad.cpp      SDL3 gamepad -> board state (SDL runs on its own thread)
    shell.cpp    DYT link: greeting, challenge answer, credits
    hooks.cpp    IAT patches in Daytona.exe: ws2_32, XInput, CreateProcessA
    edition.cpp  2017 / 2019 addresses (from game_addresses.hpp)
  third_party/   SDL3
  tests/         DYT framing and the challenge answer (host-side unit tests)
```

- **How it loads: a `dinput8.dll` proxy.** Windows loads a DLL from the
  game's own folder before the system's, so the game loads ours. Its
  `DirectInput8Create` is the board. Everything else is an import address
  table patch made in `DllMain`, which is simple because the executables have
  intact imports, a fixed base of 0x400000 and no relocations. No inline
  hooks, no code patches, no injector.
- **The shell link: a real loopback server.** A thread listens where the
  game connects and speaks DYT. Then the game's own socket code runs
  unchanged. The fallback is hooking `send` / `recv` / `select` as the
  recompilation does, if the port or the ordering is awkward.
- **The executable must be unpatched.** The 2017 folder ships a
  TeknoParrot-patched `Daytona.exe` with the pristine file as
  `Daytona.exe.original`. The launcher checks SHA-256 (the hashes are in
  `src/system/edition.cpp`) and uses the original. It never modifies the
  player's files; patched-file handling can be a copy in a writable folder.
- **Settings live outside the install** where possible. Otherwise the
  launcher backs up `ShellData.ini` before its first write.
- **Build from the Mac:** CMake with llvm-mingw (or clang-cl and lld-link),
  target `i686-w64-windows-gnu`. Both pieces are 32-bit, to match the game.
  SDL3 has an official 32-bit Windows release.
- **Logging:** a `shim.log` next to the launcher. It records every hooked
  call's first few uses and every DYT packet each way. This is the main
  debugging tool, since there is no debugger on the Deck.

## Phases

1. **Investigation.** **Done** ([NOTES.md](NOTES.md)); the window, paths
   and `InpWrapper.exe` are left for phase 2, on real Windows. No code yet:
   disassembly of the original `Daytona.exe`, both editions.
   - The shell link's exact address and port, which side listens, and the
     order of the first packets.
   - What TeknoParrot's `0x4DD697` gate and the `0x52958F` window message do,
     and whether the shell link opens the gate without a patch.
   - `0x577622`'s address check: which call supplies the address.
   - Whether the game reads XInput pads as controls (`0x6A0281` and after).
   - The window: fullscreen or windowed at 1920x1080, and whether it accepts
     other resolutions. The recompilation saw a fixed 1920x1080 client from
     `GetClientRect`.
   - The paths: the recompilation maps guest `D:` paths, so check what the
     game opens by absolute path on a real PC.
   - Done when: a short note for each, with addresses, in `NOTES.md`.
2. **Boot to attract** (2019): the launcher plus a shim with the shell link
   and the network hooks, keyboard only. **Built, not yet run.** The first
   build skips the launcher (the DLL goes beside `Daytona.exe`) and already
   has the IO board, fed by XInput and the keyboard, since the Deck has no
   keyboard at hand. Done when attract runs for 10
   minutes without the reboot request (that is, the challenge is answered).
3. **Controls:** the IO board from SDL3, then the coin and free play. Done
   when an Xbox pad and the Deck's controls play a race, and with
   `Freeplay=0` a coin gives `CREDIT(S) 1`.
4. **2017:** the edition's addresses, the original executable, and refused
   discovery connects. Done when attract and a race run, with no network
   error.
5. **The Steam Deck:** add the launcher as a non-Steam game with Proton
   (Experimental or 9+). Check that the controller reaches SDL inside Proton
   (Steam Input presents an XInput pad; SDL's own hidapi drivers may see the
   real device), the frame rate, and sound. Done when it holds a frame rate
   close to TeknoParrot's in a race.
6. **Polish:** a settings screen or `.ini` for the launcher (edition, free
   play, button layout), rumble from the game's force-feedback request, and
   a release zip.

## Risks

- **The frame gate** may need a code patch after all. If so, make it an
  in-memory patch made by the shim at load time, at an address checked
  against the hash, never a file edit.
- **Hidden checks:** the shell may send more than `0x2C` / `0x2D` / `0x2E` on
  a real cabinet (the name table at `0x1543830` lists the messages; ids are
  table index plus 20). The recompilation reached races with only these
  three, so this is unlikely.
- **Controllers under Proton:** SDL in Wine depends on Proton's HID and
  XInput support. If it is unreliable, fall back to the Windows XInput API
  inside the shim, which Steam Input always provides.
- **Anti-tamper:** none was found (PHASE0 "Packing, encryption, protection"):
  no packer, no TLS callbacks, no self-modifying code.

## Testing

- Unit tests on the Mac for the DYT framing and `shell_answer` (copy the
  recompilation's test vectors).
- A Windows PC if there is one, otherwise the Deck directly. Wine on the Mac
  (CrossOver or the Game Porting Toolkit) can work for boot checks, but its
  D3D11 path is not the Deck's.
- Reference behaviour: the recompilation's `phase5_controls` checks (coin,
  start, menus, every control in a race) are the checklist for phase 3.
