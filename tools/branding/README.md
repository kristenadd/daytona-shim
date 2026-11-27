# Logo and icon

The repository ships a generic launcher logo and icon (a chequered flag, plain
lettering). The game's own logo is Sega's and is not committed. To build with
your own art, keep it outside the repository and point a `branding.ini` at it.
That file is ignored by git, so your pictures stay on your machine.

## What uses which picture

| Key in `branding.ini` | Picture | Used by |
|---|---|---|
| `logo` | the title logo, transparent background, drawn on a white band | the launcher's title and header (built into `DaytonaLauncher.exe`) |
| `ico` | a Windows `.ico` (16 to 256 px) | `DaytonaLauncher.exe`'s icon |

The generic pictures are `launcher/logo.png` and `launcher/icon.ico`.

## 1. Make the pictures

`make_branding.py` turns your art into the whole set. It needs Pillow
(`python3 -m pip install pillow`).

```bash
python3 tools/branding/make_branding.py \
    --logo my-logo.png \
    --ico-source my-badge.png \
    --out ../daytona-branding
```

- `--logo` (required): the logo with a transparent background. It is trimmed
  and scaled to fit 980 x 440.
- `--ico-source`: a picture for the `.ico`, transparency kept; it need not be
  square. Without it, the app icon (below) is used.
- `--icon`, `--icon-back`: the square app icon and its background, for the
  recompilation's iPhone, Mac and Vision Pro apps (daytona-3-nse-decomp, the
  same tool). Here they only matter as the `.ico`'s source when there is no
  `--ico-source`.

It writes `logo.png`, `icon.ico` and the other platforms' icons into `--out`.
Keep that folder outside the repository; beside it (`../daytona-branding`) lets the
recompilation use the same set.

`--generic --font RussoOne-Regular.ttf` draws the generic set that is
committed (the font comes from `tools/fetch_assets.sh`, in
`build/third_party/assets`).

## 2. Point the build at them

```bash
cp branding.example.ini branding.ini
```

Edit the paths if your folder is elsewhere. Paths are absolute or relative to
`branding.ini`; a key you leave out keeps the generic picture, and a path that
does not exist gives a CMake warning and the generic picture.

```ini
logo = ../daytona-branding/logo.png
ico  = ../daytona-branding/icon.ico
```

## 3. Build

Build as usual. CMake rereads `branding.ini` and the pictures it names whenever
they change; the configure log shows `Branding: <key> from <path>` for each one
in use. `-DDAYTONA_BRANDING_FILE=<path>` uses a remap file somewhere else.
