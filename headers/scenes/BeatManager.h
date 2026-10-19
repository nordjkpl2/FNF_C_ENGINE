/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl, outd day19 of10 2026 */
#pragma once
#ifndef BEATMANAGER_H
#define BEATMANAGER_H

#include "std.h"
#include "Game.h"
#include <stdbool.h>

typedef struct {
    Music* music;
    bool loop; 
    double time;      // double = sem drift em música de 3min+
    double btime;     // tempo do último step
    double stepCrochet; // cache: (60.0 / bpm) / 4.0
    float bpm;
    int step;
    int beat;
} BeatManager;

void BeatManager_New(BeatManager* restrict mng);
void BeatManager_Update(BeatManager* restrict manager, void (*onStepHit)(void), void (*onBeatHit)(void));
void BeatManager_Reset(BeatManager* restrict mng);
static inline void BeatManager_SetBPM(BeatManager* restrict mng, float bpm);

static inline void BeatManager_SetBPM(BeatManager* restrict mng, float bpm) {
    if (bpm < 1.0f) bpm = 1.0f;
    mng->bpm = bpm;
    mng->stepCrochet = 60.0 / (double)bpm / 4.0;
}

#endif