/*
 * Friday Night Funkin' C Engine (c) 2026 nordjkpl
 * AnimationSets.c - binary .animset + Sparrow atlas XML + character XML
 *
 * Requires Yxml (yxml.c / yxml.h), a small streaming XML parser.
 * The parser is kept separate from this translation unit on purpose.
 */

#include "scenes/AllScenes.h"
#include "scenes/AnimationSets_XML.h"
#include "Log.h"
#include "yxml.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANIMSET_MAX_ANIMATIONS 8192
#define XML_MAX_FRAMES         262144u
#define XML_MAX_ANIM_NAME      31u
#define XML_MAX_PREFIX         31u
#define XML_MAX_FRAME_NAME     127u
#define XML_MAX_XML_DEPTH      64u
#define XML_STACK_SIZE         4096u
#define XML_READ_CHUNK         16384u
#define XML_ATTR_VALUE_SIZE    512u

/* ------------------------------------------------------------------------- */
/* Common helpers                                                            */
/* ------------------------------------------------------------------------- */

typedef bool (*YxmlTokenHandler)(yxml_t *xml, yxml_ret_t token, void *user);

static bool Yxml_ParseFile(const char *path, YxmlTokenHandler handler, void *user)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "[AnimationSets] cannot open XML: %s\n", path);
        return false;
    }

    yxml_t xml;
    unsigned char xmlStack[XML_STACK_SIZE];
    unsigned char input[XML_READ_CHUNK];
    yxml_init(&xml, xmlStack, sizeof(xmlStack));

    bool ok = true;
    size_t bytesRead;

    while ((bytesRead = fread(input, 1, sizeof(input), file)) != 0 && ok) {
        for (size_t i = 0; i < bytesRead; ++i) {
            yxml_ret_t token = yxml_parse(&xml, input[i]);
            if (token < 0) {
                fprintf(stderr,
                    "[AnimationSets] XML parse error in %s at line %u, byte %llu (code %d)\n",
                    path,
                    (unsigned)xml.line,
                    (unsigned long long)xml.byte,
                    (int)token
                );
                ok = false;
                break;
            }

            if (handler && !handler(&xml, token, user)) {
                fprintf(stderr, "[AnimationSets] invalid XML data in %s at line %u\n",
                    path, (unsigned)xml.line);
                ok = false;
                break;
            }
        }
    }

    if (ok && ferror(file)) {
        fprintf(stderr, "[AnimationSets] read error: %s\n", path);
        ok = false;
    }

    if (ok && yxml_eof(&xml) < 0) {
        fprintf(stderr, "[AnimationSets] incomplete XML document: %s\n", path);
        ok = false;
    }

    fclose(file);
    return ok;
}

static bool ParseInt32(const char *text, int32_t *out)
{
    if (!text || !*text || !out) return false;

    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);

    if (errno || end == text || *end != '\0' || value < INT32_MIN || value > INT32_MAX)
        return false;

    *out = (int32_t)value;
    return true;
}

static bool ParseFloat(const char *text, float *out)
{
    if (!text || !*text || !out) return false;

    errno = 0;
    char *end = NULL;
    float value = strtof(text, &end);

    if (errno || end == text || *end != '\0' || !isfinite(value))
        return false;

    *out = value;
    return true;
}

static bool ParseBool(const char *text, bool *out)
{
    if (!text || !out) return false;

    if (!strcmp(text, "true") || !strcmp(text, "1") || !strcmp(text, "yes")) {
        *out = true;
        return true;
    }

    if (!strcmp(text, "false") || !strcmp(text, "0") || !strcmp(text, "no")) {
        *out = false;
        return true;
    }

    return false;
}

static bool ParseColor(const char *text, Color *out)
{
    if (!text || !out) return false;

    if (*text == '#') ++text;
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;

    size_t length = strlen(text);
    if (length != 6 && length != 8) return false;

    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 16);
    if (errno || end == text || *end != '\0') return false;

    if (length == 6) {
        out->r = (unsigned char)((value >> 16) & 0xFFu);
        out->g = (unsigned char)((value >> 8) & 0xFFu);
        out->b = (unsigned char)(value & 0xFFu);
        out->a = 255;
    } else {
        out->r = (unsigned char)((value >> 24) & 0xFFu);
        out->g = (unsigned char)((value >> 16) & 0xFFu);
        out->b = (unsigned char)((value >> 8) & 0xFFu);
        out->a = (unsigned char)(value & 0xFFu);
    }

    return true;
}

static bool AppendAttrValue(char *buffer, size_t capacity, size_t *length, const char *data)
{
    if (!buffer || !capacity || !length || !data) return false;

    size_t add = strlen(data);
    if (add > capacity - 1u - *length) return false;

    memcpy(buffer + *length, data, add);
    *length += add;
    buffer[*length] = '\0';
    return true;
}

static bool EndsWithXML(const char *path)
{
    const char *extension = strrchr(path, '.');
    if (!extension) return false;
    if (strlen(extension) != 4u) return false;
    return tolower((unsigned char)extension[1]) == 'x' &&
           tolower((unsigned char)extension[2]) == 'm' &&
           tolower((unsigned char)extension[3]) == 'l';
}

static bool IsLikelyXML(const char *path)
{
    if (!path) return false;

    if (EndsWithXML(path))
        return true;

    FILE *file = fopen(path, "rb");
    if (!file) return false;

    unsigned char probe[256];
    size_t count = fread(probe, 1, sizeof(probe), file);
    fclose(file);

    size_t i = 0;
    if (count >= 3 && probe[0] == 0xEF && probe[1] == 0xBB && probe[2] == 0xBF)
        i = 3;

    while (i < count && isspace(probe[i])) ++i;
    return i < count && probe[i] == '<';
}

/* ------------------------------------------------------------------------- */
/* Sparrow / Starling TextureAtlas XML                                       */
/* ------------------------------------------------------------------------- */

typedef struct AtlasFrame {
    char prefix[XML_MAX_PREFIX + 1u];
    uint32_t frameIndex;
    uint32_t sourceOrder;
    Frame frame;
} AtlasFrame;

typedef struct AtlasParser {
    AtlasFrame *frames;
    size_t count;
    size_t capacity;
    uint32_t order;

    unsigned char tagStack[XML_MAX_XML_DEPTH];
    size_t depth;

    bool inSubTexture;
    bool hasName;
    bool hasX;
    bool hasY;
    bool hasWidth;
    bool hasHeight;
    bool rotated;

    char name[XML_MAX_FRAME_NAME + 1u];
    int32_t x, y, width, height, frameX, frameY;

    int currentAttribute;
    char attrValue[XML_ATTR_VALUE_SIZE];
    size_t attrLength;
} AtlasParser;

enum {
    ATLAS_TAG_OTHER = 0,
    ATLAS_TAG_SUBTEXTURE = 1
};

enum {
    ATLAS_ATTR_NONE = 0,
    ATLAS_ATTR_NAME,
    ATLAS_ATTR_X,
    ATLAS_ATTR_Y,
    ATLAS_ATTR_WIDTH,
    ATLAS_ATTR_HEIGHT,
    ATLAS_ATTR_FRAMEX,
    ATLAS_ATTR_FRAMEY,
    ATLAS_ATTR_ROTATED
};

static bool Atlas_PushFrame(AtlasParser *parser)
{
    if (!parser->hasName || !parser->hasX || !parser->hasY ||
        !parser->hasWidth || !parser->hasHeight ||
        parser->width <= 0 || parser->height <= 0 ||
        parser->x < 0 || parser->y < 0 || parser->rotated)
        return false;

    if (parser->count >= XML_MAX_FRAMES) return false;

    if (parser->capacity == parser->count) {
        size_t newCapacity = parser->capacity ? parser->capacity * 2u : 128u;
        if (newCapacity > XML_MAX_FRAMES) newCapacity = XML_MAX_FRAMES;

        AtlasFrame *newFrames = realloc(parser->frames, newCapacity * sizeof(*newFrames));
        if (!newFrames) return false;

        parser->frames = newFrames;
        parser->capacity = newCapacity;
    }

    size_t nameLength = strlen(parser->name);
    size_t prefixLength = nameLength;
    while (prefixLength > 0 && isdigit((unsigned char)parser->name[prefixLength - 1u]))
        --prefixLength;

    uint32_t frameIndex = parser->order;
    if (prefixLength < nameLength) {
        if (prefixLength == 0) return false;

        errno = 0;
        char *end = NULL;
        unsigned long index = strtoul(parser->name + prefixLength, &end, 10);
        if (errno || end == parser->name + prefixLength || *end != '\0' || index > UINT32_MAX)
            return false;
        frameIndex = (uint32_t)index;
    }

    if (prefixLength == 0 || prefixLength > XML_MAX_PREFIX)
        return false;

    AtlasFrame *frame = &parser->frames[parser->count++];
    memcpy(frame->prefix, parser->name, prefixLength);
    frame->prefix[prefixLength] = '\0';
    frame->frameIndex = frameIndex;
    frame->sourceOrder = parser->order++;

    frame->frame.x = parser->x;
    frame->frame.y = parser->y;
    frame->frame.w = parser->width;
    frame->frame.h = parser->height;
    frame->frame.fx = parser->frameX;
    frame->frame.fy = parser->frameY;

    return true;
}

static int AtlasFrame_Compare(const void *left, const void *right)
{
    const AtlasFrame *a = left;
    const AtlasFrame *b = right;

    int names = strcmp(a->prefix, b->prefix);
    if (names) return names;

    if (a->frameIndex < b->frameIndex) return -1;
    if (a->frameIndex > b->frameIndex) return 1;

    if (a->sourceOrder < b->sourceOrder) return -1;
    if (a->sourceOrder > b->sourceOrder) return 1;
    return 0;
}

static void Atlas_ResetSubTexture(AtlasParser *parser)
{
    parser->hasName = false;
    parser->hasX = false;
    parser->hasY = false;
    parser->hasWidth = false;
    parser->hasHeight = false;
    parser->rotated = false;
    parser->name[0] = '\0';
    parser->x = parser->y = 0;
    parser->width = parser->height = 0;
    parser->frameX = parser->frameY = 0;
}

static bool Atlas_StoreAttribute(AtlasParser *parser)
{
    int32_t number;
    bool boolean;

    switch (parser->currentAttribute) {
        case ATLAS_ATTR_NAME: {
            size_t length = strlen(parser->attrValue);
            if (length == 0 || length > XML_MAX_FRAME_NAME) return false;
            memcpy(parser->name, parser->attrValue, length + 1u);
            parser->hasName = true;
        } break;

        case ATLAS_ATTR_X:
            if (!ParseInt32(parser->attrValue, &number)) return false;
            parser->x = number;
            parser->hasX = true;
            break;

        case ATLAS_ATTR_Y:
            if (!ParseInt32(parser->attrValue, &number)) return false;
            parser->y = number;
            parser->hasY = true;
            break;

        case ATLAS_ATTR_WIDTH:
            if (!ParseInt32(parser->attrValue, &number)) return false;
            parser->width = number;
            parser->hasWidth = true;
            break;

        case ATLAS_ATTR_HEIGHT:
            if (!ParseInt32(parser->attrValue, &number)) return false;
            parser->height = number;
            parser->hasHeight = true;
            break;

        case ATLAS_ATTR_FRAMEX:
            if (!ParseInt32(parser->attrValue, &number)) return false;
            parser->frameX = number;
            break;

        case ATLAS_ATTR_FRAMEY:
            if (!ParseInt32(parser->attrValue, &number)) return false;
            parser->frameY = number;
            break;

        case ATLAS_ATTR_ROTATED:
            if (!ParseBool(parser->attrValue, &boolean)) return false;
            parser->rotated = boolean;
            break;

        default:
            break;
    }

    return true;
}

static bool Atlas_XmlToken(yxml_t *xml, yxml_ret_t token, void *user)
{
    AtlasParser *parser = user;

    switch (token) {
        case YXML_ELEMSTART: {
            if (parser->depth >= XML_MAX_XML_DEPTH) return false;
            unsigned char tag = !strcmp(xml->elem, "SubTexture")
                ? ATLAS_TAG_SUBTEXTURE : ATLAS_TAG_OTHER;
            parser->tagStack[parser->depth++] = tag;

            if (tag == ATLAS_TAG_SUBTEXTURE) {
                parser->inSubTexture = true;
                Atlas_ResetSubTexture(parser);
            }
        } break;

        case YXML_ATTRSTART:
            parser->currentAttribute = ATLAS_ATTR_NONE;
            parser->attrLength = 0;
            parser->attrValue[0] = '\0';

            if (parser->depth && parser->tagStack[parser->depth - 1u] == ATLAS_TAG_SUBTEXTURE) {
                if (!strcmp(xml->attr, "name")) parser->currentAttribute = ATLAS_ATTR_NAME;
                else if (!strcmp(xml->attr, "x")) parser->currentAttribute = ATLAS_ATTR_X;
                else if (!strcmp(xml->attr, "y")) parser->currentAttribute = ATLAS_ATTR_Y;
                else if (!strcmp(xml->attr, "width")) parser->currentAttribute = ATLAS_ATTR_WIDTH;
                else if (!strcmp(xml->attr, "height")) parser->currentAttribute = ATLAS_ATTR_HEIGHT;
                else if (!strcmp(xml->attr, "frameX")) parser->currentAttribute = ATLAS_ATTR_FRAMEX;
                else if (!strcmp(xml->attr, "frameY")) parser->currentAttribute = ATLAS_ATTR_FRAMEY;
                else if (!strcmp(xml->attr, "rotated")) parser->currentAttribute = ATLAS_ATTR_ROTATED;
            }
            break;

        case YXML_ATTRVAL:
            if (parser->currentAttribute != ATLAS_ATTR_NONE &&
                !AppendAttrValue(parser->attrValue, sizeof(parser->attrValue),
                    &parser->attrLength, xml->data))
                return false;
            break;

        case YXML_ATTREND:
            if (parser->currentAttribute != ATLAS_ATTR_NONE && !Atlas_StoreAttribute(parser))
                return false;
            parser->currentAttribute = ATLAS_ATTR_NONE;
            break;

        case YXML_ELEMEND:
            if (!parser->depth) return false;
            if (parser->tagStack[--parser->depth] == ATLAS_TAG_SUBTEXTURE) {
                if (!Atlas_PushFrame(parser)) return false;
                parser->inSubTexture = false;
            }
            break;

        default:
            break;
    }

    return true;
}

static void AnimationSet_FreeArrays(RayAnimationHandler *animations)
{
    if (!animations) return;
    free(animations->frameArena);
    free(animations->animations);
    animations->frameArena = NULL;
    animations->animations = NULL;
    animations->animationCount = 0;
}

static bool Atlas_LoadSparrowXML(const char *path, Texture2D image, RayAnimationHandler *out)
{
    if (!path || !out) return false;

    AtlasParser parser = {0};
    if (!Yxml_ParseFile(path, Atlas_XmlToken, &parser)) {
        free(parser.frames);
        return false;
    }

    if (parser.count == 0) {
        free(parser.frames);
        fprintf(stderr, "[AnimationSets] no SubTexture frames found in %s\n", path);
        return false;
    }

    /* Confere se os retângulos do atlas cabem na textura que foi fornecida. */
    for (size_t i = 0; i < parser.count; ++i) {
        const Frame *frame = &parser.frames[i].frame;
        if (frame->x < 0 || frame->y < 0 ||
            (int64_t)frame->x + frame->w > image.width ||
            (int64_t)frame->y + frame->h > image.height) {
            free(parser.frames);
            fprintf(stderr, "[AnimationSets] SubTexture outside texture bounds in %s\n", path);
            return false;
        }
    }

    qsort(parser.frames, parser.count, sizeof(*parser.frames), AtlasFrame_Compare);

    size_t animationCount = 0;
    for (size_t i = 0; i < parser.count; ) {
        ++animationCount;
        const char *prefix = parser.frames[i].prefix;
        do { ++i; } while (i < parser.count && !strcmp(parser.frames[i].prefix, prefix));
    }

    if (animationCount == 0 || animationCount > ANIMSET_MAX_ANIMATIONS ||
        animationCount > INT_MAX) {
        free(parser.frames);
        return false;
    }

    RayAnimation *animations = calloc(animationCount, sizeof(*animations));
    Frame *frameArena = malloc(parser.count * sizeof(*frameArena));
    if (!animations || !frameArena) {
        free(animations);
        free(frameArena);
        free(parser.frames);
        return false;
    }

    Frame *arena = frameArena;
    size_t animationIndex = 0;

    for (size_t i = 0; i < parser.count; ) {
        size_t start = i;
        const char *prefix = parser.frames[i].prefix;
        do { ++i; } while (i < parser.count && !strcmp(parser.frames[i].prefix, prefix));

        size_t count = i - start;
        RayAnimation *animation = &animations[animationIndex++];

        if (strlen(prefix) > sizeof(animation->name) - 1u || count > INT_MAX) {
            free(animations);
            free(frameArena);
            free(parser.frames);
            return false;
        }

        memcpy(animation->name, prefix, strlen(prefix) + 1u);
        animation->frameCount = (int)count;
        animation->frames = arena;
        animation->animationOffset = (Vector2){0.0f, 0.0f};
        animation->fps = 24.0f; /* Sparrow atlas não armazena FPS. */
        animation->frameTime = 1.0f / 24.0f;
        animation->looped = 0;

        for (size_t j = start; j < i; ++j)
            *arena++ = parser.frames[j].frame;
    }

    free(parser.frames);

    *out = (RayAnimationHandler){
        .texture = image,
        .animations = animations,
        .frameArena = frameArena,
        .animationCount = (int)animationCount
    };

    return true;
}

/* ------------------------------------------------------------------------- */
/* Binary .animset loader (keeps the original file format)                    */
/* ------------------------------------------------------------------------- */

static bool Binary_LoadAnimSet(const char *path, Texture2D image, RayAnimationHandler *out)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "[AnimationSets] cannot open animation set: %s\n", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long fileSizeLong = ftell(file);
    if (fileSizeLong < (long)sizeof(int32_t) || fileSizeLong > 128L * 1024L * 1024L ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    size_t fileSize = (size_t)fileSizeLong;
    uint8_t *buffer = malloc(fileSize);
    if (!buffer) {
        fclose(file);
        return false;
    }

    bool readOK = fread(buffer, 1, fileSize, file) == fileSize;
    fclose(file);
    if (!readOK) {
        free(buffer);
        return false;
    }

    uint8_t *ptr = buffer;
    uint8_t *end = buffer + fileSize;
    int32_t animationCount = 0;

    if ((size_t)(end - ptr) < sizeof(animationCount)) goto fail;
    memcpy(&animationCount, ptr, sizeof(animationCount));
    ptr += sizeof(animationCount);

    if (animationCount < 1 || animationCount > ANIMSET_MAX_ANIMATIONS) goto fail;

    uint8_t *scan = ptr;
    size_t totalFrames = 0;

    for (int32_t i = 0; i < animationCount; ++i) {
        int32_t nameLength;
        int32_t dataCount;

        if ((size_t)(end - scan) < sizeof(nameLength)) goto fail;
        memcpy(&nameLength, scan, sizeof(nameLength));
        scan += sizeof(nameLength);

        if (nameLength < 1 || nameLength > (int32_t)XML_MAX_ANIM_NAME ||
            (size_t)(end - scan) < (size_t)nameLength + sizeof(dataCount))
            goto fail;

        scan += nameLength;
        memcpy(&dataCount, scan, sizeof(dataCount));
        scan += sizeof(dataCount);

        if (dataCount < 6 || dataCount % 6 != 0) goto fail;

        size_t frameCount = (size_t)(dataCount / 6);
        if (frameCount > XML_MAX_FRAMES ||
            frameCount > (size_t)(end - scan) / sizeof(Frame))
            goto fail;

        if (totalFrames > XML_MAX_FRAMES - frameCount) goto fail;
        totalFrames += frameCount;
        scan += frameCount * sizeof(Frame);
    }

    if (scan != end) goto fail;

    RayAnimation *animations = calloc((size_t)animationCount, sizeof(*animations));
    Frame *frameArena = malloc(totalFrames * sizeof(*frameArena));
    if (!animations || !frameArena) {
        free(animations);
        free(frameArena);
        goto fail;
    }

    Frame *arena = frameArena;

    for (int32_t i = 0; i < animationCount; ++i) {
        int32_t nameLength;
        int32_t dataCount;

        memcpy(&nameLength, ptr, sizeof(nameLength));
        ptr += sizeof(nameLength);

        RayAnimation *animation = &animations[i];
        memcpy(animation->name, ptr, (size_t)nameLength);
        animation->name[nameLength] = '\0';
        ptr += nameLength;

        memcpy(&dataCount, ptr, sizeof(dataCount));
        ptr += sizeof(dataCount);

        size_t frameCount = (size_t)(dataCount / 6);
        animation->frameCount = (int)frameCount;
        animation->frames = arena;
        animation->animationOffset = (Vector2){0.0f, 0.0f};
        animation->fps = 1.0f;
        animation->frameTime = 1.0f;
        animation->looped = 0;

        memcpy(arena, ptr, frameCount * sizeof(Frame));
        ptr += frameCount * sizeof(Frame);

        for (size_t j = 0; j < frameCount; ++j) {
            if (arena[j].w <= 0 || arena[j].h <= 0) {
                free(animations);
                free(frameArena);
                goto fail;
            }
        }

        arena += frameCount;
    }

    free(buffer);

    *out = (RayAnimationHandler){
        .texture = image,
        .animations = animations,
        .frameArena = frameArena,
        .animationCount = animationCount
    };
    return true;

fail:
    free(buffer);
    fprintf(stderr, "[AnimationSets] invalid .animset data: %s\n", path);
    return false;
}

/* ------------------------------------------------------------------------- */
/* Public animation-set loader                                                */
/* ------------------------------------------------------------------------- */

RayAnimationHandler AnimationSet_LoadAnimations(const char *animset, Texture2D image)
{
    RayAnimationHandler result = {0};

    if (!animset) {
        Log_Fatal("AnimationSet_LoadAnimations: null path");
        return result;
    }

    bool loaded = IsLikelyXML(animset)
        ? Atlas_LoadSparrowXML(animset, image, &result)
        : Binary_LoadAnimSet(animset, image, &result);

    if (!loaded) {
        Log_FatalF("could not load animation set: %s", animset);
        return (RayAnimationHandler){0};
    }

    return result;
}

int AnimationSet_FindAnimation(const RayAnimationHandler *restrict animations,
                               const char *animationName)
{
    if (!animations || !animations->animations || !animationName)
        return -1;

    for (int i = 0; i < animations->animationCount; ++i) {
        if (strcmp(animations->animations[i].name, animationName) == 0)
            return i;
    }

    return -1;
}

void AnimationSet_FreeAnimations(RayAnimationHandler *restrict animations)
{
    AnimationSet_FreeArrays(animations);
}

void AnimationSet_FreeAll(RayAnimationHandler *restrict animations)
{
    if (!animations) return;
    AnimationSet_FreeArrays(animations);
    if (animations->texture.id)
        UnloadTexture(animations->texture);
    animations->texture = (Texture2D){0};
}

/* ------------------------------------------------------------------------- */
/* Codename-style character XML: <character><anim ... /></character>        */
/* ------------------------------------------------------------------------- */

typedef struct CharacterAnimDef {
    char name[XML_MAX_ANIM_NAME + 1u];
    char prefix[XML_MAX_PREFIX + 1u];
    char indices[XML_ATTR_VALUE_SIZE];
    float fps;
    float offsetX;
    float offsetY;
    bool looped;
    bool hasName;
    bool hasPrefix;
} CharacterAnimDef;

typedef struct CharacterXMLParser {
    RayCharacterXMLData data;

    CharacterAnimDef *anims;
    size_t animCount;
    size_t animCapacity;
    CharacterAnimDef currentAnim;

    unsigned char tagStack[XML_MAX_XML_DEPTH];
    size_t depth;

    int currentAttribute;
    char attrValue[XML_ATTR_VALUE_SIZE];
    size_t attrLength;
    bool hasCharacterRoot;
} CharacterXMLParser;

enum {
    CHAR_TAG_OTHER = 0,
    CHAR_TAG_CHARACTER = 1,
    CHAR_TAG_ANIM = 2
};

enum {
    CHAR_ATTR_NONE = 0,
    CHAR_ATTR_SPRITE,
    CHAR_ATTR_X,
    CHAR_ATTR_Y,
    CHAR_ATTR_SCALE,
    CHAR_ATTR_CAMX,
    CHAR_ATTR_CAMY,
    CHAR_ATTR_HOLDTIME,
    CHAR_ATTR_DEFFPS,
    CHAR_ATTR_ISPLAYER,
    CHAR_ATTR_FLIPX,
    CHAR_ATTR_ANTIALIASING,
    CHAR_ATTR_COLOR,
    CHAR_ATTR_NAME,
    CHAR_ATTR_ANIM,
    CHAR_ATTR_FPS,
    CHAR_ATTR_LOOP,
    CHAR_ATTR_INDICES,
    CHAR_ATTR_ANIM_X,
    CHAR_ATTR_ANIM_Y
};

static void CharacterXML_Init(CharacterXMLParser *parser)
{
    memset(parser, 0, sizeof(*parser));
    parser->data.scale = 1.0f;
    parser->data.holdTime = 4.0f;
    parser->data.defaultFPS = 24.0f;
    parser->data.color = WHITE;
    parser->data.antialiasing = true;
}

static bool CharacterXML_PushAnim(CharacterXMLParser *parser)
{
    if (!parser->currentAnim.hasName || parser->animCount >= ANIMSET_MAX_ANIMATIONS)
        return false;

    if (!parser->currentAnim.hasPrefix) {
        memcpy(parser->currentAnim.prefix, parser->currentAnim.name,
            strlen(parser->currentAnim.name) + 1u);
    }

    for (size_t i = 0; i < parser->animCount; ++i) {
        if (!strcmp(parser->anims[i].name, parser->currentAnim.name))
            return false;
    }

    if (parser->animCount == parser->animCapacity) {
        size_t newCapacity = parser->animCapacity ? parser->animCapacity * 2u : 16u;
        if (newCapacity > ANIMSET_MAX_ANIMATIONS)
            newCapacity = ANIMSET_MAX_ANIMATIONS;

        CharacterAnimDef *newAnims = realloc(parser->anims, newCapacity * sizeof(*newAnims));
        if (!newAnims) return false;

        parser->anims = newAnims;
        parser->animCapacity = newCapacity;
    }

    parser->anims[parser->animCount++] = parser->currentAnim;
    return true;
}

static bool CharacterXML_StoreAttribute(CharacterXMLParser *parser)
{
    bool boolean;
    RayCharacterXMLData *data = &parser->data;

    switch (parser->currentAttribute) {
        case CHAR_ATTR_SPRITE: {
            size_t length = strlen(parser->attrValue);
            if (length >= sizeof(data->sprite)) return false;
            memcpy(data->sprite, parser->attrValue, length + 1u);
        } break;

        case CHAR_ATTR_X:
            if (!ParseFloat(parser->attrValue, &data->x)) return false;
            data->hasX = true;
            break;
        case CHAR_ATTR_Y:
            if (!ParseFloat(parser->attrValue, &data->y)) return false;
            data->hasY = true;
            break;
        case CHAR_ATTR_SCALE:
            if (!ParseFloat(parser->attrValue, &data->scale) || data->scale <= 0.0f) return false;
            data->hasScale = true;
            break;
        case CHAR_ATTR_CAMX:
            if (!ParseFloat(parser->attrValue, &data->cameraX)) return false;
            data->hasCameraX = true;
            break;
        case CHAR_ATTR_CAMY:
            if (!ParseFloat(parser->attrValue, &data->cameraY)) return false;
            data->hasCameraY = true;
            break;
        case CHAR_ATTR_HOLDTIME:
            if (!ParseFloat(parser->attrValue, &data->holdTime) || data->holdTime < 0.0f) return false;
            break;
        case CHAR_ATTR_DEFFPS:
            if (!ParseFloat(parser->attrValue, &data->defaultFPS) ||
                data->defaultFPS <= 0.0f || data->defaultFPS > 1000.0f) return false;
            break;
        case CHAR_ATTR_ISPLAYER:
            if (!ParseBool(parser->attrValue, &boolean)) return false;
            data->isPlayer = boolean;
            break;
        case CHAR_ATTR_FLIPX:
            if (!ParseBool(parser->attrValue, &boolean)) return false;
            data->flipX = boolean;
            break;
        case CHAR_ATTR_ANTIALIASING:
            if (!ParseBool(parser->attrValue, &boolean)) return false;
            data->antialiasing = boolean;
            break;
        case CHAR_ATTR_COLOR:
            if (!ParseColor(parser->attrValue, &data->color)) return false;
            data->hasColor = true;
            break;

        case CHAR_ATTR_ANIM_X:
            if (!ParseFloat(parser->attrValue, &parser->currentAnim.offsetX)) return false;
            break;
        case CHAR_ATTR_ANIM_Y:
            if (!ParseFloat(parser->attrValue, &parser->currentAnim.offsetY)) return false;
            break;

        case CHAR_ATTR_NAME: {
            size_t length = strlen(parser->attrValue);
            if (length == 0 || length > XML_MAX_ANIM_NAME) return false;
            memcpy(parser->currentAnim.name, parser->attrValue, length + 1u);
            parser->currentAnim.hasName = true;
        } break;
        case CHAR_ATTR_ANIM: {
            size_t length = strlen(parser->attrValue);
            if (length == 0 || length > XML_MAX_PREFIX) return false;
            memcpy(parser->currentAnim.prefix, parser->attrValue, length + 1u);
            parser->currentAnim.hasPrefix = true;
        } break;
        case CHAR_ATTR_FPS:
            if (!ParseFloat(parser->attrValue, &parser->currentAnim.fps) ||
                parser->currentAnim.fps <= 0.0f || parser->currentAnim.fps > 1000.0f)
                return false;
            break;
        case CHAR_ATTR_LOOP:
            if (!ParseBool(parser->attrValue, &parser->currentAnim.looped)) return false;
            break;
        case CHAR_ATTR_INDICES: {
            size_t length = strlen(parser->attrValue);
            if (length >= sizeof(parser->currentAnim.indices)) return false;
            memcpy(parser->currentAnim.indices, parser->attrValue, length + 1u);
        } break;

        default:
            break;
    }

    return true;
}

static bool CharacterXML_XmlToken(yxml_t *xml, yxml_ret_t token, void *user)
{
    CharacterXMLParser *parser = user;

    switch (token) {
        case YXML_ELEMSTART: {
            if (parser->depth >= XML_MAX_XML_DEPTH) return false;

            unsigned char tag = CHAR_TAG_OTHER;
            if (!strcmp(xml->elem, "character")) {
                tag = CHAR_TAG_CHARACTER;
                parser->hasCharacterRoot = true;
            } else if (!strcmp(xml->elem, "anim")) {
                tag = CHAR_TAG_ANIM;
                memset(&parser->currentAnim, 0, sizeof(parser->currentAnim));
            }

            parser->tagStack[parser->depth++] = tag;
        } break;

        case YXML_ATTRSTART: {
            parser->currentAttribute = CHAR_ATTR_NONE;
            parser->attrLength = 0;
            parser->attrValue[0] = '\0';

            unsigned char tag = parser->depth ? parser->tagStack[parser->depth - 1u] : CHAR_TAG_OTHER;

            if (tag == CHAR_TAG_CHARACTER) {
                if (!strcmp(xml->attr, "sprite")) parser->currentAttribute = CHAR_ATTR_SPRITE;
                else if (!strcmp(xml->attr, "x")) parser->currentAttribute = CHAR_ATTR_X;
                else if (!strcmp(xml->attr, "y")) parser->currentAttribute = CHAR_ATTR_Y;
                else if (!strcmp(xml->attr, "scale")) parser->currentAttribute = CHAR_ATTR_SCALE;
                else if (!strcmp(xml->attr, "camx")) parser->currentAttribute = CHAR_ATTR_CAMX;
                else if (!strcmp(xml->attr, "camy")) parser->currentAttribute = CHAR_ATTR_CAMY;
                else if (!strcmp(xml->attr, "holdTime")) parser->currentAttribute = CHAR_ATTR_HOLDTIME;
                else if (!strcmp(xml->attr, "defFps")) parser->currentAttribute = CHAR_ATTR_DEFFPS;
                else if (!strcmp(xml->attr, "isPlayer")) parser->currentAttribute = CHAR_ATTR_ISPLAYER;
                else if (!strcmp(xml->attr, "flipX")) parser->currentAttribute = CHAR_ATTR_FLIPX;
                else if (!strcmp(xml->attr, "antialiasing")) parser->currentAttribute = CHAR_ATTR_ANTIALIASING;
                else if (!strcmp(xml->attr, "color")) parser->currentAttribute = CHAR_ATTR_COLOR;
            } else if (tag == CHAR_TAG_ANIM) {
                if (!strcmp(xml->attr, "name")) parser->currentAttribute = CHAR_ATTR_NAME;
                else if (!strcmp(xml->attr, "anim")) parser->currentAttribute = CHAR_ATTR_ANIM;
                else if (!strcmp(xml->attr, "fps")) parser->currentAttribute = CHAR_ATTR_FPS;
                else if (!strcmp(xml->attr, "loop")) parser->currentAttribute = CHAR_ATTR_LOOP;
                else if (!strcmp(xml->attr, "indices")) parser->currentAttribute = CHAR_ATTR_INDICES;
                else if (!strcmp(xml->attr, "x")) parser->currentAttribute = CHAR_ATTR_ANIM_X;
                else if (!strcmp(xml->attr, "y")) parser->currentAttribute = CHAR_ATTR_ANIM_Y;
            }
        } break;

        case YXML_ATTRVAL:
            if (parser->currentAttribute != CHAR_ATTR_NONE &&
                !AppendAttrValue(parser->attrValue, sizeof(parser->attrValue),
                    &parser->attrLength, xml->data))
                return false;
            break;

        case YXML_ATTREND:
            if (parser->currentAttribute != CHAR_ATTR_NONE &&
                !CharacterXML_StoreAttribute(parser))
                return false;
            parser->currentAttribute = CHAR_ATTR_NONE;
            break;

        case YXML_ELEMEND:
            if (!parser->depth) return false;
            if (parser->tagStack[--parser->depth] == CHAR_TAG_ANIM) {
                if (!CharacterXML_PushAnim(parser)) return false;
            }
            break;

        default:
            break;
    }

    return true;
}

static const RayAnimation *FindPrefixAnimation(const RayAnimationHandler *atlas,
                                               const char *prefix)
{
    for (int i = 0; i < atlas->animationCount; ++i) {
        if (!strcmp(atlas->animations[i].name, prefix))
            return &atlas->animations[i];
    }
    return NULL;
}

/* Walks indices like "0, 2, 4..7". With dst == NULL it only counts frames. */
static bool Animation_WalkIndices(const char *expression,
                                  const Frame *source,
                                  size_t sourceCount,
                                  Frame *dst,
                                  size_t dstCapacity,
                                  size_t *outCount)
{
    size_t written = 0;

    if (!expression || !expression[0]) {
        if (dst && sourceCount > dstCapacity) return false;
        if (dst) memcpy(dst, source, sourceCount * sizeof(*dst));
        *outCount = sourceCount;
        return true;
    }

    const char *cursor = expression;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',' || *cursor == ';')
            ++cursor;
        if (!*cursor) break;

        errno = 0;
        char *end = NULL;
        long first = strtol(cursor, &end, 10);
        if (errno || end == cursor || first < 0 || (size_t)first >= sourceCount)
            return false;
        cursor = end;

        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        long last = first;

        if (cursor[0] == '.' && cursor[1] == '.') {
            cursor += 2;
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            errno = 0;
            last = strtol(cursor, &end, 10);
            if (errno || end == cursor || last < 0 || (size_t)last >= sourceCount)
                return false;
            cursor = end;
        }

        long step = first <= last ? 1 : -1;
        for (long index = first;; index += step) {
            if (written >= XML_MAX_FRAMES || (dst && written >= dstCapacity))
                return false;
            if (dst) dst[written] = source[index];
            ++written;
            if (index == last) break;
        }

        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor && *cursor != ',' && *cursor != ';')
            return false;
    }

    if (written == 0) return false;
    *outCount = written;
    return true;
}

bool AnimationSet_LoadCharacterXML(const char *characterXMLPath,
                                   const char *atlasXMLPath,
                                   Texture2D image,
                                   RayCharacterXMLData *out)
{
    if (!characterXMLPath || !atlasXMLPath || !out)
        return false;

    CharacterXMLParser character;
    CharacterXML_Init(&character);

    if (!Yxml_ParseFile(characterXMLPath, CharacterXML_XmlToken, &character)) {
        free(character.anims);
        return false;
    }

    if (!character.hasCharacterRoot || character.animCount == 0) {
        free(character.anims);
        fprintf(stderr, "[AnimationSets] character XML has no <character>/<anim>: %s\n",
            characterXMLPath);
        return false;
    }

    RayAnimationHandler atlas = {0};
    if (!Atlas_LoadSparrowXML(atlasXMLPath, image, &atlas)) {
        free(character.anims);
        return false;
    }

    size_t totalFrames = 0;
    for (size_t i = 0; i < character.animCount; ++i) {
        CharacterAnimDef *def = &character.anims[i];
        const RayAnimation *source = FindPrefixAnimation(&atlas, def->prefix);

        if (!source) {
            fprintf(stderr, "[AnimationSets] atlas prefix '%s' not found (character: %s)\n",
                def->prefix, characterXMLPath);
            AnimationSet_FreeArrays(&atlas);
            free(character.anims);
            return false;
        }

        size_t selected = 0;
        if (!Animation_WalkIndices(def->indices, source->frames,
                (size_t)source->frameCount, NULL, 0, &selected) ||
            selected > XML_MAX_FRAMES - totalFrames) {
            AnimationSet_FreeArrays(&atlas);
            free(character.anims);
            return false;
        }
        totalFrames += selected;

        if (def->fps <= 0.0f)
            def->fps = character.data.defaultFPS;
    }

    RayAnimation *animations = calloc(character.animCount, sizeof(*animations));
    Frame *frameArena = malloc(totalFrames * sizeof(*frameArena));

    if (!animations || !frameArena) {
        free(animations);
        free(frameArena);
        AnimationSet_FreeArrays(&atlas);
        free(character.anims);
        return false;
    }

    Frame *arena = frameArena;

    for (size_t i = 0; i < character.animCount; ++i) {
        const CharacterAnimDef *def = &character.anims[i];
        const RayAnimation *source = FindPrefixAnimation(&atlas, def->prefix);
        RayAnimation *animation = &animations[i];

        size_t selected = 0;
        if (!Animation_WalkIndices(def->indices, source->frames,
                (size_t)source->frameCount, arena, totalFrames - (size_t)(arena - frameArena),
                &selected)) {
            free(animations);
            free(frameArena);
            AnimationSet_FreeArrays(&atlas);
            free(character.anims);
            return false;
        }

        memcpy(animation->name, def->name, strlen(def->name) + 1u);
        animation->frames = arena;
        animation->frameCount = (int)selected;
        animation->fps = def->fps;
        animation->frameTime = 1.0f / def->fps;
        animation->looped = def->looped;
        animation->animationOffset = (Vector2){def->offsetX, def->offsetY};

        arena += selected;
    }

    AnimationSet_FreeArrays(&atlas);
    free(character.anims);

    character.data.animations = (RayAnimationHandler){
        .texture = image,
        .animations = animations,
        .frameArena = frameArena,
        .animationCount = (int)character.animCount
    };

    *out = character.data;
    return true;
}
