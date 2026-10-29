> Performance

# 1 Dev

```text
assets/
mods/
<mod>
songs/ <song> /data.json (notes chart/generator file)
songs/ <song> /Inst.ogg (instrumental)
songs/ <song> /Voices.ogg (voices)
stages/ <stage>.json|.lua (stage/background)
images/ icons /<icon>.png (icon)
```

```text
main.c
headers/        # public headers (Game, Scene, Render, scenes/*)
src/            # Game.c, Scene.c, Render.c, Log.c, GameData.c
src/scenes/     # Title, Menu, Story, Freeplay, PlayState, Options,
                # Loading, Modding, ChartEditor, AllScenes
src/scenes/other/ # Song, SongList, Week, Stage, Note, Character,
                # BeatManager, AnimationSets, ModImport
lib/            # libs in use
assets/         # songs, weeks, stages, characters, sounds, fonts, save.data
assets/mods/    # mods (songs, stages, images/icons)
```

## 1.1 Development Roadmap

**Status:** `v0.3 / v1.0 (30%)`
**Progress:** `██████░░░░░░░░░░░░░░ 30%`

- [x] v0.1 — Basis
- [x] v0.2 — Basis functions
- [x] v0.3 — Music Editor and Additional Things its Basic (current)
- [ ] v0.4 — Additional features (WeekEditor, better grafics, pending)
- [ ] v0.5 — Tests and tuning
- [ ] v0.9 — Game Polish and Bug Testing
- [ ] v1.0 — Release
