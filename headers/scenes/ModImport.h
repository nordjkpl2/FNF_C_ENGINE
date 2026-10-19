/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl - Oct 19 2026 */
#pragma once
#ifndef MODIMPORT_H
#define MODIMPORT_H
#include <stddef.h>
typedef struct { int songs; int charts; int weeks; int stages; int images; int characters; int animsets; int icons; long bytes; int skippedEvents; int skippedLua; } ModImportStats;
typedef struct { int mods; int songs; long bytes; } ModImportUsage;
char ModImport_Run(const char* srcMod, const char* dstRoot, const char* modName, char* msg, size_t msgSize, ModImportStats* stats);
char ModImport_ScanMods(const char* dstRoot, ModImportUsage* out);
char ModImport_DeleteAll(const char* dstRoot, char* msg, size_t msgSize, int* removedMods, long* freedBytes);
#endif