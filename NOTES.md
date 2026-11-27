# Phase 1 notes: what the original executables do

These are from disassembly of the unpatched executables:
- 2019: `Sega/Daytona/Daytona.exe`, SHA-256 `8ba4048f…79e`.
- 2017: `Daytona/Daytona.exe.original`, SHA-256 `f5d6fa89…eb`.

Addresses are virtual (image base 0x400000; `.text` file offset = VA −
0x400C00). Tools: `llvm-objdump -d --x86-asm-syntax=intel` with
`--start-address` / `--stop-address`.

WS2_32 import slots used below (2019 IAT; the import order is the same in
2017, at different slots):

| Slot | Function | Slot | Function |
|---|---|---|---|
| 0x116C554 | htons | 0x116C578 | WSAGetLastError |
| 0x116C558 | listen | 0x116C57C | send |
| 0x116C55C | setsockopt | 0x116C5A0 | socket |
| 0x116C560 | recv | 0x116C5A4 | WSAStartup |
| 0x116C564 | connect | 0x116C5AC | ioctlsocket |
| 0x116C56C | closesocket | 0x116C5B0 | inet_addr |
| 0x116C570 | bind | 0x116C5B4 | sendto |
| 0x116C574 | accept | 0x116C5C4 | select |
| 0x116C5D0 | gethostname | 0x116C5D4 | gethostbyname |
| 0x116C5D8 | inet_ntoa | | |

## 1. The shell link: TCP, the game connects to 127.0.0.1:18888

2019 `0x5847A0` (2017: `push 0x49C8` at `0x5826CC`):
- The game connects to `inet_addr("127.0.0.1")` (string at `0x1415760`)
  on port `0x49C8` (18888).
- On success it sets the socket non-blocking (`ioctlsocket FIONBIO 1`).
- It sets `SO_RCVTIMEO` (0x1006) and `SO_SNDTIMEO` (0x1005) to 500 ms.
- On failure it clears `+0xC` and tries again later.

**The game is the client, so the shim listens on 127.0.0.1:18888** and
speaks DYT, as the recompilation's `ws_send` / `shell_inbox` do. The
challenge send and the receive loop are around `0x584B20` – `0x584E73`:
`select` at `0x584B85`, `recv` from `0x584BC9`, and `send` at `0x584E73`.

Risk: anything else on port 18888, such as a running `Shell.exe` or
TeknoParrot's shell emulation, takes the port first. The shim should log
it and fall back to hooking `connect` / `send` / `recv` / `select` on that
one socket.

### The shell window (not needed)

The game also sends `WM_COPYDATA` (0x4A) to the shell's window through
`SendMessageTimeout` with a 10 s timeout:
- 2017: `0x529570`, called by `0x529620`.
- 2019: near `0x528E48`.

The payload is 0x40 bytes, and its first byte is the argument. The game
sends it only when it holds a shell window handle (`[obj+4] != 0`). On a PC
there is no such window, so it is skipped. TeknoParrot's edit at 2017
`0x52958F` replaces this with stores to `0x15AFC44` / `0x15AFC46` for its
own outputs (lamps). The shim needs nothing here; a later version can
hook it to read lamp states.

## 2. TeknoParrot's 2017 patch: none of it is needed

The 2017 patch's code edits, at file offsets 0x7C487, 0x7C4E8, 0xDCA97 and
0x12898F:

| VA | Original | TeknoParrot | What it is |
|---|---|---|---|
| `0x47D087`, `0x47D0E8` | the IO board poll | jumps into code caves | its own input path; the shim gives the game a real board instead |
| `0x4DD697` | `jne 0x4DD759` after `0x5B8E50()` (returns byte `0x1721248`), then `cmp [0x15AF9C8], al; jne` | both NOPed | the frame update's gate: while either flag is set, the frame takes the other path |
| `0x52958F` | `memset` of the `WM_COPYDATA` payload | stores the argument for its outputs | see the shell window above |

The recompilation ran the unpatched 2017 build to attract and full races
with no shell window and only the DYT link, so both gate flags stay clear
without the patch.

To check in phase 2: if a frame ever stalls, watch `0x1721248` and
`0x15AF9C8` (2017) and find their writers. The 2019 equivalent of
`0x4DD690` is a different function, so locate 2019's gate by the same call
sequence before relying on it.

## 3. The cabinet network

### Address check, 2019 `0x5775A0`

1. `gethostname`, `gethostbyname`, `inet_ntoa`, then `inet_addr` of the
   result.
2. If it is **127.0.0.1**, there is no cabinet network. With `[obj+0x3C] == 1`
   it gives up at once (`+0x24 = 1`); otherwise it waits on a timer, then
   gives up.
3. Otherwise the last octet is the cabinet's number, and it reads
   `[Network] CabinetID` from `../ShellData/ShellData.ini` (default 1).
   ID 1 makes a master (`0x576210`); any other ID makes a slave
   (`0x576470`). Discovery is port 26000 (`push 0x6590` at 2019 `0x577495`
   and `0x578849`, 2017 `0x5757B5` and `0x576B19`).

**The shim's `gethostbyname` returns 127.0.0.1 for the machine's own
name**, so a PC on a LAN does not start a master that probes
192.168.1.2 .. .8.

### 2017 discovery

The recompilation's notes say 2017's master still runs (threads at
`0x576AD0`, connecting to 192.168.1.2 .. .8:26000). **The `connect` hook
must refuse every non-loopback address at once** (`WSAECONNREFUSED`).
Otherwise each attempt waits for a TCP timeout on a real network.

### Link-play listener, 2019 `0x529E80`

- It binds a TCP socket to **192.168.1.<own last octet>:27000** (`0x6978`)
  and listens.
- On a PC without that address, `bind` fails and the object's error byte
  `+5` is set. The recompilation always reported success.
- **The shim's `bind` maps any 192.168.x.x address to 127.0.0.1, same
  port**, so the listener comes up as on a lone cabinet.

### Link-play connect, 2019 `0x52A590`

- It connects to 192.168.1.<n>:27000 and **retries every 20 ms
  (`Sleep(0x14)`) until it connects**.
- It is not reached on a lone cabinet. The connect hook's refusal keeps it
  that way, but if it is ever reached it will spin. Log it.

### Telemetry, 2019 `0x540540` / `0x541225`

- A UDP socket (`SOCK_DGRAM`, `IPPROTO_UDP`, `SO_BROADCAST`) sends
  0x78-byte packets of floats to **127.0.0.1:20777** (`0x5129`).
- This is the Codemasters-style motion telemetry that the shell lineage
  ("Dirt Showdown Shell") used.
- Harmless with nothing listening. It is also a free feature: SimHub and
  motion rigs can read it. Leave it alone.

## 4. Input

### DirectInput

- `0x5DEBA0` keeps a device only if its instance name is
  `SEGA IO Controller`. The profiles at `0x116D490` are chosen by device
  name.
- On a PC the real controllers also enumerate, under their own names. A
  `dinput8.dll` proxy that owns `DirectInput8Create` and enumerates only
  the board avoids mixed input.
- The recompilation's board (`src/recomp/d3d11.cpp`, "DirectInput: the
  cabinet's SEGA IO board") is a complete reference to port as a real COM
  object:
  - `EnumDevices`: one `DIDEVICEINSTANCEA`, type `0x116` (driving, first
    person).
  - `EnumObjects`: four absolute axes (X, Y, Z, X rotation at offsets 0, 4,
    8, 12).
  - `GetCapabilities`: 4 axes, 12 buttons.
  - `Get/SetProperty(DIPROP_RANGE)`: the game sets -1000..1000.
  - `GetDeviceState(c_dfDIJoystick)`:
    - axes: lX steering (centred), lZ brake, lRx gas;
    - POVs: centred (`0xFFFFFFFF`);
    - buttons at `+48`: 0 start, 3 / 4 shift up / down, 8..11 views, 1 / 2 /
      6 / 7 gears.
  - `Acquire`, `SetDataFormat`, `SetCooperativeLevel`: succeed.
- **Pedals:** the game maps the axes to 0..255 and then applies
  `ShellData.ini [CalibrationData]`. The installed values are
  `AccelUp=30 AccelDown=200 BrakeUp=30 BrakeDown=200 SteeringLeft=15
  SteeringRight=247`. The recompilation reached full throttle and brake
  (`0x10000`) and full lock with these, using the full axis range.

### XInput (2019 `0x6A0268`)

- The game calls `XInputGetState` for users 0..3 and counts the connected
  pads (`[obj+8]`). For each one it makes a 0x44-byte object (`0x6A02A0`
  onward).
- So **the engine does read XInput pads**: on Windows an Xbox pad would
  reach it as well as the board.
- The shim patches `XInputGetState` to return `ERROR_DEVICE_NOT_CONNECTED`.
- To try in phase 3, with the patch off: what a real XInput pad does in
  menus and races. If the engine's own pad layer drives the whole game, it
  could replace the board.

## 5. Settings in the install (2019 `Sega/ShellData/ShellData.ini`)

```
[System] Version=2
[Credit] Freeplay=1, EntryType=0
[Audio] MusicVolume=80, SFXVolume=100, VoiceVolume=90, AttractMusic=3
[CalibrationData] AccelUp=30, AccelDown=200, BrakeUp=30, BrakeDown=200,
                  SteeringLeft=15, SteeringRight=247, FeedbackStrength=2
[Network] Enabled=0, CabinetID=1, NumCabinets=1
```

The game reads it as `../ShellData/ShellData.ini`, relative to its working
directory, so the launcher must start `Daytona.exe` in its own folder.

## 6. Force feedback (2019; 2017 the same protocol)

- The game computes it (`soc::ForceFeedbackImpl`, `src\soc\forcefeedback.cpp`,
  `config.ini [Wheel]`). The object is created at `0x528350` (pointer
  `[0x15FF1E8]`) and its per-frame update is `0x5284B0`, which turns the
  Daytona ROM's drive-board command (`+0xB`, set by `0x529060` from the
  Model 2 emulation at `0x5E3262`) into this PC motor's commands.
- `0x5283E0` starts `c:\sega\shell\InpWrapper.exe -h=%08x -p=%08x`: `-h` is
  the game's window, `-p` the `ParallelPort` from `../shell/Game.ini`
  `[Configuration]` (default 0x378). A failed start is ignored.
- The helper registers by sending the game's window `WM_COPYDATA` with
  `dwData` 3 and its own window handle as a DWORD. The handler (`0x4DC9D0`,
  a virtual method reached through the window's original procedure; 2017
  `0x4DCD98`) stores it at `+4` (`0x529090`), sends a 0 and calls
  `ReplyMessage(1)`.
- Commands: `0x528DE0` (2017 `0x529570`) sends `WM_COPYDATA` to `+4` with
  `dwData` 0x20 and one byte, through `SendMessageTimeout` (10 s), and
  repeats while the reply is 2. Bytes: 0 off, 0x01-0x0F a force of 1-15
  one way, 0x11-0x1F the other way (`0x528EF0`). Grass, kerbs and
  scraping are fast alternations (`0x5282C0`, `PERIOD*`, `GRASS_*`,
  `GARIGARI_*`). Which sign is left is not yet checked on a wheel.
- `+8` enables it (`0x528E90`: 1 in a race at `0x4E59F3`, 0 at `0x4E4E44`).
- TeknoParrot's 2017 edit at `0x52958F` is the start of this send function.

## Still open (phase 2, on the Deck or a Windows PC)

- **The window:** the game's `config.ini` has no resolution keys. Check
  what size and mode it opens with, and whether DXVK or the Deck needs a
  forced size.
- **Paths:** the recompilation maps guest `D:` paths. Check whether the game
  opens anything by absolute path on a PC, and with which drive.
- **`CreateProcessA` of `c:\sega\shell\InpWrapper.exe`:** it fails cleanly
  on a PC; confirm.
- **2019's equivalent of the 2017 frame gate:** only relevant if a stall
  shows.
