/*
 * Friday Night Funkin' C Engine (c) 2026 nordjkpl
 * Song.c - chart discovery and loading (base game + imported mods).
 *
 * Supported layouts:
 *   assets/songs/<song>/...
 *   assets/mods/<mod>/songs/<song>/...
 *   assets/mods/<mod>/data/<song>/...       (original Psych layout)
 *   assets/mods/mods/<mod>/...              (legacy duplicated root)
 */

#include "std.h"
#include "scenes/Song.h"
#include "yyjson.h"
#include "Log.h"

#include <stdint.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SONG_JSON_MAX_BYTES (64L * 1024L * 1024L)
#define SONG_PATH_MAX       1024
#define SONG_MAX_SECTIONS   100000

/* ------------------------------------------------------------------------- */
/* Basic helpers                                                             */
/* ------------------------------------------------------------------------- */

static const char *Song_BaseName(const char *path)
{
    const char *base = path;
    if (path == NULL) return "";
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

static void Song_CopyString(char dst[32], const char *src, const char *fallback)
{
    const char *value = src != NULL ? src : fallback;
    size_t length;
    memset(dst, 0, 32);
    if (value == NULL) return;
    length = strlen(value);
    if (length > 31u) length = 31u;
    memcpy(dst, value, length);
}

static int Song_EndsWithI(const char *text, const char *suffix)
{
    size_t textLen;
    size_t suffixLen;
    if (text == NULL || suffix == NULL) return 0;
    textLen = strlen(text);
    suffixLen = strlen(suffix);
    if (suffixLen > textLen) return 0;
    text += textLen - suffixLen;
    for (size_t i = 0; i < suffixLen; ++i) {
        unsigned char a = (unsigned char)text[i];
        unsigned char b = (unsigned char)suffix[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static int Song_PathJoin(char *out, size_t outSize, const char *left, const char *right)
{
    size_t len;
    int written;
    if (out == NULL || outSize == 0 || left == NULL || right == NULL) return 0;
    len = strlen(left);
    if (len > 0 && (left[len - 1] == '/' || left[len - 1] == '\\'))
        written = snprintf(out, outSize, "%s%s", left, right);
    else
        written = snprintf(out, outSize, "%s/%s", left, right);
    return written >= 0 && (size_t)written < outSize;
}

static void Song_NormalizePath(const char *src, char *out, size_t outSize)
{
    size_t i = 0;
    if (out == NULL || outSize == 0) return;
    out[0] = '\0';
    if (src == NULL) return;
    while (src[i] != '\0' && i + 1u < outSize) {
        out[i] = src[i] == '\\' ? '/' : src[i];
        ++i;
    }
    out[i] = '\0';
}

static void Song_GetModRoot(const char *songDir, char *out, size_t outSize)
{
    char normalized[SONG_PATH_MAX];
    const char marker[] = "assets/mods/";
    char *markerPos;
    char *modStart;
    char *separator;
    size_t length;

    if (out == NULL || outSize == 0) return;
    out[0] = '\0';
    if (songDir == NULL || songDir[0] == '\0') return;

    Song_NormalizePath(songDir, normalized, sizeof(normalized));
    markerPos = strstr(normalized, marker);
    if (markerPos == NULL) return;

    modStart = markerPos + sizeof(marker) - 1u;
    separator = strchr(modStart, '/');
    if (separator == NULL) return;

    /* Direct-root mod: assets/mods/songs/<song> belongs to assets/mods. */
    if ((size_t)(separator - modStart) == 5u && memcmp(modStart, "songs", 5u) == 0) {
        length = (size_t)(modStart - normalized);
        if (length >= outSize) return;
        memcpy(out, normalized, length);
        out[length] = '\0';
        return;
    }

    /* Old imports used assets/mods/mods/<modName>/... . */
    if ((size_t)(separator - modStart) == 4u && memcmp(modStart, "mods", 4u) == 0) {
        modStart = separator + 1;
        separator = strchr(modStart, '/');
        if (separator == NULL) return;
    }

    length = (size_t)(separator - normalized);
    if (length >= outSize) return;
    memcpy(out, normalized, length);
    out[length] = '\0';
}

static int Song_ReadFile(const char *path, char **outBuffer, size_t *outSize)
{
    FILE *file;
    long size;
    char *buffer;

    if (outBuffer == NULL || outSize == NULL || path == NULL) return 0;
    *outBuffer = NULL;
    *outSize = 0;

    file = fopen(path, "rb");
    if (file == NULL) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    size = ftell(file);
    if (size <= 0 || size > SONG_JSON_MAX_BYTES || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    buffer = (char *)malloc((size_t)size + 1u);
    if (buffer == NULL) {
        fclose(file);
        return 0;
    }
    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return 0;
    }
    fclose(file);
    buffer[size] = '\0';
    *outBuffer = buffer;
    *outSize = (size_t)size;
    return 1;
}

void File_ReadString(FILE *file, char str[32])
{
    int32_t length;
    if (fread(&length, sizeof(length), 1, file) != 1 || length < 1 || length > 31)
        Log_Fatal("invalid song string");
    if (fread(str, 1, (size_t)length, file) != (size_t)length)
        Log_Fatal("truncated song string");
    str[length] = '\0';
}

/* ------------------------------------------------------------------------- */
/* Binary song loader                                                        */
/* ------------------------------------------------------------------------- */

void Song_Parse(Song *song, const char *songDataPath)
{
    FILE *file;
    int32_t sectionCount;

    if (song == NULL || songDataPath == NULL) Log_Fatal("invalid song path");
    memset(song, 0, sizeof(*song));
    file = fopen(songDataPath, "rb");
    if (file == NULL) Log_FatalF("could not open file %s", songDataPath);

    File_ReadString(file, song->player1);
    File_ReadString(file, song->player2);
    File_ReadString(file, song->stage);
    Song_CopyString(song->gfVersion, NULL, "gf");

    if (fread(&song->speed, sizeof(float), 1, file) != 1 ||
        fread(&sectionCount, sizeof(sectionCount), 1, file) != 1) {
        fclose(file);
        Log_Fatal("truncated binary song header");
    }
    if (!isfinite(song->speed) || song->speed <= 0.0f ||
        sectionCount < 1 || sectionCount > SONG_MAX_SECTIONS) {
        fclose(file);
        Log_Fatal("invalid binary song metadata");
    }

    song->sections = (Section *)calloc((size_t)sectionCount, sizeof(Section));
    if (song->sections == NULL) {
        fclose(file);
        Log_Fatal("out of memory loading binary song");
    }
    song->sectionCount = sectionCount;

    for (int32_t i = 0; i < sectionCount; ++i) {
        Section *section = &song->sections[i];
        int32_t noteCount;
        if (fread(&section->len, sizeof(section->len), 1, file) != 1 ||
            fread(&section->bpm, sizeof(section->bpm), 1, file) != 1 ||
            fread(&section->mustHit, 1, 1, file) != 1 ||
            fread(&noteCount, sizeof(noteCount), 1, file) != 1) {
            fclose(file);
            Log_Fatal("truncated binary song section");
        }
        if (section->len < 1 || !isfinite(section->bpm) || section->bpm < 10.0f ||
            noteCount < 0 || (size_t)noteCount > SIZE_MAX / sizeof(DataNote)) {
            fclose(file);
            Log_Fatal("invalid binary song section");
        }
        section->noteCount = noteCount;
        if (noteCount > 0) {
            section->notes = (DataNote *)malloc(sizeof(DataNote) * (size_t)noteCount);
            if (section->notes == NULL ||
                fread(section->notes, sizeof(DataNote), (size_t)noteCount, file) != (size_t)noteCount) {
                fclose(file);
                Log_Fatal("truncated binary song notes");
            }
        }
        for (int32_t j = 0; j < noteCount; ++j) {
            const DataNote *note = &section->notes[j];
            int id = abs((int)note->id);
            if (!isfinite(note->time) || note->time < 0.0f || id < 1 || id > 4 ||
                !isfinite(note->len) || note->len < 0.0f) {
                fclose(file);
                Log_Fatal("invalid binary song note");
            }
        }
    }
    fclose(file);
}

void Song_Free(Song *song)
{
    if (song == NULL) return;
    if (song->sections != NULL) {
        for (int i = 0; i < song->sectionCount; ++i)
            free(song->sections[i].notes);
        free(song->sections);
    }
    memset(song, 0, sizeof(*song));
}

/* ------------------------------------------------------------------------- */
/* JSON helpers and parser                                                   */
/* ------------------------------------------------------------------------- */

typedef struct RawNote {
    float time;
    int rawId;
    float len;
} RawNote;

static int RawNote_Compare(const void *left, const void *right)
{
    const float a = ((const RawNote *)left)->time;
    const float b = ((const RawNote *)right)->time;
    return a < b ? -1 : a > b ? 1 : 0;
}

static double Song_JsonNumber(yyjson_val *value, double fallback)
{
    if (!yyjson_is_num(value)) return fallback;
    if (yyjson_is_int(value)) return (double)yyjson_get_sint(value);
    if (yyjson_is_uint(value)) return (double)yyjson_get_uint(value);
    return yyjson_get_real(value);
}

static float Song_JsonFloat(yyjson_val *value, float fallback)
{
    double number = Song_JsonNumber(value, fallback);
    if (!isfinite(number) || number > FLT_MAX || number < -FLT_MAX) return fallback;
    return (float)number;
}

char Song_ParseJSON(Song *song, const char *jsonPath)
{
    char *buffer = NULL;
    size_t bufferSize = 0;
    yyjson_doc *doc = NULL;
    yyjson_val *root;
    yyjson_val *songObject;
    yyjson_val *sectionsValue;
    Song parsed;
    float topBpm;
    float speed;
    float runningBpm;
    size_t sectionCount;
    int success = 0;

    if (song == NULL || jsonPath == NULL) return 0;
    memset(&parsed, 0, sizeof(parsed));
    if (!Song_ReadFile(jsonPath, &buffer, &bufferSize)) return 0;

    doc = yyjson_read(buffer, bufferSize, 0);
    free(buffer);
    buffer = NULL;
    if (doc == NULL) return 0;

    root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) goto cleanup;
    songObject = yyjson_obj_get(root, "song");
    if (!yyjson_is_obj(songObject)) songObject = root;

    sectionsValue = yyjson_obj_get(songObject, "notes");
    if (!yyjson_is_arr(sectionsValue)) goto cleanup;
    sectionCount = yyjson_arr_size(sectionsValue);
    if (sectionCount == 0 || sectionCount > SONG_MAX_SECTIONS) goto cleanup;

    topBpm = Song_JsonFloat(yyjson_obj_get(songObject, "bpm"), 100.0f);
    if (topBpm < 10.0f) topBpm = 100.0f;
    speed = Song_JsonFloat(yyjson_obj_get(songObject, "speed"), 1.0f);
    if (speed < 0.1f) speed = 1.0f;

    Song_CopyString(parsed.player1,
        yyjson_is_str(yyjson_obj_get(songObject, "player1")) ?
        yyjson_get_str(yyjson_obj_get(songObject, "player1")) : NULL, "bf");
    Song_CopyString(parsed.player2,
        yyjson_is_str(yyjson_obj_get(songObject, "player2")) ?
        yyjson_get_str(yyjson_obj_get(songObject, "player2")) : NULL, "dad");
    Song_CopyString(parsed.stage,
        yyjson_is_str(yyjson_obj_get(songObject, "stage")) ?
        yyjson_get_str(yyjson_obj_get(songObject, "stage")) : NULL, "stage");
    Song_CopyString(parsed.gfVersion,
        yyjson_is_str(yyjson_obj_get(songObject, "gfVersion")) ?
        yyjson_get_str(yyjson_obj_get(songObject, "gfVersion")) : NULL, "gf");
    Song_CopyString(parsed.noteSkin,
        yyjson_is_str(yyjson_obj_get(songObject, "noteSkin")) ?
        yyjson_get_str(yyjson_obj_get(songObject, "noteSkin")) : NULL, "");

    parsed.bpm = topBpm;
    parsed.speed = speed;
    parsed.sectionCount = (int)sectionCount;
    parsed.sections = (Section *)calloc(sectionCount, sizeof(Section));
    if (parsed.sections == NULL) goto cleanup;

    runningBpm = topBpm;
    for (size_t i = 0; i < sectionCount; ++i) {
        yyjson_val *sectionValue = yyjson_arr_get(sectionsValue, i);
        Section *section = &parsed.sections[i];
        yyjson_val *lengthValue;
        yyjson_val *mustHitValue;
        yyjson_val *sectionBpmValue;
        yyjson_val *changeBpmValue;
        yyjson_val *notesValue;
        size_t rawCount;
        RawNote *raw = NULL;
        DataNote *output = NULL;
        int valid = 0;
        int kept = 0;
        int sorted = 1;
        float lastTime = -1.0f;
        float lastLaneTime[8];

        section->len = 16;
        section->bpm = runningBpm;
        if (!yyjson_is_obj(sectionValue)) continue;

        lengthValue = yyjson_obj_get(sectionValue, "lengthInSteps");
        if (lengthValue == NULL) lengthValue = yyjson_obj_get(sectionValue, "lengthinsteps");
        mustHitValue = yyjson_obj_get(sectionValue, "mustHitSection");
        sectionBpmValue = yyjson_obj_get(sectionValue, "bpm");
        changeBpmValue = yyjson_obj_get(sectionValue, "changeBPM");
        notesValue = yyjson_obj_get(sectionValue, "sectionNotes");

        if (yyjson_is_num(lengthValue)) {
            double length = Song_JsonNumber(lengthValue, 16.0);
            if (length >= 1.0 && length <= INT_MAX) section->len = (int)length;
        }
        section->mustHit = (char)(yyjson_is_bool(mustHitValue) && yyjson_get_bool(mustHitValue));
        if (yyjson_is_bool(changeBpmValue) && yyjson_get_bool(changeBpmValue) &&
            yyjson_is_num(sectionBpmValue)) {
            float sectionBpm = Song_JsonFloat(sectionBpmValue, runningBpm);
            if (sectionBpm >= 10.0f) runningBpm = sectionBpm;
        }
        section->bpm = runningBpm;

        if (!yyjson_is_arr(notesValue)) continue;
        rawCount = yyjson_arr_size(notesValue);
        if (rawCount == 0 || rawCount > (size_t)INT_MAX ||
            rawCount > SIZE_MAX / sizeof(RawNote)) continue;

        raw = (RawNote *)malloc(sizeof(RawNote) * rawCount);
        if (raw == NULL) continue;

        for (size_t j = 0; j < rawCount; ++j) {
            yyjson_val *noteValue = yyjson_arr_get(notesValue, j);
            yyjson_val *timeValue;
            yyjson_val *laneValue;
            yyjson_val *lengthNoteValue;
            double laneNumber;
            double noteLength;
            float noteTime;
            int lane;

            if (!yyjson_is_arr(noteValue) || yyjson_arr_size(noteValue) < 2) continue;
            timeValue = yyjson_arr_get(noteValue, 0);
            laneValue = yyjson_arr_get(noteValue, 1);
            lengthNoteValue = yyjson_arr_size(noteValue) > 2 ? yyjson_arr_get(noteValue, 2) : NULL;
            if (!yyjson_is_num(timeValue) || !yyjson_is_num(laneValue)) continue;

            noteTime = Song_JsonFloat(timeValue, -1.0f);
            laneNumber = Song_JsonNumber(laneValue, -1.0);
            if (noteTime < 0.0f || !isfinite(laneNumber) ||
                laneNumber < (double)INT_MIN || laneNumber > (double)INT_MAX) continue;
            lane = (int)laneNumber % 8;
            if (lane < 0) lane += 8;

            noteLength = yyjson_is_num(lengthNoteValue) ?
                         Song_JsonNumber(lengthNoteValue, 0.0) : 0.0;
            if (!isfinite(noteLength) || noteLength < 0.0 || noteLength > FLT_MAX) continue;

            if (valid > 0 && noteTime < lastTime) sorted = 0;
            lastTime = noteTime;
            raw[valid].time = noteTime;
            raw[valid].rawId = lane;
            raw[valid].len = (float)noteLength;
            ++valid;
        }

        if (valid == 0) {
            free(raw);
            continue;
        }
        if (!sorted) qsort(raw, (size_t)valid, sizeof(*raw), RawNote_Compare);

        output = (DataNote *)malloc(sizeof(DataNote) * (size_t)valid);
        if (output == NULL) {
            free(raw);
            continue;
        }
        for (int k = 0; k < 8; ++k) lastLaneTime[k] = -999999.0f;

        for (int j = 0; j < valid; ++j) {
            int lane = raw[j].rawId;
            int id;
            if (raw[j].time - lastLaneTime[lane] < 2.0f) continue;
            lastLaneTime[lane] = raw[j].time;
            id = section->mustHit ?
                 (lane < 4 ? -(lane + 1) : lane - 3) :
                 (lane < 4 ? lane + 1 : -(lane - 3));
            output[kept].time = raw[j].time;
            output[kept].id = (int16_t)id;
            output[kept]._pad = 0;
            output[kept].len = raw[j].len;
            ++kept;
        }
        free(raw);

        if (kept == 0) {
            free(output);
            continue;
        }
        section->notes = output;
        section->noteCount = kept;
    }

    *song = parsed;
    memset(&parsed, 0, sizeof(parsed));
    success = 1;

cleanup:
    if (doc != NULL) yyjson_doc_free(doc);
    if (!success) Song_Free(&parsed);
    return (char)success;
}

/* ------------------------------------------------------------------------- */
/* Chart discovery                                                           */
/* ------------------------------------------------------------------------- */

static int Song_IsEventsFile(const char *path)
{
    return Song_EndsWithI(Song_BaseName(path), "events.json");
}

static int Song_IsDifficultyFile(const char *path)
{
    const char *base = Song_BaseName(path);
    return Song_EndsWithI(base, "-easy.json") ||
           Song_EndsWithI(base, "-normal.json") ||
           Song_EndsWithI(base, "-hard.json");
}

static int Song_TryChartFile(Song *song, const char *path)
{
    if (path == NULL || !FileExists(path) || Song_IsEventsFile(path)) return 0;
    return Song_ParseJSON(song, path);
}

static int Song_TryLoadChartDirectory(Song *song, const char *directory,
                                      const char *diff)
{
    char candidate[SONG_PATH_MAX];
    char baseDiff[SONG_PATH_MAX];
    const char *base;
    FilePathList files;
    int loaded = 0;

    if (song == NULL || directory == NULL || directory[0] == '\0' ||
        !DirectoryExists(directory)) return 0;
    base = Song_BaseName(directory);

    /* Exact difficulty names first. */
    if (diff != NULL && diff[0] != '\0' && strcmp(diff, "normal") != 0) {
        (void)snprintf(candidate, sizeof(candidate), "%s/data-%s.json", directory, diff);
        if (Song_TryChartFile(song, candidate)) return 1;
        if (snprintf(baseDiff, sizeof(baseDiff), "%s-%s.json", base, diff) < (int)sizeof(baseDiff)) {
            (void)snprintf(candidate, sizeof(candidate), "%s/%s", directory, baseDiff);
            if (Song_TryChartFile(song, candidate)) return 1;
        }
        (void)snprintf(candidate, sizeof(candidate), "%s/%s.json", directory, diff);
        if (Song_TryChartFile(song, candidate)) return 1;
    } else {
        (void)snprintf(candidate, sizeof(candidate), "%s/normal.json", directory);
        if (Song_TryChartFile(song, candidate)) return 1;
        if (snprintf(baseDiff, sizeof(baseDiff), "%s-normal.json", base) < (int)sizeof(baseDiff)) {
            (void)snprintf(candidate, sizeof(candidate), "%s/%s", directory, baseDiff);
            if (Song_TryChartFile(song, candidate)) return 1;
        }
    }

    /* Common converted/imported chart filenames. */
    (void)snprintf(candidate, sizeof(candidate), "%s/data.json", directory);
    if (Song_TryChartFile(song, candidate)) return 1;
    (void)snprintf(candidate, sizeof(candidate), "%s/%s.json", directory, base);
    if (Song_TryChartFile(song, candidate)) return 1;

    files = LoadDirectoryFilesEx(directory, ".json", false);

    /* Prefer the requested difficulty among arbitrary Psych filenames. */
    if (diff != NULL && diff[0] != '\0') {
        char suffix[64];
        (void)snprintf(suffix, sizeof(suffix), "-%s.json", diff);
        for (unsigned i = 0; i < files.count; ++i) {
            if (Song_IsEventsFile(files.paths[i])) continue;
            if (Song_EndsWithI(Song_BaseName(files.paths[i]), suffix) &&
                Song_TryChartFile(song, files.paths[i])) {
                loaded = 1;
                break;
            }
        }
    }

    /* Then try non-difficulty charts (usually the normal/default chart). */
    if (!loaded) {
        for (unsigned i = 0; i < files.count; ++i) {
            if (Song_IsEventsFile(files.paths[i]) || Song_IsDifficultyFile(files.paths[i])) continue;
            if (Song_TryChartFile(song, files.paths[i])) {
                loaded = 1;
                break;
            }
        }
    }

    /* Last resort: any valid difficulty chart in the folder. */
    if (!loaded) {
        for (unsigned i = 0; i < files.count; ++i) {
            if (Song_IsEventsFile(files.paths[i])) continue;
            if (Song_TryChartFile(song, files.paths[i])) {
                loaded = 1;
                break;
            }
        }
    }

    UnloadDirectoryFiles(files);
    return loaded;
}

static int Song_FindChartPathInDirectory(const char *directory, const char *diff,
                                         char out[SONG_PATH_MAX])
{
    char candidate[SONG_PATH_MAX];
    const char *base;
    FilePathList files;

    if (directory == NULL || !DirectoryExists(directory)) return 0;
    base = Song_BaseName(directory);

    if (diff != NULL && diff[0] != '\0' && strcmp(diff, "normal") != 0) {
        (void)snprintf(candidate, sizeof(candidate), "%s/data-%s.json", directory, diff);
        if (FileExists(candidate)) goto found_candidate;
        (void)snprintf(candidate, sizeof(candidate), "%s/%s-%s.json", directory, base, diff);
        if (FileExists(candidate)) goto found_candidate;
        (void)snprintf(candidate, sizeof(candidate), "%s/%s.json", directory, diff);
        if (FileExists(candidate)) goto found_candidate;
    }

    (void)snprintf(candidate, sizeof(candidate), "%s/data.json", directory);
    if (FileExists(candidate)) goto found_candidate;
    (void)snprintf(candidate, sizeof(candidate), "%s/%s.json", directory, base);
    if (FileExists(candidate)) goto found_candidate;

    files = LoadDirectoryFilesEx(directory, ".json", false);
    for (unsigned i = 0; i < files.count; ++i) {
        if (!Song_IsEventsFile(files.paths[i])) {
            Song_NormalizePath(files.paths[i], out, SONG_PATH_MAX);
            UnloadDirectoryFiles(files);
            return 1;
        }
    }
    UnloadDirectoryFiles(files);
    return 0;

found_candidate:
    Song_NormalizePath(candidate, out, SONG_PATH_MAX);
    return 1;
}

static int Song_ResolveChart(const char *dirPath, const char *diff,
                             char out[SONG_PATH_MAX])
{
    char normalized[SONG_PATH_MAX];
    char modRoot[SONG_PATH_MAX];
    char songName[256];
    char directory[SONG_PATH_MAX];

    if (dirPath == NULL || dirPath[0] == '\0' || out == NULL) return 0;
    Song_NormalizePath(dirPath, normalized, sizeof(normalized));
    (void)snprintf(songName, sizeof(songName), "%s", Song_BaseName(normalized));

    /* 1) New/imported layout: chart copied beside Inst.ogg in songs/<song>/. */
    if (Song_FindChartPathInDirectory(normalized, diff, out)) return 1;

    /* 2) Original Psych mod layout: assets/mods/<mod>/data/<song>/. */
    Song_GetModRoot(normalized, modRoot, sizeof(modRoot));
    if (modRoot[0] != '\0') {
        if (snprintf(directory, sizeof(directory), "%s/data/%s", modRoot, songName) < (int)sizeof(directory) &&
            Song_FindChartPathInDirectory(directory, diff, out)) return 1;
    }

    /* 3) Alternate converted layouts inside the song folder. */
    if (Song_PathJoin(directory, sizeof(directory), normalized, "charts") &&
        Song_FindChartPathInDirectory(directory, diff, out)) return 1;
    if (Song_PathJoin(directory, sizeof(directory), normalized, "data") &&
        Song_FindChartPathInDirectory(directory, diff, out)) return 1;

    /* 4) Base-game Psych layout. */
    if (snprintf(directory, sizeof(directory), "assets/data/%s", songName) < (int)sizeof(directory) &&
        Song_FindChartPathInDirectory(directory, diff, out)) return 1;

    return 0;
}

static int Song_TryLoadChart(const char *dirPath, const char *diff, Song *song)
{
    char normalized[SONG_PATH_MAX];
    char modRoot[SONG_PATH_MAX];
    char songName[256];
    char directory[SONG_PATH_MAX];

    if (dirPath == NULL || song == NULL) return 0;
    Song_NormalizePath(dirPath, normalized, sizeof(normalized));
    (void)snprintf(songName, sizeof(songName), "%s", Song_BaseName(normalized));

    if (Song_TryLoadChartDirectory(song, normalized, diff)) return 1;

    Song_GetModRoot(normalized, modRoot, sizeof(modRoot));
    if (modRoot[0] != '\0') {
        if (snprintf(directory, sizeof(directory), "%s/data/%s", modRoot, songName) < (int)sizeof(directory) &&
            Song_TryLoadChartDirectory(song, directory, diff)) return 1;
    }

    if (Song_PathJoin(directory, sizeof(directory), normalized, "charts") &&
        Song_TryLoadChartDirectory(song, directory, diff)) return 1;
    if (Song_PathJoin(directory, sizeof(directory), normalized, "data") &&
        Song_TryLoadChartDirectory(song, directory, diff)) return 1;

    if (snprintf(directory, sizeof(directory), "assets/data/%s", songName) < (int)sizeof(directory) &&
        Song_TryLoadChartDirectory(song, directory, diff)) return 1;

    return 0;
}

static int Song_TryLoadBinary(const char *dirPath, Song *song)
{
    char path[SONG_PATH_MAX];
    char normalized[SONG_PATH_MAX];
    char modRoot[SONG_PATH_MAX];
    char songName[256];

    if (dirPath == NULL || song == NULL) return 0;
    if (snprintf(path, sizeof(path), "%s/data.song", dirPath) < (int)sizeof(path) &&
        FileExists(path)) {
        Song_Parse(song, path);
        return 1;
    }

    Song_NormalizePath(dirPath, normalized, sizeof(normalized));
    Song_GetModRoot(normalized, modRoot, sizeof(modRoot));
    if (modRoot[0] != '\0') {
        (void)snprintf(songName, sizeof(songName), "%s", Song_BaseName(normalized));
        if (snprintf(path, sizeof(path), "%s/data/%s/data.song", modRoot, songName) < (int)sizeof(path) &&
            FileExists(path)) {
            Song_Parse(song, path);
            return 1;
        }
    }
    return 0;
}

void Song_LoadSong(Song *song, const char *songName)
{
    char directory[SONG_PATH_MAX];
    if (song == NULL || songName == NULL) Log_Fatal("invalid song name");
    if (snprintf(directory, sizeof(directory), "assets/songs/%s", songName) >= (int)sizeof(directory))
        Log_Fatal("song path too long");
    Song_LoadSongFromDir(song, directory);
}

void Song_LoadSongFromDir(Song *song, const char *dirPath)
{
    if (song == NULL || dirPath == NULL) Log_Fatal("invalid song directory");
    memset(song, 0, sizeof(*song));
    if (Song_TryLoadChart(dirPath, "", song)) return;
    if (Song_TryLoadBinary(dirPath, song)) return;
    Log_FatalF("no valid chart found for song directory: %s", dirPath);
}

void Song_LoadSongFromDirDiff(Song *song, const char *dirPath, const char *diff)
{
    if (song == NULL || dirPath == NULL) Log_Fatal("invalid song directory");
    memset(song, 0, sizeof(*song));
    if (Song_TryLoadChart(dirPath, diff, song)) return;
    if (Song_TryLoadBinary(dirPath, song)) return;
    Log_FatalF("no valid chart found for song directory: %s (difficulty: %s)",
               dirPath, diff != NULL ? diff : "default");
}

char Song_HasChart(const char *dirPath)
{
    char path[SONG_PATH_MAX];
    char normalized[SONG_PATH_MAX];
    char modRoot[SONG_PATH_MAX];
    char songName[256];
    if (dirPath == NULL || dirPath[0] == '\0') return 0;
    if (Song_ResolveChart(dirPath, "", path)) return 1;
    if (snprintf(path, sizeof(path), "%s/data.song", dirPath) < (int)sizeof(path) &&
        FileExists(path)) return 1;

    Song_NormalizePath(dirPath, normalized, sizeof(normalized));
    Song_GetModRoot(normalized, modRoot, sizeof(modRoot));
    if (modRoot[0] != '\0') {
        (void)snprintf(songName, sizeof(songName), "%s", Song_BaseName(normalized));
        if (snprintf(path, sizeof(path), "%s/data/%s/data.song", modRoot, songName) < (int)sizeof(path) &&
            FileExists(path)) return 1;
    }
    return 0;
}
