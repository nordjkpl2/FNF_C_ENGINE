/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#pragma once
#ifndef SCENE_H
#define SCENE_H

#include "std.h"
#include "Render.h"

typedef struct RayScene RayScene;

typedef void (*InstantiateSceneFunction)(RayScene*);
typedef void (*DrawSceneFunction)(RayScene*);
typedef void (*UpdateSceneFunction)(RayScene*);
typedef void (*DestroySceneFunction)(RayScene*);

typedef struct {
    InstantiateSceneFunction instantiateFunc;
    DrawSceneFunction        drawFunc;
    UpdateSceneFunction      updateFunc;
    DestroySceneFunction     destroyFunc;
} RaySceneFunctions;

struct RayScene {
    RaySceneFunctions functions;
};

/*#define Scene_MakeSceneCode(s, set, init, update, draw, destroy) void set(void){\
    RayGame_ClearScene();\
    RayScene_Create(&s, (RaySceneFunctions) {\
        .instantiateFunc = init,\
        .updateFunc = update,\
        .drawFunc = draw,\
        .destroyFunc = destroy\
    });\
    game.scene = &s;\
}\*/ // sem o inline
#define Scene_MakeSceneCode(s, set, init, update, draw, destroy) inline void set(void) { \
    RayGame_ClearScene(); \
    RayScene_Create(&(s), (RaySceneFunctions) { \
        .instantiateFunc = init, \
        .updateFunc = update, \
        .drawFunc = draw, \
        .destroyFunc = destroy \
    }); \
    game.scene = &(s); \
} \

void RayScene_Create(RayScene* scene, RaySceneFunctions functions);
void RayScene_Destroy(RayScene* scene);

#endif
