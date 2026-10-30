#ifndef MODIMPORT_H
#define MODIMPORT_H

#include <stddef.h>

typedef struct ModImportStats {
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

typedef struct ModImportUsage {
    int mods;
    int songs;
    long bytes;
} ModImportUsage;

/* srcMod e uma pasta de mod-fonte; dstRoot deve ser exatamente assets/mods.
 * Se srcMod/modName for "mods", importa o conteudo diretamente em dstRoot.
 * Se essa pasta for um container, importa cada mod filho em dstRoot/<modName>.
 */
char ModImport_Run(const char *srcMod, const char *dstRoot,
                   const char *modName, char *msg, size_t msgSize,
                   ModImportStats *stats);

/* Lista o conteudo de assets/mods sem incluir a propria pasta raiz. */
char ModImport_ScanMods(const char *dstRoot, ModImportUsage *out);

/* Remove o conteudo de assets/mods, preservando a pasta raiz. */
char ModImport_DeleteAll(const char *dstRoot, char *msg, size_t msgSize,
                         int *removedMods, long *freedBytes);

#endif /* MODIMPORT_H */
