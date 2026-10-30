/*
 * Friday Night Funkin' C Engine
 * (c) 2026 nordjkpl
 *
 * Character.c - Character loading (legacy .animset/JSON + XML)
 *
 * XML support uses AnimationSets_XML.h / AnimationSet_LoadCharacterXML().
 * The XML parser itself lives in AnimationSets.c and uses Yxml.
 */

#include "scenes/Character.h"
#include "scenes/AnimationSets_XML.h"
#include "Log.h"
#include "yyjson.h"
#include "yxml.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHAR_PATH_MAX       512
#define CHAR_MOD_ROOT_MAX   512
#define CHAR_MAX_DEFS       48
#define CHAR_JSON_MAX_SIZE  (1024u * 1024u)
#define CHAR_XML_STACK_SIZE 2048u
#define CHAR_XML_READ_SIZE  4096u

static char charModRoot[CHAR_MOD_ROOT_MAX];

typedef struct CharAnimDef {
    char role[16];
    char sprite[64];
    int fps;
    bool looped;
    float offsetX;
    float offsetY;
} CharAnimDef;

/* ------------------------------------------------------------------------- */
/* Path helpers                                                              */
/* ------------------------------------------------------------------------- */

static bool Char_IsSafeName(const char *name)
{
    if (!name || !name[0] || strstr(name, ".."))
        return false;

    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p < 32)
            return false;
    }

    return true;
}

static bool Char_FileExists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;

    fclose(file);
    return true;
}

static bool Char_BuildPath(char *out, size_t capacity,
                           const char *root, const char *relative)
{
    if (!out || !capacity || !root || !relative)
        return false;

    int length = snprintf(out, capacity, "%s/%s", root, relative);
    return length > 0 && (size_t)length < capacity;
}

/* Search order: active mod first, then base-game assets. */
static bool Char_FindFile(const char *relative, char out[CHAR_PATH_MAX])
{
    char candidate[CHAR_PATH_MAX];

    if (charModRoot[0] &&
        Char_BuildPath(candidate, sizeof(candidate), charModRoot, relative) &&
        Char_FileExists(candidate)) {
        memcpy(out, candidate, strlen(candidate) + 1u);
        return true;
    }

    int length = snprintf(candidate, sizeof(candidate), "assets/%s", relative);
    if (length <= 0 || (size_t)length >= sizeof(candidate) ||
        !Char_FileExists(candidate)) {
        return false;
    }

    memcpy(out, candidate, (size_t)length + 1u);
    return true;
}

static bool Char_FindFirst(const char *const *relativePaths, size_t count,
                           char out[CHAR_PATH_MAX])
{
    for (size_t i = 0; i < count; ++i) {
        if (Char_FindFile(relativePaths[i], out))
            return true;
    }
    return false;
}

static void Char_CopyString(char *out, size_t capacity, const char *source)
{
    if (!out || !capacity)
        return;

    if (!source) {
        out[0] = '\0';
        return;
    }

    size_t length = strlen(source);
    if (length >= capacity)
        length = capacity - 1u;

    memcpy(out, source, length);
    out[length] = '\0';
}

static const char *Char_BaseName(const char *path)
{
    const char *forward = strrchr(path, '/');
    const char *backward = strrchr(path, '\\');
    const char *last = forward;

    if (!last || (backward && backward > last))
        last = backward;

    return last ? last + 1 : path;
}

static void Char_NormalizeSpriteName(const char *source,
                                     char out[sizeof(((RayCharacterXMLData *)0)->sprite)])
{
    if (!source || !source[0]) {
        out[0] = '\0';
        return;
    }

    const char *base = Char_BaseName(source);
    size_t length = strlen(base);
    if (length >= sizeof(((RayCharacterXMLData *)0)->sprite))
        length = sizeof(((RayCharacterXMLData *)0)->sprite) - 1u;

    memcpy(out, base, length);
    out[length] = '\0';

    char *extension = strrchr(out, '.');
    if (extension)
        *extension = '\0';
}

void Character_SetSongDir(const char *songDir)
{
    charModRoot[0] = '\0';
    if (!songDir || !songDir[0])
        return;

    char normalized[CHAR_PATH_MAX * 2];
    Char_CopyString(normalized, sizeof(normalized), songDir);

    for (char *p = normalized; *p; ++p) {
        if (*p == '\\')
            *p = '/';
    }

    const char *mods = strstr(normalized, "assets/mods/");
    if (!mods)
        return;

    const char *modContent = mods + sizeof("assets/mods/") - 1u;
    const char *songs = strstr(modContent, "/songs/");

    size_t length;
    if (songs) {
        length = (size_t)(songs - normalized);
    } else {
        const char *lastSlash = strrchr(normalized, '/');
        length = lastSlash ? (size_t)(lastSlash - normalized) : strlen(normalized);
    }

    if (length == 0 || length >= sizeof(charModRoot))
        return;

    memcpy(charModRoot, normalized, length);
    charModRoot[length] = '\0';
}

/* ------------------------------------------------------------------------- */
/* XML character metadata discovery                                          */
/* ------------------------------------------------------------------------- */

/*
 * Reads only the root <character sprite="..."> attribute. This small pass
 * lets Character.c locate the matching PNG and Sparrow atlas before calling
 * AnimationSet_LoadCharacterXML(), which performs the complete XML parse.
 */
static bool Char_ReadXMLSprite(const char *path, char *sprite, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;

    yxml_t parser;
    unsigned char stack[CHAR_XML_STACK_SIZE];
    unsigned char input[CHAR_XML_READ_SIZE];
    yxml_init(&parser, stack, sizeof(stack));

    bool foundRoot = false;
    bool inCharacterTag = false;
    bool readingSprite = false;
    bool ok = true;
    size_t spriteLength = 0;
    size_t bytesRead;

    if (capacity)
        sprite[0] = '\0';

    while ((bytesRead = fread(input, 1, sizeof(input), file)) != 0 && ok) {
        for (size_t i = 0; i < bytesRead; ++i) {
            yxml_ret_t token = yxml_parse(&parser, input[i]);
            if (token < 0) {
                ok = false;
                break;
            }

            switch (token) {
                case YXML_ELEMSTART:
                    if (!strcmp(parser.elem, "character")) {
                        foundRoot = true;
                        inCharacterTag = true;
                    }
                    break;

                case YXML_ATTRSTART:
                    readingSprite = inCharacterTag && !strcmp(parser.attr, "sprite");
                    break;

                case YXML_ATTRVAL:
                    if (readingSprite && capacity) {
                        size_t partLength = strlen(parser.data);
                        if (partLength >= capacity - spriteLength) {
                            ok = false;
                            break;
                        }
                        memcpy(sprite + spriteLength, parser.data, partLength);
                        spriteLength += partLength;
                        sprite[spriteLength] = '\0';
                    }
                    break;

                case YXML_ATTREND:
                    readingSprite = false;
                    break;

                case YXML_ELEMEND:
                    if (!strcmp(parser.elem, "character"))
                        inCharacterTag = false;
                    break;

                default:
                    break;
            }

            if (!ok)
                break;
        }
    }

    if (ok && ferror(file))
        ok = false;
    if (ok && yxml_eof(&parser) < 0)
        ok = false;

    fclose(file);
    return ok && foundRoot;
}

static bool Char_FindCharacterXML(const char *name,
                                  char xmlPath[CHAR_PATH_MAX],
                                  char spriteName[128])
{
    char relative[CHAR_PATH_MAX];
    static const char *const formats[] = {
        "characters/%s.xml",
        "data/characters/%s.xml",
        "images/characters/%s.character.xml",
        "images/characters/%s-char.xml",
        "images/characters/%s_char.xml",
        "images/characters/%s.xml"
    };

    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        int length = snprintf(relative, sizeof(relative), formats[i], name);
        if (length <= 0 || (size_t)length >= sizeof(relative))
            continue;

        char candidate[CHAR_PATH_MAX];
        if (!Char_FindFile(relative, candidate))
            continue;

        char rawSprite[128] = {0};
        if (!Char_ReadXMLSprite(candidate, rawSprite, sizeof(rawSprite)))
            continue;

        Char_CopyString(xmlPath, CHAR_PATH_MAX, candidate);
        Char_NormalizeSpriteName(rawSprite[0] ? rawSprite : name, spriteName);
        if (!Char_IsSafeName(spriteName))
            Char_CopyString(spriteName, 128, name);
        if (!spriteName[0])
            Char_CopyString(spriteName, 128, name);
        return true;
    }

    return false;
}

static bool Char_FindAtlasXML(const char *sprite, const char *characterName,
                              const char *characterXML,
                              char atlasPath[CHAR_PATH_MAX])
{
    static const char *const suffixes[] = {
        "%s.xml",
        "%s.atlas.xml",
        "%s-atlas.xml",
        "%s_atlas.xml",
        "%sAtlas.xml"
    };

    const char *names[2] = { sprite, characterName };

    for (size_t n = 0; n < 2; ++n) {
        if (!names[n] || !names[n][0] || (n == 1 && !strcmp(names[0], names[1])))
            continue;

        for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
            char relative[CHAR_PATH_MAX];
            char candidate[CHAR_PATH_MAX];

            int length = snprintf(relative, sizeof(relative),
                "images/characters/");
            if (length <= 0 || (size_t)length >= sizeof(relative))
                continue;

            size_t used = (size_t)length;
            int suffixLength = snprintf(relative + used, sizeof(relative) - used,
                suffixes[i], names[n]);
            if (suffixLength <= 0 || (size_t)suffixLength >= sizeof(relative) - used)
                continue;

            if (!Char_FindFile(relative, candidate))
                continue;
            if (characterXML && !strcmp(candidate, characterXML))
                continue;

            Char_CopyString(atlasPath, CHAR_PATH_MAX, candidate);
            return true;
        }
    }

    return false;
}

static bool Char_FindCharacterPNG(const char *sprite, const char *name,
                                  char pngPath[CHAR_PATH_MAX])
{
    char relative[CHAR_PATH_MAX];
    const char *names[2] = { sprite, name };

    for (size_t i = 0; i < 2; ++i) {
        if (!names[i] || !names[i][0] || (i == 1 && !strcmp(names[0], names[1])))
            continue;

        int length = snprintf(relative, sizeof(relative),
            "images/characters/%s.png", names[i]);
        if (length <= 0 || (size_t)length >= sizeof(relative))
            continue;

        if (Char_FindFile(relative, pngPath))
            return true;
    }

    return false;
}

/* ------------------------------------------------------------------------- */
/* Animation helpers                                                         */
/* ------------------------------------------------------------------------- */

static int Char_FindAnimationAny(const RayAnimationHandler *set,
                                 const char *const *names, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        int index = AnimationSet_FindAnimation(set, names[i]);
        if (index >= 0)
            return index;
    }
    return -1;
}

static int Char_FindAnimation(const RayAnimationHandler *set, const char *name)
{
    return AnimationSet_FindAnimation(set, name);
}

static int Char_ConfigureAnimation(RayAnimationHandler *set, const char *name,
                                   float fps, bool looped,
                                   float offsetX, float offsetY)
{
    int index = Char_FindAnimation(set, name);
    if (index < 0)
        return -1;

    RayAnimation *animation = &set->animations[index];
    if (fps <= 0.0f)
        fps = 24.0f;

    animation->fps = fps;
    animation->frameTime = 1.0f / fps;
    animation->looped = looped;
    animation->animationOffset = (Vector2){ offsetX, offsetY };
    return index;
}

static void Char_InitAnimationSlots(Character *character)
{
    character->animations.idle = -1;

    for (int i = 0; i < 4; ++i) {
        character->animations.notes[i] = -1;
        character->missAnimations.notes[i] = -1;
    }
}

static void Char_ApplyXMLAnimationSlots(Character *character,
                                        const RayAnimationHandler *set)
{
    static const char *const idleNames[] = { "idle", "dance", "danceIdle" };
    static const char *const singNames[4][3] = {
        { "singLEFT", "singLeft", "left" },
        { "singDOWN", "singDown", "down" },
        { "singUP", "singUp", "up" },
        { "singRIGHT", "singRight", "right" }
    };
    static const char *const missNames[4][4] = {
        { "singLEFTmiss", "singLeftMiss", "missLEFT", "singLEFT_miss" },
        { "singDOWNmiss", "singDownMiss", "missDOWN", "singDOWN_miss" },
        { "singUPmiss", "singUpMiss", "missUP", "singUP_miss" },
        { "singRIGHTmiss", "singRightMiss", "missRIGHT", "singRIGHT_miss" }
    };

    character->animations.idle = Char_FindAnimationAny(set,
        idleNames, sizeof(idleNames) / sizeof(idleNames[0]));

    for (int i = 0; i < 4; ++i) {
        character->animations.notes[i] = Char_FindAnimationAny(set,
            singNames[i], sizeof(singNames[i]) / sizeof(singNames[i][0]));
        character->missAnimations.notes[i] = Char_FindAnimationAny(set,
            missNames[i], sizeof(missNames[i]) / sizeof(missNames[i][0]));
    }

    if (character->animations.idle < 0 && set->animationCount > 0)
        character->animations.idle = 0;
}

static void Char_LoadIcon(Character *character, const char *name,
                          const char *healthIcon, bool isDad)
{
    char relative[CHAR_PATH_MAX];
    char path[CHAR_PATH_MAX];
    const char *iconName = (healthIcon && healthIcon[0]) ? healthIcon : name;

    if (!Char_IsSafeName(iconName))
        iconName = name;

    int length = snprintf(relative, sizeof(relative), "images/icons/%s.png", iconName);
    bool found = length > 0 && (size_t)length < sizeof(relative) &&
                 Char_FindFile(relative, path);

    if (!found && strcmp(iconName, name) != 0) {
        length = snprintf(relative, sizeof(relative), "images/icons/%s.png", name);
        found = length > 0 && (size_t)length < sizeof(relative) &&
                Char_FindFile(relative, path);
    }

    if (found) {
        character->icon = Render_LoadTexture(path);
    } else {
        character->icon = Render_LoadTexture(isDad
            ? "assets/images/icons/dad.png"
            : "assets/images/icons/bf.png");
    }
}

static void Char_ApplyXMLMetadata(Character *character,
                                  RayCharacterXMLData *data,
                                  const char *name,
                                  bool isDad)
{
    character->object.animationSet = data->animations;
    data->animations = (RayAnimationHandler){0};

    const float scale = data->hasScale ? data->scale : 1.0f;
    character->object.scaleX = data->flipX ? -scale : scale;
    character->object.scaleY = scale;

    if (data->hasX)
        character->object.position.x = data->x;
    if (data->hasY)
        character->object.position.y = data->y;

    Vector2 camera = isDad ? (Vector2){400.0f, 300.0f}
                           : (Vector2){50.0f, 50.0f};
    if (data->hasCameraX)
        camera.x = data->cameraX;
    if (data->hasCameraY)
        camera.y = data->cameraY;
    character->cameraOffset = camera;

    if (data->hasColor)
        character->object.color = data->color;

    Char_ApplyXMLAnimationSlots(character, &character->object.animationSet);
    AnimatedObject_SetAnimation(&character->object, character->animations.idle);

    Char_LoadIcon(character, name, NULL, isDad);
}

/* ------------------------------------------------------------------------- */
/* XML character loading                                                     */
/* ------------------------------------------------------------------------- */

static bool Char_TryLoadXMLCharacter(Character *character,
                                     const char *name,
                                     bool isDad)
{
    char characterXML[CHAR_PATH_MAX];
    char atlasXML[CHAR_PATH_MAX];
    char pngPath[CHAR_PATH_MAX];
    char spriteName[128];

    if (!Char_FindCharacterXML(name, characterXML, spriteName))
        return false;

    if (!Char_FindAtlasXML(spriteName, name, characterXML, atlasXML) ||
        !Char_FindCharacterPNG(spriteName, name, pngPath)) {
        return false;
    }

    Texture2D texture = Render_LoadTexture(pngPath);
    if (!texture.id)
        return false;

    RayCharacterXMLData data = {0};
    if (!AnimationSet_LoadCharacterXML(characterXML, atlasXML, texture, &data)) {
        UnloadTexture(texture);
        return false;
    }

    if (!data.antialiasing)
        SetTextureFilter(data.animations.texture, TEXTURE_FILTER_POINT);

    Char_ApplyXMLMetadata(character, &data, name, isDad);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Legacy JSON metadata                                                      */
/* ------------------------------------------------------------------------- */

static int Char_ReadJson(const char *jsonPath,
                         CharAnimDef *defs,
                         int capacity,
                         float *scale,
                         bool *flipX,
                         float *cameraX,
                         float *cameraY,
                         char healthIcon[32],
                         char imageBase[32])
{
    *scale = 1.0f;
    *flipX = false;
    *cameraX = 0.0f;
    *cameraY = 0.0f;
    healthIcon[0] = '\0';
    imageBase[0] = '\0';

    FILE *file = fopen(jsonPath, "rb");
    if (!file)
        return 0;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    long fileSize = ftell(file);
    if (fileSize <= 0 || (unsigned long)fileSize > CHAR_JSON_MAX_SIZE ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    char *buffer = malloc((size_t)fileSize + 1u);
    if (!buffer) {
        fclose(file);
        return 0;
    }

    size_t readCount = fread(buffer, 1, (size_t)fileSize, file);
    fclose(file);
    if (readCount != (size_t)fileSize) {
        free(buffer);
        return 0;
    }
    buffer[fileSize] = '\0';

    yyjson_doc *document = yyjson_read(buffer, (size_t)fileSize, 0);
    free(buffer);
    if (!document)
        return 0;

    yyjson_val *root = yyjson_doc_get_root(document);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(document);
        return 0;
    }

    yyjson_val *value = yyjson_obj_get(root, "scale");
    if (yyjson_is_num(value)) {
        double parsed = yyjson_get_real(value);
        if (parsed > 0.05 && parsed < 8.0)
            *scale = (float)parsed;
    }

    value = yyjson_obj_get(root, "flip_x");
    if (yyjson_is_bool(value))
        *flipX = yyjson_get_bool(value);

    value = yyjson_obj_get(root, "camera_position");
    if (yyjson_is_arr(value) && yyjson_arr_size(value) >= 2u) {
        yyjson_val *x = yyjson_arr_get(value, 0);
        yyjson_val *y = yyjson_arr_get(value, 1);
        if (yyjson_is_num(x)) *cameraX = (float)yyjson_get_real(x);
        if (yyjson_is_num(y)) *cameraY = (float)yyjson_get_real(y);
    }

    value = yyjson_obj_get(root, "healthicon");
    if (yyjson_is_str(value)) {
        const char *text = yyjson_get_str(value);
        if (Char_IsSafeName(text))
            Char_CopyString(healthIcon, 32, text);
    }

    value = yyjson_obj_get(root, "image");
    if (yyjson_is_str(value)) {
        const char *text = yyjson_get_str(value);
        if (text && text[0]) {
            char base[128];
            Char_CopyString(base, sizeof(base), Char_BaseName(text));
            char *extension = strrchr(base, '.');
            if (extension)
                *extension = '\0';
            if (Char_IsSafeName(base))
                Char_CopyString(imageBase, 32, base);
        }
    }

    int count = 0;
    value = yyjson_obj_get(root, "animations");
    if (yyjson_is_arr(value)) {
        size_t index, max;
        yyjson_val *entry;

        yyjson_arr_foreach(value, index, max, entry) {
            if (count >= capacity)
                break;
            if (!yyjson_is_obj(entry))
                continue;

            yyjson_val *animValue = yyjson_obj_get(entry, "anim");
            yyjson_val *nameValue = yyjson_obj_get(entry, "name");
            if (!yyjson_is_str(animValue) || !yyjson_is_str(nameValue))
                continue;

            const char *animName = yyjson_get_str(animValue);
            const char *roleName = yyjson_get_str(nameValue);
            if (!animName || !animName[0] || !roleName || !roleName[0])
                continue;

            CharAnimDef *definition = &defs[count];
            Char_CopyString(definition->role, sizeof(definition->role), animName);
            Char_CopyString(definition->sprite, sizeof(definition->sprite), roleName);
            definition->fps = 24;
            definition->looped = false;
            definition->offsetX = 0.0f;
            definition->offsetY = 0.0f;

            yyjson_val *fps = yyjson_obj_get(entry, "fps");
            if (yyjson_is_num(fps)) {
                int parsedFPS = (int)yyjson_get_int(fps);
                if (parsedFPS > 0 && parsedFPS <= 1000)
                    definition->fps = parsedFPS;
            }

            yyjson_val *loop = yyjson_obj_get(entry, "loop");
            if (yyjson_is_bool(loop))
                definition->looped = yyjson_get_bool(loop);

            yyjson_val *offsets = yyjson_obj_get(entry, "offsets");
            if (yyjson_is_arr(offsets) && yyjson_arr_size(offsets) >= 2u) {
                yyjson_val *x = yyjson_arr_get(offsets, 0);
                yyjson_val *y = yyjson_arr_get(offsets, 1);
                if (yyjson_is_num(x)) definition->offsetX = (float)yyjson_get_real(x);
                if (yyjson_is_num(y)) definition->offsetY = (float)yyjson_get_real(y);
            }

            ++count;
        }
    }

    yyjson_doc_free(document);
    return count;
}

static void Char_SetRole(RayAnimationHandler *set,
                         const CharAnimDef *defs,
                         int count,
                         const char *role,
                         int *slot)
{
    *slot = -1;

    for (int i = 0; i < count; ++i) {
        const CharAnimDef *definition = &defs[i];
        if (strcmp(definition->role, role) != 0)
            continue;

        *slot = Char_ConfigureAnimation(set, definition->sprite,
            (float)definition->fps, definition->looped,
            definition->offsetX, definition->offsetY);
        return;
    }
}

static bool Char_LoadLegacyCustom(Character *character,
                                  const char *name,
                                  bool isDad)
{
    char relative[CHAR_PATH_MAX];
    char setPath[CHAR_PATH_MAX];
    char jsonPath[CHAR_PATH_MAX];
    char pngPath[CHAR_PATH_MAX];

    int length = snprintf(relative, sizeof(relative),
        "images/characters/%s.animset", name);
    if (length <= 0 || (size_t)length >= sizeof(relative) ||
        !Char_FindFile(relative, setPath)) {
        return false;
    }

    length = snprintf(relative, sizeof(relative), "characters/%s.json", name);
    bool hasJSON = length > 0 && (size_t)length < sizeof(relative) &&
                   Char_FindFile(relative, jsonPath);

    float scale = 1.0f;
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    bool flipX = false;
    char healthIcon[32] = {0};
    char imageBase[32] = {0};
    CharAnimDef definitions[CHAR_MAX_DEFS];
    int definitionCount = 0;

    if (hasJSON) {
        definitionCount = Char_ReadJson(jsonPath, definitions, CHAR_MAX_DEFS,
            &scale, &flipX, &cameraX, &cameraY, healthIcon, imageBase);
    }

    const char *textureName = imageBase[0] ? imageBase : name;
    length = snprintf(relative, sizeof(relative),
        "images/characters/%s.png", textureName);
    bool hasPNG = length > 0 && (size_t)length < sizeof(relative) &&
                  Char_FindFile(relative, pngPath);

    if (!hasPNG && strcmp(textureName, name) != 0) {
        length = snprintf(relative, sizeof(relative),
            "images/characters/%s.png", name);
        hasPNG = length > 0 && (size_t)length < sizeof(relative) &&
                 Char_FindFile(relative, pngPath);
    }

    if (!hasPNG)
        return false;

    Texture2D texture = Render_LoadTexture(pngPath);
    if (!texture.id)
        return false;

    RayAnimationHandler set = AnimationSet_LoadAnimations(setPath, texture);

    if (!hasJSON || definitionCount == 0) {
        static const char *const roles[] = {
            "idle", "singLEFT", "singDOWN", "singUP", "singRIGHT"
        };

        for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]) &&
             definitionCount < CHAR_MAX_DEFS; ++i) {
            CharAnimDef *definition = &definitions[definitionCount++];
            Char_CopyString(definition->role, sizeof(definition->role), roles[i]);
            Char_CopyString(definition->sprite, sizeof(definition->sprite), roles[i]);
            definition->fps = 24;
            definition->looped = false;
            definition->offsetX = 0.0f;
            definition->offsetY = 0.0f;
        }
    }

    Char_InitAnimationSlots(character);
    Char_SetRole(&set, definitions, definitionCount, "idle", &character->animations.idle);

    static const char *const singRoles[4] = {
        "singLEFT", "singDOWN", "singUP", "singRIGHT"
    };

    for (int i = 0; i < 4; ++i) {
        Char_SetRole(&set, definitions, definitionCount,
            singRoles[i], &character->animations.notes[i]);
        character->missAnimations.notes[i] = character->animations.notes[i];
    }

    if (character->animations.idle < 0 && set.animationCount > 0)
        character->animations.idle = 0;

    character->object.animationSet = set;
    character->object.scaleX = flipX ? -scale : scale;
    character->object.scaleY = scale;
    character->object.color = WHITE;
    character->cameraOffset = (cameraX != 0.0f || cameraY != 0.0f)
        ? (Vector2){cameraX, cameraY}
        : (isDad ? (Vector2){400.0f, 300.0f} : (Vector2){50.0f, 50.0f});

    AnimatedObject_SetAnimation(&character->object, character->animations.idle);
    Char_LoadIcon(character, name, healthIcon, isDad);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Built-in character loading                                                */
/* ------------------------------------------------------------------------- */

static bool Char_LoadBuiltin(Character *character, bool isDad)
{
    const char *name = isDad ? "dad" : "bf";
    char relative[CHAR_PATH_MAX];
    char setPath[CHAR_PATH_MAX];
    char pngPath[CHAR_PATH_MAX];

    int length = snprintf(relative, sizeof(relative),
        "images/characters/%s.animset", name);
    if (length <= 0 || (size_t)length >= sizeof(relative) ||
        !Char_FindFile(relative, setPath)) {
        return false;
    }

    length = snprintf(relative, sizeof(relative), "images/characters/%s.png", name);
    if (length <= 0 || (size_t)length >= sizeof(relative) ||
        !Char_FindFile(relative, pngPath)) {
        return false;
    }

    Texture2D texture = Render_LoadTexture(pngPath);
    if (!texture.id)
        return false;

    RayAnimationHandler set = AnimationSet_LoadAnimations(setPath, texture);
    Char_InitAnimationSlots(character);

    if (isDad) {
        character->animations.idle = Char_ConfigureAnimation(
            &set, "Dad idle dance", 24.0f, false, 0.0f, 0.0f);
        character->animations.notes[0] = Char_ConfigureAnimation(
            &set, "Dad Sing Note LEFT", 24.0f, false, -9.0f, 10.0f);
        character->animations.notes[1] = Char_ConfigureAnimation(
            &set, "Dad Sing Note DOWN", 24.0f, false, 0.0f, -30.0f);
        character->animations.notes[2] = Char_ConfigureAnimation(
            &set, "Dad Sing Note UP", 24.0f, false, -6.0f, 50.0f);
        character->animations.notes[3] = Char_ConfigureAnimation(
            &set, "Dad Sing Note RIGHT", 24.0f, false, 0.0f, 27.0f);
        character->cameraOffset = (Vector2){400.0f, 300.0f};
    } else {
        character->animations.idle = Char_ConfigureAnimation(
            &set, "BF idle dance", 24.0f, false, -5.0f, 0.0f);
        character->animations.notes[0] = Char_ConfigureAnimation(
            &set, "BF NOTE LEFT", 24.0f, false, 5.0f, -6.0f);
        character->animations.notes[1] = Char_ConfigureAnimation(
            &set, "BF NOTE DOWN", 24.0f, false, -20.0f, -51.0f);
        character->animations.notes[2] = Char_ConfigureAnimation(
            &set, "BF NOTE UP", 24.0f, false, -46.0f, 27.0f);
        character->animations.notes[3] = Char_ConfigureAnimation(
            &set, "BF NOTE RIGHT", 24.0f, false, -48.0f, -7.0f);

        character->missAnimations.notes[0] = Char_ConfigureAnimation(
            &set, "BF NOTE LEFT MISS", 24.0f, false, 7.0f, 19.0f);
        character->missAnimations.notes[1] = Char_ConfigureAnimation(
            &set, "BF NOTE DOWN MISS", 24.0f, false, -15.0f, -19.0f);
        character->missAnimations.notes[2] = Char_ConfigureAnimation(
            &set, "BF NOTE UP MISS", 24.0f, false, -46.0f, 27.0f);
        character->missAnimations.notes[3] = Char_ConfigureAnimation(
            &set, "BF NOTE RIGHT MISS", 24.0f, false, -48.0f, 19.0f);
        character->cameraOffset = (Vector2){50.0f, 50.0f};
    }

    if (character->animations.idle < 0 && set.animationCount > 0)
        character->animations.idle = 0;

    character->object.animationSet = set;
    character->object.scaleX = 1.0f;
    character->object.scaleY = 1.0f;
    character->object.color = WHITE;

    AnimatedObject_SetAnimation(&character->object, character->animations.idle);
    Char_LoadIcon(character, name, NULL, isDad);
    return true;
}

void Character_Load(Character *restrict character,
                    const char *restrict characterName,
                    bool isDad)
{
    if (!character)
        return;

    Render_DefaultAnimated(&character->object);
    character->idled = false;
    character->idleTimer = 0.0f;
    Char_InitAnimationSlots(character);

    if (!Char_IsSafeName(characterName))
        characterName = isDad ? "dad" : "bf";

    /* XML definitions take priority, including mod overrides of dad/bf. */
    if (Char_TryLoadXMLCharacter(character, characterName, isDad))
        return;

    const bool nameIsDad = strcmp(characterName, "dad") == 0;
    const bool nameIsBF = strcmp(characterName, "bf") == 0;
    const bool nameIsGF = strcmp(characterName, "gf") == 0;
    const bool useDadFallback = isDad || nameIsDad;

    if (!nameIsDad && !nameIsBF && !nameIsGF &&
        Char_LoadLegacyCustom(character, characterName, isDad)) {
        return;
    }

    if (Char_LoadBuiltin(character, useDadFallback))
        return;

    /* Last-resort fallback: try the base-game BF assets. */
    if (!nameIsBF && Char_LoadBuiltin(character, false))
        return;

    Log_Fatal("Character_Load: could not load character assets");
}

/* ------------------------------------------------------------------------- */
/* Death animations                                                          */
/* ------------------------------------------------------------------------- */

void Character_LoadDeathAnimations(Character *restrict character,
                                   const char *restrict characterName,
                                   int *restrict anims)
{
    (void)characterName;
    if (!character || !anims)
        return;

    RayAnimationHandler *set = &character->object.animationSet;

    static const char *const deathStartNames[] = {
        "firstDeath", "deathStart", "BF dies"
    };
    static const char *const deathLoopNames[] = {
        "deathLoop", "BF Dead Loop"
    };
    static const char *const deathConfirmNames[] = {
        "deathConfirm", "BF Dead confirm"
    };

    anims[0] = Char_FindAnimationAny(set, deathStartNames,
        sizeof(deathStartNames) / sizeof(deathStartNames[0]));
    anims[1] = Char_FindAnimationAny(set, deathLoopNames,
        sizeof(deathLoopNames) / sizeof(deathLoopNames[0]));
    anims[2] = Char_FindAnimationAny(set, deathConfirmNames,
        sizeof(deathConfirmNames) / sizeof(deathConfirmNames[0]));

    const int fallback = character->animations.idle >= 0
        ? character->animations.idle : 0;

    for (int i = 0; i < 3; ++i) {
        if (anims[i] < 0)
            anims[i] = fallback;
    }

    if (anims[1] >= 0 && anims[1] < set->animationCount)
        set->animations[anims[1]].looped = true;
}

/* ------------------------------------------------------------------------- */
/* Girlfriend                                                                */
/* ------------------------------------------------------------------------- */

void Girlfriend_Load(Girlfriend *restrict gf, const char *restrict name)
{
    if (!gf)
        return;

    const char *characterName = (name && Char_IsSafeName(name)) ? name : "gf";
    Render_DefaultAnimated(&gf->object);
    gf->f = 0;

    /* Reuse the same XML/mod discovery logic as regular characters. */
    Character temporary = {0};
    Render_DefaultAnimated(&temporary.object);
    Char_InitAnimationSlots(&temporary);

    if (Char_TryLoadXMLCharacter(&temporary, characterName, false)) {
        gf->object = temporary.object;
        UnloadTexture(temporary.icon);

        static const char *const leftNames[] = {
            "danceLeft", "danceLEFT", "idle", "GF Dancing Beat0"
        };
        static const char *const rightNames[] = {
            "danceRight", "danceRIGHT", "idle", "GF Dancing Beat1"
        };

        gf->anims[0] = Char_FindAnimationAny(&gf->object.animationSet,
            leftNames, sizeof(leftNames) / sizeof(leftNames[0]));
        gf->anims[1] = Char_FindAnimationAny(&gf->object.animationSet,
            rightNames, sizeof(rightNames) / sizeof(rightNames[0]));

        if (gf->anims[0] < 0 && gf->object.animationSet.animationCount > 0)
            gf->anims[0] = 0;
        if (gf->anims[1] < 0)
            gf->anims[1] = gf->anims[0];

        AnimatedObject_SetAnimation(&gf->object, gf->anims[0]);
        return;
    }

    char setPath[CHAR_PATH_MAX];
    char pngPath[CHAR_PATH_MAX];
    char relative[CHAR_PATH_MAX];

    int length = snprintf(relative, sizeof(relative),
        "images/characters/%s.animset", characterName);
    bool hasSet = length > 0 && (size_t)length < sizeof(relative) &&
                  Char_FindFile(relative, setPath);

    length = snprintf(relative, sizeof(relative),
        "images/characters/%s.png", characterName);
    bool hasPNG = length > 0 && (size_t)length < sizeof(relative) &&
                  Char_FindFile(relative, pngPath);

    if (!hasSet || !hasPNG) {
        if (strcmp(characterName, "gf") != 0) {
            length = snprintf(relative, sizeof(relative), "images/characters/gf.animset");
            hasSet = length > 0 && (size_t)length < sizeof(relative) &&
                     Char_FindFile(relative, setPath);
            length = snprintf(relative, sizeof(relative), "images/characters/gf.png");
            hasPNG = length > 0 && (size_t)length < sizeof(relative) &&
                     Char_FindFile(relative, pngPath);
        }
    }

    if (!hasSet || !hasPNG)
        Log_Fatal("Girlfriend_Load: could not locate girlfriend assets");

    Texture2D texture = Render_LoadTexture(pngPath);
    if (!texture.id)
        Log_Fatal("Girlfriend_Load: could not load girlfriend texture");

    RayAnimationHandler set = AnimationSet_LoadAnimations(setPath, texture);
    for (int i = 0; i < set.animationCount && i < 2; ++i) {
        set.animations[i].fps = 24.0f;
        set.animations[i].frameTime = 1.0f / 24.0f;
        set.animations[i].looped = false;
    }

    gf->object.animationSet = set;
    gf->anims[0] = 0;
    gf->anims[1] = set.animationCount > 1 ? 1 : 0;
    gf->object.scaleX = 1.0f;
    gf->object.scaleY = 1.0f;
    gf->object.color = WHITE;
    AnimatedObject_SetAnimation(&gf->object, gf->anims[0]);
}
