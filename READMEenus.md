# Friday Night Funkin' — C Engine

A performance-focused engine for Friday Night Funkin' written in pure (C23) using the raylib library. It features chart support, mod support via files, a built-in native chart editor, and (likely) support for all mods—though I'm not certain about that last part yet

> Not a commercial product. All Friday Night Funkin' assets belong to their respective owners.

**Status:** `v0.3 / v1.0 (30%)`
**Progress:** `██████░░░░░░░░░░░░░░ 30%`

```text
assets/mods/<mod>/songs/<song>/data.json
assets/mods/<mod>/songs/<song>/Inst.ogg
assets/mods/<mod>/songs/<song>/Voices.ogg
assets/mods/<mod>/stages/<stage>.json|.lua
assets/mods/<mod>/images/icons/<char>.png
```

```text
main.c
headers/        # public headers (Game, Scene, Render, scenes/*)
src/            # Game.c, Scene.c, Render.c, Log.c, GameData.c
src/scenes/     # Title, Menu, Story, Freeplay, PlayState, Options,
                # Loading, Modding, ChartEditor, AllScenes
src/scenes/other/ # Song, SongList, Week, Stage, Note, Character,
                # BeatManager, AnimationSets, ModImport
lib/            # vendored cJSON and libs used
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
