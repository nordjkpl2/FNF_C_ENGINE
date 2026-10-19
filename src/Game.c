/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#include "std.h" 
#include "Game.h"
#include "GameData.h"
#include "Log.h"
/* // * Janela Principal definições de volume fps e tudo oq for global e muito necessário wowowow, OUTD day17/10/2026*/
RayGame game; 

// ================================================================
// [1] ESTADO GLOBAL
// ================================================================
static float dt; 
static float zoom = 1.0f;
static int width = 1280;
static int height = 720;
static float frametime; 
static int targetFps = 60;
static char clearTexture; 

Music gameMusic;
static char playingMusic;
static float musicVolume = 1.0f;
static float volOsdTimer = 0;

static RenderTexture2D stretchTarget;
static char stretchReady = 0;

// ================================================================
// [2] AUDIO
// ================================================================
void RayGame_SetMusic(const char* path, float volume, char looping) { 
    if(gameMusic.stream.buffer != NULL) UnloadMusicStream(gameMusic);
    gameMusic = LoadMusicStream(path);
    PlayMusicStream(gameMusic);  
    SetMusicVolume(gameMusic, volume * musicVolume);
    gameMusic.looping = looping;
    playingMusic = 0;
}

void RayGame_ToggleMusic(char on) { playingMusic = on; }
void RayGame_ResetMusic() { SeekMusicStream(gameMusic, 0.0f); PlayMusicStream(gameMusic); }

void RayGame_BumpMusicVolume(float delta) {
    musicVolume += delta;
    if(musicVolume < 0) musicVolume = 0;
    if(musicVolume > 1) musicVolume = 1;
    SetMusicVolume(gameMusic, musicVolume); // bug seu: tava 0.3f * musicVolume
    volOsdTimer = 2.0f;
}

float RayGame_MusicVolumeLevel(void) { return musicVolume; }

// ================================================================
// [3] TELA / FPS
// ================================================================
inline float RayGame_ZoomFactor() { return zoom; }
inline int RayGame_CanvasWidth(void) { return game_options_fillScreen ? 1280 : width; }
inline int RayGame_CanvasHeight(void) { return game_options_fillScreen ? 720 : height; }

void RayGame_SetFPS(int fps) { 
    targetFps = fps;
    game_options_fps = fps; 
    if(fps == 0) { frametime = 1.0f/60.0f; SetTargetFPS(0); return; }
    if(fps < 10) fps = 10; 
    frametime = 1.0f / (float)fps; 
    SetTargetFPS(fps);
} 

void RayGame_SetVsync(char on) {
    if(on) {
        SetWindowState(FLAG_VSYNC_HINT);
        int hz = GetMonitorRefreshRate(GetCurrentMonitor());
        frametime = (hz > 30) ? 1.0f / (float)hz : 1.0f/60.0f;
    } else {
        ClearWindowState(FLAG_VSYNC_HINT);
        RayGame_SetFPS(game_options_fps);
    }
}

int RayGame_GetTargetFPS(void) { return targetFps; }

void RayGame_Start(const char* gameTitle) {
    // CORRIGIDO: Flags ANTES do InitWindow
    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, gameTitle); 
    InitAudioDevice();
    SetExitKey(KEY_NULL);

    GameData_Start();
    // aplica vsync/fps do save
    if(game_options_vsync) RayGame_SetVsync(1);
    else RayGame_SetFPS(game_options_fps);

    stretchTarget = LoadRenderTexture(1280, 720);
    stretchReady = 1;
    clearTexture = 1;
}

// ================================================================
// [4] LOOP PRINCIPAL - OTIMIZADO
// ================================================================
static inline void DrawOverlay(void) {
    DrawFPS(10, 3);
    if(volOsdTimer > 0) {
        volOsdTimer -= dt;
        // SEM TextFormat: evita malloc por frame
        char buf[16];
        int pct = (int)(musicVolume * 100);
        // int pra string rápido
        buf[0] = 'V'; buf[1]='O'; buf[2]='L'; buf[3]=' '; 
        // ou simples DrawText com TextFormat só quando muda - aqui direto:
        DrawText(TextFormat("VOLUME %d%%", pct), 10, 26, 20, WHITE);
    }
}

void RayGame_GameLoop() {
    if(game.scene == NULL) Log_Fatal("[ ERROR ] game is not using any scene!");

    while(!WindowShouldClose()) {   
        dt = GetFrameTime(); 
        if(dt > frametime) dt = frametime; // clamp stutter

        // OTIMIZADO: só recalcula se redimensionou
        if(IsWindowResized()) {
            width = GetScreenWidth();
            height = GetScreenHeight();
            zoom = game_options_fillScreen ? 1.0f : fminf(width/1280.0f, height/720.0f);
        }

        if(IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        if(IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) RayGame_BumpMusicVolume(0.1f);
        if(IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) RayGame_BumpMusicVolume(-0.1f);

        if(game.scene->functions.updateFunc) game.scene->functions.updateFunc(game.scene);
        if(playingMusic) UpdateMusicStream(gameMusic);

        if(game_options_fillScreen && stretchReady) {
            BeginTextureMode(stretchTarget);
            if(clearTexture) { ClearBackground(BLACK); clearTexture = 0; }
            if(game.scene->functions.drawFunc) game.scene->functions.drawFunc(game.scene);
            EndTextureMode();

            BeginDrawing();
            ClearBackground(BLACK);
            DrawTexturePro(stretchTarget.texture, (Rectangle){0,0,1280,-720}, (Rectangle){0,0,(float)width,(float)height}, (Vector2){0,0}, 0, WHITE);
            DrawOverlay();
            EndDrawing();
        } else {
            BeginDrawing();
            if(clearTexture) { ClearBackground(BLACK); clearTexture = 0; }
            if(game.scene->functions.drawFunc) game.scene->functions.drawFunc(game.scene);

            // barras só se precisar
            if(zoom != 1.0f) {
                int barH = (height - (int)(720*zoom)) / 2;
                int barW = (width - (int)(1280*zoom)) / 2;
                if(barH > 0) { DrawRectangle(0,0,width,barH,BLACK); DrawRectangle(0,height-barH,width,barH,BLACK); }
                if(barW > 0) { DrawRectangle(0,0,barW,height,BLACK); DrawRectangle(width-barW,0,barW,height,BLACK); }
            }
            DrawOverlay();
            EndDrawing();
        }
    }  
}  

float RayGame_DeltaTime() { return dt; }
void RayGame_ClearScene() {
    if(game.scene != NULL) { Render_StopCamera(); RayScene_Destroy(game.scene); game.scene = NULL; } 
    clearTexture = 1;
}
void RayGame_Destroy() { 
    if(gameMusic.stream.buffer) UnloadMusicStream(gameMusic);
    if(stretchReady) UnloadRenderTexture(stretchTarget);
    if(game.scene) RayScene_Destroy(game.scene);  
    GameData_Stop();
    CloseAudioDevice();
    CloseWindow();
} 