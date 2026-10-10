#include"scenes/AllScenes.h"
#include"scenes/SongList.h"

// Story: lista de weeks (estilo Psych), tracklist, dificuldade, campanha.
// DELETE apaga a week com confirmacao.

static WeekEntry* weeks = NULL;
static int weekCount = 0;
static int selected = 0;
static float offset = 0;
static char delConfirm = 0;
static char statusText[128];
static float statusTimer = 0;

// icone grande do oponente da week (1 textura, troca so na selecao)
static Texture2D weekIcon;
static char weekIconHas = 0;
static int weekIconIdx = -1;

static float discAngle = 0;

static RayGraphicObject bg;
static Camera2D cam;

static Color outlineColor = {0, 0, 0, 0};
static Color fontColor = {255, 255, 255, 255};

static void StatusMsg(const char* m) {
    strncpy(statusText, m, sizeof(statusText) - 1);
    statusText[sizeof(statusText) - 1] = 0;
    statusTimer = 3.0f;
}

// raiz do mod a partir do dir da week (".../weeks" -> mod; base -> "").
// Normaliza '\' (scan devolve misto no Windows).
static void Story_ModRoot(const char* weekDir, char root[256]) {
    root[0] = 0;
    if(weekDir == NULL)
        return;
    char norm[300];
    size_t L = strlen(weekDir);
    if(L >= sizeof(norm))
        L = sizeof(norm) - 1;
    for(size_t i = 0; i < L; i++)
        norm[i] = (weekDir[i] == '\\') ? '/' : weekDir[i];
    norm[L] = 0;
    size_t n = (L >= 6 && strcmp(norm + L - 6, "/weeks") == 0) ? L - 6 : 0;
    if(n == 0 || n >= 256)
        return;
    memcpy(root, norm, n);
    root[n] = 0;
}

// <mod>/images/icons/<opp>.png, depois global. 1 = achou em out.
static char Story_IconPath(const char* weekDir, const char* opp, char out[300]) {
    if(opp == NULL || opp[0] == 0)
        return 0;
    if(strchr(opp, '/') != NULL || strchr(opp, '\\') != NULL || strstr(opp, "..") != NULL)
        return 0;
    char root[256];
    Story_ModRoot(weekDir, root);
    char p[300];
    FILE* f;
    if(root[0] != 0) {
        snprintf(p, sizeof(p), "%s/images/icons/%s.png", root, opp);
        f = fopen(p, "rb");
        if(f != NULL) {
            fclose(f);
            strncpy(out, p, 299);
            out[299] = 0;
            return 1;
        }
    }
    snprintf(p, sizeof(p), "assets/images/icons/%s.png", opp);
    f = fopen(p, "rb");
    if(f != NULL) {
        fclose(f);
        strncpy(out, p, 299);
        out[299] = 0;
        return 1;
    }
    return 0;
}

static void Story_UnloadIcon(void) {
    if(weekIconHas)
        UnloadTexture(weekIcon);
    weekIconHas = 0;
    weekIconIdx = -1;
}

// (re)carrega o icone do oponente da week selecionada (1 tex, so na troca)
static void Story_LoadWeekIcon(void) {
    if(weekIconIdx == selected && weekIconHas)
        return;
    Story_UnloadIcon();
    if(selected < 0 || selected >= weekCount)
        return;
    WeekEntry* w = weeks + selected;
    const char* opp = (w->songCount > 0 && w->songs[0].character[0] != 0) ? w->songs[0].character : "dad";
    char ip[300];
    if(!Story_IconPath(w->dir, opp, ip))
        return;
    weekIcon = Render_LoadTexture(ip);
    weekIconHas = 1;
    weekIconIdx = selected;
}

static void Story_StartWeek(void) {
    WeekEntry* w = weeks + selected;
    WeekRun_Start(w, 1); // sem seletor: sempre normal (mod nao gira em torno de diff)
    const char* dir = WeekRun_Dir();
    if(dir[0] == 0) {
        const char* miss = WeekRun_Missing();
        if(miss[0] != 0) {
            char b[128];
            snprintf(b, sizeof(b), "MISSING SONG: %s", miss);
            StatusMsg(b);
        } else {
            StatusMsg("EMPTY WEEK");
        }
        WeekRun_Stop();
        return;
    }
    RayGame_ToggleMusic(0);
    PlayState_SetSongDir(dir);
    PlayState_SetReturnWeek(1);
    PlayState_SetCharsOverride(WeekRun_Char());
    PlayState_SetDiff(WeekRun_DiffSuffix());
    if(SongList_IsHeavy(dir)) {
        LoadingState_SetSongDir(dir);
        LoadingState_SetScene();
    } else {
        PlayState_SetScene();
    }
}

static void StoryState_Create([[maybe_unused]] RayScene* scene) {
    Render_DefaultRGT(&bg);

    bg.objType = RGT_IMAGE;
    bg.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");

    cam.target = (Vector2) {1280 / 2, 720 / 2};
    cam.zoom = 1;

    WeekList_Free(weeks);
    weeks = NULL;
    weekCount = WeekList_Scan(&weeks);
    selected = 0;
    offset = 0;
    delConfirm = 0;
    discAngle = 0;
    statusText[0] = 0;
    statusTimer = 0;
    Story_UnloadIcon();
    Story_LoadWeekIcon();

    if(WeekRun_Missing()[0] != 0) {
        char b[128];
        snprintf(b, sizeof(b), "SKIPPED (missing): %s", WeekRun_Missing());
        StatusMsg(b);
    }
}

static void StoryState_Draw([[maybe_unused]] RayScene* scene) {
    if(statusTimer > 0)
        statusTimer -= RayGame_DeltaTime();

    if(delConfirm) {
        if(IsKeyPressed(KEY_Y)) {
            if(Week_Delete(weeks + selected))
                StatusMsg("DELETED");
            else
                StatusMsg("NOT DELETED");
            WeekList_Free(weeks);
            weeks = NULL;
            weekCount = WeekList_Scan(&weeks);
            if(selected >= weekCount) selected = weekCount - 1;
            if(selected < 0) selected = 0;
            delConfirm = 0;
            Story_UnloadIcon();
            Story_LoadWeekIcon();
            return;
        }
        if(IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
            delConfirm = 0;
            return;
        }
    } else {
        if(IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
            MenuState_SetScene();
            return;
        }

        if(weekCount > 0) {
            if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
                selected++;
                if(selected >= weekCount)
                    selected = 0;
                Story_LoadWeekIcon();
            }
            if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
                selected--;
                if(selected < 0)
                    selected = weekCount - 1;
                Story_LoadWeekIcon();
            }
            if(IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_SLASH) || IsKeyPressed(KEY_KP_DIVIDE)) {
                delConfirm = 1;
                return;
            }
            if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
                Story_StartWeek();
                return;
            }
        } else {
            if(IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
                MenuState_SetScene();
                return;
            }
        }
    }

    offset = Lerp(offset, (float)selected, RayGame_DeltaTime() * 10.0f);

    Render_SetCamera(&cam);
    Render_DrawGraphicObject(&bg);

    // disco procedural embaixo dos textos: 0 disco, 0 VRAM, +5 draws. Gira lento atras.
    // icone da week por baixo do disco (bordas aparecendo em volta).
    {
        if(weekIconHas) {
            DrawTexturePro(weekIcon, (Rectangle) {0, 0, 150, 150},
                (Rectangle) {1055 - 100, 575 - 100, 200, 200}, VECTOR_ZERO, 0, WHITE);
        }
        discAngle += RayGame_DeltaTime() * 0.6f;
        Vector2 c = {1055, 575};
        DrawCircleV(c, 135, (Color){16, 16, 24, 255});
        DrawCircleLines((int)c.x, (int)c.y, 110, (Color){60, 60, 80, 255});
        DrawCircleLines((int)c.x, (int)c.y, 78, (Color){60, 60, 80, 255});
        DrawCircleV(c, 46, (Color){130, 90, 170, 255});
        DrawCircleV(c, 10, (Color){220, 220, 230, 255});
        Vector2 tip = {c.x + 126 * cosf(discAngle), c.y + 126 * sinf(discAngle)};
        DrawLineEx(c, tip, 5, (Color){255, 255, 255, 40});
    }

    if(weekCount == 0) {
        DrawTextEx(mainFont, "NO WEEKS - add to assets/weeks/", (Vector2) {150, 720 / 2}, 32, 2, fontColor);
        Render_StopCamera();
        return;
    }

    if(delConfirm) {
        char b[128];
        snprintf(b, sizeof(b), "DELETE %s?  Y = yes   N = no", weeks[selected].title);
        Vector2 s = MeasureTextEx(mainFont, b, 40, 4);
        DrawTextEx(mainFont, b, (Vector2) {1280 / 2 - s.x / 2, 720 / 2 - 20}, 40, 4, (Color) {255, 80, 80, 255});
        Render_StopCamera();
        return;
    }

    // lista da esquerda
    for(int i = 0; i < weekCount; i++) {
        const int fontSize = 56;
        const int shadow = 4;
        float d = fabsf((float)i - offset);
        Vector2 pos = {(float)(shadow + 120 + d * -24), (float)(shadow + 200 + (i - offset) * (10 + fontSize))};
        if(pos.y < -80 || pos.y > 800)
            continue;

        fontColor.a = 150;
        outlineColor.a = 100;
        if(selected == i)
            fontColor.a = outlineColor.a = 255;

        DrawTextEx(mainFont, weeks[i].title, pos, fontSize, 4, outlineColor);
        pos.x -= shadow;
        pos.y -= shadow;
        DrawTextEx(mainFont, weeks[i].title, pos, fontSize, 4, fontColor);

        // selecionadores do item: ">" amarelo + agulha verde (4 draws, sem string)
        if(selected == i) {
            Vector2 mp = {pos.x - 56, pos.y};
            DrawTextEx(mainFont, ">", (Vector2) {mp.x + shadow, mp.y + shadow}, fontSize, 4, outlineColor);
            DrawTextEx(mainFont, ">", mp, fontSize, 4, (Color) {255, 220, 80, 255});
            Vector2 np = {pos.x - 116, pos.y + 8};
            DrawTriangle((Vector2) {np.x, np.y}, (Vector2) {np.x, np.y + 36},
                (Vector2) {np.x + 30, np.y + 18}, (Color) {80, 220, 100, 255});
        }
    }

    // titulo da week no canto superior direito (+ frase so se o json definir)
    {
        WeekEntry* w = weeks + selected;
        Vector2 ts = MeasureTextEx(mainFont, w->title, 40, 3);
        float tx = 1280 - 50 - ts.x;
        DrawTextEx(mainFont, w->title, (Vector2) {tx + 3, 53}, 40, 3, outlineColor);
        DrawTextEx(mainFont, w->title, (Vector2) {tx, 50}, 40, 3, fontColor);
        if(w->flavor[0] != 0) {
            const int fs = 22;
            const int lh = 26;
            int line = 0;
            const char* s = w->flavor;
            char linebuf[48];
            while(line < 4) {
                size_t k = 0;
                while(s[k] != 0 && s[k] != '\n' && k < 47) { linebuf[k] = s[k]; k++; }
                linebuf[k] = 0;
                Vector2 ls = MeasureTextEx(mainFont, linebuf, fs, 2);
                float lx = 1280 - 50 - ls.x;
                DrawTextEx(mainFont, linebuf, (Vector2) {lx + 2, 102 + line * lh}, fs, 2, outlineColor);
                DrawTextEx(mainFont, linebuf, (Vector2) {lx, 100 + line * lh}, fs, 2, (Color) {255, 220, 130, 255});
                if(s[k] == 0) break;
                s += k + 1;
                line++;
            }
        }
    }

    // tracklist da selecionada (direita, parte de cima)
    {
        WeekEntry* w = weeks + selected;
        float x = 800;
        float y = 150;
        char b[128];
        snprintf(b, sizeof(b), "%d songs", w->songCount);
        DrawTextEx(mainFont, b, (Vector2) {x, y}, 28, 2, (Color) {255, 220, 80, 255});
        y += 44;
        for(int i = 0; i < w->songCount; i++) {
            snprintf(b, sizeof(b), "%d. %s", i + 1, w->songs[i].song);
            DrawTextEx(mainFont, b, (Vector2) {x, y}, 24, 2, WHITE);
            y += 32;
            if(y > 440)
                break;
        }
        if(WeekRun_LastTotal() > 0) {
            snprintf(b, sizeof(b), "LAST: %d", WeekRun_LastTotal());
            DrawTextEx(mainFont, b, (Vector2) {x, y + 8}, 24, 2, (Color) {200, 200, 200, 255});
        }
    }

    if(statusTimer > 0)
        DrawTextEx(mainFont, statusText, (Vector2) {150, 660}, 24, 2, YELLOW);

    DrawTextEx(mainFont, "ENTER start   DEL or / delete   ESC back",
        (Vector2) {150, 692}, 20, 1, YELLOW);
    Render_StopCamera();
}

static void StoryState_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bg.image);
    Story_UnloadIcon();
    WeekList_Free(weeks);
    weeks = NULL;
    weekCount = 0;
}

static RayScene storyScene;
Scene_MakeSceneCode(
    storyScene,
    StoryState_SetScene,
    StoryState_Create,
    NULL,
    StoryState_Draw,
    StoryState_Destroy
)
