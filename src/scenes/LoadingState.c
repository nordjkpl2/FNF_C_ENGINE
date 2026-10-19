/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */

#include "scenes/AllScenes.h"

/* // * Tela de Loading pra não ter que esperar carregar charts de teste, outd 18 of10 2026 */

// --- cache estático pra não recalcular todo frame ---
static char pendingDir[256];
static bool presented;
static Vector2 textPos;
static Camera2D cam;

static const char* MSG = "LOADING...";
static const int FONT_SIZE = 64;
static const int SPACING = 5;

void LoadingState_SetSongDir(const char* dir) {
    // strncpy é lento e não garante \0 se estourar, snprintf é mais seguro e inline
    snprintf(pendingDir, sizeof(pendingDir), "%s", dir);
}

static void LoadingState_Create(RayScene* scene) {
    (void)scene;
    // câmera fixa, sem recalcular offset/target toda hora
    // Se seu Render não precisa de câmera pra essa tela, pode remover tudo de câmera
    cam.target = (Vector2){ 640, 360 };
    cam.offset = (Vector2){ 640, 360 };
    cam.rotation = 0.0f;
    cam.zoom = 1.0f;

    presented = false;

    // CACHE: mede o texto UMA VEZ só na criação, não 60x por segundo
    Vector2 size = MeasureTextEx(mainFont, MSG, FONT_SIZE, SPACING);
    textPos = (Vector2){ 640 - size.x * 0.5f, 360 - size.y * 0.5f };
}

static void LoadingState_Update(RayScene* scene) {
    (void)scene;
    if (!presented) {
        presented = true;
        return; // deixa a Draw apresentar 1 frame
    }
    PlayState_SetSongDirKeep(pendingDir);
    PlayState_SetScene();
}

static void LoadingState_Draw(RayScene* scene) {
    (void)scene;
    ClearBackground(BLACK);
    
    // OTIMIZAÇÃO MÁXIMA: Se não precisa de parallax/camera aqui, remova essas 2 linhas
    // É 2 push/pop de matriz do OpenGL atoa por frame
    // Render_SetCamera(&cam);
    DrawTextEx(mainFont, MSG, textPos, FONT_SIZE, SPACING, WHITE);
    // Render_StopCamera();
}

static void LoadingState_Destroy(RayScene* scene) {
    (void)scene;
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