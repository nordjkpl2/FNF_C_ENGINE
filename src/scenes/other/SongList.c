#include"std.h"
#include"scenes/Song.h"
#include"scenes/SongList.h"

static SongEntry* entries = NULL;
static int entryCount = 0;
static int entryCap = 0;

static int Entry_Compare(const void* a, const void* b) {
    return strcmp(((const SongEntry*)a)->name, ((const SongEntry*)b)->name);
}

static const char* Dir_BaseName(const char* path) {
    const char* s1 = strrchr(path, '/');
    const char* s2 = strrchr(path, '\\');
    const char* s = (s1 > s2) ? s1 : s2;
    return (s != NULL) ? s + 1 : path;
}

static void Scan_Add(const char* dirPath, const char* displayName) {
    char instPath[300];
    snprintf(instPath, sizeof(instPath), "%s/Inst.ogg", dirPath);

    FILE* f = fopen(instPath, "rb");
    if(f == NULL)
        return;
    fclose(f);

    if(!Song_HasChart(dirPath))
        return;

    // dedup by dir
    for(int i = 0; i < entryCount; i++) {
        if(strcmp(entries[i].dir, dirPath) == 0)
            return;
    }

    if(entryCount >= entryCap) {
        int newCap = (entryCap == 0) ? 16 : entryCap * 2;
        SongEntry* grown = realloc(entries, sizeof(SongEntry) * (size_t)newCap);
        if(grown == NULL)
            return;
        entries = grown;
        entryCap = newCap;
    }

    SongEntry* e = entries + entryCount;
    strncpy(e->name, (displayName != NULL) ? displayName : Dir_BaseName(dirPath), sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = 0;
    strncpy(e->dir, dirPath, sizeof(e->dir) - 1);
    e->dir[sizeof(e->dir) - 1] = 0;
    entryCount++;
}

// Lists immediate subdirectories of base using raylib (portable).
// When isMods is set, also descends one level into <mod>/songs/* (mod pack layout).
static void Scan_Subdirs(const char* base, char isMods) {
    if(!DirectoryExists(base))
        return;

    FilePathList list = LoadDirectoryFilesEx(base, "DIR", false);
    for(unsigned int i = 0; i < list.count; i++) {
        const char* sub = list.paths[i];
        if(!isMods) {
            Scan_Add(sub, NULL);
        } else {
            const char* modName = Dir_BaseName(sub);
            if(Song_HasChart(sub)) {
                Scan_Add(sub, modName);
            } else {
                // mod pack layout: <mod>/songs/<song>
                char songsDir[300];
                snprintf(songsDir, sizeof(songsDir), "%s/songs", sub);
                if(DirectoryExists(songsDir)) {
                    FilePathList inner = LoadDirectoryFilesEx(songsDir, "DIR", false);
                    for(unsigned int j = 0; j < inner.count; j++) {
                        char disp[64];
                        snprintf(disp, sizeof(disp), "%s/%s", modName, Dir_BaseName(inner.paths[j]));
                        Scan_Add(inner.paths[j], disp);
                    }
                    UnloadDirectoryFiles(inner);
                }
            }
        }
    }
    UnloadDirectoryFiles(list);
}

int SongList_Scan(SongEntry** outEntries) {
    *outEntries = NULL;

    // free previous scan buffer (reentrancy for scene re-enter)
    free(entries);
    entries = NULL;
    entryCount = 0;
    entryCap = 0;

    Scan_Subdirs("assets/songs", 0);
    Scan_Subdirs("assets/mods", 1);

    if(entryCount > 1)
        qsort(entries, (size_t)entryCount, sizeof(SongEntry), Entry_Compare);

    *outEntries = entries;

    int n = entryCount;
    // hand ownership to caller
    entries = NULL;
    entryCount = 0;
    entryCap = 0;
    return n;
}

void SongList_Free(SongEntry* list) {
    free(list);
}

#define HEAVY_INST_BYTES (1024 * 1024)
#define HEAVY_CHART_BYTES (512 * 1024)

static long File_Size(const char* path) {
    FILE* f = fopen(path, "rb");
    if(f == NULL)
        return -1;
    fseek(f, 0, SEEK_END);
    long s = ftell(f);
    fclose(f);
    return s;
}

char SongList_IsHeavy(const char* dir) {
    char p[300];
    snprintf(p, sizeof(p), "%s/Inst.ogg", dir);
    if(File_Size(p) > HEAVY_INST_BYTES)
        return 1;
    snprintf(p, sizeof(p), "%s/data.json", dir);
    if(File_Size(p) > HEAVY_CHART_BYTES)
        return 1;
    snprintf(p, sizeof(p), "%s/data.song", dir);
    if(File_Size(p) > HEAVY_CHART_BYTES)
        return 1;
    return 0;
}
