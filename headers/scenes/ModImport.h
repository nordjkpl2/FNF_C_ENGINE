#ifndef MODIMPORT_H
#define MODIMPORT_H

#include <stddef.h>

// Importador de mod estilo Psych p/ o layout da engine.
// TUDO fica dentro de dstRoot/<mod>/ (apagar a pasta desfaz o import;
// nada vai p/ assets/ global).
// Le: songs/<musica>/{Inst.ogg,Voices*.ogg} + data/<musica>/*.json,
//     weeks/{main,extra,joke}/*.json, stages/*.{lua,json},
//     images/*.{png,PNG} + images/characters/*.{png,xml},
//     characters/*.json. Icones vao p/ <mod>/images/icons/<nome>.png
//     (o editor e o jogo procuram no mod antes do global).
// Converte Sparrow .xml + animations[] do character .json em .animset
// (o loader da engine nao muda; offsets vao do .json no load).
// Ignora: events.json, .lua em data/, scripts/shaders/videos/custom_*,
// music/sounds/fonts (sem runtime Lua/shader/video/menu-music na engine).
// Roda uma vez por acao do usuario (fora do hot path).
typedef struct {
    int songs;
    int charts;
    int weeks;
    int stages;
    int images;
    int characters;
    int animsets;
    int icons;
    long bytes;
    int skippedEvents;
    int skippedLua;
} ModImportStats;

// srcMod = pasta do mod (a que contem songs/, data/, weeks/...).
// dstRoot = raiz de mods (jogo: "assets/mods"; teste: TEMP).
// modName = nome da pasta de destino (saneado: so [a-zA-Z0-9_-]).
// msg recebe resumo legivel. Retorna 1 = ok (mesmo parcial), 0 = falha.
char ModImport_Run(const char* srcMod, const char* dstRoot,
    const char* modName, char* msg, size_t msgSize, ModImportStats* stats);

// Uso atual de dstRoot (nº de mods, musicas, bytes). Sempre retorna 1.
typedef struct {
    int mods;
    int songs;
    long bytes;
} ModImportUsage;
char ModImport_ScanMods(const char* dstRoot, ModImportUsage* out);

// Apaga TODOS os mods de dstRoot (arquivos + pastas). So aceita raiz
// terminada em "mods" (trava anti-uso-errado). msg resume.
// Retorna 1 = ok (mesmo com 0 mods), 0 = falha.
char ModImport_DeleteAll(const char* dstRoot, char* msg, size_t msgSize,
    int* removedMods, long* freedBytes);

#endif
