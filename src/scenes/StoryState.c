#include"scenes/AllScenes.h"
#include"scenes/SongList.h"

// Story: lista de weeks (estilo Psych), tracklist, dificuldade, campanha.
// DELETE apaga a week com confirmacao.

static WeekEntry* weeks = NULL;
static int weekCount = 0;
static int selected = 0;
static float offset = 0;
static int diffIdx = 1; // 0 easy, 1 normal, 2 hard
static char delConfirm = 0;
static char statusText[128];
static float statusTimer = 0;

static const char* DIFF_NAMES[3] = {"EASY", "NORMAL", "HARD"};

static RayGraphicObject bg;
static Camera2D cam;

static Color outlineColor = {0, 0, 0, 0};
static Color fontColor = {255, 255, 255, 255};

static void StatusMsg(const char* m) {
    strncpy(statusText, m, sizeof(statusText) - 1);
    statusText[sizeof(statusText) - 1] = 0;
    statusTimer = 3.0f;
}

static void Story_StartWeek(void) {
    WeekEntry* w = weeks + selected;
    WeekRun_Start(w, diffIdx);
    const char* dir = WeekRun_Dir();
    if(dir[0] == 0) {
        const char* miss = WeekRun_Missing();
        if(miss[0] != 0) {
            char b[128];
            snprintf(b, sizeof(b), "MUSICA NAO ACHADA: %s", miss);
            StatusMsg(b);
        } else {
            StatusMsg("WEEK VAZIA");
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
    statusText[0] = 0;
    statusTimer = 0;

    if(WeekRun_Missing()[0] != 0) {
        char b[128];
        snprintf(b, sizeof(b), "PULADA (sem pasta): %s", WeekRun_Missing());
        StatusMsg(b);
    }
}

static void StoryState_Draw([[maybe_unused]] RayScene* scene) {
    if(statusTimer > 0)
        statusTimer -= RayGame_DeltaTime();

    if(delConfirm) {
        if(IsKeyPressed(KEY_Y)) {
            if(Week_Delete(weeks + selected))
                StatusMsg("APAGADA");
            else
                StatusMsg("NAO APAGOU");
            WeekList_Free(weeks);
            weeks = NULL;
            weekCount = WeekList_Scan(&weeks);
            if(selected >= weekCount) selected = weekCount - 1;
            if(selected < 0) selected = 0;
            delConfirm = 0;
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
            }
            if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
                selected--;
                if(selected < 0)
                    selected = weekCount - 1;
            }
            if(IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) {
                diffIdx--;
                if(diffIdx < 0) diffIdx = 2;
            }
            if(IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) {
                diffIdx++;
                if(diffIdx > 2) diffIdx = 0;
            }
            if(IsKeyPressed(KEY_DELETE)) {
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

    if(weekCount == 0) {
        DrawTextEx(mainFont, "SEM WEEKS - poe em assets/weeks/", (Vector2) {150, 720 / 2}, 32, 2, fontColor);
        Render_StopCamera();
        return;
    }

    if(delConfirm) {
        char b[128];
        snprintf(b, sizeof(b), "APAGAR %s?  Y = sim   N = nao", weeks[selected].title);
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
    }

    // tracklist da selecionada (direita)
    {
        WeekEntry* w = weeks + selected;
        float x = 800;
        float y = 180;
        char b[128];
        snprintf(b, sizeof(b), "%d songs", w->songCount);
        DrawTextEx(mainFont, b, (Vector2) {x, y}, 28, 2, (Color) {255, 220, 80, 255});
        y += 44;
        for(int i = 0; i < w->songCount; i++) {
            snprintf(b, sizeof(b), "%d. %s", i + 1, w->songs[i].song);
            DrawTextEx(mainFont, b, (Vector2) {x, y}, 24, 2, WHITE);
            y += 32;
            if(y > 520)
                break;
        }
        snprintf(b, sizeof(b), "< %s >", DIFF_NAMES[diffIdx]);
        DrawTextEx(mainFont, b, (Vector2) {x, 560}, 30, 3, (Color) {140, 255, 140, 255});
        if(WeekRun_LastTotal() > 0) {
            snprintf(b, sizeof(b), "ULTIMA: %d", WeekRun_LastTotal());
            DrawTextEx(mainFont, b, (Vector2) {x, 600}, 24, 2, (Color) {200, 200, 200, 255});
        }
    }

    if(statusTimer > 0)
        DrawTextEx(mainFont, statusText, (Vector2) {150, 660}, 24, 2, YELLOW);

    DrawTextEx(mainFont, "ENTER comeca   <-/-> dificuldade   DEL apaga   ESC volta",
        (Vector2) {150, 692}, 20, 1, (Color) {200, 200, 200, 255});
    Render_StopCamera();
}

static void StoryState_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bg.image);
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
