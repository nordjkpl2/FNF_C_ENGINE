/*
 * Friday Night Funkin' C Engine
 * (c) 2026 nordjkpl
 *
 * Stage.c - Stage Lua discovery, lightweight parsing and background fitting.
 *
 * The Lua parser intentionally supports the small subset used by stage
 * scripts. It does not execute Lua code.
 */

#include "std.h"
#include "scenes/Stage.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Path discovery
 * ------------------------------------------------------------------------- */

#define STAGE_PATH_MAX 300
#define STAGE_MOD_ROOT_MAX 256
#define STAGE_TAG_MAX 64

static bool Stage_IsSafeName(const char *name)
{
    if (name == NULL || name[0] == '\0' || strstr(name, "..") != NULL)
        return false;

    for (const char *p = name; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\')
            return false;
    }

    return true;
}

static void Stage_GetModRoot(const char *songDir,
                             char out[STAGE_MOD_ROOT_MAX])
{
    out[0] = '\0';

    if (songDir == NULL || songDir[0] == '\0')
        return;

    /* Accept both Windows and portable path separators. */
    const char *marker = strstr(songDir, "assets/mods/");
    if (marker == NULL)
        marker = strstr(songDir, "assets\\mods\\");
    if (marker == NULL)
        return;

    /* strlen("assets/mods/") == 12. */
    const char *modName = marker + 12;
    const char *separator = strpbrk(modName, "/\\");

    size_t length = separator != NULL
        ? (size_t)(separator - songDir)
        : strlen(songDir);

    if (length >= STAGE_MOD_ROOT_MAX)
        length = STAGE_MOD_ROOT_MAX - 1;

    memcpy(out, songDir, length);
    out[length] = '\0';
}

static bool Stage_FileExists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;

    fclose(file);
    return true;
}

static bool Stage_TryPath(const char *candidate,
                          char out[STAGE_PATH_MAX])
{
    const int written = snprintf(out, STAGE_PATH_MAX, "%s", candidate);
    return written > 0 && written < STAGE_PATH_MAX && Stage_FileExists(out);
}

char Stage_FindLua(const char *stageName,
                   const char *songDir,
                   char out[300])
{
    char modRoot[STAGE_MOD_ROOT_MAX];
    char candidate[STAGE_PATH_MAX];

    if (out == NULL)
        return 0;

    out[0] = '\0';

    if (!Stage_IsSafeName(stageName) || strcmp(stageName, "stage") == 0)
        return 0;

    Stage_GetModRoot(songDir, modRoot);

    /* Prefer the active mod, then the base-game stage directories. */
    if (modRoot[0] != '\0') {
        int n = snprintf(candidate, sizeof(candidate),
                         "%s/stages/%s.lua", modRoot, stageName);
        if (n > 0 && n < (int)sizeof(candidate) &&
            Stage_TryPath(candidate, out))
            return 1;

        n = snprintf(candidate, sizeof(candidate),
                     "%s/data/stages/%s.lua", modRoot, stageName);
        if (n > 0 && n < (int)sizeof(candidate) &&
            Stage_TryPath(candidate, out))
            return 1;
    }

    int n = snprintf(candidate, sizeof(candidate),
                     "assets/stages/%s.lua", stageName);
    if (n > 0 && n < (int)sizeof(candidate) &&
        Stage_TryPath(candidate, out))
        return 1;

    n = snprintf(candidate, sizeof(candidate),
                 "assets/data/stages/%s.lua", stageName);
    if (n > 0 && n < (int)sizeof(candidate) &&
        Stage_TryPath(candidate, out))
        return 1;

    if (songDir != NULL && songDir[0] != '\0') {
        n = snprintf(candidate, sizeof(candidate),
                     "%s/%s.lua", songDir, stageName);
        if (n > 0 && n < (int)sizeof(candidate) &&
            Stage_TryPath(candidate, out))
            return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Small Lua lexer
 * -------------------------------------------------------------------------
 * Only known stage-construction functions are parsed. Comments and strings
 * are skipped so text that merely contains a function name is not interpreted
 * as a stage command. StageSprite stores graphic dimensions and RGBA color
 * without depending on Raylib types. Layer order remains a back/front flag.
 */

typedef struct StageTagMap {
    char tag[STAGE_TAG_MAX];
    int index;
} StageTagMap;

typedef enum StageLuaCall {
    LUA_CALL_NONE = 0,
    LUA_CALL_MAKE_SPRITE,
    LUA_CALL_MAKE_ANIMATED_SPRITE,
    LUA_CALL_MAKE_GRAPHIC,
    LUA_CALL_SET_SCROLL,
    LUA_CALL_SET_SCROLL_SHORT,
    LUA_CALL_SET_SCALE,
    LUA_CALL_SET_SCALE_LUA,
    LUA_CALL_ADD_SPRITE,
    LUA_CALL_SET_OBJECT_ORDER,
    LUA_CALL_GLITCH,
    LUA_CALL_IGNORED_ANIMATION,
    LUA_CALL_UPDATE
} StageLuaCall;

static bool Stage_TokenEquals(const char *token,
                              size_t tokenLength,
                              const char *literal)
{
    const size_t literalLength = strlen(literal);
    return tokenLength == literalLength &&
           memcmp(token, literal, literalLength) == 0;
}

static StageLuaCall Stage_GetCall(const char *token, size_t length)
{
    if (Stage_TokenEquals(token, length, "makeLuaSprite"))
        return LUA_CALL_MAKE_SPRITE;
    if (Stage_TokenEquals(token, length, "makeAnimatedLuaSprite"))
        return LUA_CALL_MAKE_ANIMATED_SPRITE;
    if (Stage_TokenEquals(token, length, "makeGraphic"))
        return LUA_CALL_MAKE_GRAPHIC;
    if (Stage_TokenEquals(token, length, "setLuaSpriteScrollFactor"))
        return LUA_CALL_SET_SCROLL;
    if (Stage_TokenEquals(token, length, "setScrollFactor"))
        return LUA_CALL_SET_SCROLL_SHORT;
    if (Stage_TokenEquals(token, length, "scaleObject"))
        return LUA_CALL_SET_SCALE;
    if (Stage_TokenEquals(token, length, "scaleLuaSprite"))
        return LUA_CALL_SET_SCALE_LUA;
    if (Stage_TokenEquals(token, length, "addLuaSprite"))
        return LUA_CALL_ADD_SPRITE;
    if (Stage_TokenEquals(token, length, "setObjectOrder"))
        return LUA_CALL_SET_OBJECT_ORDER;
    if (Stage_TokenEquals(token, length, "addGlitchEffect"))
        return LUA_CALL_GLITCH;
    if (Stage_TokenEquals(token, length, "setProperty"))
        return LUA_CALL_IGNORED_ANIMATION;
    if (Stage_TokenEquals(token, length, "onUpdate"))
        return LUA_CALL_UPDATE;

    return LUA_CALL_NONE;
}

static const char *Stage_SkipQuotedString(const char *p)
{
    const char quote = *p++;

    while (*p != '\0') {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
            continue;
        }
        if (*p++ == quote)
            break;
    }

    return p;
}

/* Skips Lua long-bracket strings/comments: [[...]], [=[...]=], etc. */
static const char *Stage_SkipLongBracket(const char *p)
{
    if (*p != '[')
        return p;

    const char *q = p + 1;
    while (*q == '=')
        ++q;

    if (*q != '[')
        return p;

    const size_t equalsCount = (size_t)(q - (p + 1));
    p = q + 1;

    while (*p != '\0') {
        if (*p == ']') {
            const char *close = p + 1;
            size_t i = 0;
            while (i < equalsCount && close[i] == '=')
                ++i;
            if (i == equalsCount && close[i] == ']')
                return close + i + 1;
        }
        ++p;
    }

    return p;
}

static const char *Stage_SkipComment(const char *p)
{
    /* p points to the first '-' of '--'. */
    const char *longText = Stage_SkipLongBracket(p + 2);
    if (longText != p + 2)
        return longText;

    while (*p != '\0' && *p != '\n')
        ++p;
    return p;
}

static const char *Stage_SkipSpace(const char *p)
{
    while (isspace((unsigned char)*p))
        ++p;
    return p;
}

static const char *Stage_SkipArgumentSeparators(const char *p,
                                                 const char *end)
{
    while (p < end && (isspace((unsigned char)*p) || *p == ','))
        ++p;
    return p;
}

/* Returns the character after the matching ')' or NULL if malformed. */
static const char *Stage_FindCallEnd(const char *openParen)
{
    unsigned depth = 1;
    const char *p = openParen + 1;

    while (*p != '\0') {
        if (*p == '\'' || *p == '"') {
            p = Stage_SkipQuotedString(p);
            continue;
        }

        if (p[0] == '-' && p[1] == '-') {
            p = Stage_SkipComment(p);
            continue;
        }

        if (*p == '[') {
            const char *afterLong = Stage_SkipLongBracket(p);
            if (afterLong != p) {
                p = afterLong;
                continue;
            }
        }

        if (*p == '(') {
            ++depth;
        } else if (*p == ')') {
            if (--depth == 0)
                return p + 1;
        }

        ++p;
    }

    return NULL;
}

static bool Stage_ParseStringArg(const char **cursor,
                                 const char *end,
                                 char *out,
                                 size_t outCapacity)
{
    const char *p = Stage_SkipArgumentSeparators(*cursor, end);
    if (p >= end || (*p != '\'' && *p != '"') || outCapacity == 0)
        return false;

    const char quote = *p++;
    size_t written = 0;
    bool overflow = false;
    bool closed = false;

    while (p < end && *p != '\0') {
        char c = *p++;

        if (c == quote) {
            closed = true;
            break;
        }

        if (c == '\\' && p < end) {
            c = *p++;
            switch (c) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                default: break;
            }
        }

        if (written + 1 < outCapacity)
            out[written++] = c;
        else
            overflow = true;
    }

    out[written] = '\0';
    *cursor = p;
    return closed && !overflow && written > 0;
}

static bool Stage_ParseFloatArg(const char **cursor,
                                const char *end,
                                float *out)
{
    const char *p = Stage_SkipArgumentSeparators(*cursor, end);
    if (p >= end)
        return false;

    char *numberEnd = NULL;
    const float value = strtof(p, &numberEnd);
    if (numberEnd == p || numberEnd > end || !isfinite(value))
        return false;

    *out = value;
    *cursor = numberEnd;
    return true;
}

static bool Stage_ParseIntArg(const char **cursor,
                              const char *end,
                              int *out)
{
    const char *p = Stage_SkipArgumentSeparators(*cursor, end);
    if (p >= end)
        return false;

    errno = 0;
    char *numberEnd = NULL;
    const long value = strtol(p, &numberEnd, 10);
    if (numberEnd == p || numberEnd > end || errno == ERANGE ||
        value < INT_MIN || value > INT_MAX)
        return false;

    *out = (int)value;
    *cursor = numberEnd;
    return true;
}

static bool Stage_ParseBoolArg(const char **cursor,
                               const char *end,
                               bool *out)
{
    const char *p = Stage_SkipArgumentSeparators(*cursor, end);
    if (p >= end)
        return false;

    const char *start = p;
    while (p < end && (isalpha((unsigned char)*p) || *p == '_'))
        ++p;

    if (Stage_TokenEquals(start, (size_t)(p - start), "true")) {
        *out = true;
        *cursor = p;
        return true;
    }
    if (Stage_TokenEquals(start, (size_t)(p - start), "false")) {
        *out = false;
        *cursor = p;
        return true;
    }

    return false;
}

static int Stage_FindTag(const StageTagMap *maps,
                         int mapCount,
                         const char *tag)
{
    for (int i = 0; i < mapCount; ++i) {
        if (strcmp(maps[i].tag, tag) == 0)
            return i;
    }
    return -1;
}

static int Stage_GetOrCreateSprite(StageTagMap *maps,
                                   int *mapCount,
                                   StageSprite *out,
                                   int *objectCount,
                                   int capacity,
                                   const char *tag)
{
    const int mapIndex = Stage_FindTag(maps, *mapCount, tag);
    if (mapIndex >= 0)
        return maps[mapIndex].index;

    if (*objectCount >= capacity ||
        *objectCount >= STAGE_MAX_SPRITES ||
        *mapCount >= STAGE_MAX_SPRITES)
        return -1;

    const int index = (*objectCount)++;
    StageSprite *sprite = &out[index];
    memset(sprite, 0, sizeof(*sprite));

    sprite->scrollX = 1.0f;
    sprite->scrollY = 1.0f;
    sprite->scaleX = 1.0f;
    sprite->scaleY = 1.0f;
    sprite->graphicWidth = 0.0f;
    sprite->graphicHeight = 0.0f;
    sprite->colorR = 255;
    sprite->colorG = 255;
    sprite->colorB = 255;
    sprite->colorA = 255;
    sprite->isGraphic = 0;

    snprintf(maps[*mapCount].tag, sizeof(maps[*mapCount].tag), "%s", tag);
    maps[*mapCount].index = index;
    ++(*mapCount);

    return index;
}

static bool Stage_ParseTagArg(const char **args,
                              const char *argsEnd,
                              char tag[STAGE_TAG_MAX])
{
    return Stage_ParseStringArg(args, argsEnd, tag, STAGE_TAG_MAX);
}

/* Decode Psych-style hex colors: RRGGBB, #RRGGBB, 0xRRGGBB,
 * or RRGGBBAA. Six-digit colors are fully opaque.
 */
static int Stage_HexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool Stage_ParseColor(const char *text,
                             uint8_t *red,
                             uint8_t *green,
                             uint8_t *blue,
                             uint8_t *alpha)
{
    if (text == NULL)
        return false;

    if (text[0] == '#') {
        ++text;
    } else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
    }

    const size_t length = strlen(text);
    if (length != 6 && length != 8)
        return false;

    uint8_t channels[4] = {0, 0, 0, 255};
    const size_t channelCount = length / 2;

    for (size_t i = 0; i < channelCount; ++i) {
        const int hi = Stage_HexDigit(text[i * 2]);
        const int lo = Stage_HexDigit(text[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        channels[i] = (uint8_t)((hi << 4) | lo);
    }

    *red = channels[0];
    *green = channels[1];
    *blue = channels[2];
    *alpha = channels[3];
    return true;
}

/* -------------------------------------------------------------------------
 * Public Lua stage parser
 * ------------------------------------------------------------------------- */

int Stage_ParseLua(const char *luaText,
                   StageSprite *out,
                   int capacity,
                   int *ignoredGlitch,
                   int *ignoredAnim)
{
    if (ignoredGlitch != NULL)
        *ignoredGlitch = 0;
    if (ignoredAnim != NULL)
        *ignoredAnim = 0;

    if (luaText == NULL || out == NULL || capacity <= 0)
        return 0;

    if (capacity > STAGE_MAX_SPRITES)
        capacity = STAGE_MAX_SPRITES;

    StageTagMap maps[STAGE_MAX_SPRITES];
    int mapCount = 0;
    int objectCount = 0;
    int glitchCount = 0;
    int ignoredAnimCount = 0;

    const char *p = luaText;

    while (*p != '\0') {
        if (isspace((unsigned char)*p)) {
            ++p;
            continue;
        }

        if (p[0] == '-' && p[1] == '-') {
            p = Stage_SkipComment(p);
            continue;
        }

        if (*p == '\'' || *p == '"') {
            p = Stage_SkipQuotedString(p);
            continue;
        }

        if (*p == '[') {
            const char *afterLong = Stage_SkipLongBracket(p);
            if (afterLong != p) {
                p = afterLong;
                continue;
            }
        }

        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *token = p++;
            while (isalnum((unsigned char)*p) || *p == '_')
                ++p;

            const StageLuaCall call =
                Stage_GetCall(token, (size_t)(p - token));
            if (call == LUA_CALL_NONE)
                continue;

            const char *openParen = Stage_SkipSpace(p);
            if (*openParen != '(') {
                p = openParen;
                continue;
            }

            const char *afterCall = Stage_FindCallEnd(openParen);
            if (afterCall == NULL)
                break;

            const char *args = openParen + 1;
            const char *argsEnd = afterCall - 1;
            char tag[STAGE_TAG_MAX] = {0};
            int index = -1;

            switch (call) {
                case LUA_CALL_MAKE_SPRITE:
                case LUA_CALL_MAKE_ANIMATED_SPRITE: {
                    char image[128] = {0};
                    float x = 0.0f;
                    float y = 0.0f;

                    if (Stage_ParseTagArg(&args, argsEnd, tag) &&
                        Stage_ParseStringArg(&args, argsEnd,
                                             image, sizeof(image)) &&
                        Stage_ParseFloatArg(&args, argsEnd, &x) &&
                        Stage_ParseFloatArg(&args, argsEnd, &y)) {
                        index = Stage_GetOrCreateSprite(
                            maps, &mapCount, out, &objectCount,
                            capacity, tag);
                        if (index >= 0) {
                            snprintf(out[index].image,
                                     sizeof(out[index].image), "%s", image);
                            out[index].x = x;
                            out[index].y = y;
                            out[index].isGraphic = 0;
                            out[index].graphicWidth = 0.0f;
                            out[index].graphicHeight = 0.0f;
                        }
                    }
                } break;

                case LUA_CALL_MAKE_GRAPHIC: {
                    float width = 0.0f;
                    float height = 0.0f;
                    char colorText[24] = {0};
                    uint8_t red = 255, green = 255, blue = 255, alpha = 255;

                    if (Stage_ParseTagArg(&args, argsEnd, tag) &&
                        Stage_ParseFloatArg(&args, argsEnd, &width) &&
                        Stage_ParseFloatArg(&args, argsEnd, &height) &&
                        width >= 0.0f && height >= 0.0f) {
                        /* Color is optional in Psych stage scripts. */
                        if (Stage_ParseStringArg(&args, argsEnd,
                                                 colorText, sizeof(colorText))) {
                            (void)Stage_ParseColor(colorText,
                                                   &red, &green, &blue, &alpha);
                        }

                        index = Stage_GetOrCreateSprite(
                            maps, &mapCount, out, &objectCount,
                            capacity, tag);
                        if (index >= 0) {
                            StageSprite *sprite = &out[index];
                            sprite->image[0] = '\0';
                            sprite->x = 0.0f;
                            sprite->y = 0.0f;
                            sprite->graphicWidth = width;
                            sprite->graphicHeight = height;
                            sprite->colorR = red;
                            sprite->colorG = green;
                            sprite->colorB = blue;
                            sprite->colorA = alpha;
                            sprite->isGraphic = 1;
                        }
                    }
                } break;

                case LUA_CALL_SET_SCROLL:
                case LUA_CALL_SET_SCROLL_SHORT:
                case LUA_CALL_SET_SCALE:
                case LUA_CALL_SET_SCALE_LUA: {
                    float x = 1.0f;
                    float y = 1.0f;

                    if (Stage_ParseTagArg(&args, argsEnd, tag) &&
                        Stage_ParseFloatArg(&args, argsEnd, &x)) {
                        if (!Stage_ParseFloatArg(&args, argsEnd, &y))
                            y = x;

                        index = Stage_GetOrCreateSprite(
                            maps, &mapCount, out, &objectCount,
                            capacity, tag);
                        if (index >= 0) {
                            if (call == LUA_CALL_SET_SCROLL ||
                                call == LUA_CALL_SET_SCROLL_SHORT) {
                                out[index].scrollX = x;
                                out[index].scrollY = y;
                            } else {
                                out[index].scaleX = x;
                                out[index].scaleY = y;
                            }
                        }
                    }
                } break;

                case LUA_CALL_ADD_SPRITE: {
                    bool front = false;
                    if (Stage_ParseTagArg(&args, argsEnd, tag)) {
                        /* Psych-style addLuaSprite(tag, front=false). */
                        (void)Stage_ParseBoolArg(&args, argsEnd, &front);
                        index = Stage_GetOrCreateSprite(
                            maps, &mapCount, out, &objectCount,
                            capacity, tag);
                        if (index >= 0)
                            out[index].front = front ? 1 : 0;
                    }
                } break;

                case LUA_CALL_SET_OBJECT_ORDER: {
                    int order = 0;
                    if (Stage_ParseTagArg(&args, argsEnd, tag) &&
                        Stage_ParseIntArg(&args, argsEnd, &order)) {
                        index = Stage_GetOrCreateSprite(
                            maps, &mapCount, out, &objectCount,
                            capacity, tag);
                        if (index >= 0) {
                            /* StageSprite exposes only back/front, not a full z-index. */
                            out[index].front = order > 0 ? 1 : 0;
                        }
                    }
                } break;

                case LUA_CALL_GLITCH:
                    ++glitchCount;
                    break;

                case LUA_CALL_IGNORED_ANIMATION:
                case LUA_CALL_UPDATE:
                    ++ignoredAnimCount;
                    break;

                case LUA_CALL_NONE:
                default:
                    break;
            }

            p = afterCall;
            continue;
        }

        ++p;
    }

    /* Remove placeholders created only by property calls. */
    int outputCount = 0;
    for (int i = 0; i < objectCount; ++i) {
        if (out[i].image[0] == '\0' && !out[i].isGraphic)
            continue;

        if (outputCount != i) {
            out[outputCount] = out[i];
        }
        ++outputCount;
    }

    if (ignoredGlitch != NULL)
        *ignoredGlitch = glitchCount;
    if (ignoredAnim != NULL)
        *ignoredAnim = ignoredAnimCount;

    return outputCount;
}

/* -------------------------------------------------------------------------
 * Background fitting
 * -------------------------------------------------------------------------
 * First pass computes the original bounds. The second pass applies one
 * combined scale + translation transform, avoiding two separate mutation
 * passes over the background array.
 */

void Stage_CoverBackgrounds(StageBg *backgrounds,
                            int count,
                            float zoom,
                            float dadX,
                            float dadY,
                            float dadOffX,
                            float dadOffY,
                            float bfX,
                            float bfY,
                            float bfOffX,
                            float bfOffY)
{
    if (backgrounds == NULL || count <= 0 ||
        !isfinite(zoom) || zoom < 0.3f || zoom > 2.0f)
        return;

    float minX = INFINITY;
    float minY = INFINITY;
    float maxX = -INFINITY;
    float maxY = -INFINITY;

    for (int i = 0; i < count; ++i) {
        const StageBg *bg = &backgrounds[i];
        const float width = bg->imgW * bg->scaleX;
        const float height = bg->imgH * bg->scaleY;

        if (!isfinite(bg->x) || !isfinite(bg->y) ||
            !isfinite(width) || !isfinite(height) ||
            width <= 0.0f || height <= 0.0f)
            continue;

        if (bg->x < minX) minX = bg->x;
        if (bg->y < minY) minY = bg->y;
        if (bg->x + width > maxX) maxX = bg->x + width;
        if (bg->y + height > maxY) maxY = bg->y + height;
    }

    if (!(minX < maxX) || !(minY < maxY))
        return;

    const float viewWidth = 1280.0f / zoom;
    const float viewHeight = 720.0f / zoom;
    const float marginX = viewWidth * 0.10f;
    const float marginY = viewHeight * 0.10f;

    const float anchorMinX = fminf(dadX,
        fminf(bfX + bfOffX, dadX + dadOffX));
    const float anchorMaxX = fmaxf(dadX,
        fmaxf(bfX + bfOffX, dadX + dadOffX));
    const float anchorMinY = fminf(dadY,
        fminf(bfY + bfOffY, dadY + dadOffY));
    const float anchorMaxY = fmaxf(dadY,
        fmaxf(bfY + bfOffY, dadY + dadOffY));

    const float requiredMinX = anchorMinX - viewWidth * 0.5f - marginX;
    const float requiredMaxX = anchorMaxX + viewWidth * 0.5f + marginX;
    const float requiredMinY = anchorMinY - viewHeight * 0.5f - marginY;
    const float requiredMaxY = anchorMaxY + viewHeight * 0.5f + marginY;

    const float worldWidth = maxX - minX;
    const float worldHeight = maxY - minY;
    if (worldWidth <= 0.0f || worldHeight <= 0.0f)
        return;

    float scale = fmaxf((requiredMaxX - requiredMinX) / worldWidth,
                        (requiredMaxY - requiredMinY) / worldHeight);
    if (!isfinite(scale) || scale < 1.0f)
        scale = 1.0f;

    const float scaledMaxX = minX + worldWidth * scale;
    const float scaledMaxY = minY + worldHeight * scale;

    const float translateX = (minX > requiredMinX)
        ? requiredMinX - minX
        : (scaledMaxX < requiredMaxX)
            ? requiredMaxX - scaledMaxX
            : 0.0f;

    const float translateY = (minY > requiredMinY)
        ? requiredMinY - minY
        : (scaledMaxY < requiredMaxY)
            ? requiredMaxY - scaledMaxY
            : 0.0f;

    for (int i = 0; i < count; ++i) {
        StageBg *bg = &backgrounds[i];
        bg->x = minX + (bg->x - minX) * scale + translateX;
        bg->y = minY + (bg->y - minY) * scale + translateY;
        bg->scaleX *= scale;
        bg->scaleY *= scale;
    }
}
