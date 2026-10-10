#ifndef GAME_H
#define GAME_H

#include "std.h"
#include "Scene.h"

#define GLSL_VERSION 330

typedef struct RayGame RayGame;

extern RayGame game;
extern Music gameMusic;

struct RayGame{ 
    RayScene* scene; 
};

void RayGame_Start(const char* gameTitle);

void RayGame_GameLoop(void);
void RayGame_Destroy(void);
void RayGame_ClearScene(void);
void RayGame_SetFPS(int fps);
void RayGame_SetVsync(char on);
float RayGame_DeltaTime(void);
float RayGame_ZoomFactor(void);
int RayGame_GetTargetFPS(void);

void RayGame_SetMusic(const char* path, float volume, char looping);
void RayGame_ToggleMusic(char on);
void RayGame_ResetMusic();
void RayGame_BumpMusicVolume(float delta);
float RayGame_MusicVolumeLevel(void);
int RayGame_CanvasWidth(void);
int RayGame_CanvasHeight(void);
#endif