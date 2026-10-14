#include"scenes/Character.h"
#include"cJSON.h"
#include"yyjson.h"

// raiz do mod extraida do songDir (assets/mods/<mod>/...), "" = base.
// ~ Line 70 Eu Acho tem q listar aqui o que é
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
// Retorna defs em defs (cap) e metadados. Sem alloc (agora vai ter alloc eu acho kkk) 
static int Char_ReadJson(const char* jsonPath, CharAnimDef* defs, int cap,
    float* scale, char* flipX, float* camX, float* camY, char* healthicon,
    char* imageBase, Color* healthColor) {
    (void)healthColor; // Ignorado, pois a cor agora é controlada no código
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

    // Parse usando yyjson
    yyjson_doc* doc = yyjson_read(buf, (size_t)size, 0);
    free(buf);
    if(doc == NULL)
        return 0;
    yyjson_val* root = yyjson_doc_get_root(doc);
    if(root == NULL) {
        yyjson_doc_free(doc);
        return 0;
    }

    yyjson_val* js = yyjson_obj_get(root, "scale");
    if(yyjson_is_num(js)) {
        double s_val = yyjson_get_real(js);
        if(s_val > 0.05 && s_val < 8)
            *scale = (float)s_val;
    }

    yyjson_val* jf = yyjson_obj_get(root, "flip_x");
    if(yyjson_is_bool(jf) && yyjson_get_bool(jf))
        *flipX = 1;

    yyjson_val* jc = yyjson_obj_get(root, "camera_position");
    if(yyjson_is_arr(jc) && yyjson_arr_size(jc) >= 2) {
        yyjson_val* a = yyjson_arr_get(jc, 0);
        yyjson_val* b = yyjson_arr_get(jc, 1);
        if(yyjson_is_num(a)) *camX = (float)yyjson_get_real(a);
        if(yyjson_is_num(b)) *camY = (float)yyjson_get_real(b);
    }

    yyjson_val* jh = yyjson_obj_get(root, "healthicon");
    if(yyjson_is_str(jh)) {
        const char* jh_str = yyjson_get_str(jh);
        if(jh_str && jh_str[0] != 0 && Char_Safe(jh_str)) {
            strncpy(healthicon, jh_str, 31);
            healthicon[31] = 0;
        }
    }

    // base do png: "image": "characters/base" -> "base" (senao usa o nome)
    yyjson_val* ji = yyjson_obj_get(root, "image");
    if(yyjson_is_str(ji)) {
        const char* b = yyjson_get_str(ji);
        if(b && b[0] != 0) {
            const char* s1 = strrchr(b, '/');
            const char* s2 = strrchr(b, '\\');
            const char* s = (s1 > s2) ? s1 : s2;
            b = (s != NULL) ? s + 1 : b;
            if(b[0] != 0 && Char_Safe(b)) {
                strncpy(imageBase, b, 31);
                imageBase[31] = 0;
            }
        }
    }

    int n = 0;
    yyjson_val* ja = yyjson_obj_get(root, "animations");
    if(yyjson_is_arr(ja)) {
        size_t idx, max;
        yyjson_val* a;
        yyjson_arr_foreach(ja, idx, max, a) {
            if(n >= cap) break;
            yyjson_val* jr = yyjson_obj_get(a, "anim");
            yyjson_val* jn = yyjson_obj_get(a, "name");
            if(!yyjson_is_str(jr) || !yyjson_is_str(jn))
                continue;
            
            const char* r_str = yyjson_get_str(jr);
            const char* n_str = yyjson_get_str(jn);
            if(!r_str || !n_str || r_str[0] == 0 || n_str[0] == 0)
                continue;

            CharAnimDef* d = defs + n;
            strncpy(d->role, r_str, sizeof(d->role) - 1);
            d->role[sizeof(d->role) - 1] = 0;
            strncpy(d->sprite, n_str, sizeof(d->sprite) - 1);
            d->sprite[sizeof(d->sprite) - 1] = 0;

            yyjson_val* jfps = yyjson_obj_get(a, "fps");
            d->fps = (yyjson_is_num(jfps) && yyjson_get_int(jfps) > 0) ? yyjson_get_int(jfps) : 24;

            yyjson_val* jl = yyjson_obj_get(a, "loop");
            d->looped = (yyjson_is_bool(jl) && yyjson_get_bool(jl)) ? 1 : 0;

            d->ox = 0;
            d->oy = 0;
            yyjson_val* jo = yyjson_obj_get(a, "offsets");
            if(yyjson_is_arr(jo) && yyjson_arr_size(jo) >= 2) {
                yyjson_val* ox = yyjson_arr_get(jo, 0);
                yyjson_val* oy = yyjson_arr_get(jo, 1);
                if(yyjson_is_num(ox)) d->ox = (float)yyjson_get_real(ox);
                if(yyjson_is_num(oy)) d->oy = (float)yyjson_get_real(oy);
            }
            n++;
        }
    }
    yyjson_doc_free(doc);
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
    CharAnimDef defs[48];
    int ndefs = 0;
    
    // Chamada limpa do Char_ReadJson (sem o parâmetro de cor)
    if(hasJson)
        ndefs = Char_ReadJson(json, defs, 48, &scale, &flipX, &camX, &camY, healthicon, imageBase, NULL);

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
            defs[ndefs].role[sizeof(defs[ndefs].role) - 1] = 0;
            strncpy(defs[ndefs].sprite, sprs[i], sizeof(defs[ndefs].sprite) - 1);
            defs[ndefs].sprite[sizeof(defs[ndefs].sprite) - 1] = 0;
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
    }
    // fallback caso não tiver arquivo personalizado
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
