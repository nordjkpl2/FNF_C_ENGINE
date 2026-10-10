#include "scenes/AllScenes.h"
 
static BeatManager beatHandle;

static RayAnimatedObject gf;

static RayGraphicObject fnf;
static float logoOffset;
  
static char pressedEnter;

static float fadeTime;
static float time;
static int beat = 0;

static Camera2D cam; 

static const float fadeDuration = 2; 

static void TitleState_Create([[maybe_unused]] RayScene* scene) { 
    // resetting params
    // very important if we revisit this state in the future
    {  
        pressedEnter = 0;
        time = 0;
        fadeTime = 0;
        beat = 0;

        cam.zoom = 1;
        cam.rotation = 0;
        cam.target = (Vector2) {1280 / 2, 720 / 2};
    }

    // fnf logo
    {
        logoOffset = 0;

        fnf.scrollFactor = (Vector2) {1, 1};
        fnf.color = WHITE;
        fnf.objType = RGT_IMAGE;
        fnf.rotation = 0;
        fnf.image = Render_LoadTexture("assets/images/title/logo.png");
        fnf.scaleX = fnf.scaleY = 1;
    }

    // dancing gf
    {
        RayAnimationHandler gfAnimations = AnimationSet_LoadAnimations("assets/images/characters/gfDanceTitle.animset", Render_LoadTexture("assets/images/characters/gfDanceTitle.png"));
        AnimationSet_SetAnimationData(&gfAnimations, 0, 24, 0);
        AnimationSet_SetAnimationData(&gfAnimations, 1, 24, 0);

        Render_DefaultAnimated(&gf);
        gf.position = (Vector2) {600, 50};
        gf.animationSet = gfAnimations; 
        AnimatedObject_SetAnimation(&gf, 0);
    }

    // music and beat manager 
    {
        BeatManager_New(&beatHandle);
        beatHandle.bpm = 102;
        beatHandle.music = &gameMusic;
        beatHandle.loop = 1;
    } 
} 

static void onBeatHit() {
    beat++;

    fnf.scaleX = fnf.scaleY = 1.1f;
    AnimatedObject_SetAnimation(&gf, beat % 2); 
} 

// updating with drawing
static void TitleState_Draw([[maybe_unused]] RayScene* scene) { 
    Render_SetCamera(&cam);
 
    BeatManager_Update(&beatHandle, NULL, onBeatHit);

    ClearBackground(BLACK);
    AnimatedObject_UpdateFrame(&gf);
    Render_DrawAnimatedObject(&gf);

    // bopping fnf logo
    {
        fnf.scaleX = fnf.scaleY = Lerp(fnf.scaleX, 1, RayGame_DeltaTime() * 10);

        Vector2 sizes = GraphicObject_Sizes(&fnf);
        fnf.position.x = 380 - sizes.x * 0.5f + logoOffset;
        fnf.position.y = 270 - sizes.y * 0.5f;
        Render_DrawGraphicObject(&fnf);
    }

    if(pressedEnter) {
        if(time > 3) {
            MenuState_SetScene();
            return;
        } 
        else if(time > 1) {
            gf.position.x += (time - 1) * 700 * RayGame_DeltaTime();
            logoOffset -= (time - 1) * 700 * RayGame_DeltaTime();
        }
    }
    else { 
        DrawTextEx(mainFont, "PRESS ENTER TO PLAY!", (Vector2) {50, 670}, 30, 5, WHITE); 

        if(IsKeyPressed(KEY_ENTER)) {
            pressedEnter = 1;

            // reusing the time for the transitions 
            time = 0;
            fadeTime = fadeDuration;
        } 
    }

    // white flash
    if(fadeTime > 0) {
        DrawRectangle(0, 0, 1280, 720, (Color) {255, 255, 255, 255 * (fadeTime / fadeDuration)});
        fadeTime -= RayGame_DeltaTime();
    }
    time += RayGame_DeltaTime();

    Render_StopCamera();
}

static void TitleState_Destroy([[maybe_unused]] RayScene* scene) { 
    AnimationSet_FreeAll(&gf.animationSet); 
    UnloadTexture(fnf.image); 
}

static RayScene titleScene;
Scene_MakeSceneCode(
    titleScene, 
    TitleState_SetScene, 
    TitleState_Create, 
    NULL, 
    TitleState_Draw, 
    TitleState_Destroy
);