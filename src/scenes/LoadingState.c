#include"scenes/AllScenes.h"

// Presents one loading frame, then runs the heavy PlayState load
// (blocking). Used only for heavy songs; light ones go direct.
static char pendingDir[256];
static char presented;
static Camera2D cam;

void LoadingState_SetSongDir(const char* dir) {
    strncpy(pendingDir, dir, sizeof(pendingDir) - 1);
    pendingDir[sizeof(pendingDir) - 1] = 0;
}

static void LoadingState_Create([[maybe_unused]] RayScene* scene) {
    cam.target = (Vector2) {1280 / 2, 720 / 2};
    cam.zoom = 1;
    cam.rotation = 0;
    cam.offset = (Vector2) {0, 0};
    presented = 0;
}

static void LoadingState_Update([[maybe_unused]] RayScene* scene) {
    if(!presented) {
        presented = 1;
        return; // let Draw present the loading screen first
    }
    PlayState_SetSongDirKeep(pendingDir);
    PlayState_SetScene();
}

static void LoadingState_Draw([[maybe_unused]] RayScene* scene) {
    ClearBackground(BLACK);
    Render_SetCamera(&cam);
    const char* msg = "LOADING...";
    const int fontSize = 64;
    Vector2 size = MeasureTextEx(mainFont, msg, fontSize, 5);
    DrawTextEx(mainFont, msg, (Vector2) {1280 / 2 - size.x / 2, 720 / 2 - size.y / 2}, fontSize, 5, WHITE);
    Render_StopCamera();
}

static void LoadingState_Destroy([[maybe_unused]] RayScene* scene) {
}

static RayScene loadingScene;
Scene_MakeSceneCode(
    loadingScene,
    LoadingState_SetScene,
    LoadingState_Create,
    LoadingState_Update,
    LoadingState_Draw,
    LoadingState_Destroy
)
