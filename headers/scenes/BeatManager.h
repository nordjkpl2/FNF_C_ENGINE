#ifndef BEATMANAGER_H
#define BEATMANAGER_H

#include "std.h"
#include "Game.h"
 
typedef struct {
    Music* music;
    char loop; 
    float time;
    float btime;
    float bpm;
    int step;
    int beat;
} BeatManager;

void BeatManager_New(BeatManager* mng);
void BeatManager_Update(BeatManager* manager, void (*onStepHit)(void), void (*onBeatHit)(void));
void BeatManager_Reset(BeatManager* mng);

#endif