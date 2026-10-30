/*
 * Friday Night Funkin' C Engine
 * SongList.c - song discovery and opponent metadata
 * (c) 2026 nordjkpl
 */

#include "std.h"
#include "scenes/Song.h"
#include "scenes/SongList.h"
#include "yyjson.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SONGLIST_PATH_MAX       512
#define SONGLIST_JSON_MAX       (64L * 1024L * 1024L)

static SongEntry *entries;
static int entryCount;
static int entryCap;

static const char *Dir_BaseName(const char *path)
{
    const char *base = path;
    if (!path) return "";

    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

static void Path_Normalize(const char *src, char *out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!src) return;

    size_t i = 0;
    for (; src[i] && i + 1 < outSize; ++i)
        out[i] = src[i] == '\\' ? '/' : src[i];
    out[i] = '\0';
}

static long File_SizeFast(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long size = ftell(file);
    fclose(file);
    return size;
}

static int Entry_Compare(const void *a, const void *b)
{
    const SongEntry *left = (const SongEntry *)a;
    const SongEntry *right = (const SongEntry *)b;
    return strcmp(left->name, right->name);
}

static char Opp_IsSafeName(const char *name)
{
    if (!name || !name[0] || strstr(name, "..")) return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p < 32) return 0;
    }
    return 1;
}

static char Opp_FromJsonFile(const char *path, char out[32])
{
    out[0] = '\0';

    FILE *file = fopen(path, "rb");
    if (!file) return 0;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    long size = ftell(file);
    if (size <= 0 || size > SONGLIST_JSON_MAX ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    char *buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return 0;
    }

    size_t bytesRead = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (bytesRead != (size_t)size) {
        free(buffer);
        return 0;
    }
    buffer[bytesRead] = '\0';

    yyjson_doc *doc = yyjson_read(buffer, bytesRead, 0);
    free(buffer);
    if (!doc) return 0;

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *song = yyjson_is_obj(root) ? yyjson_obj_get(root, "song") : NULL;
    yyjson_val *objects[2] = {
        yyjson_is_obj(song) ? song : NULL,
        yyjson_is_obj(root) ? root : NULL
    };
    static const char *const keys[] = {
        "player2", "opponent", "opponentCharacter", "opponent_character"
    };

    char found = 0;
    for (size_t oi = 0; oi < 2 && !found; ++oi) {
        if (!objects[oi]) continue;
        for (size_t ki = 0; ki < sizeof(keys) / sizeof(keys[0]); ++ki) {
            yyjson_val *value = yyjson_obj_get(objects[oi], keys[ki]);
            if (!yyjson_is_str(value)) continue;

            const char *name = yyjson_get_str(value);
            if (!Opp_IsSafeName(name) || strlen(name) >= 32) continue;

            strcpy(out, name);
            found = 1;
            break;
        }
    }

    yyjson_doc_free(doc);
    return found;
}

static char Opp_FromSongBin(const char *path, char out[32])
{
    out[0] = '\0';
    FILE *file = fopen(path, "rb");
    if (!file) return 0;

    char values[3][32] = {{0}};
    for (int i = 0; i < 3; ++i) {
        int32_t length = 0;
        if (fread(&length, sizeof(length), 1, file) != 1 ||
            length < 0 || length >= (int32_t)sizeof(values[i])) {
            fclose(file);
            return 0;
        }
        if (length > 0 && fread(values[i], 1, (size_t)length, file) != (size_t)length) {
            fclose(file);
            return 0;
        }
        values[i][length] = '\0';
    }
    fclose(file);

    if (!Opp_IsSafeName(values[1])) return 0;
    strcpy(out, values[1]);
    return 1;
}

static char Opp_FromDirectory(const char *directory, char out[32])
{
    if (!directory || !directory[0] || !DirectoryExists(directory)) return 0;

    FilePathList files = LoadDirectoryFilesEx(directory, ".json", false);
    char found = 0;

    for (unsigned i = 0; i < files.count; ++i) {
        const char *base = Dir_BaseName(files.paths[i]);
        if (strcmp(base, "events.json") == 0 || strcmp(base, "data.json") == 0)
            continue;
        if (Opp_FromJsonFile(files.paths[i], out)) {
            found = 1;
            break;
        }
    }

    UnloadDirectoryFiles(files);
    return found;
}

static void SongList_GetModRoot(const char *songDir, char out[SONGLIST_PATH_MAX])
{
    out[0] = '\0';
    if (!songDir || !songDir[0]) return;

    char normalized[SONGLIST_PATH_MAX];
    Path_Normalize(songDir, normalized, sizeof(normalized));

    const char marker[] = "assets/mods/";
    char *mods = strstr(normalized, marker);
    if (!mods) return;

    char *mod = mods + sizeof(marker) - 1;
    char *separator = strchr(mod, '/');
    if (!separator) return;

    /* Direct-root mod: assets/mods/songs/<song> belongs to assets/mods. */
    if ((size_t)(separator - mod) == 5 && memcmp(mod, "songs", 5) == 0) {
        size_t length = (size_t)(mod - normalized);
        if (length < SONGLIST_PATH_MAX) {
            memcpy(out, normalized, length);
            out[length] = '\0';
        }
        return;
    }

    /* Compatibilidade temporária com assets/mods/mods/<nome>/... */
    if ((size_t)(separator - mod) == 4 && memcmp(mod, "mods", 4) == 0) {
        mod = separator + 1;
        separator = strchr(mod, '/');
        if (!separator) return;
    }

    size_t length = (size_t)(separator - normalized);
    if (length >= SONGLIST_PATH_MAX) return;
    memcpy(out, normalized, length);
    out[length] = '\0';
}

/* ------------------------------------------------------------------------- */
/* Week icon map: Psych week JSON stores [songName, iconName, color].          */
/* It is loaded once per song-list scan, not while rendering Freeplay.          */
/* ------------------------------------------------------------------------- */
#define SONGLIST_WEEK_ICON_MAX 512
#define SONGLIST_WEEK_NAME_MAX 96

typedef struct {
    char song[SONGLIST_WEEK_NAME_MAX];
    char icon[SONGLIST_WEEK_NAME_MAX];
} SongListWeekIcon;

static SongListWeekIcon weekIconMap[SONGLIST_WEEK_ICON_MAX];
static int weekIconCount;

static char SongList_NameEqual(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static void SongList_AddWeekIcon(const char *songName, const char *iconName)
{
    if (!Opp_IsSafeName(songName) || !Opp_IsSafeName(iconName) ||
        strlen(songName) >= SONGLIST_WEEK_NAME_MAX ||
        strlen(iconName) >= SONGLIST_WEEK_NAME_MAX ||
        weekIconCount >= SONGLIST_WEEK_ICON_MAX) return;

    for (int i = 0; i < weekIconCount; ++i) {
        if (SongList_NameEqual(weekIconMap[i].song, songName)) return;
    }

    SongListWeekIcon *entry = &weekIconMap[weekIconCount++];
    (void)snprintf(entry->song, sizeof(entry->song), "%s", songName);
    (void)snprintf(entry->icon, sizeof(entry->icon), "%s", iconName);
}

static void SongList_ReadWeekFile(const char *path)
{
    FILE *file = fopen(path, "rb");
    char *buffer;
    long size;
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *songs;

    if (!file) return;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return;
    }
    size = ftell(file);
    if (size <= 0 || size > 2L * 1024L * 1024L ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return;
    }

    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return;
    }
    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return;
    }
    fclose(file);
    buffer[size] = '\0';

    doc = yyjson_read(buffer, (size_t)size, 0);
    free(buffer);
    if (!doc) return;

    root = yyjson_doc_get_root(doc);
    songs = yyjson_is_obj(root) ? yyjson_obj_get(root, "songs") : NULL;
    if (yyjson_is_arr(songs)) {
        size_t i, max;
        yyjson_val *row;
        yyjson_arr_foreach(songs, i, max, row) {
            yyjson_val *songValue;
            yyjson_val *iconValue;
            const char *songName;
            const char *iconName;

            if (!yyjson_is_arr(row) || yyjson_arr_size(row) < 2) continue;
            songValue = yyjson_arr_get(row, 0);
            iconValue = yyjson_arr_get(row, 1);
            if (!yyjson_is_str(songValue) || !yyjson_is_str(iconValue)) continue;
            songName = yyjson_get_str(songValue);
            iconName = yyjson_get_str(iconValue);
            SongList_AddWeekIcon(songName, iconName);
        }
    }

    yyjson_doc_free(doc);
}

static char SongList_EndsWithJson(const char *path)
{
    const char *base = Dir_BaseName(path);
    size_t length = strlen(base);
    return length >= 5 && SongList_NameEqual(base + length - 5, ".json");
}

static void SongList_ReadWeekTree(const char *directory, int depth)
{
    if (!directory || depth > 4 || !DirectoryExists(directory)) return;

    FilePathList files = LoadDirectoryFilesEx(directory, NULL, false);
    for (unsigned i = 0; i < files.count; ++i) {
        if (DirectoryExists(files.paths[i])) {
            SongList_ReadWeekTree(files.paths[i], depth + 1);
        } else if (SongList_EndsWithJson(files.paths[i])) {
            SongList_ReadWeekFile(files.paths[i]);
        }
    }
    UnloadDirectoryFiles(files);
}

static void SongList_LoadWeekIcons(const char *weekDirectory)
{
    weekIconCount = 0;
    SongList_ReadWeekTree(weekDirectory, 0);
}

static void SongList_FindWeekIcon(const char *songName, char out[64])
{
    out[0] = '\0';
    if (!songName || !songName[0]) return;
    for (int i = 0; i < weekIconCount; ++i) {
        if (SongList_NameEqual(weekIconMap[i].song, songName)) {
            (void)snprintf(out, 64, "%s", weekIconMap[i].icon);
            return;
        }
    }
}

char SongList_Opponent(const char *dir, char out[32])
{
    if (!out) return 0;
    out[0] = '\0';
    if (!dir || !dir[0]) return 0;

    char path[SONGLIST_PATH_MAX];
    char normalized[SONGLIST_PATH_MAX];
    Path_Normalize(dir, normalized, sizeof(normalized));
    const char *base = Dir_BaseName(normalized);

    /* Prefer actual difficulty/chart files. A generic data.json can contain
       stale/default metadata (often "dad") and must not shadow chart player2. */
    (void)snprintf(path, sizeof(path), "%s/%s.json", normalized, base);
    if (Opp_FromJsonFile(path, out)) return 1;
    if (Opp_FromDirectory(normalized, out)) return 1;

    /* Original Psych layout: <mod>/data/<song>/<difficulty>.json. */
    char modRoot[SONGLIST_PATH_MAX];
    SongList_GetModRoot(normalized, modRoot);
    if (modRoot[0]) {
        (void)snprintf(path, sizeof(path), "%s/data/%s", modRoot, base);
        if (Opp_FromDirectory(path, out)) return 1;
        (void)snprintf(path, sizeof(path), "%s/data/%s.json", modRoot, base);
        if (Opp_FromJsonFile(path, out)) return 1;
    }

    /* Base-game Psych layout. */
    (void)snprintf(path, sizeof(path), "assets/data/%s", base);
    if (Opp_FromDirectory(path, out)) return 1;
    (void)snprintf(path, sizeof(path), "assets/data/%s.json", base);
    if (Opp_FromJsonFile(path, out)) return 1;

    /* Alternate converted layouts. */
    (void)snprintf(path, sizeof(path), "%s/charts", normalized);
    if (Opp_FromDirectory(path, out)) return 1;
    (void)snprintf(path, sizeof(path), "%s/data", normalized);
    if (Opp_FromDirectory(path, out)) return 1;

    /* data.json is deliberately last: some packs use it as their chart, but
       it is often generic metadata and should not override difficulty charts. */
    (void)snprintf(path, sizeof(path), "%s/data.json", normalized);
    if (Opp_FromJsonFile(path, out)) return 1;

    (void)snprintf(path, sizeof(path), "%s/data.song", normalized);
    return Opp_FromSongBin(path, out);
}

static char SongList_HasInstrumental(const char *dirPath)
{
    static const char *const names[] = {"Inst.ogg", "Inst.mp3", "Inst.wav"};
    char path[SONGLIST_PATH_MAX];

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        (void)snprintf(path, sizeof(path), "%s/%s", dirPath, names[i]);
        if (FileExists(path)) return 1;
    }
    return 0;
}

static char SongList_HasChartForSong(const char *songDir)
{
    char path[SONGLIST_PATH_MAX];
    char modRoot[SONGLIST_PATH_MAX];
    const char *base;

    if (!songDir || !songDir[0]) return 0;
    if (Song_HasChart(songDir)) return 1;

    base = Dir_BaseName(songDir);
    SongList_GetModRoot(songDir, modRoot);
    if (modRoot[0]) {
        (void)snprintf(path, sizeof(path), "%s/data/%s", modRoot, base);
        if (Song_HasChart(path)) return 1;
    }

    (void)snprintf(path, sizeof(path), "%s/charts", songDir);
    if (Song_HasChart(path)) return 1;
    (void)snprintf(path, sizeof(path), "%s/data", songDir);
    if (Song_HasChart(path)) return 1;

    (void)snprintf(path, sizeof(path), "assets/data/%s", base);
    return Song_HasChart(path);
}

static void Scan_Add(const char *dirPath, const char *displayName)
{
    char normalized[256];
    if (!SongList_HasInstrumental(dirPath)) return;
    if (!SongList_HasChartForSong(dirPath)) return;

    Path_Normalize(dirPath, normalized, sizeof(normalized));
    for (int i = 0; i < entryCount; ++i)
        if (strcmp(entries[i].dir, normalized) == 0) return;

    if (entryCount >= entryCap) {
        int newCap = entryCap ? entryCap * 2 : 16;
        SongEntry *grown = (SongEntry *)realloc(
            entries, sizeof(*entries) * (size_t)newCap);
        if (!grown) return;
        entries = grown;
        entryCap = newCap;
    }

    SongEntry *entry = &entries[entryCount++];
    memset(entry, 0, sizeof(*entry));
    const char *base = displayName ? displayName : Dir_BaseName(dirPath);
    snprintf(entry->name, sizeof(entry->name), "%s", base);
    snprintf(entry->dir, sizeof(entry->dir), "%s", normalized);

    /* Resolve chart opponent and week icon once during the scan. */
    (void)SongList_Opponent(normalized, entry->opp);
    SongList_FindWeekIcon(Dir_BaseName(normalized), entry->weekIcon);
}

static void Scan_SongFolders(const char *songsDirectory)
{
    if (!DirectoryExists(songsDirectory)) return;
    FilePathList inner = LoadDirectoryFilesEx(songsDirectory, "DIR", false);
    for (unsigned i = 0; i < inner.count; ++i)
        Scan_Add(inner.paths[i], Dir_BaseName(inner.paths[i]));
    UnloadDirectoryFiles(inner);
}

static void Scan_ModRoot(const char *modRoot)
{
    char path[SONGLIST_PATH_MAX];
    const char *base = Dir_BaseName(modRoot);

    /* One week metadata pass per mod, before its songs are appended. */
    (void)snprintf(path, sizeof(path), "%s/weeks", modRoot);
    SongList_LoadWeekIcons(path);

    /* Some older layouts store a song directly under a mod folder. */
    if (SongList_HasInstrumental(modRoot) && SongList_HasChartForSong(modRoot)) {
        Scan_Add(modRoot, base);
        return;
    }

    (void)snprintf(path, sizeof(path), "%s/songs", modRoot);
    Scan_SongFolders(path);
}

static char SongList_IsModLayout(const char *directory)
{
    static const char *const layoutFolders[] = {"songs", "weeks"};
    char candidate[SONGLIST_PATH_MAX];

    if (!directory || !directory[0] || !DirectoryExists(directory)) return 0;

    /* The importer can place a mod directly in assets/mods/. In that case,
       assets/mods/songs and assets/mods/weeks belong to the mod itself. */
    for (size_t i = 0; i < sizeof(layoutFolders) / sizeof(layoutFolders[0]); ++i) {
        if (snprintf(candidate, sizeof(candidate), "%s/%s", directory,
                     layoutFolders[i]) >= (int)sizeof(candidate)) continue;
        if (DirectoryExists(candidate)) return 1;
    }
    return 0;
}

static char SongList_IsRootContentDirectory(const char *name)
{
    static const char *const names[] = {
        "songs", "weeks", "stages", "characters", "images", "data"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (SongList_NameEqual(name, names[i])) return 1;
    return 0;
}

static void Scan_ModRoots(const char *modsDirectory)
{
    if (!DirectoryExists(modsDirectory)) return;

    /* Supports both assets/mods/<mod>/... and a mod imported directly into
       assets/mods/; nested mods can coexist with the direct-root mod. */
    if (SongList_IsModLayout(modsDirectory))
        Scan_ModRoot(modsDirectory);

    FilePathList roots = LoadDirectoryFilesEx(modsDirectory, "DIR", false);
    for (unsigned i = 0; i < roots.count; ++i) {
        const char *root = roots.paths[i];
        const char *base = Dir_BaseName(root);

        /* Content folders of a root-level mod are not separate mods. */
        if (SongList_IsRootContentDirectory(base)) continue;

        /* Legacy duplicated root: assets/mods/mods/<modname>/. */
        if (SongList_NameEqual(base, "mods")) {
            FilePathList nested = LoadDirectoryFilesEx(root, "DIR", false);
            for (unsigned j = 0; j < nested.count; ++j)
                Scan_ModRoot(nested.paths[j]);
            UnloadDirectoryFiles(nested);
        } else {
            Scan_ModRoot(root);
        }
    }
    UnloadDirectoryFiles(roots);
}

int SongList_Scan(SongEntry **outEntries)
{
    if (!outEntries) return 0;
    *outEntries = NULL;

    free(entries);
    entries = NULL;
    entryCount = 0;
    entryCap = 0;

    SongList_LoadWeekIcons("assets/weeks");
    Scan_SongFolders("assets/songs");
    Scan_ModRoots("assets/mods");

    if (entryCount > 1)
        qsort(entries, (size_t)entryCount, sizeof(*entries), Entry_Compare);

    *outEntries = entries;
    int result = entryCount;
    entries = NULL;
    entryCount = 0;
    entryCap = 0;
    weekIconCount = 0;
    return result;
}

void SongList_Free(SongEntry *list)
{
    free(list);
}

char SongList_IsHeavy(const char *dir)
{
    static const char *const instrumentals[] = {"Inst.ogg", "Inst.mp3", "Inst.wav"};
    char path[SONGLIST_PATH_MAX];

    for (size_t i = 0; i < sizeof(instrumentals) / sizeof(instrumentals[0]); ++i) {
        (void)snprintf(path, sizeof(path), "%s/%s", dir, instrumentals[i]);
        if (File_SizeFast(path) > 1024L * 1024L) return 1;
    }

    (void)snprintf(path, sizeof(path), "%s/data.json", dir);
    if (File_SizeFast(path) > 512L * 1024L) return 1;

    (void)snprintf(path, sizeof(path), "%s/data.song", dir);
    return File_SizeFast(path) > 512L * 1024L;
}
