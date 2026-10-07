#ifndef RENDER_H
#define RENDER_H

#include "std.h" 

#define ADD_VEC(a, b) (Vector2) {a.x + b.x, a.y + b.y}

typedef enum {
    RGT_CIRCLE,
    RGT_RECTANGLE,
    RGT_IMAGE,
} RayGraphicObjectType;

// Basically an object that is either a shape or a static image
typedef struct {
    Vector2 position;  
    Vector2 scrollFactor;
    Color color; 

    float scaleX;
    float scaleY;
    float rotation;

    union {
        float radius;
        Vector2 rectSize;
        Texture2D image; 
    };

    RayGraphicObjectType objType;

} RayGraphicObject;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int fx;
    int fy;
} Frame;

typedef struct {
    int currentFrame;
    int animationIndex;
    float time; 
    char isValid;
} RayAnimationState;

typedef struct {
    int fps;
    int frameCount;
    char looped;
    char name[32];

    // allocated
    Frame* frames;

    // affected by the scale!
    Vector2 animationOffset;
} RayAnimation;

// allocated
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

void Render_SetCamera(Camera2D *cam);
void Render_StopCamera();

void Render_DefaultRGT(RayGraphicObject* obj);
void Render_DefaultAnimated(RayAnimatedObject* obj);
void Render_DefaultCamera(Camera2D* cam);

RayGraphicObject* Render_NewRayGraphicObjects(int count);

Texture2D Render_LoadTexture(const char* path);

RayAnimationHandler AnimationSet_LoadAnimations(const char* animset, Texture2D image);
int AnimationSet_FindAnimation(RayAnimationHandler* animations, const char* animationName);
void AnimationSet_FreeAnimations(RayAnimationHandler* animations);
void AnimationSet_FreeAll(RayAnimationHandler* animations);
void AnimationSet_SetAnimationOffset(RayAnimationHandler* animations, int animationIndex, Vector2 offset);
void AnimationSet_SetAnimationData(RayAnimationHandler* animations, int animationIndex, int fps, char looped);
float AnimationSet_AnimationLength(RayAnimationHandler* animations, int animIndex);

Vector2 GraphicObject_Sizes(RayGraphicObject* obj);

Vector2 AnimatedObject_Sizes(RayAnimatedObject* obj);
char AnimatedObject_FinishedAnimation(RayAnimatedObject* obj);
void AnimatedObject_SetAnimation(RayAnimatedObject* obj, int animation);
void AnimatedObject_UpdateFrame(RayAnimatedObject* obj);

void Render_DrawGraphicObject(RayGraphicObject* obj);
void Render_DrawAnimatedObject(RayAnimatedObject* obj);

// this macro exists for the simple cases you know
#define AnimationSet_SetData(vName, animSet, animName, fps, isLooped, animOffsetX, animOffsetY) vName = AnimationSet_FindAnimation(&animSet, animName); AnimationSet_SetAnimationData(&animSet, vName, fps, isLooped); AnimationSet_SetAnimationOffset(&animSet, vName, (Vector2) {animOffsetX, animOffsetY});


#endif