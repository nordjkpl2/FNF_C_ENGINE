# Friday Night Funkin' — C Engine

A performance-focused Friday Night Funkin' engine written in pure C (C23) on top of raylib. Data-driven charts and stages, file-based modding, and a built-in chart editor.

> Not a commercial product. All Friday Night Funkin' assets belong to their respective owners.

**Status:** `v0.3 / v1.0 (30%)`
**Progress:** `██████░░░░░░░░░░░░░░ 30%`

## Getting started (new here? start here)

### Requirements (for compiling the code, for playing use the distribuitons disponible)

- Windows + MinGW-w64 GCC with C23 support
- raylib DLL (`raylib.dll` next to the exe, `lib/` headers already vendored)
- support OpenGL 3.3 (`GLSL_VERSION 330`)

### Build

The exact build command lives in the `build` file and changes over time — always check it before compiling. Current reference:

```sh
cd "D:/dev/fnf c engine"
gcc main.c $(find src -name "*.c") $(find lib -name "*.c") -o FNF_C_ENGINE.exe -I./headers -I./src -I./lib -L./lib -lraylibdll -lopengl32 -lgdi32 -lwinmm -std=c23 -Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,-O1 -s -DNDEBUG -fomit-frame-pointer
```

Output: `FNF_C_ENGINE.exe` — double-click to play (Title → Menu → Story / Freeplay / Options / Modding).

### Modding

```text
assets/mods/<mod>/songs/<song>/data.json
assets/mods/<mod>/songs/<song>/Inst.ogg
assets/mods/<mod>/songs/<song>/Voices.ogg
assets/mods/<mod>/stages/<stage>.json|.lua
assets/mods/<mod>/images/icons/<char>.png
```

Use Modding > Import Mod and paste the path, `ENTER` to import. `assets/mods/spookeez` is test content.

### Project structure

```text
main.c
headers/        # public headers (Game, Scene, Render, scenes/*)
src/            # Game.c, Scene.c, Render.c, Log.c, GameData.c
src/scenes/     # Title, Menu, Story, Freeplay, PlayState, Options,
                # Loading, Modding, ChartEditor, AllScenes
src/scenes/other/ # Song, SongList, Week, Stage, Note, Character,
                # BeatManager, AnimationSets, ModImport
lib/            # vendored cJSON
assets/         # songs, weeks, stages, characters, sounds, fonts, save.data
assets/mods/    # mods (songs, stages, images/icons)
```

## Roadmap

**Status:** `v0.3 / v1.0 (30%)`
**Progress:** `██████░░░░░░░░░░░░░░ 30%`

- [x] v0.1 — Basis
- [x] v0.2 — Basis functions
- [x] v0.3 — Chart Editor (current)
- [ ] v0.4 — Additional features (WeekEditor, pending)
- [ ] v0.5 — Tests and tuning
- [ ] v1.0 — Release
