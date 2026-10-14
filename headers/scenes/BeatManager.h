/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#pragma once
#ifndef BEATMANAGER_H
#define BEATMANAGER_H
/* // ! Includes */
#include "std.h"
#include "Game.h"
 /* // * code */
typedef struct {
    Music* music;
    char loop; 
    float time;
    float btime;
    float bpm;
    int step;
    int beat;
} BeatManager;
/* // ? Beat Manager Functions */
void BeatManager_New(BeatManager* mng);
void BeatManager_Update(BeatManager* manager, void (*onStepHit)(void), void (*onBeatHit)(void));
void BeatManager_Reset(BeatManager* mng);
#endif