#include "std.h" 
#include "Game.h"
#include "GameData.h"
 
RayGame game; 

static float dt; 
static float zoom;
static int width;
static int height;

static float frametime; 
static int targetFps;
static char clearTexture; 

Music gameMusic;
 
static char playingMusic;

void RayGame_SetMusic(const char* path, float volume, char looping) { 
    gameMusic = LoadMusicStream(path);

    PlayMusicStream(gameMusic);  
    SetMusicVolume(gameMusic, volume);
    gameMusic.looping = looping;

    playingMusic = 0;
}

void RayGame_ToggleMusic(char on) {
    playingMusic = on;
}

void RayGame_ResetMusic() {
    SeekMusicStream(gameMusic, 0.0f);
    PlayMusicStream(gameMusic);
}

void RayGame_ClearMusic() {
    UnloadMusicStream(gameMusic);
}

// these are not really necessary, more for clarity ig
float RayGame_ZoomFactor() {
    return zoom;
}

int RayGame_WindowWidth() {
    return width;
}

int RayGame_WindowHeight() {
    return height;
}

void RayGame_SetFPS(int fps) { 
    targetFps = fps;
    game_options_fps = fps; 
    
    if(fps == 0) { 
        frametime = 0.03f; 
        SetTargetFPS(0);
        return;
    }

    if(fps < 10)
        fps = 10; 

    frametime = 1.0f / (float) fps; 
    SetTargetFPS(fps);
} 

void RayGame_SetVsync(char on) {
    if(on) {
        SetWindowState(FLAG_VSYNC_HINT);
        frametime = 1.0f / GetMonitorRefreshRate(GetCurrentMonitor());
    }
    else {
        ClearWindowState(FLAG_VSYNC_HINT);
        RayGame_SetFPS(game_options_fps);
    }
}

int RayGame_GetTargetFPS() {
    return targetFps;
}

void RayGame_Start(const char* gameTitle) {
    ChangeDirectory(GetApplicationDirectory());

    clearTexture = 0; 

    InitWindow(1280, 720, gameTitle); 
    InitAudioDevice();

    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN);
    SetWindowState(FLAG_WINDOW_RESIZABLE);   

    GameData_Start();
    RayGame_SetVsync(game_options_vsync); 
}


void RayGame_GameLoop() {
    if(game.scene == NULL) {
        puts("\n[ ERROR ] game is not using any scene!");
        exit(1);
    } 
    // close the game only if the x button was pressed 
    while(!WindowShouldClose() || IsKeyPressed(KEY_ESCAPE)) {   
        dt = GetFrameTime(); 

        width = GetScreenWidth();
        height = GetScreenHeight();
        zoom = fminf(width / 1280.0f, height / 720.0f); 

        // we do not want the game to move too fast if we are tabed out or something
        if(dt > frametime)
            dt = frametime; 
        
        if(IsKeyPressed(KEY_F11))
            ToggleBorderlessWindowed();

        if(game.scene->functions.updateFunc != NULL)
            game.scene->functions.updateFunc(game.scene);

        BeginDrawing();

        if(clearTexture) {
            ClearBackground(BLACK);
            clearTexture = 0;
        } 

        if(game.scene->functions.drawFunc != NULL && IsWindowFocused())
            game.scene->functions.drawFunc(game.scene);
        
        int barHeight = (height - 720 * zoom) / 2;
        DrawRectangle(0, 0, width, barHeight, BLACK);
        DrawRectangle(0, (int) height - barHeight, width, barHeight, BLACK);

        int barWidth = (width - 1280 * zoom) / 2;
        DrawRectangle(0, 0, barWidth, height, BLACK);
        DrawRectangle(width - barWidth, 0, barWidth, height, BLACK);

        DrawFPS(10, 3);

        EndDrawing();  

        if(playingMusic)
            UpdateMusicStream(gameMusic);
    }  
}  

float RayGame_DeltaTime() { 
    return dt;
}

void RayGame_ClearScene() {
    if(game.scene != NULL) { 
        Render_StopCamera(); // just in case
        RayScene_Destroy(game.scene);
    } 
    clearTexture = 1;
}

void RayGame_Destroy() { 
    UnloadMusicStream(gameMusic);
    RayScene_Destroy(game.scene);  
    GameData_Stop();
    CloseAudioDevice();
    CloseWindow();
}
