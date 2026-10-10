#include "scenes/AllScenes.h"

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
    char* animationPath;
    char* texturePath;
    char isLoaded;
} CachedAnimation; 

Font mainFont;  
Vector2 VECTOR_ZERO; 

static CachedAnimation notes;  // legacy global default (NOTE_assets)
static CachedNoteSkin noteSkins[NOTE_SKIN_MAX];
static int noteSkinCount = 0;

static void NoteSkin_InitDefault(void) {
    notes.animationPath = "assets/images/NOTE_assets.animset";
    notes.texturePath   = "assets/images/NOTE_assets.png";
    notes.isLoaded = 0;
}

static void NoteSkins_Init(void) {
    noteSkinCount = 0;
    for(int i = 0; i < NOTE_SKIN_MAX; i++) {
        noteSkins[i].name[0] = 0;
        noteSkins[i].isLoaded = 0;
    }
}

// carrega skin do mod (NOTE_<name>_assets.xml/.png) ou global (assets/images/NOTE_<name>_assets.xml/.png)
// retorna 1 se carregou, 0 se nao achou
static char NoteSkin_LoadFromMod(const char* skinName, const char* songDir, CachedNoteSkin* out) {
    char xmlPath[260], pngPath[260];
    FILE* f = NULL;

    // 1) tenta no mod da musica: <songDir>/../images/NOTE_<skinName>_assets.xml/.png
    if(songDir && songDir[0]) {
        const char* pm = strstr(songDir, "assets/mods/");
        if(pm) {
            const char* after = pm + 12;
            const char* slash = strchr(after, '/');
            char modRoot[260] = {0};
            if(slash) {
                const char* songs = strstr(slash, "/songs/");
                size_t n = (size_t)((songs ? songs : slash) - songDir);
                if(n < sizeof(modRoot)) {
                    memcpy(modRoot, songDir, n);
                    modRoot[n] = 0;
                }
            }
            if(modRoot[0]) {
                snprintf(xmlPath, sizeof(xmlPath), "%s/images/NOTE_%s_assets.xml", modRoot, skinName);
                snprintf(pngPath, sizeof(pngPath), "%s/images/NOTE_%s_assets.png", modRoot, skinName);
                f = fopen(xmlPath, "rb");
                if(f) { fclose(f); f = fopen(pngPath, "rb"); if(f) fclose(f); else f = NULL; }
                if(f) {
                    strncpy(out->animationPath, xmlPath, sizeof(out->animationPath) - 1);
                    strncpy(out->texturePath, pngPath, sizeof(out->texturePath) - 1);
                    return 1;
                }
            }
        }
    }

    // 2) tenta global: assets/images/NOTE_<skinName>_assets.xml/.png
    snprintf(xmlPath, sizeof(xmlPath), "assets/images/NOTE_%s_assets.xml", skinName);
    snprintf(pngPath, sizeof(pngPath), "assets/images/NOTE_%s_assets.png", skinName);
    f = fopen(xmlPath, "rb");
    if(f) { fclose(f); f = fopen(pngPath, "rb"); if(f) fclose(f); else f = NULL; }
    if(f) {
        strncpy(out->animationPath, xmlPath, sizeof(out->animationPath) - 1);
        strncpy(out->texturePath, pngPath, sizeof(out->texturePath) - 1);
        return 1;
    }

    // 3) fallback .animset (binario antigo)
    snprintf(xmlPath, sizeof(xmlPath), "assets/images/NOTE_%s_assets.animset", skinName);
    snprintf(pngPath, sizeof(pngPath), "assets/images/NOTE_%s_assets.png", skinName);
    f = fopen(xmlPath, "rb");
    if(f) { fclose(f); f = fopen(pngPath, "rb"); if(f) fclose(f); else f = NULL; }
    if(f) {
        strncpy(out->animationPath, xmlPath, sizeof(out->animationPath) - 1);
        strncpy(out->texturePath, pngPath, sizeof(out->texturePath) - 1);
        return 1;
    }

    return 0;
}

// inicializa animation handler com dados padrao das setas (fps, offsets)
static void NoteSkin_SetupAnimations(RayAnimationHandler* handler) {
    int _;
    AnimationSet_SetData(_, *handler, "arrowDOWN", 1, 0, 0, 0);
    AnimationSet_SetData(_, *handler, "arrowUP", 1, 0, 0, 0);
    AnimationSet_SetData(_, *handler, "arrowLEFT", 1, 0, 0, 0);
    AnimationSet_SetData(_, *handler, "arrowRIGHT", 1, 0, 0, 0); 
    AnimationSet_SetData(_, *handler, "left press", 24, 0, -4, -4);
    AnimationSet_SetData(_, *handler, "right press", 24, 0, -4, -4);
    AnimationSet_SetData(_, *handler, "up press", 24, 0, -4, -4);
    AnimationSet_SetData(_, *handler, "down press", 24, 0, -4, -4);
    AnimationSet_SetData(_, *handler, "left confirm", 24, 0, 37, 37);
    AnimationSet_SetData(_, *handler, "right confirm", 24, 0, 37, 37);
    AnimationSet_SetData(_, *handler, "up confirm", 24, 0, 37, 37);
    AnimationSet_SetData(_, *handler, "down confirm", 24, 0, 37, 37);
}

// pega skin pelo nome; se nao achar, retorna default global
// songDir usado pra resolver caminho do mod
RayAnimationHandler Cache_GetNoteSkin(const char* skinName, const char* songDir) {
    if(skinName == NULL || skinName[0] == 0) {
        // default global
        if(!notes.isLoaded) {
            notes.animations = AnimationSet_LoadAnimations(notes.animationPath, Render_LoadTexture(notes.texturePath));
            NoteSkin_SetupAnimations(&notes.animations);
            notes.isLoaded = 1;
        }
        return notes.animations;
    }

    // procura no cache
    for(int i = 0; i < noteSkinCount; i++) {
        if(strcmp(noteSkins[i].name, skinName) == 0) {
            if(!noteSkins[i].isLoaded) {
                noteSkins[i].animations = AnimationSet_LoadAnimations(noteSkins[i].animationPath, Render_LoadTexture(noteSkins[i].texturePath));
                NoteSkin_SetupAnimations(&noteSkins[i].animations);
                noteSkins[i].isLoaded = 1;
            }
            return noteSkins[i].animations;
        }
    }

    // nao esta no cache: tenta carregar
    if(noteSkinCount < NOTE_SKIN_MAX) {
        CachedNoteSkin* slot = &noteSkins[noteSkinCount];
        strncpy(slot->name, skinName, NOTE_SKIN_NAME_MAX);
        slot->name[NOTE_SKIN_NAME_MAX] = 0;
        if(NoteSkin_LoadFromMod(skinName, songDir, slot)) {
            slot->animations = AnimationSet_LoadAnimations(slot->animationPath, Render_LoadTexture(slot->texturePath));
            NoteSkin_SetupAnimations(&slot->animations);
            slot->isLoaded = 1;
            noteSkinCount++;
            return slot->animations;
        }
        // falhou: limpa slot
        slot->name[0] = 0;
    }

    // fallback pro default global
    if(!notes.isLoaded) {
        notes.animations = AnimationSet_LoadAnimations(notes.animationPath, Render_LoadTexture(notes.texturePath));
        NoteSkin_SetupAnimations(&notes.animations);
        notes.isLoaded = 1;
    }
    return notes.animations;
}

// descarrega todas as custom skins (chama no fim da musica / troca de musica)
void Cache_UnloadCustomNoteSkins(void) {
    for(int i = 0; i < noteSkinCount; i++) {
        if(noteSkins[i].isLoaded) {
            AnimationSet_FreeAll(&noteSkins[i].animations);
            noteSkins[i].isLoaded = 0;
        }
        noteSkins[i].name[0] = 0;
    }
    noteSkinCount = 0;
}

// retorna ponteiro pro default global (usado por Note.c pra pegar endereco valido)
RayAnimationHandler* Cache_GetDefaultNoteSkinPtr(void) {
    if(!notes.isLoaded) {
        notes.animations = AnimationSet_LoadAnimations(notes.animationPath, Render_LoadTexture(notes.texturePath));
        NoteSkin_SetupAnimations(&notes.animations);
        notes.isLoaded = 1;
    }
    return &notes.animations;
}

// compatibilidade: retorna default global (usado por trail, end, etc que nao dependem de skin)
RayAnimationHandler Cache_GetNoteAnimations(void) {
    return Cache_GetNoteSkin(NULL, NULL);
}

void AllScenes_StartGame(void) {
    VECTOR_ZERO = (Vector2) {0, 0};
    
    // music
    RayGame_SetMusic("assets/music/freakyMenu.ogg", 0.3f, 1);
    RayGame_ToggleMusic(1);

    // fonts
    {
        mainFont = LoadFontEx("assets/fonts/main.ttf", 64, 0, 0);
        SetTextureFilter(mainFont.texture, TEXTURE_FILTER_BILINEAR);  
    }

    // cached items
    NoteSkin_InitDefault();
    NoteSkins_Init();
}

void AllScenes_DestroyGame(void) {
    if(notes.isLoaded)
        AnimationSet_FreeAll(&notes.animations);
    Cache_UnloadCustomNoteSkins();
    UnloadFont(mainFont);  
}