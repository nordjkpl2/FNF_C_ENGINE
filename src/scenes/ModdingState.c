#include"scenes/AllScenes.h"

// Modding hub: freeplay-style clickable list.
// Editors land here as tabs; stubs show EM BREVE until implemented.
typedef enum {
    MOD_LIST,
    MOD_STUB
} ModdingMode;

static const char* tabs[] = {
    "CHART EDITOR",
    "CHARACTER EDITOR"
};
#define TAB_COUNT 2

static ModdingMode mode;
static int selected;
static float offset;
static const char* stubTitle;

static RayGraphicObject bg;
static Camera2D cam;

static Color outlineColor = {0, 0, 0, 0};
static Color fontColor = {255, 255, 255, 255};

static void ModdingState_Create([[maybe_unused]] RayScene* scene) {
    Render_DefaultRGT(&bg);

    bg.objType = RGT_IMAGE;
    bg.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");

    cam.target = (Vector2) {1280 / 2, 720 / 2};
    cam.zoom = 1;

    mode = MOD_LIST;
    selected = 0;
    offset = 0;
    stubTitle = NULL;
}

static void ModdingState_Draw([[maybe_unused]] RayScene* scene) {
    if(IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        if(mode == MOD_STUB) {
            mode = MOD_LIST;
            return;
        }
        RayGame_ResetMusic();
        RayGame_ToggleMusic(1);
        MenuState_SetScene();
        return;
    }

    if(mode == MOD_LIST) {
        if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
            selected++;
            if(selected >= TAB_COUNT)
                selected = 0;
        }

        if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
            selected--;
            if(selected < 0)
                selected = TAB_COUNT - 1;
        }

        if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            if(selected == 0) {
                ChartEditor_SetScene();
                return;
            }
            stubTitle = tabs[selected];
            mode = MOD_STUB;
            return;
        }
    }

    offset = Lerp(offset, (float)selected, RayGame_DeltaTime() * 10.0f);

    Render_SetCamera(&cam);
    Render_DrawGraphicObject(&bg);

    if(mode == MOD_STUB) {
        const char* msg = "EM BREVE";
        Vector2 size = MeasureTextEx(mainFont, msg, 80, 5);
        DrawTextEx(mainFont, msg, (Vector2) {1280 / 2 - size.x / 2, 720 / 2 - size.y / 2}, 80, 5, fontColor);
        if(stubTitle != NULL) {
            Vector2 sub = MeasureTextEx(mainFont, stubTitle, 32, 4);
            DrawTextEx(mainFont, stubTitle, (Vector2) {1280 / 2 - sub.x / 2, 720 / 2 + 80}, 32, 4, fontColor);
        }
        Render_StopCamera();
        return;
    }

    for(int i = 0; i < TAB_COUNT; i++) {
        const int fontSize = 80;
        const int shadow = 5;

        float d = fabsf((float)i - offset);

        Vector2 pos = {(float)(shadow + 150 + d * -30), (float)(shadow + 720 / 2 + (i - offset) * (10 + fontSize))};

        fontColor.a = 150;
        outlineColor.a = 100;

        if(selected == i)
            fontColor.a = outlineColor.a = 255;

        DrawTextEx(mainFont, tabs[i], pos, fontSize, 5, outlineColor);
        pos.x -= shadow;
        pos.y -= shadow;
        DrawTextEx(mainFont, tabs[i], pos, fontSize, 5, fontColor);
    }
    Render_StopCamera();
}

static void ModdingState_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bg.image);
}

static RayScene moddingScene;
Scene_MakeSceneCode(
    moddingScene,
    ModdingState_SetScene,
    ModdingState_Create,
    NULL,
    ModdingState_Draw,
    ModdingState_Destroy
)
