/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl, outd day19 of10 2026 */
#include "scenes/Character.h"
#include "yyjson.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define CHAR_MOD_ROOT 256
#define CHAR_PATH_MAX 300
#define CHAR_MAX_DEFS 48

static char charModRoot[CHAR_MOD_ROOT] = {0};

#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__)
#define FORCE_INLINE inline __attribute__((always_inline))
#else
#define FORCE_INLINE inline
#endif

void Character_SetSongDir(const char* restrict songDir) {
    charModRoot[0] = 0;
    if (!songDir) return;
    const char* pm = strstr(songDir, "assets/mods/");
    if (!pm) return;
    const char* after = pm + 12;
    const char* slash = strchr(after, '/');
    if (!slash) return;
    const char* songs = strstr(slash, "/songs/");
    size_t n = (songs ? (size_t)(songs - songDir) : (size_t)(slash - songDir));
    if (n >= sizeof(charModRoot)) n = sizeof(charModRoot) - 1;
    memcpy(charModRoot, songDir, n);
    charModRoot[n] = 0;
}

static FORCE_INLINE bool Char_IsSafeName(const char* restrict s) {
    if (!s || !*s) return false;
    for (; *s; s++) {
        if (*s == '/' || *s == '\\') return false;
        if (s[0] == '.' && s[1] == '.') return false;
    }
    return true;
}

static FORCE_INLINE bool Char_TryFile(const char* restrict path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static FORCE_INLINE bool Char_Find(const char* restrict rel, char out[CHAR_PATH_MAX]) {
    char p[CHAR_PATH_MAX];
    if (charModRoot[0]) {
        int len = snprintf(p, sizeof(p), "%s/%s", charModRoot, rel);
        if (len > 0 && len < (int)sizeof(p) && Char_TryFile(p)) {
            memcpy(out, p, len + 1);
            return true;
        }
    }
    int len = snprintf(p, sizeof(p), "assets/%s", rel);
    if (len > 0 && len < (int)sizeof(p) && Char_TryFile(p)) {
        memcpy(out, p, len + 1);
        return true;
    }
    return false;
}

typedef struct {
    char role[16];
    char sprite[64];
    int fps;
    bool looped;
    float ox, oy;
} CharAnimDef;

static int Char_ReadJson(const char* restrict jsonPath, CharAnimDef* restrict defs, int cap,
    float* restrict scale, bool* restrict flipX, float* restrict camX, float* restrict camY, 
    char* restrict healthicon, char* restrict imageBase) {
    *scale = 1.0f; *flipX = false; *camX = 0; *camY = 0;
    healthicon[0] = 0; imageBase[0] = 0;
    FILE* f = fopen(jsonPath, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 1024 * 1024) { fclose(f); return 0; }
    char* buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    buf[size] = 0;
    fclose(f);
    yyjson_doc* doc = yyjson_read(buf, (size_t)size, 0);
    free(buf);
    if (!doc) return 0;
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) { yyjson_doc_free(doc); return 0; }
    yyjson_val* js = yyjson_obj_get(root, "scale");
    if (yyjson_is_num(js)) {
        double v = yyjson_get_real(js);
        if (v > 0.05 && v < 8.0) *scale = (float)v;
    }
    yyjson_val* jf = yyjson_obj_get(root, "flip_x");
    if (yyjson_is_bool(jf)) *flipX = yyjson_get_bool(jf);
    yyjson_val* jc = yyjson_obj_get(root, "camera_position");
    if (yyjson_is_arr(jc) && yyjson_arr_size(jc) >= 2) {
        yyjson_val *a = yyjson_arr_get(jc, 0), *b = yyjson_arr_get(jc, 1);
        if (yyjson_is_num(a)) *camX = (float)yyjson_get_real(a);
        if (yyjson_is_num(b)) *camY = (float)yyjson_get_real(b);
    }
    yyjson_val* jh = yyjson_obj_get(root, "healthicon");
    if (yyjson_is_str(jh)) {
        const char* s = yyjson_get_str(jh);
        if (s && Char_IsSafeName(s)) { strncpy(healthicon, s, 31); healthicon[31]=0; }
    }
    yyjson_val* ji = yyjson_obj_get(root, "image");
    if (yyjson_is_str(ji)) {
        const char* b = yyjson_get_str(ji);
        if (b && *b) {
            const char* s1 = strrchr(b, '/');
            const char* s2 = strrchr(b, '\\');
            const char* s = (s1 > s2 ? s1 : s2);
            b = s ? s + 1 : b;
            if (Char_IsSafeName(b)) { strncpy(imageBase, b, 31); imageBase[31]=0; }
        }
    }
    int n = 0;
    yyjson_val* ja = yyjson_obj_get(root, "animations");
    if (yyjson_is_arr(ja)) {
        size_t idx, max; yyjson_val* val;
        yyjson_arr_foreach(ja, idx, max, val) {
            if (n >= cap) break;
            yyjson_val *jr = yyjson_obj_get(val, "anim"), *jn = yyjson_obj_get(val, "name");
            if (!yyjson_is_str(jr) || !yyjson_is_str(jn)) continue;
            const char *r = yyjson_get_str(jr), *ns = yyjson_get_str(jn);
            if (!r || !*r || !ns || !*ns) continue;
            CharAnimDef* d = &defs[n];
            strncpy(d->role, r, sizeof(d->role)-1); d->role[sizeof(d->role)-1]=0;
            strncpy(d->sprite, ns, sizeof(d->sprite)-1); d->sprite[sizeof(d->sprite)-1]=0;
            yyjson_val* jfps = yyjson_obj_get(val, "fps");
            d->fps = (yyjson_is_num(jfps) && yyjson_get_int(jfps) > 0) ? (int)yyjson_get_int(jfps) : 24;
            yyjson_val* jl = yyjson_obj_get(val, "loop");
            d->looped = yyjson_is_bool(jl) ? yyjson_get_bool(jl) : false;
            d->ox = 0; d->oy = 0;
            yyjson_val* jo = yyjson_obj_get(val, "offsets");
            if (yyjson_is_arr(jo) && yyjson_arr_size(jo) >= 2) {
                yyjson_val *ox = yyjson_arr_get(jo, 0), *oy = yyjson_arr_get(jo, 1);
                if (yyjson_is_num(ox)) d->ox = (float)yyjson_get_real(ox);
                if (yyjson_is_num(oy)) d->oy = (float)yyjson_get_real(oy);
            }
            n++;
        }
    }
    yyjson_doc_free(doc);
    return n;
}

static FORCE_INLINE void Char_SetRole(RayAnimationHandler* restrict set, CharAnimDef* restrict defs, int ndefs, const char* restrict role, int* restrict slot) {
    for (int i = 0; i < ndefs; i++) {
        if (strcmp(defs[i].role, role) != 0) continue;
        int idx = AnimationSet_FindAnimation(set, defs[i].sprite);
        if (idx < 0) return;
        AnimationSet_SetData(*slot, *set, defs[i].sprite, defs[i].fps, defs[i].looped, defs[i].ox, defs[i].oy);
        return;
    }
    *slot = -1;
}

static bool Character_LoadCustom(Character* restrict character, const char* restrict name, bool isDad) {
    char relSet[CHAR_PATH_MAX], relJson[CHAR_PATH_MAX];
    snprintf(relSet, sizeof(relSet), "images/characters/%s.animset", name);
    snprintf(relJson, sizeof(relJson), "characters/%s.json", name);
    char setPath[CHAR_PATH_MAX], jsonPath[CHAR_PATH_MAX], pngPath[CHAR_PATH_MAX];
    bool hasJson = Char_Find(relJson, jsonPath);
    if (!Char_Find(relSet, setPath)) return false;
    float scale = 1.0f, camX = 0, camY = 0;
    bool flipX = false; char healthicon[32]={0}, imageBase[32]={0};
    CharAnimDef defs[CHAR_MAX_DEFS];
    int ndefs = 0;
    if (hasJson) ndefs = Char_ReadJson(jsonPath, defs, CHAR_MAX_DEFS, &scale, &flipX, &camX, &camY, healthicon, imageBase);
    bool gotPng = false;
    if (imageBase[0]) {
        char relPng[CHAR_PATH_MAX];
        snprintf(relPng, sizeof(relPng), "images/characters/%s.png", imageBase);
        gotPng = Char_Find(relPng, pngPath);
    }
    if (!gotPng) {
        char relPng[CHAR_PATH_MAX];
        snprintf(relPng, sizeof(relPng), "images/characters/%s.png", name);
        gotPng = Char_Find(relPng, pngPath);
    }
    if (!gotPng) return false;
    RayAnimationHandler animSet = AnimationSet_LoadAnimations(setPath, Render_LoadTexture(pngPath));
    if (!hasJson) {
        static const char* roles[5] = {"idle","singLEFT","singDOWN","singUP","singRIGHT"};
        for(int i=0; i<5 && ndefs < CHAR_MAX_DEFS; i++){
            strncpy(defs[ndefs].role, roles[i], 15); defs[ndefs].role[15]=0;
            strncpy(defs[ndefs].sprite, roles[i], 63); defs[ndefs].sprite[63]=0;
            defs[ndefs].fps=24; defs[ndefs].looped=false; defs[ndefs].ox=0; defs[ndefs].oy=0;
            ndefs++;
        }
    }
    character->animations.idle = -1;
    for(int i=0;i<4;i++) character->animations.notes[i] = character->missAnimations.notes[i] = -1;
    Char_SetRole(&animSet, defs, ndefs, "idle", &character->animations.idle);
    static const char* singRoles[4] = {"singLEFT","singDOWN","singUP","singRIGHT"};
    for(int i=0;i<4;i++){
        Char_SetRole(&animSet, defs, ndefs, singRoles[i], &character->animations.notes[i]);
        character->missAnimations.notes[i] = character->animations.notes[i];
    }
    if(character->animations.idle < 0 && animSet.animationCount > 0) character->animations.idle = 0;
    character->object.animationSet = animSet;
    character->object.scaleX = flipX ? -scale : scale;
    character->object.scaleY = scale;
    character->cameraOffset = (camX != 0 || camY != 0) ? (Vector2){camX, camY} : (isDad ? (Vector2){400,300} : (Vector2){50,50});
    AnimatedObject_SetAnimation(&character->object, character->animations.idle);
    char relIcon[CHAR_PATH_MAX], iconPath[CHAR_PATH_MAX];
    const char* iconKey = healthicon[0] ? healthicon : name;
    bool gotIcon = false;
    snprintf(relIcon, sizeof(relIcon), "images/icons/%s.png", iconKey);
    gotIcon = Char_Find(relIcon, iconPath);
    if(!gotIcon && iconKey != name){
        snprintf(relIcon, sizeof(relIcon), "images/icons/%s.png", name);
        gotIcon = Char_Find(relIcon, iconPath);
    }
    character->icon = Render_LoadTexture(gotIcon ? iconPath : "assets/images/icons/bf.png");
    return true;
}

void Character_Load(Character* restrict character, const char* restrict characterName, bool isDad) {
    Render_DefaultAnimated(&character->object);
    character->idled = false;
    character->idleTimer = 0;
    if (Char_IsSafeName(characterName) && strcmp(characterName,"dad")!=0 && strcmp(characterName,"bf")!=0 && strcmp(characterName,"gf")!=0) {
        if (Character_LoadCustom(character, characterName, isDad)) return;
    }
    RayAnimationHandler animSet;
    if (strcmp(characterName,"dad")==0 || isDad) {
        animSet = AnimationSet_LoadAnimations("assets/images/characters/dad.animset", Render_LoadTexture("assets/images/characters/dad.png"));
        AnimationSet_SetData(character->animations.idle, animSet, "Dad idle dance", 24, 0, 0, 0);
        AnimationSet_SetData(character->animations.notes[0], animSet, "Dad Sing Note LEFT", 24, 0, -9, 10);
        AnimationSet_SetData(character->animations.notes[1], animSet, "Dad Sing Note DOWN", 24, 0, 0, -30);
        AnimationSet_SetData(character->animations.notes[2], animSet, "Dad Sing Note UP", 24, 0, -6, 50);
        AnimationSet_SetData(character->animations.notes[3], animSet, "Dad Sing Note RIGHT", 24, 0, 0, 27);
        character->cameraOffset = (Vector2){400,300};
        character->icon = Render_LoadTexture("assets/images/icons/dad.png");
    } else {
        animSet = AnimationSet_LoadAnimations("assets/images/characters/bf.animset", Render_LoadTexture("assets/images/characters/bf.png"));
        AnimationSet_SetData(character->animations.idle, animSet, "BF idle dance", 24, 0, -5, 0);
        AnimationSet_SetData(character->animations.notes[0], animSet, "BF NOTE LEFT", 24, 0, 5, -6);
        AnimationSet_SetData(character->animations.notes[1], animSet, "BF NOTE DOWN", 24, 0, -20, -51);
        AnimationSet_SetData(character->animations.notes[2], animSet, "BF NOTE UP", 24, 0, -46, 27);
        AnimationSet_SetData(character->animations.notes[3], animSet, "BF NOTE RIGHT", 24, 0, -48, -7);
        for(int i=0;i<4;i++) character->missAnimations.notes[i] = -1;
        AnimationSet_SetData(character->missAnimations.notes[0], animSet, "BF NOTE LEFT MISS", 24, 0, 7, 19);
        AnimationSet_SetData(character->missAnimations.notes[1], animSet, "BF NOTE DOWN MISS", 24, 0, -15, -19);
        AnimationSet_SetData(character->missAnimations.notes[2], animSet, "BF NOTE UP MISS", 24, 0, -46, 27);
        AnimationSet_SetData(character->missAnimations.notes[3], animSet, "BF NOTE RIGHT MISS", 24, 0, -48, 19);
        character->cameraOffset = (Vector2){50,50};
        character->icon = Render_LoadTexture("assets/images/icons/bf.png");
    }
    character->object.animationSet = animSet;
    AnimatedObject_SetAnimation(&character->object, character->animations.idle);
}

void Character_LoadDeathAnimations(Character* restrict character, const char* restrict characterName, int* restrict anims) {
    (void)characterName;
    RayAnimationHandler set = character->object.animationSet;
    AnimationSet_SetData(anims[0], set, "BF dies", 24, 0, 37, 11);
    AnimationSet_SetData(anims[1], set, "BF Dead Loop", 24, 1, 37, 5);
    AnimationSet_SetData(anims[2], set, "BF Dead confirm", 24, 0, 37, 69);
    int idle = character->animations.idle < 0 ? 0 : character->animations.idle;
    for(int i=0;i<3;i++) if(anims[i] < 0) anims[i] = idle;
}

void Girlfriend_Load(Girlfriend* restrict gf, const char* restrict name) {
    (void)name;
    Render_DefaultAnimated(&gf->object);
    gf->f = 0;
    RayAnimationHandler s = AnimationSet_LoadAnimations("assets/images/characters/gf.animset", Render_LoadTexture("assets/images/characters/gf.png"));
    AnimationSet_SetAnimationData(&s, 0, 24, 0);
    AnimationSet_SetAnimationData(&s, 1, 24, 0);
    gf->object.animationSet = s;
    gf->anims[0]=0; gf->anims[1]=1;
    AnimatedObject_SetAnimation(&gf->object, gf->anims[0]);
}
