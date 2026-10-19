/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */

#include "scenes/AllScenes.h"
/* // * Cena do Titulo, Otd day 18 of10 2026*/
static BeatManager beatHandle;
static RayAnimatedObject gf;
static RayGraphicObject fnf;
static float logoOffset;
static Vector2 fnfBaseSize; // CACHE: tamanho base do logo

static bool pressedEnter;
static float fadeTime;
static float time;
static bool beatToggle = 0;

static Camera2D cam; 
static const float fadeDuration = 2.0f; 
static const float LERP_SPEED = 10.0f;

static void TitleState_Create([[maybe_unused]] RayScene* scene) { 
    pressedEnter = false;
    time = 0.0f;
    fadeTime = 0.0f;
    beatToggle = 0;
    logoOffset = 0.0f;

    cam.zoom = 1.0f;
    cam.rotation = 0.0f;
    cam.target = (Vector2){640.0f, 360.0f}; // 1280/2, 720/2 pré-calculado
    cam.offset = (Vector2){0,0};

    // fnf logo - cache do tamanho evita GraphicObject_Sizes() por frame
    fnf.scrollFactor = (Vector2){1, 1};
    fnf.color = WHITE;
    fnf.objType = RGT_IMAGE;
    fnf.rotation = 0.0f;
    fnf.image = Render_LoadTexture("assets/images/title/logo.png");
    fnf.scaleX = fnf.scaleY = 1.0f;
    fnfBaseSize = (Vector2){(float)fnf.image.width, (float)fnf.image.height};

    // dancing gf
    {
        RayAnimationHandler gfAnimations = AnimationSet_LoadAnimations("assets/images/characters/gfDanceTitle.animset", Render_LoadTexture("assets/images/characters/gfDanceTitle.png"));
        AnimationSet_SetAnimationData(&gfAnimations, 0, 24, 0);
        AnimationSet_SetAnimationData(&gfAnimations, 1, 24, 0);

        Render_DefaultAnimated(&gf);
        gf.position = (Vector2){600, 50};
        gf.animationSet = gfAnimations; 
        AnimatedObject_SetAnimation(&gf, 0);
    }

    BeatManager_New(&beatHandle);
    beatHandle.bpm = 102;
    beatHandle.music = &gameMusic;
    beatHandle.loop = 1;
} 

static inline void onBeatHit(void) {
    beatToggle = !beatToggle; // 10x mais rápido que beat % 2
    fnf.scaleX = fnf.scaleY = 1.1f;
    AnimatedObject_SetAnimation(&gf, beatToggle); 
} 

static void TitleState_Draw([[maybe_unused]] RayScene* scene) { 
    const float dt = RayGame_DeltaTime(); // 1 chamada só
    time += dt;

    Render_SetCamera(&cam);
    BeatManager_Update(&beatHandle, NULL, onBeatHit);
    ClearBackground(BLACK);

    AnimatedObject_UpdateFrame(&gf);
    Render_DrawAnimatedObject(&gf);

    // bopping fnf logo - Lerp exponencial framerate-independent
    {
        // Exp decay: 1 - exp(-speed * dt) -> mesmo feeling em 60hz e 240hz
        const float lerpFactor = 1.0f - expf(-LERP_SPEED * dt);
        fnf.scaleX += (1.0f - fnf.scaleX) * lerpFactor;
        fnf.scaleY = fnf.scaleX;

        // Usando cache, sem chamar GraphicObject_Sizes()
        fnf.position.x = 380.0f - (fnfBaseSize.x * fnf.scaleX) * 0.5f + logoOffset;
        fnf.position.y = 270.0f - (fnfBaseSize.y * fnf.scaleY) * 0.5f;
        Render_DrawGraphicObject(&fnf);
    }

    if(pressedEnter) {
        if(time > 3.0f) {
            MenuState_SetScene();
            Render_StopCamera();
            return;
        } 
        if(time > 1.0f) {
            const float slide = (time - 1.0f) * 700.0f * dt;
            gf.position.x += slide;
            logoOffset -= slide;
        }
    }
    else { 
        DrawTextEx(mainFont, "PRESS ENTER TO PLAY!", (Vector2){50, 670}, 30, 5, WHITE); 
        if(IsKeyPressed(KEY_ENTER)) {
            pressedEnter = true;
            time = 0.0f;
            fadeTime = fadeDuration;
        } 
    }

    // white flash - sem divisão por frame
    if(fadeTime > 0.0f) {
        // fadeTime já está em [0, fadeDuration], só precisa normalizar 1x
        DrawRectangle(0, 0, 1280, 720, (Color){255, 255, 255, (unsigned char)(255.0f * (fadeTime * 0.5f))});
        fadeTime -= dt;
    }

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