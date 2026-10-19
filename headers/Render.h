/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#pragma once
#ifndef RENDER_H
#define RENDER_H

#include "std.h" 
#include <string.h>

#define ADD_VEC(a, b) ((Vector2){ (a).x + (b).x, (a).y + (b).y })

typedef enum {
    RGT_CIRCLE,
    RGT_RECTANGLE,
    RGT_IMAGE,
} RayGraphicObjectType;

typedef struct {
    Vector2 position;  
    Vector2 scrollFactor;
    Color color; 
    float scaleX;
    float scaleY;
    float rotation;
    RayGraphicObjectType objType;
    union {
        float radius;
        Vector2 rectSize;
        Texture2D image; 
    };
} RayGraphicObject;

typedef struct {
    int x, y, w, h;
    int fx, fy;
} Frame;

typedef struct {
    int currentFrame;
    int animationIndex;
    float time; 
    char isValid;
    char _pad[3]; // alinha pra 16 bytes
} RayAnimationState;

typedef struct {
    char name[32];
    Frame* frames;
    Vector2 animationOffset;
    int frameCount;
    int fps;
    float frameTime; // 1.0f / fps -> PRÉ-CALCULADO NO LOAD, nunca dividir em hot path
    char looped;
    char _pad2[3];
} RayAnimation;

typedef struct {
    Texture2D texture;
    RayAnimation* animations;
    int animationCount;
} RayAnimationHandler;

typedef struct {
    Vector2 position; 
    Vector2 scrollFactor;
    Color color;
    float rotation;
    float scaleX;
    float scaleY;
    RayAnimationHandler animationSet;
    RayAnimationState currentAnim;
} RayAnimatedObject;

// --- camera ---
void Render_SetCamera(Camera2D *cam);
void Render_StopCamera(void);
void Render_DefaultCamera(Camera2D* cam);

// --- defaults: viram inline, zero call ---
static inline void Render_DefaultRGT(RayGraphicObject* restrict obj) {
    *obj = (RayGraphicObject){ .position={0,0}, .scrollFactor={1,1}, .color=WHITE, .scaleX=1, .scaleY=1, .rotation=0, .objType=RGT_RECTANGLE };
}
static inline void Render_DefaultAnimated(RayAnimatedObject* restrict obj) {
    *obj = (RayAnimatedObject){ .position={0,0}, .scrollFactor={1,1}, .color=WHITE, .scaleX=1, .scaleY=1, .rotation=0, .currentAnim.isValid=0 };
}

Texture2D Render_LoadTexture(const char* path);

// --- AnimationSet ---
RayAnimationHandler AnimationSet_LoadAnimations(const char* animset, Texture2D image);
int AnimationSet_FindAnimation(const RayAnimationHandler* restrict animations, const char* animationName);
void AnimationSet_FreeAnimations(RayAnimationHandler* animations);
void AnimationSet_FreeAll(RayAnimationHandler* animations);

static inline void AnimationSet_SetAnimationOffset(RayAnimationHandler* restrict animations, int idx, Vector2 offset) {
    animations->animations[idx].animationOffset = offset;
}
static inline void AnimationSet_SetAnimationData(RayAnimationHandler* restrict animations, int idx, int fps, char looped) {
    RayAnimation* a = &animations->animations[idx];
    a->fps = fps;
    a->looped = looped;
    a->frameTime = fps > 0 ? 1.0f / (float)fps : 0.0f; // calcula 1x aqui
}
static inline float AnimationSet_AnimationLength(const RayAnimationHandler* restrict animations, int idx) {
    const RayAnimation* a = &animations->animations[idx];
    return a->frameCount * a->frameTime;
}

// --- utils / draw ---
Vector2 GraphicObject_Sizes(const RayGraphicObject* restrict obj);
Vector2 AnimatedObject_Sizes(const RayAnimatedObject* restrict obj);
char AnimatedObject_FinishedAnimation(const RayAnimatedObject* restrict obj);
void AnimatedObject_SetAnimation(RayAnimatedObject* restrict obj, int animation);
void AnimatedObject_UpdateFrame(RayAnimatedObject* restrict obj);

void Render_DrawGraphicObject(const RayGraphicObject* restrict obj);
void Render_DrawAnimatedObject(const RayAnimatedObject* restrict obj);

#define AnimationSet_SetData(vName, animSet, animName, fps, isLooped, animOffsetX, animOffsetY) do { \
    vName = AnimationSet_FindAnimation(&(animSet), animName); \
    if ((unsigned)vName < (unsigned)(animSet).animationCount) { \
        AnimationSet_SetAnimationData(&(animSet), vName, fps, isLooped); \
        AnimationSet_SetAnimationOffset(&(animSet), vName, (Vector2){animOffsetX, animOffsetY}); \
    } \
} while(0)

#endif