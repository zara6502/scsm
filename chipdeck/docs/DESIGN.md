# ChipDeck — player and plugin API design (draft v1)

[Русская версия](DESIGN.ru.md)

Design draft. The ABI definitions live in `include/chipdeck_api.h`.

## 1. Architecture

```
+--------------------------------------------------------------+
|                         ChipDeck host                        |
|  tray (click = play/pause)  player window (skin)  playlist   |
|  WASAPI output   file cache   settings dialog   cancel flag  |
+------------------+-------------------------+-----------------+
                   |                         |
        CHP_ENGINE DLL                      CHP_LIBRARY DLL
   (GME, libsidplayfp, ayfly,           (ModArchive API, BotB JSON,
    ASAP, stSound, VGMPlay...)           ModLand mirror, ...)
```

The host owns: audio output (one shared WASAPI instance), UI, playlist,
file cache, settings. Plugins are stateless with respect to the host: an
engine lives while its session handle is open, a library while connected.

The two plugin kinds are independent: a player with one engine and no
library plugins works as a local player (drag & drop).

## 2. Decoder engine: `ChipEngine`

Data model: file → session (`ChipHandle`) → subtunes (`GetTrackCount`) →
PCM stream via pull-style `RenderFloat/RenderInt16` calls.

Key decisions:

- **Pull-model rendering.** The host controls pacing and buffer sizes; a
  plugin never allocates PCM memory and never creates audio threads.
- **Position = a running frame counter.** `Select(track, rate)` resets
  state; `Seek` is optional (not every emulation re-seeks deterministically —
  hence the `CHP_ENGINE_SEEK` capability flag).
- **Duration may be unknown** (`flags & HAS_DURATION == 0`): AY/RMT music
  loops on interrupts and has no natural length. The host records the
  actual duration of the first playthrough in its database.
- **End of track** = `Render` returned fewer frames than requested (the
  engine pads the remainder with silence). Looping is the host's job.
- **Visualizations** are optional: `GetVoiceLevels` (per-voice levels 0..1:
  3 AY voices, 3 POKEY channels, SID voices) and `GetSpectrum` (bands 0..1),
  declared through capabilities. They feed skin widgets.

Threading: `Render/Seek/GetTrackInfo/Get*Vis` are called from the audio
output thread or the UI thread, one call at a time; `Open/Close/Select`
from any thread but never concurrently with Render.

## 3. Library provider: `ChipLibrary`

Model: `Connect(config) → Query(request, callback) → Fetch(entry) →
plugin-owned buffer → host copies → FreeBuffer`.

Key decisions:

- **Networking lives inside the plugin.** The plugin does its own HTTP
  (ModArchive needs an API key and XML, BotB uses JSON); the host knows
  nothing about the network. WinHTTP (system) is sufficient — no external
  dependencies.
- **Blocking `Query` with a result callback and a cancel flag.** Results
  arrive one at a time so the UI can fill incrementally; `cancel` is a
  pointer to the host's `volatile int`, polled by the plugin. No message
  queues or timers across the ABI.
- **Settings via a schema** (`ChipLibGetConfigSchema`): id, label,
  default, `secret` (masked like a password). The host stores the values
  and passes them to `Connect`. This mirrors the `EncodeOptions` pattern
  of the Ride Codec API.
- **`entry_id` is a stable string ID** within a library: the host caches
  downloads by it and can re-`Fetch` without a new `Query`.
- **Download returns a plugin-owned buffer** rather than a URL: the plugin
  can handle authentication, gzip, and XML→file conversion itself.
  Progress is a callback; cancellation uses the same flag.

Error codes: base codes 1–8 are defined in the header (`CHP_ERR_*`);
plugins may extend from 16 upward, documenting their own.

## 4. Skins — declarative packages

A skin = ZIP/folder: `skin.json` + PNGs. No executable code.

```json
{ "api": 1, "name": "Glass", "author": "...", "window": {"w": 320, "h": 200},
  "tray": { "idle": "tray_idle.png", "playing": "tray_play.png",
            "paused": "tray_pause.png", "error": "tray_error.png" },
  "elements": [
    { "type": "image",  "image": "bg.png", "rect": [0, 0, 320, 200] },
    { "type": "button", "action": "play_pause", "image": "btn.png",
      "frames": ["norm", "hover", "pressed", "active"], "x": 8, "y": 152 },
    { "type": "spectrum", "source": "engine", "bands": 32,
      "rect": [8, 8, 304, 96] },
    { "type": "voices", "source": "engine", "rect": [8, 110, 80, 16] },
    { "type": "text", "source": "title", "rect": [96, 110, 216, 18],
      "font": {"face": "Segoe UI", "size": 12, "color": "#FFFFFF"} },
    { "type": "progress", "rect": [8, 132, 304, 6] },
    { "type": "rating", "source": "rating", "rect": [8, 142, 100, 14] } ] }
```

- **Buttons**: the action set is fixed (`play_pause`, `stop`, `next`,
  `prev`, `open`, `mute`, `show_window`, `quit`) — a skin cannot run
  arbitrary commands.
- **Data sources** (`source`): `title`, `author`, `format`, `rating`,
  `position`, `duration`, `lib_name` are host channels; new channels do
  not break old skins.
- **Visual channels**: `spectrum`/`voices` come from engine capabilities;
  if the engine does not provide them, the widget renders empty (no error).
- **Window scaling**: v1 uses the skin's fixed size (DPI-aware); the host
  scale factor applies to the whole window. Anchored resizing is v2.
- **Tray**: four icon states from the skin set; a broken skin falls back
  to the built-in default and the player keeps running.

## 5. Audio pipeline

WASAPI shared mode, float32; the sample rate is agreed with the engine at
`Select` (engines may produce any rate; the host resamples or requests a
different rate — most emulators support any). Pause = the host stops
calling Render; engines are stateful, so pausing is free.

## 6. Decisions for v1 (approved)

1. **Playback**: download-fully-then-play only. Partial streaming is a
   v2 candidate; `CHP_ENTRY_STREAMABLE` stays a reserved flag.
2. **Local files**: drag & drop and open are host features. No
   "local folder" plugin.
3. **Engine output format**: the host prefers float32 when declared;
   int16 is the fallback.
4. **Playlist** is a separate host window (a plain Win32 list); the skin
   draws only the main player window and tray icons.
5. **Naming**: project `ChipDeck`, ABI prefix `CHP`.
