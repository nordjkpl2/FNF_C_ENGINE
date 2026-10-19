/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl, outd day19 of10 2026*/
#include "scenes/BeatManager.h" 
#include <math.h>

void BeatManager_New(BeatManager* restrict mng) {
    mng->beat = 0;
    mng->step = 0;
    mng->bpm = 100.0f; // nunca 0 para evitar div/0
    mng->stepCrochet = 60.0 / 100.0 / 4.0;
    mng->music = NULL;
    mng->time = 0;
    mng->btime = 0;
    mng->loop = 0;
}

void BeatManager_Reset(BeatManager* restrict mng) {
    mng->beat = 0;
    mng->step = 0;
    mng->time = 0;
    mng->btime = 0;
    if (mng->music) SeekMusicStream(*mng->music, 0.0f);
}

// HOT PATH - roda 60x por segundo
void BeatManager_Update(BeatManager* restrict manager, void (*onStepHit)(void), void (*onBeatHit)(void)) { 
    if (manager->music == NULL || manager->bpm < 1.0f) return;
    if (!IsMusicStreamPlaying(*manager->music)) return;

    const double audioTime = (double)GetMusicTimePlayed(*manager->music);
    
    // Resync único por frame (fora do while) = 1 branch vs N branches
    // Psych faz isso: se drift > 20ms, teleporta
    if (fabs(audioTime - manager->time) > 0.02)
        manager->time = audioTime;
    else
        manager->time = audioTime; // fonte única = audioTime. DeltaTime só para interpolação visual se precisar

    if (manager->loop) {
        float len = GetMusicTimeLength(*manager->music);
        if (audioTime >= len - 0.02f) {
            BeatManager_Reset(manager);
            return;
        }
    }

    const double crochet = manager->stepCrochet;
    // Trava de segurança: se lagar, não roda 500 steps de uma vez
    int stepsThisFrame = 0;
    while (audioTime - manager->btime >= crochet && stepsThisFrame < 32) { 
        manager->btime += crochet;
        manager->step++;
        stepsThisFrame++;

        if (onStepHit) onStepHit();

        if ((manager->step & 3) == 0) { // step % 4 == 0 mas sem divisão
            manager->beat++;
            if (onBeatHit) onBeatHit();
        }
    }
    // Se ficou muito atrás (lag spike), teleporta btime para não ficar em loop infinito
    if (stepsThisFrame == 32) {
        manager->btime = audioTime;
    }
}