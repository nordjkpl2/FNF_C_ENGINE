#include"scenes/AllScenes.h"
#include"scenes/SongList.h"

static SongEntry* songs = NULL;
static int songCount = 0;
static int selectedSong = 0;
static float offset;

static RayGraphicObject bg;
static Camera2D cam;

static Color outlineColor = {0, 0, 0, 0};
static Color fontColor = {255, 255, 255, 255};

// cache de icones boss: selecionada +-1 (3 slots). Load/unload so na troca de selecao,
// 1 slot por frame, nunca no hot path. 0 disco novo (reusa pngs), ~3 texturas no max.
#define ICON_WIN 3
#define ICON_HALF 1
static struct {
    int idx; // indice em songs, -1 = vazio
    char dir[256];
    char opp[32];
    Texture2D icon;
    char has;
} iconCache[ICON_WIN];
static int iconAnchor = -999999;
static int iconBase = 0; // selectedSong da janela atual do cache
static char iconInit = 0;

// raiz do mod de um songDir (assets/mods/<mod>/...), "" = base.
static void ModRoot_FromDir(const char* dir, char root[256]) {
    root[0] = 0;
    if(dir == NULL) return;
    const char* pm = strstr(dir, "assets/mods/");
    if(pm == NULL) return;
    const char* after = pm + 12;
    const char* slash = strchr(after, '/');
    const char* sm = (slash != NULL) ? strstr(slash, "/songs/") : NULL;
    const char* end = (sm != NULL) ? sm : slash;
    size_t n = (end != NULL) ? (size_t)(end - dir) : strlen(dir);
    if(n >= 256) n = 255;
    memcpy(root, dir, n);
    root[n] = 0;
}

// campo string de json pequeno (character .json). 1 = achou em out.
static char Json_StrField(const char* path, const char* key, char out[32]) {
    FILE* f = fopen(path, "rb");
    if(f == NULL) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size < 12 || size > 1L * 1024 * 1024) { fclose(f); return 0; }
    char* buf = malloc((size_t)size);
    if(buf == NULL) { fclose(f); return 0; }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    size_t kl = strlen(key);
    for(size_t i = 0; i + kl + 3 < n; i++) {
        if(buf[i] != '"') continue;
        size_t k = 0;
        while(k < kl && buf[i + 1 + k] == key[k]) k++;
        if(k != kl || buf[i + 1 + k] != '"') continue;
        size_t j = i + 1 + k + 1;
        while(j < n && (buf[j] == ' ' || buf[j] == '\t' || buf[j] == '\r' || buf[j] == '\n')) j++;
        if(j >= n || buf[j] != ':') continue;
        j++;
        while(j < n && (buf[j] == ' ' || buf[j] == '\t' || buf[j] == '\r' || buf[j] == '\n')) j++;
        if(j >= n || buf[j] != '"') continue;
        j++;
        size_t w = 0;
        while(j < n && buf[j] != '"' && w < 31) out[w++] = buf[j++];
        if(j >= n || buf[j] != '"' || w == 0) continue;
        out[w] = 0;
        char bad = 0;
        for(size_t t = 0; t < w; t++) {
            if(out[t] == '/' || out[t] == '\\') { bad = 1; break; }
        }
        if(!bad && strstr(out, "..") != NULL) bad = 1;
        if(bad) continue;
        free(buf);
        return 1;
    }
    free(buf);
    return 0;
}

// chave do icone: healthicon do character .json, senao o proprio nome.
// (o pack mistura os dois: spaci_astray->spaci1, lacuna_danBIGtest->ele mesmo)
static void Icon_KeyFor(const char* dir, const char* opp, char out[32]) {
    strncpy(out, opp, 31);
    out[31] = 0;
    if(opp == NULL || opp[0] == 0) return;
    char root[256];
    ModRoot_FromDir(dir, root);
    char jp[300];
    if(root[0] != 0) {
        snprintf(jp, sizeof(jp), "%s/characters/%s.json", root, opp);
        char h[32];
        if(Json_StrField(jp, "healthicon", h)) {
            strncpy(out, h, 31);
            out[31] = 0;
            return;
        }
    }
    snprintf(jp, sizeof(jp), "assets/characters/%s.json", opp);
    {
        char h[32];
        if(Json_StrField(jp, "healthicon", h)) {
            strncpy(out, h, 31);
            out[31] = 0;
        }
    }
}

// <mod>/images/icons/<key|opp>.png, depois global (key primeiro). 1 = achou em out.
static char Icon_PathFor(const char* dir, const char* opp, char out[300]) {
    if(opp == NULL || opp[0] == 0) return 0;
    if(strchr(opp, '/') != NULL || strchr(opp, '\\') != NULL || strstr(opp, "..") != NULL) return 0;
    char key[32];
    Icon_KeyFor(dir, opp, key);
    char root[256];
    ModRoot_FromDir(dir, root);
    const char* names[2] = {key, opp};
    for(int ni = 0; ni < 2; ni++) {
        if(ni == 1 && strcmp(names[1], names[0]) == 0) continue;
        if(names[ni][0] == 0) continue;
        char p[300];
        FILE* f;
        if(root[0] != 0) {
            snprintf(p, sizeof(p), "%s/images/icons/%s.png", root, names[ni]);
            f = fopen(p, "rb");
            if(f != NULL) { fclose(f); strncpy(out, p, 299); out[299] = 0; return 1; }
        }
        snprintf(p, sizeof(p), "assets/images/icons/%s.png", names[ni]);
        f = fopen(p, "rb");
        if(f != NULL) { fclose(f); strncpy(out, p, 299); out[299] = 0; return 1; }
    }
    return 0;
}

static void IconCache_Evict(int s) {
    if(iconCache[s].has) UnloadTexture(iconCache[s].icon);
    iconCache[s].idx = -1;
    iconCache[s].dir[0] = 0;
    iconCache[s].opp[0] = 0;
    iconCache[s].has = 0;
}

static void IconCache_Clear(void) {
    for(int i = 0; i < ICON_WIN; i++)
        IconCache_Evict(i);
    iconAnchor = -999999;
    iconBase = 0;
    iconInit = 0;
}

static void IconCache_Refresh(void) {
    if(selectedSong == iconAnchor) return; // assentado: 1 compare por frame
    if(!iconInit) {
        iconBase = selectedSong;
        iconInit = 1;
    }
    // desloca a janela reaproveitando slots (sem IO, sem upload)
    {
        int delta = selectedSong - iconBase;
        if(delta >= ICON_WIN || delta <= -ICON_WIN) {
            for(int s = 0; s < ICON_WIN; s++)
                IconCache_Evict(s);
        } else if(delta > 0) {
            for(int s = 0; s < delta; s++)
                IconCache_Evict(s);
            for(int s = 0; s < ICON_WIN - delta; s++)
                iconCache[s] = iconCache[s + delta];
            for(int s = ICON_WIN - delta; s < ICON_WIN; s++) {
                iconCache[s].idx = -1;
                iconCache[s].has = 0;
            }
        } else if(delta < 0) {
            for(int s = ICON_WIN + delta; s < ICON_WIN; s++)
                IconCache_Evict(s);
            for(int s = ICON_WIN - 1; s >= -delta; s--)
                iconCache[s] = iconCache[s + delta];
            for(int s = 0; s < -delta; s++) {
                iconCache[s].idx = -1;
                iconCache[s].has = 0;
            }
        }
        iconBase = selectedSong;
    }
    // preenche 1 slot faltante por frame: no max 1 decode + 1 upload (sem hitch)
    for(int s = 0; s < ICON_WIN; s++) {
        int idx = selectedSong + (s - ICON_HALF);
        if(idx < 0 || idx >= songCount) {
            if(iconCache[s].idx != -1) IconCache_Evict(s);
            continue;
        }
        if(iconCache[s].idx == idx) continue; // assentado (com icone ou dad-vazio)
        if(iconCache[s].idx != -1) IconCache_Evict(s);
        iconCache[s].idx = idx;
        strncpy(iconCache[s].dir, songs[idx].dir, sizeof(iconCache[s].dir) - 1);
        iconCache[s].dir[sizeof(iconCache[s].dir) - 1] = 0;
        char opp[32];
        SongList_Opponent(songs[idx].dir, opp);
        // usa bf.png como fallback universal se nao achar icone do opponent
        strncpy(iconCache[s].opp, opp[0] ? opp : "bf", sizeof(iconCache[s].opp) - 1);
        iconCache[s].opp[sizeof(iconCache[s].opp) - 1] = 0;
        char ip[300];
        if(!Icon_PathFor(songs[idx].dir, iconCache[s].opp, ip)) {
            // fallback final: bf.png
            iconCache[s].icon = Render_LoadTexture("assets/images/icons/bf.png");
        } else {
            iconCache[s].icon = Render_LoadTexture(ip);
        }
        iconCache[s].has = 1;
        return;
    }
    iconAnchor = selectedSong;
}

static void Freeplay_Create([[maybe_unused]] RayScene* scene) {
    Render_DefaultRGT(&bg);

    bg.objType = RGT_IMAGE;
    bg.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");

    cam.target = (Vector2) {1280 / 2, 720 / 2};
    cam.zoom = 1;

    SongList_Free(songs);
    songs = NULL;
    songCount = SongList_Scan(&songs);
    selectedSong = 0;
    offset = 0;
    IconCache_Clear();
    IconCache_Refresh();
}

static void Freeplay_Draw([[maybe_unused]] RayScene* scene) {
    if(IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        MenuState_SetScene();
        return;
    }

    if(songCount > 0) {
        if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
            selectedSong++;
            if(selectedSong >= songCount)
                selectedSong = 0;
        }

        if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
            selectedSong--;
            if(selectedSong < 0)
                selectedSong = songCount - 1;
        }

        if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            RayGame_ToggleMusic(0);
            const char* dir = songs[selectedSong].dir;
            if(SongList_IsHeavy(dir)) {
                LoadingState_SetSongDir(dir);
                LoadingState_SetScene();
            } else {
                PlayState_SetSongDir(dir);
                PlayState_SetScene();
            }
            return;
        }
    }

    offset = Lerp(offset, (float)selectedSong, RayGame_DeltaTime() * 10.0f);
    IconCache_Refresh();

    Render_SetCamera(&cam);
    Render_DrawGraphicObject(&bg);

    if(songCount == 0) {
        const char* msg = "No songs found - add to assets/songs/ or assets/mods/";
        DrawTextEx(mainFont, msg, (Vector2) {150, 720 / 2}, 32, 2, fontColor);
        Render_StopCamera();
        return;
    }

    for(int i = 0; i < songCount; i++) {
        const int fontSize = 80;
        const int shadow = 5;

        float y = 720 / 2 + ((float)i - offset) * (10 + fontSize);
        // culling: pula itens totalmente fora da tela
        if(y < -120 || y > 840)
            continue;

        float d = fabsf((float)i - offset);

        Vector2 pos = {(float)(shadow + 150 + d * -30), (float)(shadow + 720 / 2 + (i - offset) * (10 + fontSize))};

        fontColor.a = 150;
        outlineColor.a = 100;

        if(selectedSong == i)
            fontColor.a = outlineColor.a = 255;

        DrawTextEx(mainFont, songs[i].name, pos, fontSize, 5, outlineColor);
        pos.x -= shadow;
        pos.y -= shadow;
        DrawTextEx(mainFont, songs[i].name, pos, fontSize, 5, fontColor);

        // icone grandao atras do ">" (so no selecionado; o cache +-1 ja resolve)
        if(selectedSong == i) {
            for(int s = 0; s < ICON_WIN; s++) {
                if(iconCache[s].idx == i && iconCache[s].has) {
                    float iy = 720 / 2 + (i - offset) * (10 + fontSize) + (fontSize - 130) / 2;
                    if(iy > -140 && iy < 800)
                        DrawTexturePro(iconCache[s].icon, (Rectangle) {0, 0, 150, 150},
                            (Rectangle) {pos.x - 200, iy, 130, 130}, VECTOR_ZERO, 0, WHITE);
                    break;
                }
            }
            Vector2 mp = {pos.x - 62, pos.y};
            DrawTextEx(mainFont, ">", (Vector2) {mp.x + shadow, mp.y + shadow}, fontSize, 5, outlineColor);
            DrawTextEx(mainFont, ">", mp, fontSize, 5, (Color) {255, 220, 80, 255});
        }
    }
    Render_StopCamera();
}

static void Freeplay_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bg.image);
    IconCache_Clear();
    SongList_Free(songs);
    songs = NULL;
    songCount = 0;
}

static RayScene scene;
Scene_MakeSceneCode(scene, Freeplay_SetScene, Freeplay_Create, NULL, Freeplay_Draw, Freeplay_Destroy);
