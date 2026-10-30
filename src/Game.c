/*
 * Friday Night Funkin' C Engine
 * (c) 2026 nordjkpl
 *
 * RayGame.c - Estado global, áudio, janela e loop principal.
 */

#include "std.h"
#include "Game.h"
#include "GameData.h"
#include "Log.h"

#include <stdbool.h>
#include <stdio.h>

#define RAYGAME_CANVAS_WIDTH  1280
#define RAYGAME_CANVAS_HEIGHT 720
#define RAYGAME_DEFAULT_FPS   60

/* ================================================================
   1. ESTADO GLOBAL
   ================================================================ */

RayGame game;

static float dt = 0.0f;
static float zoom = 1.0f;

static int width = RAYGAME_CANVAS_WIDTH;
static int height = RAYGAME_CANVAS_HEIGHT;
static int targetFps = RAYGAME_DEFAULT_FPS;

static bool vsyncEnabled = false;
static bool viewportInitialized = false;
static bool lastFillScreen = false;
static bool audioReady = false;
static bool gameDataReady = false;

Music gameMusic = {0};

static bool musicLoaded = false;
static bool playingMusic = false;

/* Volume global x volume configurado para a faixa atual. */
static float musicVolume = 1.0f;
static float musicBaseVolume = 1.0f;
static float volOsdTimer = 0.0f;

/* Texto do OSD atualizado somente quando o volume muda. */
static char volumeText[24] = "VOLUME 100%";
static int cachedVolumePercent = 100;

static RenderTexture2D stretchTarget = {0};
static bool stretchReady = false;

/* ================================================================
   2. UTILITÁRIOS INTERNOS
   ================================================================ */

static float RayGame_Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static void RayGame_UpdateVolumeText(void)
{
    const int percent = (int)(musicVolume * 100.0f + 0.5f);

    if (percent == cachedVolumePercent)
        return;

    cachedVolumePercent = percent;
    (void)snprintf(volumeText, sizeof(volumeText), "VOLUME %d%%", percent);
}

/* Atualiza dimensões e escala somente quando necessário. */
static void RayGame_UpdateViewport(bool force)
{
    const bool fillScreen = (game_options_fillScreen != 0);
    const bool resized = IsWindowResized();

    if (!force && viewportInitialized && !resized &&
        fillScreen == lastFillScreen)
    {
        return;
    }

    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();

    if (screenWidth > 0)
        width = screenWidth;

    if (screenHeight > 0)
        height = screenHeight;

    if (fillScreen)
    {
        zoom = 1.0f;
    }
    else
    {
        const float scaleX = (float)width / (float)RAYGAME_CANVAS_WIDTH;
        const float scaleY = (float)height / (float)RAYGAME_CANVAS_HEIGHT;
        zoom = (scaleX < scaleY) ? scaleX : scaleY;
    }

    lastFillScreen = fillScreen;
    viewportInitialized = true;
}

static void RayGame_UpdateOverlay(float frameDelta)
{
    if (volOsdTimer > 0.0f)
    {
        volOsdTimer -= frameDelta;
        if (volOsdTimer < 0.0f)
            volOsdTimer = 0.0f;
    }
}

static void RayGame_DrawOverlay(void)
{
    DrawFPS(10, 3);

    if (volOsdTimer > 0.0f)
        DrawText(volumeText, 10, 26, 20, WHITE);
}

/* ================================================================
   3. ÁUDIO
   ================================================================ */

void RayGame_SetMusic(const char *path, float volume, char looping)
{
    if (musicLoaded)
    {
        StopMusicStream(gameMusic);
        UnloadMusicStream(gameMusic);
        musicLoaded = false;
        playingMusic = false;
        gameMusic = (Music){0};
    }

    if (!audioReady || path == NULL || path[0] == '\0')
        return;

    gameMusic = LoadMusicStream(path);

    /* LoadMusicStream pode falhar; não use um stream inválido. */
    if (gameMusic.stream.buffer == NULL)
    {
        gameMusic = (Music){0};
        return;
    }

    musicLoaded = true;
    musicBaseVolume = RayGame_Clamp01(volume);
    gameMusic.looping = (looping != 0);

    SetMusicVolume(gameMusic, musicBaseVolume * musicVolume);
    PlayMusicStream(gameMusic);
    playingMusic = true;
}

void RayGame_ToggleMusic(char on)
{
    const bool enable = (on != 0);

    if (!musicLoaded || !audioReady || enable == playingMusic)
        return;

    if (enable)
        ResumeMusicStream(gameMusic);
    else
        PauseMusicStream(gameMusic);

    playingMusic = enable;
}

void RayGame_ResetMusic(void)
{
    if (!musicLoaded || !audioReady)
        return;

    StopMusicStream(gameMusic);
    SeekMusicStream(gameMusic, 0.0f);
    PlayMusicStream(gameMusic);
    playingMusic = true;
}

void RayGame_BumpMusicVolume(float delta)
{
    musicVolume = RayGame_Clamp01(musicVolume + delta);

    if (musicLoaded)
        SetMusicVolume(gameMusic, musicBaseVolume * musicVolume);

    RayGame_UpdateVolumeText();
    volOsdTimer = 2.0f;
}

float RayGame_MusicVolumeLevel(void)
{
    return musicVolume;
}

/* ================================================================
   4. TELA / FPS
   ================================================================ */

float RayGame_ZoomFactor(void)
{
    return zoom;
}

int RayGame_CanvasWidth(void)
{
    return game_options_fillScreen ? RAYGAME_CANVAS_WIDTH : width;
}

int RayGame_CanvasHeight(void)
{
    return game_options_fillScreen ? RAYGAME_CANVAS_HEIGHT : height;
}

void RayGame_SetFPS(int fps)
{
    /* 0 = sem limitador; valores positivos abaixo de 10 são normalizados. */
    if (fps < 0)
        fps = 10;
    else if (fps > 0 && fps < 10)
        fps = 10;

    targetFps = fps;
    game_options_fps = fps;

    /* Com VSync, deixa o sincronismo da tela controlar a apresentação. */
    if (!vsyncEnabled)
        SetTargetFPS(targetFps);
}

void RayGame_SetVsync(char on)
{
    vsyncEnabled = (on != 0);
    game_options_vsync = vsyncEnabled;

    if (vsyncEnabled)
    {
        SetWindowState(FLAG_VSYNC_HINT);
        SetTargetFPS(0);
    }
    else
    {
        ClearWindowState(FLAG_VSYNC_HINT);
        SetTargetFPS(targetFps);
    }
}

int RayGame_GetTargetFPS(void)
{
    return targetFps;
}

/* ================================================================
   5. INICIALIZAÇÃO
   ================================================================ */

void RayGame_Start(const char *gameTitle)
{
    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN |
                   FLAG_WINDOW_RESIZABLE |
                   FLAG_VSYNC_HINT);

    InitWindow(RAYGAME_CANVAS_WIDTH, RAYGAME_CANVAS_HEIGHT, gameTitle);

    if (!IsWindowReady())
    {
        Log_Fatal("[ ERROR ] could not initialize the game window!");
        return;
    }

    SetExitKey(KEY_NULL);

    InitAudioDevice();
    audioReady = IsAudioDeviceReady();

    GameData_Start();
    gameDataReady = true;

    /* Aplica as opções carregadas do GameData. */
    RayGame_SetVsync(game_options_vsync);
    if (!vsyncEnabled)
        RayGame_SetFPS(game_options_fps);

    stretchTarget = LoadRenderTexture(
        RAYGAME_CANVAS_WIDTH,
        RAYGAME_CANVAS_HEIGHT
    );

    stretchReady = (stretchTarget.id != 0);

    if (stretchReady)
        SetTextureFilter(stretchTarget.texture, TEXTURE_FILTER_BILINEAR);

    RayGame_UpdateViewport(true);
}

/* ================================================================
   6. LOOP PRINCIPAL
   ================================================================ */

void RayGame_GameLoop(void)
{
    if (game.scene == NULL)
    {
        Log_Fatal("[ ERROR ] game is not using any scene!");
        return;
    }

    while (!WindowShouldClose())
    {
        /*
         * Não limite dt a 1/FPS: isso descarta tempo real em frames lentos.
         * SetTargetFPS já controla a apresentação; dt deve medir o tempo
         * efetivamente decorrido entre frames.
         */
        dt = GetFrameTime();
        if (dt < 0.0f)
            dt = 0.0f;

        RayGame_UpdateViewport(false);
        RayGame_UpdateOverlay(dt);

        /* Input global. */
        if (IsKeyPressed(KEY_F11))
        {
            ToggleBorderlessWindowed();
            RayGame_UpdateViewport(true);
        }

        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
            RayGame_BumpMusicVolume(0.1f);

        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
            RayGame_BumpMusicVolume(-0.1f);

        /* Atualiza a cena atual. Ela pode trocar durante o callback. */
        if (game.scene != NULL && game.scene->functions.updateFunc != NULL)
            game.scene->functions.updateFunc(game.scene);

        if (playingMusic && musicLoaded)
            UpdateMusicStream(gameMusic);

        /* A cena pode ter sido removida durante updateFunc. */
        if (game.scene == NULL)
        {
            BeginDrawing();
            ClearBackground(BLACK);
            RayGame_DrawOverlay();
            EndDrawing();
            continue;
        }

        if (game_options_fillScreen && stretchReady)
        {
            /* Renderiza primeiro no canvas lógico 1280x720. */
            BeginTextureMode(stretchTarget);
            ClearBackground(BLACK);

            if (game.scene->functions.drawFunc != NULL)
                game.scene->functions.drawFunc(game.scene);

            EndTextureMode();

            /* Escala o canvas para a janela. */
            BeginDrawing();
            ClearBackground(BLACK);

            DrawTexturePro(
                stretchTarget.texture,
                (Rectangle){
                    0.0f,
                    0.0f,
                    (float)RAYGAME_CANVAS_WIDTH,
                    -(float)RAYGAME_CANVAS_HEIGHT
                },
                (Rectangle){
                    0.0f,
                    0.0f,
                    (float)width,
                    (float)height
                },
                (Vector2){0.0f, 0.0f},
                0.0f,
                WHITE
            );

            RayGame_DrawOverlay();
            EndDrawing();
        }
        else
        {
            BeginDrawing();
            ClearBackground(BLACK);

            if (game.scene->functions.drawFunc != NULL)
                game.scene->functions.drawFunc(game.scene);

            /* Barras de aspecto: também são necessárias quando a janela
               é maior que o canvas, mas não tem proporção 16:9. */
            {
                const int contentHeight = (int)(RAYGAME_CANVAS_HEIGHT * zoom);
                const int contentWidth = (int)(RAYGAME_CANVAS_WIDTH * zoom);
                const int barH = (height - contentHeight) / 2;
                const int barW = (width - contentWidth) / 2;

                if (barH > 0)
                {
                    DrawRectangle(0, 0, width, barH, BLACK);
                    DrawRectangle(0, height - barH, width, barH, BLACK);
                }

                if (barW > 0)
                {
                    DrawRectangle(0, 0, barW, height, BLACK);
                    DrawRectangle(width - barW, 0, barW, height, BLACK);
                }
            }

            RayGame_DrawOverlay();
            EndDrawing();
        }
    }
}

/* ================================================================
   7. DELTA TIME / LIMPEZA
   ================================================================ */

float RayGame_DeltaTime(void)
{
    return dt;
}

void RayGame_ClearScene(void)
{
    if (game.scene != NULL)
    {
        Render_StopCamera();
        RayScene_Destroy(game.scene);
        game.scene = NULL;
    }
}

void RayGame_Destroy(void)
{
    RayGame_ClearScene();

    if (musicLoaded && audioReady)
    {
        StopMusicStream(gameMusic);
        UnloadMusicStream(gameMusic);
        gameMusic = (Music){0};
        musicLoaded = false;
        playingMusic = false;
    }

    if (stretchReady)
    {
        UnloadRenderTexture(stretchTarget);
        stretchTarget = (RenderTexture2D){0};
        stretchReady = false;
    }

    if (gameDataReady)
    {
        GameData_Stop();
        gameDataReady = false;
    }

    if (audioReady)
    {
        CloseAudioDevice();
        audioReady = false;
    }

    if (IsWindowReady())
        CloseWindow();
}
