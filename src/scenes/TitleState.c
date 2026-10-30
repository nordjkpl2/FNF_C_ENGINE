/*
 * Friday Night Funkin' C Engine
 * (c) 2026 nordjkpl
 * TitleState.c - title screen
 */

#include "scenes/AllScenes.h"

#include <math.h>
#include <stdbool.h>

#define TITLE_FADE_DURATION 2.0f
#define TITLE_TRANSITION_TIME 3.0f
#define TITLE_SLIDE_START 1.0f
#define TITLE_SLIDE_SPEED 700.0f
#define TITLE_LOGO_LERP_SPEED 10.0f

static BeatManager beatHandle;
static RayAnimatedObject gf;
static RayGraphicObject fnf;
static Camera2D cam;

/* Cached logo dimensions: do not query texture size every frame. */
static Vector2 fnfBaseSize;

static float logoScale = 1.0f;
static float logoOffset;
static float fadeTime;
static float enterElapsed;

static bool pressedEnter;
static bool beatToggle;

static void TitleState_OnBeatHit(void)
{
    beatToggle = !beatToggle;
    logoScale = 1.1f;
    AnimatedObject_SetAnimation(&gf, beatToggle ? 1 : 0);
}

static void TitleState_Create([[maybe_unused]] RayScene *scene)
{
    pressedEnter = false;
    beatToggle = false;

    logoScale = 1.0f;
    logoOffset = 0.0f;
    fadeTime = 0.0f;
    enterElapsed = 0.0f;

    cam = (Camera2D){
        .target = {640.0f, 360.0f},
        .offset = {0.0f, 0.0f},
        .rotation = 0.0f,
        .zoom = 1.0f
    };

    /* Title logo. */
    Render_DefaultRGT(&fnf);
    fnf.objType = RGT_IMAGE;
    fnf.image = Render_LoadTexture("assets/images/title/logo.png");
    fnf.scrollFactor = (Vector2){1.0f, 1.0f};
    fnf.color = WHITE;
    fnf.rotation = 0.0f;
    fnf.scaleX = 1.0f;
    fnf.scaleY = 1.0f;
    fnfBaseSize = (Vector2){
        (float)fnf.image.width,
        (float)fnf.image.height
    };

    /* Dancing girlfriend. */
    {
        RayAnimationHandler animations = AnimationSet_LoadAnimations(
            "assets/images/characters/gfDanceTitle.animset",
            Render_LoadTexture("assets/images/characters/gfDanceTitle.png")
        );

        AnimationSet_SetAnimationData(&animations, 0, 24, 0);
        AnimationSet_SetAnimationData(&animations, 1, 24, 0);

        Render_DefaultAnimated(&gf);
        gf.position = (Vector2){600.0f, 50.0f};
        gf.animationSet = animations;
        AnimatedObject_SetAnimation(&gf, 0);
    }

    BeatManager_New(&beatHandle);
    beatHandle.bpm = 102;
    beatHandle.music = &gameMusic;
    beatHandle.loop = 1;
}

/*
 * State and input updates belong in the update callback, not Draw.
 * This keeps animation timing and movement independent from render count.
 */
static void TitleState_Update([[maybe_unused]] RayScene *scene)
{
    float dt = RayGame_DeltaTime();

    /* Ignore invalid/negative delta time without introducing a frame-rate cap. */
    if (!(dt > 0.0f))
        dt = 0.0f;

    BeatManager_Update(&beatHandle, NULL, TitleState_OnBeatHit);
    AnimatedObject_UpdateFrame(&gf);

    /* Frame-rate-independent return of the logo to its base scale. */
    if (logoScale != 1.0f && dt > 0.0f)
    {
        const float blend = 1.0f - expf(-TITLE_LOGO_LERP_SPEED * dt);
        logoScale += (1.0f - logoScale) * blend;

        if (logoScale < 1.0001f)
            logoScale = 1.0f;
    }

    if (!pressedEnter)
    {
        if (IsKeyPressed(KEY_ENTER))
        {
            pressedEnter = true;
            enterElapsed = 0.0f;
            fadeTime = TITLE_FADE_DURATION;
        }
    }
    else
    {
        enterElapsed += dt;

        if (enterElapsed >= TITLE_TRANSITION_TIME)
        {
            MenuState_SetScene();
            return;
        }

        if (enterElapsed >= TITLE_SLIDE_START && dt > 0.0f)
        {
            const float slide = TITLE_SLIDE_SPEED * dt;
            gf.position.x += slide;
            logoOffset -= slide;
        }
    }

    if (fadeTime > 0.0f)
    {
        fadeTime -= dt;
        if (fadeTime < 0.0f)
            fadeTime = 0.0f;
    }
}

static void TitleState_Draw([[maybe_unused]] RayScene *scene)
{
    ClearBackground(BLACK);

    /* World-space title elements. */
    Render_SetCamera(&cam);
    {
        fnf.scaleX = logoScale;
        fnf.scaleY = logoScale;

        fnf.position.x = 380.0f - (fnfBaseSize.x * logoScale) * 0.5f + logoOffset;
        fnf.position.y = 270.0f - (fnfBaseSize.y * logoScale) * 0.5f;

        Render_DrawAnimatedObject(&gf);
        Render_DrawGraphicObject(&fnf);
    }
    Render_StopCamera();

    /* Screen-space UI: do not inherit the world camera transform. */
    if (!pressedEnter)
    {
        DrawTextEx(
            mainFont,
            "PRESS ENTER TO PLAY!",
            (Vector2){50.0f, 670.0f},
            30.0f,
            5.0f,
            WHITE
        );
    }

    if (fadeTime > 0.0f)
    {
        const float alphaFactor = fadeTime / TITLE_FADE_DURATION;
        const unsigned char alpha = (unsigned char)(255.0f * alphaFactor);
        DrawRectangle(0, 0, 1280, 720, (Color){255, 255, 255, alpha});
    }
}

static void TitleState_Destroy([[maybe_unused]] RayScene *scene)
{
    AnimationSet_FreeAll(&gf.animationSet);
    UnloadTexture(fnf.image);
}

static RayScene titleScene;
Scene_MakeSceneCode(
    titleScene,
    TitleState_SetScene,
    TitleState_Create,
    TitleState_Update,
    TitleState_Draw,
    TitleState_Destroy
);
