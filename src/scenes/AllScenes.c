/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl - AllScenes.c Optimized, OUTD day19 of10 2026 */

#include "scenes/AllScenes.h"
#include <string.h>
#include <stdbool.h>

#define NOTE_SKIN_MAX 16
#define NOTE_SKIN_NAME_MAX 31

typedef struct {
    RayAnimationHandler animations;
    char name[NOTE_SKIN_NAME_MAX + 1];
    char animationPath[260];
    char texturePath[260];
    char isLoaded;
} CachedNoteSkin; 

typedef struct {
    RayAnimationHandler animations;
    const char* animationPath;
    const char* texturePath;
    char isLoaded;
} CachedAnimation; 

Font mainFont;  
Vector2 VECTOR_ZERO; 

static CachedAnimation notes;
static CachedNoteSkin noteSkins[NOTE_SKIN_MAX];
static int noteSkinCount = 0;

// --- FORWARD DECLARE (fix do erro) ---
static void NoteSkin_SetupAnimations(RayAnimationHandler* h);

static inline void EnsureDefaultLoaded(void) {
    if (notes.isLoaded) return;
    notes.animations = AnimationSet_LoadAnimations(notes.animationPath, Render_LoadTexture(notes.texturePath));
    NoteSkin_SetupAnimations(&notes.animations);
    notes.isLoaded = 1;
}

static inline int FindSkinIndex(const char* name) {
    for (int i = 0; i < noteSkinCount; i++) {
        if (noteSkins[i].name[0] == name[0] && strcmp(noteSkins[i].name, name) == 0) 
            return i;
    }
    return -1;
}

static inline bool TrySkinPaths(const char* xml, const char* png, CachedNoteSkin* out) {
    if (FileExists(xml) && FileExists(png)) {
        strncpy(out->animationPath, xml, sizeof(out->animationPath)-1);
        strncpy(out->texturePath, png, sizeof(out->texturePath)-1);
        out->animationPath[sizeof(out->animationPath)-1] = 0;
        out->texturePath[sizeof(out->texturePath)-1] = 0;
        return true;
    }
    return false;
}

static inline bool ExtractModRoot(const char* songDir, char* outRoot, size_t cap) {
    if (!songDir || !songDir[0]) return false;
    const char* pm = strstr(songDir, "assets/mods/");
    if (!pm) return false;
    const char* slash = strchr(pm + 12, '/');
    if (!slash) return false;
    const char* songs = strstr(slash, "/songs/");
    const char* end = songs ? songs : slash;
    size_t n = (size_t)(end - songDir);
    if (n >= cap) n = cap - 1;
    memcpy(outRoot, songDir, n);
    outRoot[n] = 0;
    return outRoot[0] != 0;
}

static void NoteSkin_InitDefault(void) {
    notes.animationPath = "assets/images/NOTE_assets.animset";
    notes.texturePath   = "assets/images/NOTE_assets.png";
    notes.isLoaded = 0;
}

static void NoteSkins_Init(void) {
    noteSkinCount = 0;
    memset(noteSkins, 0, sizeof(noteSkins));
}

static void NoteSkin_SetupAnimations(RayAnimationHandler* h) {
    static const struct { const char* name; int fps; int ox, oy; } defs[] = {
        {"arrowDOWN", 1, 0, 0}, {"arrowUP", 1, 0, 0}, {"arrowLEFT", 1, 0, 0}, {"arrowRIGHT", 1, 0, 0},
        {"left press", 24, 0, -4}, {"right press", 24, 0, -4}, {"up press", 24, 0, -4}, {"down press", 24, 0, -4},
        {"left confirm", 24, 0, 37}, {"right confirm", 24, 0, 37}, {"up confirm", 24, 0, 37}, {"down confirm", 24, 0, 37},
    };
    for (int i = 0; i < (int)(sizeof(defs)/sizeof(defs[0])); i++) {
        int dummy = 0;
        AnimationSet_SetData(dummy, *h, defs[i].name, defs[i].fps, 0, defs[i].ox, defs[i].oy);
    }
}

static char NoteSkin_LoadFromMod(const char* skinName, const char* songDir, CachedNoteSkin* out) {
    char xmlPath[260], pngPath[260];
    char modRoot[260] = {0};
    if (ExtractModRoot(songDir, modRoot, sizeof(modRoot))) {
        snprintf(xmlPath, sizeof(xmlPath), "%s/images/NOTE_%s_assets.xml", modRoot, skinName);
        snprintf(pngPath, sizeof(pngPath), "%s/images/NOTE_%s_assets.png", modRoot, skinName);
        if (TrySkinPaths(xmlPath, pngPath, out)) return 1;
    }
    snprintf(xmlPath, sizeof(xmlPath), "assets/images/NOTE_%s_assets.xml", skinName);
    snprintf(pngPath, sizeof(pngPath), "assets/images/NOTE_%s_assets.png", skinName);
    if (TrySkinPaths(xmlPath, pngPath, out)) return 1;
    snprintf(xmlPath, sizeof(xmlPath), "assets/images/NOTE_%s_assets.animset", skinName);
    if (TrySkinPaths(xmlPath, pngPath, out)) return 1;
    return 0;
}

RayAnimationHandler Cache_GetNoteSkin(const char* skinName, const char* songDir) {
    if (!skinName || !skinName[0]) { EnsureDefaultLoaded(); return notes.animations; }
    int idx = FindSkinIndex(skinName);
    if (idx >= 0) {
        if (!noteSkins[idx].isLoaded) {
            noteSkins[idx].animations = AnimationSet_LoadAnimations(noteSkins[idx].animationPath, Render_LoadTexture(noteSkins[idx].texturePath));
            NoteSkin_SetupAnimations(&noteSkins[idx].animations);
            noteSkins[idx].isLoaded = 1;
        }
        return noteSkins[idx].animations;
    }
    if (noteSkinCount < NOTE_SKIN_MAX) {
        CachedNoteSkin* slot = &noteSkins[noteSkinCount];
        strncpy(slot->name, skinName, NOTE_SKIN_NAME_MAX);
        slot->name[NOTE_SKIN_NAME_MAX] = 0;
        slot->isLoaded = 0;
        if (NoteSkin_LoadFromMod(skinName, songDir, slot)) {
            slot->animations = AnimationSet_LoadAnimations(slot->animationPath, Render_LoadTexture(slot->texturePath));
            NoteSkin_SetupAnimations(&slot->animations);
            slot->isLoaded = 1;
            noteSkinCount++;
            return slot->animations;
        }
        slot->name[0] = 0;
    }
    EnsureDefaultLoaded();
    return notes.animations;
}

const RayAnimationHandler* Cache_GetNoteSkinPtr(const char* skinName, const char* songDir) {
    if (!skinName || !skinName[0]) { EnsureDefaultLoaded(); return &notes.animations; }
    int idx = FindSkinIndex(skinName);
    if (idx >= 0) {
        if (!noteSkins[idx].isLoaded) {
            noteSkins[idx].animations = AnimationSet_LoadAnimations(noteSkins[idx].animationPath, Render_LoadTexture(noteSkins[idx].texturePath));
            NoteSkin_SetupAnimations(&noteSkins[idx].animations);
            noteSkins[idx].isLoaded = 1;
        }
        return &noteSkins[idx].animations;
    }
    if (noteSkinCount < NOTE_SKIN_MAX) {
        CachedNoteSkin* slot = &noteSkins[noteSkinCount];
        strncpy(slot->name, skinName, NOTE_SKIN_NAME_MAX);
        slot->name[NOTE_SKIN_NAME_MAX] = 0;
        slot->isLoaded = 0;
        if (NoteSkin_LoadFromMod(skinName, songDir, slot)) {
            slot->animations = AnimationSet_LoadAnimations(slot->animationPath, Render_LoadTexture(slot->texturePath));
            NoteSkin_SetupAnimations(&slot->animations);
            slot->isLoaded = 1;
            noteSkinCount++;
            return &slot->animations;
        }
        slot->name[0] = 0;
    }
    EnsureDefaultLoaded();
    return &notes.animations;
}

void Cache_UnloadCustomNoteSkins(void) {
    for (int i = 0; i < noteSkinCount; i++) if (noteSkins[i].isLoaded) AnimationSet_FreeAll(&noteSkins[i].animations);
    memset(noteSkins, 0, sizeof(noteSkins));
    noteSkinCount = 0;
}
RayAnimationHandler* Cache_GetDefaultNoteSkinPtr(void) { EnsureDefaultLoaded(); return &notes.animations; }
RayAnimationHandler Cache_GetNoteAnimations(void) { EnsureDefaultLoaded(); return notes.animations; }

void AllScenes_StartGame(void) {
    VECTOR_ZERO = (Vector2){0, 0};
    RayGame_SetMusic("assets/music/freakyMenu.ogg", 0.3f, 1);
    RayGame_ToggleMusic(1);
    mainFont = LoadFontEx("assets/fonts/main.ttf", 64, 0, 0);
    //SetTextureFilter(mainFont.texture, TEXTURE_FILTER_BILINEAR);  
    NoteSkin_InitDefault();
    NoteSkins_Init();
}
void AllScenes_DestroyGame(void) {
    if (notes.isLoaded) AnimationSet_FreeAll(&notes.animations);
    Cache_UnloadCustomNoteSkins();
    UnloadFont(mainFont);  
}