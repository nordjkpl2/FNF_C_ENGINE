/*
 * Friday Night Funkin' C Engine (c) 2026 nordjkpl
 * ModImport.c - importacao, inventario e remocao de mods.
 *
 * Layout de destino:
 *   assets/mods/<modName>/{songs,weeks,stages,characters,images}
 *
 * Dependencias: raylib (via std.h) e yyjson.
 * A conversao Sparrow XML -> .animset e feita apenas durante a importacao.
 */

#include "std.h"
#include "scenes/ModImport.h"
#include "yyjson.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <direct.h>
    #define MI_MKDIR(path) _mkdir(path)
    #define MI_RMDIR(path) _rmdir(path)
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #define MI_MKDIR(path) mkdir((path), 0777)
    #define MI_RMDIR(path) rmdir(path)
#endif

#define MI_PATH_MAX          1024
#define MI_NAME_MAX          128
#define MI_MAX_XML_FRAMES    16384
#define MI_MAX_CHAR_ANIMS    64
#define MI_MAX_ANIM_FRAMES   256
#define MI_MAX_ICON_NAMES    128
#define MI_MAX_DEPTH         32
#define MI_COPY_BUFFER       65536u

/* Formato binario consumido pelo AnimationSet_LoadAnimations():
 * int32 animationCount; por animacao: int32 nameLength, name bytes,
 * int32 dataCount (= frames * 6), seguido por seis int32 por frame.
 */
typedef struct MI_XmlFrame {
    char name[128];
    int32_t x, y, w, h, fx, fy;
} MI_XmlFrame;

typedef struct MI_CharAnim {
    char role[32];
    char prefix[64];
    int32_t indices[MI_MAX_ANIM_FRAMES];
    int indexCount;
} MI_CharAnim;

typedef struct MI_CharInfo {
    char healthicon[64];
    char imagePath[192];
    MI_CharAnim anims[MI_MAX_CHAR_ANIMS];
    int animCount;
} MI_CharInfo;

typedef struct MI_FrameCandidate {
    int frameIndex;
    uint64_t order;
} MI_FrameCandidate;

typedef struct MI_NameList {
    char names[MI_MAX_ICON_NAMES][64];
    int count;
} MI_NameList;

typedef struct MI_ImportContext {
    const char *srcRoot;
    const char *dstRoot;
    ModImportStats *stats;
    MI_NameList iconNames;
} MI_ImportContext;

/* ------------------------------------------------------------------------- */
/* Caminhos e nomes                                                          */
/* ------------------------------------------------------------------------- */

static const char *MI_BaseName(const char *path)
{
    const char *slash1;
    const char *slash2;
    const char *base;

    if (path == NULL) return "";
    slash1 = strrchr(path, '/');
    slash2 = strrchr(path, '\\');
    base = path;
    if (slash1 != NULL && (base == path || slash1 > base)) base = slash1 + 1;
    if (slash2 != NULL && (base == path || slash2 > base)) base = slash2 + 1;
    return base;
}

static int MI_CaseEqual(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static int MI_EndsWithI(const char *text, const char *suffix)
{
    size_t textLen;
    size_t suffixLen;
    if (text == NULL || suffix == NULL) return 0;
    textLen = strlen(text);
    suffixLen = strlen(suffix);
    return suffixLen <= textLen &&
           MI_CaseEqual(text + textLen - suffixLen, suffix);
}

static int MI_IsSafeComponent(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;
    if (name == NULL || name[0] == '\0') return 0;
    if (strstr(name, "..") != NULL) return 0;
    for (; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p < 32) return 0;
    }
    return 1;
}

static void MI_SanitizeModName(char *dst, size_t dstSize, const char *src)
{
    size_t out = 0;
    if (dst == NULL || dstSize == 0) return;
    if (src == NULL) src = "mod";

    for (size_t i = 0; src[i] != '\0' && out + 1 < dstSize; ++i) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            dst[out++] = (char)c;
        } else if (c == ' ' || c == '.') {
            dst[out++] = '_';
        }
    }

    while (out > 0 && dst[out - 1] == '_') --out;
    if (out == 0) {
        const char fallback[] = "mod";
        size_t n = sizeof(fallback) - 1;
        if (n >= dstSize) n = dstSize - 1;
        memcpy(dst, fallback, n);
        out = n;
    }
    dst[out] = '\0';
}

static void MI_CopyString(char *dst, size_t dstSize, const char *src)
{
    size_t length = 0;
    if (dst == NULL || dstSize == 0) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    while (length + 1u < dstSize && src[length] != '\0') ++length;
    memcpy(dst, src, length);
    dst[length] = '\0';
}

static int MI_Join(char *out, size_t outSize, const char *a, const char *b)
{
    int written;
    size_t len;
    if (out == NULL || outSize == 0 || a == NULL || b == NULL) return 0;
    len = strlen(a);
    if (len > 0 && (a[len - 1] == '/' || a[len - 1] == '\\'))
        written = snprintf(out, outSize, "%s%s", a, b);
    else
        written = snprintf(out, outSize, "%s/%s", a, b);
    return written >= 0 && (size_t)written < outSize;
}

static int MI_EnsureDirectory(const char *path)
{
    char buffer[MI_PATH_MAX];
    size_t len;

    if (path == NULL || path[0] == '\0') return 0;
    len = strlen(path);
    if (len >= sizeof(buffer)) return 0;
    memcpy(buffer, path, len + 1);

    for (size_t i = 0; i < len; ++i) {
        if (buffer[i] == '\\') buffer[i] = '/';
    }

    for (size_t i = 1; i <= len; ++i) {
        if (buffer[i] == '/' || buffer[i] == '\0') {
            char saved = buffer[i];
            buffer[i] = '\0';
            if (buffer[0] != '\0' && !DirectoryExists(buffer)) {
                (void)MI_MKDIR(buffer);
                if (!DirectoryExists(buffer)) {
                    buffer[i] = saved;
                    return 0;
                }
            }
            buffer[i] = saved;
        }
    }
    return DirectoryExists(buffer);
}

static int MI_EnsureParent(const char *filePath)
{
    char parent[MI_PATH_MAX];
    char *slash;
    size_t len;
    if (filePath == NULL) return 0;
    len = strlen(filePath);
    if (len >= sizeof(parent)) return 0;
    memcpy(parent, filePath, len + 1);
    slash = strrchr(parent, '/');
    {
        char *backslash = strrchr(parent, '\\');
        if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    }
    if (slash == NULL) return 1;
    *slash = '\0';
    return MI_EnsureDirectory(parent);
}

static int MI_IsModsRoot(const char *root)
{
    size_t len;
    size_t modsStart;
    size_t assetsEnd;
    size_t assetsStart;
    if (root == NULL || root[0] == '\0' || strstr(root, "..") != NULL) return 0;
    len = strlen(root);
    while (len > 0 && (root[len - 1] == '/' || root[len - 1] == '\\')) --len;
    if (len < 11u) return 0;

    modsStart = len - 4u;
    if (modsStart == 0 || (root[modsStart - 1] != '/' && root[modsStart - 1] != '\\')) return 0;
    if (memcmp(root + modsStart, "mods", 4) != 0 &&
        memcmp(root + modsStart, "MODS", 4) != 0) {
        for (size_t i = 0; i < 4u; ++i) {
            unsigned char c = (unsigned char)root[modsStart + i];
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
            if (c != (unsigned char)"mods"[i]) return 0;
        }
    }

    assetsEnd = modsStart - 1u;
    assetsStart = assetsEnd;
    while (assetsStart > 0 && root[assetsStart - 1u] != '/' &&
           root[assetsStart - 1u] != '\\') --assetsStart;
    if (assetsEnd - assetsStart != 6u) return 0;
    for (size_t i = 0; i < 6u; ++i) {
        unsigned char c = (unsigned char)root[assetsStart + i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        if (c != (unsigned char)"assets"[i]) return 0;
    }
    return 1;
}

/* A pasta selecionada pode ser um mod individual ou a pasta-container "mods".
 * Quando ela ja contem a estrutura do jogo, ela propria e o mod (assets/mods).
 */
static int MI_HasModLayout(const char *root)
{
    static const char *const folders[] = {
        "songs", "weeks", "stages", "characters", "images", "data"
    };
    char path[MI_PATH_MAX];
    if (root == NULL || !DirectoryExists(root)) return 0;
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); ++i) {
        if (MI_Join(path, sizeof(path), root, folders[i]) && DirectoryExists(path))
            return 1;
    }
    return 0;
}

static int MI_IsNameMods(const char *name)
{
    return MI_CaseEqual(name, "mods");
}

static void MI_StatsAdd(ModImportStats *dst, const ModImportStats *src)
{
    if (dst == NULL || src == NULL) return;
    dst->songs += src->songs;
    dst->charts += src->charts;
    dst->weeks += src->weeks;
    dst->stages += src->stages;
    dst->images += src->images;
    dst->characters += src->characters;
    dst->animsets += src->animsets;
    dst->icons += src->icons;
    if (src->bytes > 0 && dst->bytes <= LONG_MAX - src->bytes)
        dst->bytes += src->bytes;
    dst->skippedEvents += src->skippedEvents;
    dst->skippedLua += src->skippedLua;
}

static void MI_GetStem(const char *path, char *out, size_t outSize)
{
    const char *base = MI_BaseName(path);
    size_t len;
    char *dot;
    if (out == NULL || outSize == 0) return;
    len = strlen(base);
    if (len >= outSize) len = outSize - 1;
    memcpy(out, base, len);
    out[len] = '\0';
    dot = strrchr(out, '.');
    if (dot != NULL && dot != out) *dot = '\0';
}

/* ------------------------------------------------------------------------- */
/* Arquivos e diretorios                                                     */
/* ------------------------------------------------------------------------- */

static long MI_FileSize(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    if (file == NULL) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    size = ftell(file);
    fclose(file);
    return size > 0 ? size : 0;
}

static char *MI_ReadFile(const char *path, long *outSize, long maxSize)
{
    FILE *file;
    long size;
    char *buffer;

    if (outSize != NULL) *outSize = 0;
    file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size <= 0 || size > maxSize || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char *)malloc((size_t)size + 1u);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return NULL;
    }
    fclose(file);
    buffer[size] = '\0';
    if (outSize != NULL) *outSize = size;
    return buffer;
}

static char MI_CopyFile(const char *source, const char *destination, long *bytes)
{
    FILE *in;
    FILE *out;
    unsigned char buffer[MI_COPY_BUFFER];
    size_t count;
    long total = 0;
    int ok = 1;

    if (source == NULL || destination == NULL || !FileExists(source)) return 0;
    if (strcmp(source, destination) == 0) return 1;
    if (!MI_EnsureParent(destination)) return 0;

    in = fopen(source, "rb");
    if (in == NULL) return 0;
    out = fopen(destination, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }

    while ((count = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, count, out) != count) {
            ok = 0;
            break;
        }
        if (total <= LONG_MAX - (long)count) total += (long)count;
        else {
            ok = 0;
            break;
        }
    }
    if (ferror(in)) ok = 0;
    if (fclose(in) != 0) ok = 0;
    if (fclose(out) != 0) ok = 0;

    if (!ok) {
        (void)remove(destination);
        return 0;
    }
    if (bytes != NULL) *bytes += total;
    return 1;
}

static int MI_IsDirectoryPath(const char *path)
{
    return path != NULL && DirectoryExists(path);
}

static FilePathList MI_ListDirectories(const char *path)
{
    return LoadDirectoryFilesEx(path, "DIR", false);
}

static FilePathList MI_ListEntries(const char *path)
{
    return LoadDirectoryFilesEx(path, NULL, false);
}

static int MI_CopyTree(const char *source, const char *destination,
                       long *bytes, int *copiedFiles, int depth)
{
    FilePathList dirs;
    FilePathList entries;
    int ok = 1;

    if (depth > MI_MAX_DEPTH) return 0;
    if (!MI_EnsureDirectory(destination)) return 0;

    dirs = MI_ListDirectories(source);
    for (unsigned i = 0; i < dirs.count; ++i) {
        char childDst[MI_PATH_MAX];
        const char *base = MI_BaseName(dirs.paths[i]);
        if (!MI_IsSafeComponent(base)) continue;
        if (!MI_Join(childDst, sizeof(childDst), destination, base) ||
            !MI_CopyTree(dirs.paths[i], childDst, bytes, copiedFiles, depth + 1)) {
            ok = 0;
        }
    }
    UnloadDirectoryFiles(dirs);

    entries = MI_ListEntries(source);
    for (unsigned i = 0; i < entries.count; ++i) {
        char childDst[MI_PATH_MAX];
        const char *base = MI_BaseName(entries.paths[i]);
        if (MI_IsDirectoryPath(entries.paths[i]) || !MI_IsSafeComponent(base)) continue;
        if (!MI_Join(childDst, sizeof(childDst), destination, base) ||
            !MI_CopyFile(entries.paths[i], childDst, bytes)) {
            ok = 0;
        } else if (copiedFiles != NULL) {
            ++*copiedFiles;
        }
    }
    UnloadDirectoryFiles(entries);
    return ok;
}

static long MI_CountTreeBytes(const char *path, int depth)
{
    FilePathList dirs;
    FilePathList entries;
    long total = 0;
    if (depth > MI_MAX_DEPTH) return 0;

    entries = MI_ListEntries(path);
    for (unsigned i = 0; i < entries.count; ++i) {
        if (!MI_IsDirectoryPath(entries.paths[i])) {
            long size = MI_FileSize(entries.paths[i]);
            if (size > 0 && total <= LONG_MAX - size) total += size;
        }
    }
    UnloadDirectoryFiles(entries);

    dirs = MI_ListDirectories(path);
    for (unsigned i = 0; i < dirs.count; ++i) {
        long size = MI_CountTreeBytes(dirs.paths[i], depth + 1);
        if (size > 0 && total <= LONG_MAX - size) total += size;
    }
    UnloadDirectoryFiles(dirs);
    return total;
}

static int MI_RemoveTree(const char *path, int depth, int *removedFiles)
{
    FilePathList entries;
    FilePathList dirs;
    int ok = 1;

    if (depth > MI_MAX_DEPTH) return 0;

    entries = MI_ListEntries(path);
    for (unsigned i = 0; i < entries.count; ++i) {
        if (MI_IsDirectoryPath(entries.paths[i])) continue;
        if (remove(entries.paths[i]) == 0) {
            if (removedFiles != NULL) ++*removedFiles;
        } else {
            ok = 0;
        }
    }
    UnloadDirectoryFiles(entries);

    dirs = MI_ListDirectories(path);
    for (unsigned i = 0; i < dirs.count; ++i) {
        if (!MI_RemoveTree(dirs.paths[i], depth + 1, removedFiles)) ok = 0;
        if (MI_RMDIR(dirs.paths[i]) != 0 && DirectoryExists(dirs.paths[i])) ok = 0;
    }
    UnloadDirectoryFiles(dirs);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* JSON                                                                       */
/* ------------------------------------------------------------------------- */

static const char *MI_JsonString(yyjson_val *object, const char *key)
{
    yyjson_val *value;
    if (object == NULL || !yyjson_is_obj(object)) return NULL;
    value = yyjson_obj_get(object, key);
    if (!yyjson_is_str(value)) return NULL;
    return yyjson_get_str(value);
}

static void MI_AddName(MI_NameList *list, const char *name)
{
    char candidate[64];
    size_t len;
    if (list == NULL || name == NULL || !MI_IsSafeComponent(name)) return;
    len = strlen(name);
    if (len == 0 || len >= sizeof(candidate)) return;
    memcpy(candidate, name, len + 1);

    for (int i = 0; i < list->count; ++i) {
        if (MI_CaseEqual(list->names[i], candidate)) return;
    }
    if (list->count < MI_MAX_ICON_NAMES) {
        memcpy(list->names[list->count], candidate, len + 1);
        ++list->count;
    }
}

static void MI_ParseChartActors(const char *chartPath, MI_NameList *names)
{
    long size = 0;
    char *buffer = MI_ReadFile(chartPath, &size, 8L * 1024L * 1024L);
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *song;
    static const char *keys[] = {"player1", "player2"};

    if (buffer == NULL) return;
    doc = yyjson_read(buffer, (size_t)size, 0);
    free(buffer);
    if (doc == NULL) return;

    root = yyjson_doc_get_root(doc);
    song = yyjson_obj_get(root, "song");
    if (!yyjson_is_obj(song)) song = root;

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const char *value = MI_JsonString(song, keys[i]);
        MI_AddName(names, value);
    }
    yyjson_doc_free(doc);
}

static void MI_ParseWeekIcons(const char *weekPath, MI_NameList *names)
{
    long size = 0;
    char *buffer = MI_ReadFile(weekPath, &size, 2L * 1024L * 1024L);
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *songs;
    yyjson_val *weekCharacters;

    if (buffer == NULL) return;
    doc = yyjson_read(buffer, (size_t)size, 0);
    free(buffer);
    if (doc == NULL) return;

    root = yyjson_doc_get_root(doc);
    songs = yyjson_obj_get(root, "songs");
    if (yyjson_is_arr(songs)) {
        size_t i, max;
        yyjson_val *row;
        yyjson_arr_foreach(songs, i, max, row) {
            if (!yyjson_is_arr(row) || yyjson_arr_size(row) < 2) continue;
            {
                yyjson_val *icon = yyjson_arr_get(row, 1);
                if (yyjson_is_str(icon)) MI_AddName(names, yyjson_get_str(icon));
            }
        }
    }

    weekCharacters = yyjson_obj_get(root, "weekCharacters");
    if (yyjson_is_arr(weekCharacters)) {
        size_t i, max;
        yyjson_val *value;
        yyjson_arr_foreach(weekCharacters, i, max, value) {
            if (yyjson_is_str(value)) MI_AddName(names, yyjson_get_str(value));
        }
    }
    yyjson_doc_free(doc);
}

static int MI_ParseCharacterJson(const char *jsonPath, const char *fallbackName,
                                 MI_CharInfo *out)
{
    long size = 0;
    char *buffer;
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *animations;
    const char *healthicon;
    const char *image;

    if (out == NULL) return 0;
    memset(out, 0, sizeof(*out));
    if (fallbackName != NULL) {
        MI_CopyString(out->healthicon, sizeof(out->healthicon), fallbackName);
        (void)snprintf(out->imagePath, sizeof(out->imagePath), "characters/%s", fallbackName);
    }

    buffer = MI_ReadFile(jsonPath, &size, 1024L * 1024L);
    if (buffer == NULL) return 0;
    doc = yyjson_read(buffer, (size_t)size, 0);
    free(buffer);
    if (doc == NULL) return 0;
    root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return 0;
    }

    healthicon = MI_JsonString(root, "healthicon");
    if (healthicon != NULL && MI_IsSafeComponent(healthicon))
        (void)snprintf(out->healthicon, sizeof(out->healthicon), "%s", healthicon);

    image = MI_JsonString(root, "image");
    if (image != NULL && image[0] != '\0' && strstr(image, "..") == NULL &&
        strchr(image, '\\') == NULL) {
        const char *normalized = image;
        size_t len;
        if (strncmp(normalized, "assets/", 7) == 0) normalized += 7;
        if (strncmp(normalized, "images/", 7) == 0) normalized += 7;
        len = strlen(normalized);
        if (len < sizeof(out->imagePath)) {
            memcpy(out->imagePath, normalized, len + 1);
            {
                char *dot = strrchr(out->imagePath, '.');
                if (dot != NULL) *dot = '\0';
            }
        }
    }

    animations = yyjson_obj_get(root, "animations");
    if (yyjson_is_arr(animations)) {
        size_t i, max;
        yyjson_val *entry;
        yyjson_arr_foreach(animations, i, max, entry) {
            const char *role;
            const char *prefix;
            MI_CharAnim *anim;
            yyjson_val *indices;
            if (out->animCount >= MI_MAX_CHAR_ANIMS || !yyjson_is_obj(entry)) break;
            role = MI_JsonString(entry, "anim");
            prefix = MI_JsonString(entry, "name");
            if (role == NULL || role[0] == '\0' || prefix == NULL || prefix[0] == '\0') continue;
            if (strlen(prefix) >= sizeof(out->anims[0].prefix) ||
                strlen(role) >= sizeof(out->anims[0].role)) continue;

            anim = &out->anims[out->animCount];
            (void)snprintf(anim->role, sizeof(anim->role), "%s", role);
            (void)snprintf(anim->prefix, sizeof(anim->prefix), "%s", prefix);
            indices = yyjson_obj_get(entry, "indices");
            if (yyjson_is_arr(indices)) {
                size_t k, kmax;
                yyjson_val *indexValue;
                yyjson_arr_foreach(indices, k, kmax, indexValue) {
                    if (anim->indexCount >= MI_MAX_ANIM_FRAMES) break;
                    if (yyjson_is_int(indexValue)) {
                        int64_t index = yyjson_get_int(indexValue);
                        if (index >= 0 && index <= INT_MAX)
                            anim->indices[anim->indexCount++] = (int32_t)index;
                    }
                }
            }
            ++out->animCount;
        }
    }

    yyjson_doc_free(doc);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Sparrow XML -> .animset                                                    */
/* ------------------------------------------------------------------------- */

static int MI_ParseInteger(const char *text, int32_t *out)
{
    char *end;
    long value;
    if (text == NULL || text[0] == '\0' || out == NULL) return 0;
    errno = 0;
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE ||
        value < INT32_MIN || value > INT32_MAX) return 0;
    *out = (int32_t)value;
    return 1;
}

static int MI_XmlAttribute(const char *tagStart, const char *tagEnd,
                           const char *attribute, char *out, size_t outSize)
{
    const char *p = tagStart;
    size_t attrLen;
    if (out == NULL || outSize == 0 || tagStart == NULL || tagEnd == NULL ||
        attribute == NULL) return 0;
    out[0] = '\0';
    attrLen = strlen(attribute);

    while (p < tagEnd && *p != '<') ++p;
    if (p < tagEnd) ++p;
    while (p < tagEnd && !isspace((unsigned char)*p) && *p != '>') ++p;

    while (p < tagEnd) {
        const char *nameStart;
        size_t nameLen;
        char quote;
        size_t valueLen;

        while (p < tagEnd && (isspace((unsigned char)*p) || *p == '/')) ++p;
        if (p >= tagEnd || *p == '>') break;
        nameStart = p;
        while (p < tagEnd && !isspace((unsigned char)*p) && *p != '=' && *p != '>') ++p;
        nameLen = (size_t)(p - nameStart);
        while (p < tagEnd && isspace((unsigned char)*p)) ++p;
        if (p >= tagEnd || *p != '=') {
            while (p < tagEnd && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p;
            continue;
        }
        ++p;
        while (p < tagEnd && isspace((unsigned char)*p)) ++p;
        if (p >= tagEnd || (*p != '\'' && *p != '"')) continue;
        quote = *p++;
        {
            const char *valueStart = p;
            while (p < tagEnd && *p != quote) ++p;
            valueLen = (size_t)(p - valueStart);
            if (nameLen == attrLen && memcmp(nameStart, attribute, attrLen) == 0) {
                if (valueLen >= outSize) valueLen = outSize - 1;
                memcpy(out, valueStart, valueLen);
                out[valueLen] = '\0';
                return valueLen > 0;
            }
        }
        if (p < tagEnd) ++p;
    }
    return 0;
}

static int MI_ParseXmlFrames(const char *xml, MI_XmlFrame *frames, int capacity)
{
    const char *p = xml;
    int count = 0;
    if (xml == NULL || frames == NULL || capacity <= 0) return 0;

    while (count < capacity) {
        const char *tag = strstr(p, "<SubTexture");
        const char *tagEnd;
        char value[128];
        MI_XmlFrame frame;

        if (tag == NULL) break;
        if (!(isspace((unsigned char)tag[11]) || tag[11] == '>' || tag[11] == '/')) {
            p = tag + 11;
            continue;
        }
        tagEnd = strchr(tag, '>');
        if (tagEnd == NULL) break;
        memset(&frame, 0, sizeof(frame));

        if (!MI_XmlAttribute(tag, tagEnd, "name", frame.name, sizeof(frame.name)) ||
            !MI_XmlAttribute(tag, tagEnd, "x", value, sizeof(value)) ||
            !MI_ParseInteger(value, &frame.x) ||
            !MI_XmlAttribute(tag, tagEnd, "y", value, sizeof(value)) ||
            !MI_ParseInteger(value, &frame.y) ||
            !MI_XmlAttribute(tag, tagEnd, "width", value, sizeof(value)) ||
            !MI_ParseInteger(value, &frame.w) ||
            !MI_XmlAttribute(tag, tagEnd, "height", value, sizeof(value)) ||
            !MI_ParseInteger(value, &frame.h) || frame.w <= 0 || frame.h <= 0) {
            p = tagEnd + 1;
            continue;
        }

        if (MI_XmlAttribute(tag, tagEnd, "frameX", value, sizeof(value)))
            (void)MI_ParseInteger(value, &frame.fx);
        if (MI_XmlAttribute(tag, tagEnd, "frameY", value, sizeof(value)))
            (void)MI_ParseInteger(value, &frame.fy);

        frames[count++] = frame;
        p = tagEnd + 1;
    }
    return count;
}

static int MI_FrameSuffix(const char *frameName, const char *prefix, uint64_t *order)
{
    const char *suffix;
    uint64_t value = 0;
    if (strncmp(frameName, prefix, strlen(prefix)) != 0) return 0;
    suffix = frameName + strlen(prefix);
    if (*suffix == '\0') {
        *order = 0;
        return 1;
    }
    if (*suffix == '.') ++suffix;
    if (*suffix == '\0') return 0;
    for (; *suffix; ++suffix) {
        unsigned digit;
        if (*suffix < '0' || *suffix > '9') return 0;
        digit = (unsigned)(*suffix - '0');
        if (value > (UINT64_MAX - digit) / 10u) return 0;
        value = value * 10u + digit;
    }
    *order = value;
    return 1;
}

static int MI_CompareFrameCandidate(const void *left, const void *right)
{
    const MI_FrameCandidate *a = (const MI_FrameCandidate *)left;
    const MI_FrameCandidate *b = (const MI_FrameCandidate *)right;
    if (a->order < b->order) return -1;
    if (a->order > b->order) return 1;
    return a->frameIndex < b->frameIndex ? -1 : a->frameIndex > b->frameIndex;
}

static int MI_WriteExact(FILE *file, const void *data, size_t size)
{
    return size == 0 || fwrite(data, 1, size, file) == size;
}

static int MI_WriteAnimset(const MI_CharInfo *info, const char *xmlPath,
                           const char *animsetPath)
{
    long xmlSize = 0;
    char *xml = NULL;
    MI_XmlFrame *frames = NULL;
    MI_FrameCandidate *candidates = NULL;
    int32_t *selected = NULL;
    int *counts = NULL;
    int frameCount;
    int kept = 0;
    int written = 0;
    char tempPath[MI_PATH_MAX] = {0};
    FILE *out = NULL;
    int ok = 0;

    if (info == NULL || info->animCount <= 0 || xmlPath == NULL || animsetPath == NULL)
        return 0;
    xml = MI_ReadFile(xmlPath, &xmlSize, 16L * 1024L * 1024L);
    if (xml == NULL) goto cleanup;

    frames = (MI_XmlFrame *)malloc(sizeof(*frames) * MI_MAX_XML_FRAMES);
    if (frames == NULL) goto cleanup;
    frameCount = MI_ParseXmlFrames(xml, frames, MI_MAX_XML_FRAMES);
    if (frameCount <= 0) goto cleanup;

    candidates = (MI_FrameCandidate *)malloc(sizeof(*candidates) * (size_t)frameCount);
    selected = (int32_t *)malloc(sizeof(*selected) *
               (size_t)MI_MAX_CHAR_ANIMS * MI_MAX_ANIM_FRAMES);
    counts = (int *)calloc((size_t)MI_MAX_CHAR_ANIMS, sizeof(*counts));
    if (candidates == NULL || selected == NULL || counts == NULL) goto cleanup;

    for (int a = 0; a < info->animCount; ++a) {
        const MI_CharAnim *anim = &info->anims[a];
        int candidateCount = 0;
        int outputCount = 0;

        for (int f = 0; f < frameCount; ++f) {
            uint64_t order;
            if (MI_FrameSuffix(frames[f].name, anim->prefix, &order)) {
                candidates[candidateCount].frameIndex = f;
                candidates[candidateCount].order = order;
                ++candidateCount;
            }
        }
        if (candidateCount == 0) continue;
        qsort(candidates, (size_t)candidateCount, sizeof(*candidates),
              MI_CompareFrameCandidate);

        if (anim->indexCount > 0) {
            for (int k = 0; k < anim->indexCount && outputCount < MI_MAX_ANIM_FRAMES; ++k) {
                int index = anim->indices[k];
                if (index >= 0 && index < candidateCount)
                    selected[(size_t)a * MI_MAX_ANIM_FRAMES + outputCount++] =
                        candidates[index].frameIndex;
            }
        } else {
            int take = candidateCount < MI_MAX_ANIM_FRAMES ? candidateCount : MI_MAX_ANIM_FRAMES;
            for (int k = 0; k < take; ++k)
                selected[(size_t)a * MI_MAX_ANIM_FRAMES + outputCount++] =
                    candidates[k].frameIndex;
        }
        counts[a] = outputCount;
        if (outputCount > 0) ++kept;
    }
    if (kept == 0) goto cleanup;

    if (snprintf(tempPath, sizeof(tempPath), "%s.tmp", animsetPath) >= (int)sizeof(tempPath))
        goto cleanup;
    if (!MI_EnsureParent(tempPath)) goto cleanup;
    out = fopen(tempPath, "wb");
    if (out == NULL) goto cleanup;

    {
        int32_t animationCount = (int32_t)kept;
        if (!MI_WriteExact(out, &animationCount, sizeof(animationCount))) goto cleanup;
    }

    for (int a = 0; a < info->animCount; ++a) {
        const MI_CharAnim *anim = &info->anims[a];
        int32_t nameLength;
        int32_t dataCount;
        if (counts[a] <= 0) continue;
        nameLength = (int32_t)strlen(anim->prefix);
        dataCount = (int32_t)(counts[a] * 6);
        if (!MI_WriteExact(out, &nameLength, sizeof(nameLength)) ||
            !MI_WriteExact(out, anim->prefix, (size_t)nameLength) ||
            !MI_WriteExact(out, &dataCount, sizeof(dataCount))) goto cleanup;

        for (int k = 0; k < counts[a]; ++k) {
            const MI_XmlFrame *frame = &frames[
                selected[(size_t)a * MI_MAX_ANIM_FRAMES + k]];
            int32_t packed[6] = {
                frame->x, frame->y, frame->w, frame->h, frame->fx, frame->fy
            };
            if (!MI_WriteExact(out, packed, sizeof(packed))) goto cleanup;
        }
        ++written;
    }

    if (ferror(out)) goto cleanup;
    if (fclose(out) != 0) {
        out = NULL;
        goto cleanup;
    }
    out = NULL;

    (void)remove(animsetPath);
    if (rename(tempPath, animsetPath) != 0) goto cleanup;
    ok = written;

cleanup:
    if (out != NULL) fclose(out);
    if (!ok && tempPath[0] != '\0') (void)remove(tempPath);
    free(counts);
    free(selected);
    free(candidates);
    free(frames);
    free(xml);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Icones e importacao de recursos                                            */
/* ------------------------------------------------------------------------- */

static int MI_CopyIconAlias(const char *srcMod, const char *dstMod,
                            const char *iconKey, long *bytes)
{
    char srcIcons[MI_PATH_MAX];
    char dstIcons[MI_PATH_MAX];
    char source[MI_PATH_MAX];
    char destination[MI_PATH_MAX];
    char candidate[96];
    const char *names[2];

    if (!MI_IsSafeComponent(iconKey)) return 0;
    if (!MI_Join(srcIcons, sizeof(srcIcons), srcMod, "images/icons") ||
        !MI_Join(dstIcons, sizeof(dstIcons), dstMod, "images/icons")) return 0;
    if (!MI_EnsureDirectory(dstIcons)) return 0;
    if (snprintf(destination, sizeof(destination), "%s/%s.png", dstIcons, iconKey) >=
        (int)sizeof(destination)) return 0;

    names[0] = iconKey;
    names[1] = NULL;
    if (strncmp(iconKey, "icon-", 5) != 0) {
        (void)snprintf(candidate, sizeof(candidate), "icon-%s", iconKey);
        names[1] = candidate;
    }

    for (int i = 0; i < 2; ++i) {
        if (names[i] == NULL) continue;
        if (snprintf(source, sizeof(source), "%s/%s.png", srcIcons, names[i]) >=
            (int)sizeof(source)) continue;
        if (FileExists(source) && MI_CopyFile(source, destination, bytes)) return 1;
    }
    return 0;
}

static void MI_CopyCollectedIcons(MI_ImportContext *context)
{
    for (int i = 0; i < context->iconNames.count; ++i) {
        if (MI_CopyIconAlias(context->srcRoot, context->dstRoot,
                             context->iconNames.names[i], &context->stats->bytes)) {
            ++context->stats->icons;
        }
        /* A chave de week como "icon-charlie" tambem pode ser pedida sem prefixo
         * pelo HUD de personagem. Preservamos as duas formas como alias. */
        if (strncmp(context->iconNames.names[i], "icon-", 5) == 0 &&
            context->iconNames.names[i][5] != '\0') {
            if (MI_CopyIconAlias(context->srcRoot, context->dstRoot,
                                 context->iconNames.names[i] + 5,
                                 &context->stats->bytes)) {
                ++context->stats->icons;
            }
        }
    }
}

static int MI_CopyWeeksRecursive(const char *sourceDir, const char *destinationDir,
                                 MI_ImportContext *context, int depth)
{
    FilePathList entries;
    FilePathList dirs;
    int ok = 1;
    if (depth > MI_MAX_DEPTH || !DirectoryExists(sourceDir)) return depth <= MI_MAX_DEPTH;
    if (!MI_EnsureDirectory(destinationDir)) return 0;

    entries = MI_ListEntries(sourceDir);
    for (unsigned i = 0; i < entries.count; ++i) {
        const char *base = MI_BaseName(entries.paths[i]);
        char destination[MI_PATH_MAX];
        if (MI_IsDirectoryPath(entries.paths[i]) || !MI_EndsWithI(base, ".json")) continue;
        if (!MI_IsSafeComponent(base) || !MI_Join(destination, sizeof(destination), destinationDir, base)) {
            ok = 0;
            continue;
        }
        MI_ParseWeekIcons(entries.paths[i], &context->iconNames);
        if (MI_CopyFile(entries.paths[i], destination, &context->stats->bytes))
            ++context->stats->weeks;
        else
            ok = 0;
    }
    UnloadDirectoryFiles(entries);

    dirs = MI_ListDirectories(sourceDir);
    for (unsigned i = 0; i < dirs.count; ++i) {
        if (!MI_CopyWeeksRecursive(dirs.paths[i], destinationDir, context, depth + 1)) ok = 0;
    }
    UnloadDirectoryFiles(dirs);
    return ok;
}

static int MI_CopyStagesRecursive(const char *sourceDir, const char *destinationDir,
                                  ModImportStats *stats, int depth)
{
    FilePathList entries;
    FilePathList dirs;
    int ok = 1;
    if (depth > MI_MAX_DEPTH || !DirectoryExists(sourceDir)) return depth <= MI_MAX_DEPTH;
    if (!MI_EnsureDirectory(destinationDir)) return 0;

    entries = MI_ListEntries(sourceDir);
    for (unsigned i = 0; i < entries.count; ++i) {
        const char *base = MI_BaseName(entries.paths[i]);
        char destination[MI_PATH_MAX];
        if (MI_IsDirectoryPath(entries.paths[i])) continue;
        if (!MI_EndsWithI(base, ".lua") && !MI_EndsWithI(base, ".json")) continue;
        if (!MI_IsSafeComponent(base) || !MI_Join(destination, sizeof(destination), destinationDir, base)) {
            ok = 0;
            continue;
        }
        if (MI_CopyFile(entries.paths[i], destination, &stats->bytes)) ++stats->stages;
        else ok = 0;
    }
    UnloadDirectoryFiles(entries);

    dirs = MI_ListDirectories(sourceDir);
    for (unsigned i = 0; i < dirs.count; ++i) {
        if (!MI_CopyStagesRecursive(dirs.paths[i], destinationDir, stats, depth + 1)) ok = 0;
    }
    UnloadDirectoryFiles(dirs);
    return ok;
}

static int MI_CopyChartsFromDirectory(const char *sourceDir, const char *destinationSong,
                                     MI_ImportContext *context, int *chartCount)
{
    FilePathList entries;
    int ok = 1;
    if (!DirectoryExists(sourceDir)) return 1;
    entries = MI_ListEntries(sourceDir);

    for (unsigned i = 0; i < entries.count; ++i) {
        const char *base = MI_BaseName(entries.paths[i]);
        char destination[MI_PATH_MAX];
        if (MI_IsDirectoryPath(entries.paths[i])) continue;
        if (MI_EndsWithI(base, ".lua")) {
            ++context->stats->skippedLua;
            continue;
        }
        if (!MI_EndsWithI(base, ".json")) continue;
        if (MI_CaseEqual(base, "events.json")) {
            ++context->stats->skippedEvents;
            continue;
        }
        if (!MI_IsSafeComponent(base) || !MI_Join(destination, sizeof(destination), destinationSong, base)) {
            ok = 0;
            continue;
        }
        if (FileExists(destination)) {
            /* Reimport atualiza arquivos existentes sem duplicar a estatistica. */
            (void)MI_CopyFile(entries.paths[i], destination, &context->stats->bytes);
            MI_ParseChartActors(entries.paths[i], &context->iconNames);
            continue;
        }
        if (MI_CopyFile(entries.paths[i], destination, &context->stats->bytes)) {
            ++context->stats->charts;
            if (chartCount != NULL) ++*chartCount;
            MI_ParseChartActors(entries.paths[i], &context->iconNames);
        } else {
            ok = 0;
        }
    }
    UnloadDirectoryFiles(entries);
    return ok;
}

static int MI_StartsWithI(const char *text, const char *prefix)
{
    if (text == NULL || prefix == NULL) return 0;
    while (*prefix) {
        unsigned char a = (unsigned char)*text++;
        unsigned char b = (unsigned char)*prefix++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static void MI_CopyVoiceFiles(const char *sourceSong, const char *destinationSong,
                              MI_ImportContext *context)
{
    FilePathList entries = MI_ListEntries(sourceSong);
    for (unsigned i = 0; i < entries.count; ++i) {
        const char *base = MI_BaseName(entries.paths[i]);
        char destination[MI_PATH_MAX];
        if (MI_IsDirectoryPath(entries.paths[i]) || !MI_StartsWithI(base, "Voices")) continue;
        if (!MI_EndsWithI(base, ".ogg") && !MI_EndsWithI(base, ".mp3") &&
            !MI_EndsWithI(base, ".wav")) continue;
        if (!MI_IsSafeComponent(base) ||
            !MI_Join(destination, sizeof(destination), destinationSong, base)) continue;
        (void)MI_CopyFile(entries.paths[i], destination, &context->stats->bytes);
    }
    UnloadDirectoryFiles(entries);
}

static int MI_CopySongFolder(const char *sourceSong, const char *destinationSong,
                             const char *sourceData, MI_ImportContext *context)
{
    static const char *const instrumentals[] = {"Inst.ogg", "Inst.mp3", "Inst.wav"};
    char sourceFile[MI_PATH_MAX];
    char destinationFile[MI_PATH_MAX];
    char chartDir[MI_PATH_MAX];
    int hasInst = 0;
    int charts = 0;

    if (!MI_EnsureDirectory(destinationSong)) return 0;

    /* Audio e opcional para importar charts: mods podem conter apenas dados. */
    if (sourceSong != NULL && DirectoryExists(sourceSong)) {
        for (size_t i = 0; i < sizeof(instrumentals) / sizeof(instrumentals[0]); ++i) {
            if (!MI_Join(sourceFile, sizeof(sourceFile), sourceSong, instrumentals[i])) continue;
            if (!FileExists(sourceFile)) continue;
            if (!MI_Join(destinationFile, sizeof(destinationFile), destinationSong, instrumentals[i])) continue;
            if (MI_CopyFile(sourceFile, destinationFile, &context->stats->bytes)) {
                hasInst = 1;
                break;
            }
        }
        MI_CopyVoiceFiles(sourceSong, destinationSong, context);
    }

    /* O formato Psych guarda os charts em data/<song>; o destino do engine
     * padroniza tudo em songs/<song>/<chart>.json. */
    if (sourceData != NULL && sourceData[0] != '\0' && DirectoryExists(sourceData))
        (void)MI_CopyChartsFromDirectory(sourceData, destinationSong, context, &charts);

    if (sourceSong != NULL && DirectoryExists(sourceSong)) {
        (void)MI_CopyChartsFromDirectory(sourceSong, destinationSong, context, &charts);
        if (MI_Join(chartDir, sizeof(chartDir), sourceSong, "charts") && DirectoryExists(chartDir))
            (void)MI_CopyChartsFromDirectory(chartDir, destinationSong, context, &charts);
    }

    if (charts > 0) ++context->stats->songs;
    return (hasInst || charts > 0);
}

static void MI_ImportLooseSongCharts(const char *sourceSongs, const char *destinationSongs,
                                     MI_ImportContext *context)
{
    FilePathList entries;
    if (!DirectoryExists(sourceSongs) || !MI_EnsureDirectory(destinationSongs)) return;
    entries = MI_ListEntries(sourceSongs);
    for (unsigned i = 0; i < entries.count; ++i) {
        const char *base = MI_BaseName(entries.paths[i]);
        char songName[MI_NAME_MAX];
        char destinationSong[MI_PATH_MAX];
        if (MI_IsDirectoryPath(entries.paths[i]) || !MI_EndsWithI(base, ".json")) continue;
        if (MI_CaseEqual(base, "events.json")) {
            ++context->stats->skippedEvents;
            continue;
        }
        MI_GetStem(base, songName, sizeof(songName));
        if (!MI_IsSafeComponent(songName) || songName[0] == '\0' ||
            !MI_Join(destinationSong, sizeof(destinationSong), destinationSongs, songName)) continue;
        (void)MI_EnsureDirectory(destinationSong);
        {
            char destination[MI_PATH_MAX];
            if (MI_Join(destination, sizeof(destination), destinationSong, base)) {
                if (FileExists(destination)) {
                    (void)MI_CopyFile(entries.paths[i], destination, &context->stats->bytes);
                } else if (MI_CopyFile(entries.paths[i], destination, &context->stats->bytes)) {
                    ++context->stats->charts;
                    ++context->stats->songs;
                }
                MI_ParseChartActors(entries.paths[i], &context->iconNames);
            }
        }
    }
    UnloadDirectoryFiles(entries);
}

static int MI_ImportSongs(const char *srcMod, const char *dstMod,
                          MI_ImportContext *context)
{
    char sourceSongs[MI_PATH_MAX];
    char sourceData[MI_PATH_MAX];
    char destinationSongs[MI_PATH_MAX];
    FilePathList songDirs;
    FilePathList dataDirs;
    int haveSongs;
    int haveData;

    if (!MI_Join(sourceSongs, sizeof(sourceSongs), srcMod, "songs") ||
        !MI_Join(sourceData, sizeof(sourceData), srcMod, "data") ||
        !MI_Join(destinationSongs, sizeof(destinationSongs), dstMod, "songs")) return 0;

    haveSongs = DirectoryExists(sourceSongs);
    haveData = DirectoryExists(sourceData);
    if (!haveSongs && !haveData) return 1;
    if (!MI_EnsureDirectory(destinationSongs)) return 0;

    if (haveSongs) {
        songDirs = MI_ListDirectories(sourceSongs);
        for (unsigned i = 0; i < songDirs.count; ++i) {
            char destinationSong[MI_PATH_MAX];
            char sourceChart[MI_PATH_MAX];
            const char *songName = MI_BaseName(songDirs.paths[i]);
            if (!MI_IsSafeComponent(songName) ||
                !MI_Join(destinationSong, sizeof(destinationSong), destinationSongs, songName)) continue;

            sourceChart[0] = '\0';
            if (haveData && MI_Join(sourceChart, sizeof(sourceChart), sourceData, songName) &&
                !DirectoryExists(sourceChart)) sourceChart[0] = '\0';

            (void)MI_CopySongFolder(songDirs.paths[i], destinationSong,
                                    sourceChart[0] ? sourceChart : NULL, context);
        }
        UnloadDirectoryFiles(songDirs);
        MI_ImportLooseSongCharts(sourceSongs, destinationSongs, context);
    }

    /* Charts podem existir mesmo quando o mod nao traz a pasta songs/<nome>.
     * Importa os que ainda nao foram associados acima, mantendo-os em songs/. */
    if (haveData) {
        dataDirs = MI_ListDirectories(sourceData);
        for (unsigned i = 0; i < dataDirs.count; ++i) {
            char matchingSong[MI_PATH_MAX];
            char destinationSong[MI_PATH_MAX];
            const char *songName = MI_BaseName(dataDirs.paths[i]);
            if (MI_CaseEqual(songName, "stages") || !MI_IsSafeComponent(songName) ||
                !MI_Join(destinationSong, sizeof(destinationSong), destinationSongs, songName)) continue;
            if (haveSongs && MI_Join(matchingSong, sizeof(matchingSong), sourceSongs, songName) &&
                DirectoryExists(matchingSong)) continue;
            (void)MI_CopySongFolder(NULL, destinationSong, dataDirs.paths[i], context);
        }
        UnloadDirectoryFiles(dataDirs);
    }
    return 1;
}

static int MI_ImportCharacters(const char *srcMod, const char *dstMod,
                               MI_ImportContext *context)
{
    char sourceChars[MI_PATH_MAX];
    char destinationChars[MI_PATH_MAX];
    char sourceImageRoot[MI_PATH_MAX];
    char destinationImageChars[MI_PATH_MAX];
    FilePathList entries;
    int ok = 1;

    if (!MI_Join(sourceChars, sizeof(sourceChars), srcMod, "characters") ||
        !MI_Join(destinationChars, sizeof(destinationChars), dstMod, "characters") ||
        !MI_Join(sourceImageRoot, sizeof(sourceImageRoot), srcMod, "images") ||
        !MI_Join(destinationImageChars, sizeof(destinationImageChars), dstMod, "images/characters"))
        return 0;
    if (!DirectoryExists(sourceChars)) return 1;
    if (!MI_EnsureDirectory(destinationChars)) return 0;

    entries = MI_ListEntries(sourceChars);
    for (unsigned i = 0; i < entries.count; ++i) {
        char destinationJson[MI_PATH_MAX];
        char stem[MI_NAME_MAX];
        MI_CharInfo info;
        const char *base = MI_BaseName(entries.paths[i]);

        if (MI_IsDirectoryPath(entries.paths[i]) || !MI_EndsWithI(base, ".json") ||
            !MI_IsSafeComponent(base)) continue;
        if (!MI_Join(destinationJson, sizeof(destinationJson), destinationChars, base)) {
            ok = 0;
            continue;
        }
        if (!MI_CopyFile(entries.paths[i], destinationJson, &context->stats->bytes)) {
            ok = 0;
            continue;
        }
        ++context->stats->characters;
        MI_GetStem(base, stem, sizeof(stem));
        MI_AddName(&context->iconNames, stem);

        if (MI_ParseCharacterJson(entries.paths[i], stem, &info)) {
            char atlasXml[MI_PATH_MAX];
            char animsetPath[MI_PATH_MAX];
            const char *imagePath = info.imagePath[0] ? info.imagePath : "characters";
            int xmlFound = 0;

            if (snprintf(atlasXml, sizeof(atlasXml), "%s/%s.xml", sourceImageRoot, imagePath) <
                (int)sizeof(atlasXml) && FileExists(atlasXml)) {
                xmlFound = 1;
            } else {
                const char *imageBase = MI_BaseName(imagePath);
                if (snprintf(atlasXml, sizeof(atlasXml), "%s/characters/%s.xml",
                             sourceImageRoot, imageBase) < (int)sizeof(atlasXml) &&
                    FileExists(atlasXml)) xmlFound = 1;
            }

            if (xmlFound &&
                snprintf(animsetPath, sizeof(animsetPath), "%s/%s.animset",
                         destinationImageChars, stem) < (int)sizeof(animsetPath)) {
                int generated = MI_WriteAnimset(&info, atlasXml, animsetPath);
                if (generated > 0) {
                    ++context->stats->animsets;
                    context->stats->bytes += MI_FileSize(animsetPath);
                }
            }

            MI_AddName(&context->iconNames, info.healthicon);
        }
        MI_AddName(&context->iconNames, stem);
    }
    UnloadDirectoryFiles(entries);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* API publica                                                               */
/* ------------------------------------------------------------------------- */

static char MI_ImportSingle(const char *srcMod, const char *destModPath,
                            const char *modName, char *msg, size_t msgSize,
                            ModImportStats *stats)
{
    ModImportStats localStats;
    MI_ImportContext context;
    char modNameSafe[64];
    char sourceWeeks[MI_PATH_MAX];
    char destinationWeeks[MI_PATH_MAX];
    char sourceStages[MI_PATH_MAX];
    char destinationStages[MI_PATH_MAX];
    char sourceDataStages[MI_PATH_MAX];
    char destinationDataStages[MI_PATH_MAX];
    char sourceImages[MI_PATH_MAX];
    char destinationImages[MI_PATH_MAX];
    int copiedImages = 0;
    int ok = 1;

    memset(&localStats, 0, sizeof(localStats));
    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (msg != NULL && msgSize > 0) msg[0] = '\0';

    if (srcMod == NULL || destModPath == NULL || modName == NULL ||
        !DirectoryExists(srcMod)) {
        if (msg != NULL && msgSize > 0)
            (void)snprintf(msg, msgSize, "import: origem/destino invalido");
        return 0;
    }

    MI_SanitizeModName(modNameSafe, sizeof(modNameSafe), modName);
    if (!MI_EnsureDirectory(destModPath)) {
        if (msg != NULL && msgSize > 0)
            (void)snprintf(msg, msgSize, "import: nao foi possivel criar %s", destModPath);
        return 0;
    }

    memset(&context, 0, sizeof(context));
    context.srcRoot = srcMod;
    context.dstRoot = destModPath;
    context.stats = &localStats;

    if (!MI_Join(sourceWeeks, sizeof(sourceWeeks), srcMod, "weeks") ||
        !MI_Join(destinationWeeks, sizeof(destinationWeeks), destModPath, "weeks") ||
        !MI_Join(sourceStages, sizeof(sourceStages), srcMod, "stages") ||
        !MI_Join(destinationStages, sizeof(destinationStages), destModPath, "stages") ||
        !MI_Join(sourceDataStages, sizeof(sourceDataStages), srcMod, "data/stages") ||
        !MI_Join(destinationDataStages, sizeof(destinationDataStages), destModPath, "data/stages") ||
        !MI_Join(sourceImages, sizeof(sourceImages), srcMod, "images") ||
        !MI_Join(destinationImages, sizeof(destinationImages), destModPath, "images")) {
        if (msg != NULL && msgSize > 0)
            (void)snprintf(msg, msgSize, "import: caminho excede o limite permitido");
        return 0;
    }

    if (!MI_ImportSongs(srcMod, destModPath, &context)) ok = 0;

    if (DirectoryExists(sourceWeeks) &&
        !MI_CopyWeeksRecursive(sourceWeeks, destinationWeeks, &context, 0)) ok = 0;
    if (DirectoryExists(sourceStages) &&
        !MI_CopyStagesRecursive(sourceStages, destinationStages, &localStats, 0)) ok = 0;
    if (DirectoryExists(sourceDataStages) &&
        !MI_CopyStagesRecursive(sourceDataStages, destinationDataStages, &localStats, 0)) ok = 0;

    if (DirectoryExists(sourceImages)) {
        if (!MI_CopyTree(sourceImages, destinationImages, &localStats.bytes,
                         &copiedImages, 0)) ok = 0;
        localStats.images += copiedImages;
    }

    if (!MI_ImportCharacters(srcMod, destModPath, &context)) ok = 0;
    MI_CopyCollectedIcons(&context);

    if (stats != NULL) *stats = localStats;
    if (msg != NULL && msgSize > 0) {
        (void)snprintf(msg, msgSize,
            "import %s: %d musicas, %d charts, %d weeks, %d stages, %d arquivos de imagem, %d personagens, %d animsets, %d icones (%ld KB)",
            modNameSafe, localStats.songs, localStats.charts, localStats.weeks,
            localStats.stages, localStats.images, localStats.characters,
            localStats.animsets, localStats.icons, localStats.bytes / 1024L);
    }

    /* Mods sem songs tambem podem conter stages/personagens/imagens validos. */
    if (localStats.songs == 0 && localStats.weeks == 0 && localStats.stages == 0 &&
        localStats.images == 0 && localStats.characters == 0) return 0;
    return (char)(ok != 0);
}

char ModImport_Run(const char *srcMod, const char *dstRoot, const char *modName,
                   char *msg, size_t msgSize, ModImportStats *stats)
{
    ModImportStats total;
    char sourceIsMods;
    char destination[MI_PATH_MAX];
    memset(&total, 0, sizeof(total));
    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (msg != NULL && msgSize > 0) msg[0] = '\0';

    if (srcMod == NULL || dstRoot == NULL || modName == NULL ||
        !DirectoryExists(srcMod) || !MI_IsModsRoot(dstRoot)) {
        if (msg != NULL && msgSize > 0)
            (void)snprintf(msg, msgSize, "import: origem/raiz invalida (destino deve ser assets/mods)");
        return 0;
    }

    sourceIsMods = (char)(MI_IsNameMods(MI_BaseName(srcMod)) || MI_IsNameMods(modName));
    if (!sourceIsMods) {
        char safeName[64];
        MI_SanitizeModName(safeName, sizeof(safeName), modName);
        if (!MI_Join(destination, sizeof(destination), dstRoot, safeName)) return 0;
        return MI_ImportSingle(srcMod, destination, safeName, msg, msgSize, stats);
    }

    /* A pasta chamada "mods" pode ser o proprio mod (com songs/weeks/etc.)
     * ou um container com varios mods. Nunca cria assets/mods/mods. */
    if (MI_HasModLayout(srcMod)) {
        return MI_ImportSingle(srcMod, dstRoot, "mods", msg, msgSize, stats);
    }

    {
        FilePathList children = MI_ListDirectories(srcMod);
        int imported = 0;
        int ok = 1;
        for (unsigned i = 0; i < children.count; ++i) {
            const char *childName = MI_BaseName(children.paths[i]);
            char safeName[64];
            ModImportStats childStats;
            char childMsg[256];
            if (!MI_IsSafeComponent(childName) || MI_IsNameMods(childName)) continue;
            MI_SanitizeModName(safeName, sizeof(safeName), childName);
            if (!MI_Join(destination, sizeof(destination), dstRoot, safeName)) {
                ok = 0;
                continue;
            }
            memset(&childStats, 0, sizeof(childStats));
            if (MI_ImportSingle(children.paths[i], destination, safeName,
                                childMsg, sizeof(childMsg), &childStats)) {
                MI_StatsAdd(&total, &childStats);
                ++imported;
            } else if (childStats.charts || childStats.songs || childStats.weeks ||
                       childStats.stages || childStats.images || childStats.characters) {
                MI_StatsAdd(&total, &childStats);
                ok = 0;
            }
        }
        UnloadDirectoryFiles(children);
        if (stats != NULL) *stats = total;
        if (msg != NULL && msgSize > 0) {
            (void)snprintf(msg, msgSize,
                "import mods: %d mods, %d musicas, %d charts, %d weeks, %d stages, %d imagens (%ld KB)",
                imported, total.songs, total.charts, total.weeks, total.stages,
                total.images, total.bytes / 1024L);
        }
        return (char)(imported > 0 && ok);
    }
}

char ModImport_ScanMods(const char *dstRoot, ModImportUsage *out)
{
    ModImportUsage usage;
    FilePathList mods;
    memset(&usage, 0, sizeof(usage));
    if (out != NULL) *out = usage;
    if (!MI_IsModsRoot(dstRoot) || !DirectoryExists(dstRoot)) return 0;

    if (MI_HasModLayout(dstRoot)) {
        char songsPath[MI_PATH_MAX];
        FilePathList songs;
        usage.mods = 1;
        usage.bytes = MI_CountTreeBytes(dstRoot, 0);
        if (MI_Join(songsPath, sizeof(songsPath), dstRoot, "songs") && DirectoryExists(songsPath)) {
            songs = MI_ListDirectories(songsPath);
            usage.songs = (int)songs.count;
            UnloadDirectoryFiles(songs);
        }
    } else {
        mods = MI_ListDirectories(dstRoot);
        for (unsigned i = 0; i < mods.count; ++i) {
            char songsPath[MI_PATH_MAX];
            FilePathList songs;
            if (!MI_IsSafeComponent(MI_BaseName(mods.paths[i]))) continue;
            ++usage.mods;
            {
                long bytes = MI_CountTreeBytes(mods.paths[i], 0);
                if (bytes > 0 && usage.bytes <= LONG_MAX - bytes) usage.bytes += bytes;
            }
            if (!MI_Join(songsPath, sizeof(songsPath), mods.paths[i], "songs") ||
                !DirectoryExists(songsPath)) continue;
            songs = MI_ListDirectories(songsPath);
            usage.songs += (int)songs.count;
            UnloadDirectoryFiles(songs);
        }
        UnloadDirectoryFiles(mods);
    }
    if (out != NULL) *out = usage;
    return 1;
}

char ModImport_DeleteAll(const char *dstRoot, char *msg, size_t msgSize,
                         int *removedMods, long *freedBytes)
{
    FilePathList mods;
    FilePathList rootEntries;
    int modCount = 0;
    int rootIsMod = 0;
    int removedFiles = 0;
    long bytes = 0;
    int ok = 1;

    if (removedMods != NULL) *removedMods = 0;
    if (freedBytes != NULL) *freedBytes = 0;
    if (msg != NULL && msgSize > 0) msg[0] = '\0';

    if (!MI_IsModsRoot(dstRoot)) {
        if (msg != NULL && msgSize > 0)
            (void)snprintf(msg, msgSize, "delete: raiz recusada (use somente assets/mods)");
        return 0;
    }
    if (!DirectoryExists(dstRoot)) {
        if (msg != NULL && msgSize > 0)
            (void)snprintf(msg, msgSize, "delete: pasta de mods ainda nao existe");
        return 1;
    }

    bytes = MI_CountTreeBytes(dstRoot, 0);
    rootIsMod = MI_HasModLayout(dstRoot);
    mods = MI_ListDirectories(dstRoot);
    for (unsigned i = 0; i < mods.count; ++i) {
        if (!rootIsMod) ++modCount;
        if (!MI_RemoveTree(mods.paths[i], 0, &removedFiles)) ok = 0;
        if (MI_RMDIR(mods.paths[i]) != 0 && DirectoryExists(mods.paths[i])) ok = 0;
    }
    UnloadDirectoryFiles(mods);
    if (rootIsMod && bytes > 0) modCount = 1;

    /* Remove arquivos soltos na raiz, mas preserva a pasta assets/mods. */
    rootEntries = MI_ListEntries(dstRoot);
    for (unsigned i = 0; i < rootEntries.count; ++i) {
        if (MI_IsDirectoryPath(rootEntries.paths[i])) continue;
        if (remove(rootEntries.paths[i]) == 0) ++removedFiles;
        else ok = 0;
    }
    UnloadDirectoryFiles(rootEntries);

    if (removedMods != NULL) *removedMods = modCount;
    if (freedBytes != NULL) *freedBytes = bytes;
    if (msg != NULL && msgSize > 0) {
        (void)snprintf(msg, msgSize,
            "delete: %d mods, %d arquivos, aproximadamente %ld KB liberados em %s",
            modCount, removedFiles, bytes / 1024L, dstRoot);
    }
    return (char)(ok != 0);
}
