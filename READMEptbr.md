# Friday Night Funkin' — C Engine

engine para Friday Night Funkin' com foco em desempenho, em puro (C23) utilizando raylib de biblioteca, Conta com charts e cenários baseados em dados, suporte a mods via arquivos e um editor de charts nativo integrado.

> Não é um produto comercial. Todos os recursos (assets) de Friday Night Funkin' pertencem aos seus respectivos proprietários. 
**Status:** `v0.3 / v1.0 (30%)`
**Progresso:** `██████░░░░░░░░░░░░░░ 30%`

```text
assets/mods/<mod>/songs/<song>/data.json
assets/mods/<mod>/songs/<song>/Inst.ogg
assets/mods/<mod>/songs/<song>/Voices.ogg
assets/mods/<mod>/stages/<stage>.json|.lua
assets/mods/<mod>/images/icons/<char>.png
```

```text
main.c
headers/        # arquivos Header públicos (Game, Scene, Render, scenes/*)
src/            # Game.c, Scene.c, Render.c, Log.c, GameData.c
src/scenes/     # Titúlo, Menu, Historia, Aba de Freeplay, PlayState (Estado de Jogo), Opções,
# Loading, Modding, ChartEditor, AllScenes (Todas as Cenas)
src/scenes/other/ # Song (Música), SongList (Lista de Músicas), Week (Suporte a Aba de Week Do Friday Night Funkin'), Stage (Background), Note (Notas), Character (Personagens),
# BeatManager (Part e Coisas a ver com a música), AnimationSets (Sets de Animação), ModImport (Tentando fazer um importador de Mods)
lib/            # cJSON e bibliotecas externas incluídas
assets/         # músicas, semanas, cenários, personagens, sons, fontes, save.data
assets/mods/    # mods (músicas, cenários, imagens/ícones)
```

## Roteiro

**Status:** `v0.3 / v1.0 (30%)`
**Progresso:** `██████░░░░░░░░░░░░░░ 30%`

- [x] v0.1 — Base
- [x] v0.2 — Funções básicas
- [x] v0.3 — Editor de Charts (Fase de Desenvolvimento Está Aqui)
- [ ] v0.4 — Recursos adicionais (Week Editor (criador pra aba de semanas), coisas adicionais como um gráfico melhor)
- [ ] v0.5 — Testes e ajustes
- [ ] v0.9 — Polimento e Testes de Bugs
- [ ] v1.0 — Lançamento