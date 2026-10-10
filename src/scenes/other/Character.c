#include"scenes/Character.h"
#include"cJSON.h"

// raiz do mod extraida do songDir (assets/mods/<mod>/...), "" = base.
static char charModRoot[256] = {0};

void Character_SetSongDir(const char* songDir) {
    charModRoot[0] = 0;
    if(songDir == NULL)
        return;
    const char* pm = strstr(songDir, "assets/mods/");
    if(pm == NULL)
        return;
    const char* after = pm + 12;
    const char* slash = strchr(after, '/');
    const char* songs = (slash != NULL) ? strstr(slash, "/songs/") : NULL;
    size_t n = (size_t)((songs != NULL ? songs : slash) - songDir);
    if(slash == NULL)
        n = strlen(songDir);
    if(n >= sizeof(charModRoot))
        n = sizeof(charModRoot) - 1;
    memcpy(charModRoot, songDir, n);
    charModRoot[n] = 0;
}

static char Char_Safe(const char* s) {
    if(s == NULL || s[0] == 0)
        return 0;
    if(strchr(s, '/') != NULL || strchr(s, '\\') != NULL || strstr(s, "..") != NULL)
        return 0;
    return 1;
}

static char Char_TryFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if(f == NULL)
        return 0;
    fclose(f);
    return 1;
}

// procura <rel> no mod e no global. Retorna 1 com o caminho em out.
static char Char_Find(const char* rel, char out[300]) {
    char p[300];
    if(charModRoot[0] != 0) {
        snprintf(p, sizeof(p), "%s/%s", charModRoot, rel);
        if(Char_TryFile(p)) {
            strncpy(out, p, 299);
            out[299] = 0;
            return 1;
        }
    }
    snprintf(p, sizeof(p), "assets/%s", rel);
    if(Char_TryFile(p)) {
        strncpy(out, p, 299);
        out[299] = 0;
        return 1;
    }
    return 0;
}

typedef struct {
    char role[16];
    char sprite[64];
    int fps;
    char looped;
    float ox, oy;
} CharAnimDef;

// Le animations[] + scale/flip/camera/healthicon/healthbar_colors do character .json (Psych).
// Retorna defs em defs (cap) e metadados. Sem alloc.
static int Char_ReadJson(const char* jsonPath, CharAnimDef* defs, int cap,
    float* scale, char* flipX, float* camX, float* camY, char* healthicon,
    char* imageBase, Color* healthColor) {
    *scale = 1.0f;
    *flipX = 0;
    *camX = 0;
    *camY = 0;
    healthicon[0] = 0;
    imageBase[0] = 0;
    FILE* f = fopen(jsonPath, "rb");
    if(f == NULL)
        return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0 || size > 1024 * 1024) {
        fclose(f);
        return 0;
    }
    char* buf = malloc((size_t)size + 1);
    if(buf == NULL) {
        fclose(f);
        return 0;
    }
    if(fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return 0;
    }
    buf[size] = 0;
    fclose(f);
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if(root == NULL)
        return 0;
    cJSON* js = cJSON_GetObjectItem(root, "scale");
    if(cJSON_IsNumber(js) && js->valuedouble > 0.05 && js->valuedouble < 8)
        *scale = (float)js->valuedouble;
    cJSON* jf = cJSON_GetObjectItem(root, "flip_x");
    if(cJSON_IsTrue(jf))
        *flipX = 1;
    cJSON* jc = cJSON_GetObjectItem(root, "camera_position");
    if(cJSON_IsArray(jc) && cJSON_GetArraySize(jc) >= 2) {
        cJSON* a = cJSON_GetArrayItem(jc, 0);
        cJSON* b = cJSON_GetArrayItem(jc, 1);
        if(cJSON_IsNumber(a))
            *camX = (float)a->valuedouble;
        if(cJSON_IsNumber(b))
            *camY = (float)b->valuedouble;
    }
    cJSON* jh = cJSON_GetObjectItem(root, "healthicon");
    if(cJSON_IsString(jh) && jh->valuestring[0] != 0 && Char_Safe(jh->valuestring)) {
        strncpy(healthicon, jh->valuestring, 31);
        healthicon[31] = 0;
    }
    // base do png: "image": "characters/base" -> "base" (senao usa o nome)
    cJSON* ji = cJSON_GetObjectItem(root, "image");
    if(cJSON_IsString(ji) && ji->valuestring[0] != 0) {
        const char* b = ji->valuestring;
        const char* s1 = strrchr(b, '/');
        const char* s2 = strrchr(b, '\\');
        const char* s = (s1 > s2) ? s1 : s2;
        b = (s != NULL) ? s + 1 : b;
        if(b[0] != 0 && Char_Safe(b)) {
            strncpy(imageBase, b, 31);
            imageBase[31] = 0;
        }
    }
    // cor da barra: Psych usa "healthbar_colors": [R,G,B]. Aceita singular + hex.
    {
        cJSON* jc = cJSON_GetObjectItem(root, "healthbar_colors");
        if(jc == NULL) jc = cJSON_GetObjectItem(root, "healthbar_color");
        if(jc == NULL) jc = cJSON_GetObjectItem(root, "health_color");
        if(cJSON_IsArray(jc) && cJSON_GetArraySize(jc) >= 3) {
            cJSON* r = cJSON_GetArrayItem(jc, 0);
            cJSON* g = cJSON_GetArrayItem(jc, 1);
            cJSON* b2 = cJSON_GetArrayItem(jc, 2);
            if(cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b2)) {
                int ri = r->valueint, gi = g->valueint, bi = b2->valueint;
                if(ri < 0) ri = 0; if(ri > 255) ri = 255;
                if(gi < 0) gi = 0; if(gi > 255) gi = 255;
                if(bi < 0) bi = 0; if(bi > 255) bi = 255;
                if(healthColor != NULL)
                    *healthColor = (Color){(unsigned char)ri, (unsigned char)gi, (unsigned char)bi, 255};
            }
        } else if(cJSON_IsString(jc) && jc->valuestring[0] != 0 && healthColor != NULL) {
            const char* s = jc->valuestring;
            if(s[0] == '#') s++;
            else if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
            unsigned int hex = 0;
            int ok = 1;
            for(int k = 0; k < 6; k++) {
                char c = s[k];
                int v = -1;
                if(c >= '0' && c <= '9') v = c - '0';
                else if(c >= 'a' && c <= 'f') v = c - 'a' + 10;
                else if(c >= 'A' && c <= 'F') v = c - 'A' + 10;
                else { ok = 0; break; }
                hex = (hex << 4) | (unsigned int)v;
            }
            if(ok)
                *healthColor = (Color){(unsigned char)((hex >> 16) & 0xFF), (unsigned char)((hex >> 8) & 0xFF), (unsigned char)(hex & 0xFF), 255};
        }
    }
    int n = 0;
    cJSON* ja = cJSON_GetObjectItem(root, "animations");
    if(cJSON_IsArray(ja)) {
        int total = cJSON_GetArraySize(ja);
        for(int i = 0; i < total && n < cap; i++) {
            cJSON* a = cJSON_GetArrayItem(ja, i);
            cJSON* jr = cJSON_GetObjectItem(a, "anim");
            cJSON* jn = cJSON_GetObjectItem(a, "name");
            if(!cJSON_IsString(jr) || !cJSON_IsString(jn))
                continue;
            if(jr->valuestring[0] == 0 || jn->valuestring[0] == 0)
                continue;
            CharAnimDef* d = defs + n;
            strncpy(d->role, jr->valuestring, sizeof(d->role) - 1);
            d->role[sizeof(d->role) - 1] = 0;
            strncpy(d->sprite, jn->valuestring, sizeof(d->sprite) - 1);
            d->sprite[sizeof(d->sprite) - 1] = 0;
            cJSON* jfps = cJSON_GetObjectItem(a, "fps");
            d->fps = (cJSON_IsNumber(jfps) && jfps->valueint > 0) ? jfps->valueint : 24;
            cJSON* jl = cJSON_GetObjectItem(a, "loop");
            d->looped = cJSON_IsTrue(jl) ? 1 : 0;
            d->ox = 0;
            d->oy = 0;
            cJSON* jo = cJSON_GetObjectItem(a, "offsets");
            if(cJSON_IsArray(jo) && cJSON_GetArraySize(jo) >= 2) {
                cJSON* ox = cJSON_GetArrayItem(jo, 0);
                cJSON* oy = cJSON_GetArrayItem(jo, 1);
                if(cJSON_IsNumber(ox))
                    d->ox = (float)ox->valuedouble;
                if(cJSON_IsNumber(oy))
                    d->oy = (float)oy->valuedouble;
            }
            n++;
        }
    }
    cJSON_Delete(root);
    return n;
}

static void Char_SetRole(RayAnimationHandler* set,
    CharAnimDef* defs, int ndefs, const char* role, int* slot) {
    for(int i = 0; i < ndefs; i++) {
        if(strcmp(defs[i].role, role) != 0)
            continue;
        int idx = AnimationSet_FindAnimation(set, defs[i].sprite);
        if(idx < 0)
            return;
        AnimationSet_SetData(*slot, *set, defs[i].sprite, defs[i].fps, defs[i].looped, defs[i].ox, defs[i].oy);
        return;
    }
    *slot = -1;
}

// custom data-driven (mod e futuro): png+animset por nome + .json Psych.
// 1 = carregou custom, 0 = cair no fallback dad/bf.
static char Character_LoadCustom(Character* character, const char* name, char isDad) {
    char relSet[300], relJson[300];
    snprintf(relSet, sizeof(relSet), "images/characters/%s.animset", name);
    snprintf(relJson, sizeof(relJson), "characters/%s.json", name);
    char set[300], json[300], png[300];
    // json e opcional (sem ele usa roles padrao Psych nos nomes do .animset)
    char hasJson = Char_Find(relJson, json);
    if(!Char_Find(relSet, set))
        return 0;

    float scale = 1.0f, camX = 0, camY = 0;
    char flipX = 0, healthicon[32] = {0}, imageBase[32] = {0};
    character->healthColor = isDad ? (Color){230, 41, 55, 255} : (Color){42, 209, 86, 255};
    CharAnimDef defs[48];
    int ndefs = 0;
    if(hasJson)
        ndefs = Char_ReadJson(json, defs, 48, &scale, &flipX, &camX, &camY, healthicon, imageBase, &character->healthColor);

    // png: base do campo "image" ("characters/base"), senao o proprio nome
    char gotPng = 0;
    if(imageBase[0] != 0) {
        char relPng[300];
        snprintf(relPng, sizeof(relPng), "images/characters/%s.png", imageBase);
        gotPng = Char_Find(relPng, png);
    }
    if(!gotPng && (imageBase[0] == 0 || strcmp(imageBase, name) != 0)) {
        char relPng[300];
        snprintf(relPng, sizeof(relPng), "images/characters/%s.png", name);
        gotPng = Char_Find(relPng, png);
    }
    if(!gotPng)
        return 0;

    RayAnimationHandler characterAnimations =
        AnimationSet_LoadAnimations(set, Render_LoadTexture(png));

    if(!hasJson) {
        // sem .json: tenta os nomes padrao da Psych direto no .animset
        static const char* roles[5] = {"idle", "singLEFT", "singDOWN", "singUP", "singRIGHT"};
        static const char* sprs[5] = {"idle", "singLEFT", "singDOWN", "singUP", "singRIGHT"};
        for(int i = 0; i < 5 && ndefs < 48; i++) {
            strncpy(defs[ndefs].role, roles[i], sizeof(defs[ndefs].role) - 1);
            strncpy(defs[ndefs].sprite, sprs[i], sizeof(defs[ndefs].sprite) - 1);
            defs[ndefs].fps = 24;
            defs[ndefs].looped = 0;
            defs[ndefs].ox = 0;
            defs[ndefs].oy = 0;
            ndefs++;
        }
    }

    character->animations.idle = -1;
    for(int i = 0; i < 4; i++) {
        character->animations.notes[i] = -1;
        character->missAnimations.notes[i] = -1;
    }
    Char_SetRole(&characterAnimations, defs, ndefs, "idle", &character->animations.idle);
    static const char* singRoles[4] = {"singLEFT", "singDOWN", "singUP", "singRIGHT"};
    for(int i = 0; i < 4; i++) {
        Char_SetRole(&characterAnimations, defs, ndefs, singRoles[i], &character->animations.notes[i]);
        // Psych nao tem miss: reusa o sing
        character->missAnimations.notes[i] = character->animations.notes[i];
    }
    if(character->animations.idle < 0 && characterAnimations.animationCount > 0)
        character->animations.idle = 0; // ultimo recurso: primeira anim

    character->object.animationSet = characterAnimations;
    character->object.scaleX = flipX ? -scale : scale;
    character->object.scaleY = scale;
    if(camX != 0 || camY != 0)
        character->cameraOffset = (Vector2) {camX, camY};
    else
        character->cameraOffset = isDad ? (Vector2) {400, 300} : (Vector2) {50, 50};
    AnimatedObject_SetAnimation(&character->object, character->animations.idle);

    // icone: healthicon (ou nome) no mod, depois global, senao bf (fallback universal)
    char relIcon[300], iconPath[300];
    const char* iconKey = (healthicon[0] != 0) ? healthicon : name;
    char gotIcon = 0;
    snprintf(relIcon, sizeof(relIcon), "images/icons/%s.png", iconKey);
    if(Char_Find(relIcon, iconPath))
        gotIcon = 1;
    else if(iconKey != name) {
        snprintf(relIcon, sizeof(relIcon), "images/icons/%s.png", name);
        if(Char_Find(relIcon, iconPath))
            gotIcon = 1;
    }
    if(gotIcon)
        character->icon = Render_LoadTexture(iconPath);
    else
        character->icon = Render_LoadTexture("assets/images/icons/bf.png"); // bf como fallback universal
    return 1;
}

void Character_Load(Character* character, char* characterName, char isDad) {
    Render_DefaultAnimated(&character->object);
    RayAnimationHandler characterAnimations;

    character->idled = 0;
    character->idleTimer = 0;

    // custom primeiro (mod/global); base cai no builtin
    if(Char_Safe(characterName) && strcmp(characterName, "dad") != 0 && strcmp(characterName, "bf") != 0 &&
        strcmp(characterName, "gf") != 0) {
        if(Character_LoadCustom(character, characterName, isDad))
            return;
        // sem asset custom: bf/dad padrao (comportamento antigo preservado)
    }

    if(strcmp(characterName, "dad") == 0 || isDad) {
        characterAnimations = AnimationSet_LoadAnimations("assets/images/characters/dad.animset", Render_LoadTexture("assets/images/characters/dad.png"));

        AnimationSet_SetData(character->animations.idle, characterAnimations, "Dad idle dance", 24, 0, 0, 0);
        AnimationSet_SetData(character->animations.notes[0], characterAnimations, "Dad Sing Note LEFT", 24, 0, -9, 10);
        AnimationSet_SetData(character->animations.notes[1], characterAnimations, "Dad Sing Note DOWN", 24, 0, 0, -30);
        AnimationSet_SetData(character->animations.notes[2], characterAnimations, "Dad Sing Note UP", 24, 0, -6, 50);
        AnimationSet_SetData(character->animations.notes[3], characterAnimations, "Dad Sing Note RIGHT", 24, 0, 0, 27);

        character->cameraOffset = (Vector2) {400, 300};
        character->icon = Render_LoadTexture("assets/images/icons/dad.png");
        character->healthColor = (Color){230, 41, 55, 255};
    }
    else if(strcmp(characterName, "bf") == 0 || !isDad) {
        characterAnimations = AnimationSet_LoadAnimations("assets/images/characters/bf.animset", Render_LoadTexture("assets/images/characters/bf.png"));

        AnimationSet_SetData(character->animations.idle, characterAnimations, "BF idle dance", 24, 0, -5, 0);
        AnimationSet_SetData(character->animations.notes[0], characterAnimations, "BF NOTE LEFT", 24, 0, 5, -6);
        AnimationSet_SetData(character->animations.notes[1], characterAnimations, "BF NOTE DOWN", 24, 0, -20, -51);
        AnimationSet_SetData(character->animations.notes[2], characterAnimations, "BF NOTE UP", 24, 0, -46, 27);
        AnimationSet_SetData(character->animations.notes[3], characterAnimations, "BF NOTE RIGHT", 24, 0, -48, -7);

        AnimationSet_SetData(character->missAnimations.notes[0], characterAnimations, "BF NOTE LEFT MISS", 24, 0, 7, 19);
        AnimationSet_SetData(character->missAnimations.notes[1], characterAnimations, "BF NOTE DOWN MISS", 24, 0, -15, -19);
        AnimationSet_SetData(character->missAnimations.notes[2], characterAnimations, "BF NOTE UP MISS", 24, 0, -46, 27);
        AnimationSet_SetData(character->missAnimations.notes[3], characterAnimations, "BF NOTE RIGHT MISS", 24, 0, -48, 19);

        character->cameraOffset = (Vector2) {50, 50};
        character->icon = Render_LoadTexture("assets/images/icons/bf.png");
        character->healthColor = (Color){42, 209, 86, 255};
    }
    character->object.animationSet = characterAnimations;
    AnimatedObject_SetAnimation(&character->object, character->animations.idle);
}

// 3 animations, [ die, loop, confirm ]
void Character_LoadDeathAnimations(Character* character, char* characterName, int* anims) {
    RayAnimationHandler set = character->object.animationSet;

    AnimationSet_SetData(anims[0], set, "BF dies", 24, 0, 37, 11);
    AnimationSet_SetData(anims[1], set, "BF Dead Loop", 24, 1, 37, 5);
    AnimationSet_SetData(anims[2], set, "BF Dead confirm", 24, 0, 37, 69);

    // char custom nao tem anim de morte: cai no idle (sempre valido) em vez de tela preta
    (void)characterName;
    int idle = character->animations.idle;
    if(idle < 0 && set.animationCount > 0)
        idle = 0;
    for(int i = 0; i < 3; i++) {
        if(anims[i] < 0)
            anims[i] = idle;
    }
}

void Girlfriend_Load(Girlfriend* gf, char* name) {
    Render_DefaultAnimated(&gf->object);

    gf->f = 0;
    RayAnimationHandler characterAnimations;

    // this spritesheet has the necessary animations for actual gameplay (the sad and cheer animations are pretty useless imo)
    characterAnimations = AnimationSet_LoadAnimations("assets/images/characters/gf.animset", Render_LoadTexture("assets/images/characters/gf.png"));
    AnimationSet_SetAnimationData(&characterAnimations, 0, 24, 0);
    AnimationSet_SetAnimationData(&characterAnimations, 1, 24, 0);

    gf->object.animationSet = characterAnimations;
    gf->anims[0] = 0;
    gf->anims[1] = 1;

    AnimatedObject_SetAnimation(&gf->object, gf->anims[0]);
}
