#include"scenes/AllScenes.h"

#define SONG_COUNT 10
static char* songs[SONG_COUNT] = {
    "test", 
    "test2", 
    "test3", 
    "idk", 
    "pingas", 
    "manipulation", 
    "control", 
    "execution",
    "you_cant_run",
    "manhunt"
};

static float offset; 
static int selectedSong; 

static RayGraphicObject bg;
static Camera2D cam;

static Color outlineColor = {0, 0, 0, 0};
static Color fontColor = {255, 255, 255, 255};

static void Freeplay_Create([[maybe_unused]] RayScene* scene) {
    Render_DefaultRGT(&bg);

    bg.objType = RGT_IMAGE;
    bg.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");

    cam.target = (Vector2) {1280 / 2, 720 / 2};
    cam.zoom = 1;
}

static void Freeplay_Draw([[maybe_unused]] RayScene* scene) { 
    if(IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        MenuState_SetScene();
        return;
    }

    if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
        selectedSong++;
        if(selectedSong >= SONG_COUNT)
            selectedSong = 0;
    }

    if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
        selectedSong--;
        if(selectedSong < 0)
            selectedSong = SONG_COUNT - 1;
    }

    if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
        RayGame_ToggleMusic(0);
        PlayState_SetSong(songs[selectedSong]);
        PlayState_SetScene();
        return;
    }
 
    offset = Lerp(offset, selectedSong, RayGame_DeltaTime() * 10.0f);

    Render_SetCamera(&cam);
    Render_DrawGraphicObject(&bg);
 
    for(int i = 0; i < SONG_COUNT; i++) {
        const int fontSize = 80;
        const int shadow = 5;

        float d = fabsf(i - offset);

        Vector2 pos = {shadow + 150 + d * -30, shadow + 720 / 2 + (i - offset) * (10 + fontSize)};
         
        fontColor.a = 150;
        outlineColor.a = 100; 

        if(selectedSong == i)
            fontColor.a = outlineColor.a = 255; 

        DrawTextEx(mainFont, songs[i], pos, fontSize, 5, outlineColor);
        pos.x -= shadow;
        pos.y -= shadow;
        DrawTextEx(mainFont, songs[i], pos, fontSize, 5, fontColor);  
    }
    Render_StopCamera();
}

static void Freeplay_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bg.image);
}

static RayScene scene;
Scene_MakeSceneCode(scene, Freeplay_SetScene, Freeplay_Create, NULL, Freeplay_Draw, Freeplay_Destroy);