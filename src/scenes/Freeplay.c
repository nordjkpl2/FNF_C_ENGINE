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
    }
    Render_StopCamera();
}

static void Freeplay_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bg.image);
    SongList_Free(songs);
    songs = NULL;
    songCount = 0;
}

static RayScene scene;
Scene_MakeSceneCode(scene, Freeplay_SetScene, Freeplay_Create, NULL, Freeplay_Draw, Freeplay_Destroy);
