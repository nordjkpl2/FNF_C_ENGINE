/* //p Friday Night Funkin' C Engine (c) 2026 nordjkpl outd day30 of10 2026*/
#include "scenes/BeatManager.h"
#include <math.h>
#include <stddef.h>

#define BEATMANAGER_DEFAULT_BPM 100.0f
#define BEATMANAGER_MIN_BPM 1.0f
#define BEATMANAGER_MAX_STEPS_PER_UPDATE 32
#define BEATMANAGER_LOOP_EPSILON 0.02f

static inline char BeatManager_IsValidBpm(float bpm) {
    return isfinite(bpm) && bpm >= BEATMANAGER_MIN_BPM;
}

static inline double BeatManager_Crochet(float bpm) {
    /* 60 segundos / BPM / 4 steps = 15 / BPM. */
    return 15.0 / (double)bpm;
}

void BeatManager_New(BeatManager* restrict manager) {
    if(manager == NULL)
        return;

    manager->beat = 0;
    manager->step = 0;
    manager->bpm = BEATMANAGER_DEFAULT_BPM;
    manager->stepCrochet = BeatManager_Crochet(BEATMANAGER_DEFAULT_BPM);
    manager->music = NULL;
    manager->time = 0.0;
    manager->btime = 0.0;
    manager->loop = 0;
}

void BeatManager_Reset(BeatManager* restrict manager) {
    if(manager == NULL)
        return;

    manager->beat = 0;
    manager->step = 0;
    manager->time = 0.0;
    manager->btime = 0.0;

    if(manager->music != NULL)
        SeekMusicStream(*manager->music, 0.0f);
}

/* Executado uma vez por frame. O relógio da música é a fonte de tempo. */
void BeatManager_Update(
    BeatManager* restrict manager,
    void (*onStepHit)(void),
    void (*onBeatHit)(void)
) {
    if(manager == NULL || manager->music == NULL)
        return;

    if(!BeatManager_IsValidBpm(manager->bpm))
        return;

    Music* music = manager->music;
    if(!IsMusicStreamPlaying(*music))
        return;

    const double audioTime = (double)GetMusicTimePlayed(*music);
    if(!isfinite(audioTime) || audioTime < 0.0)
        return;

    /* Mantém o tempo público sincronizado com o áudio; o if anterior era redundante. */
    manager->time = audioTime;

    if(manager->loop) {
        const float musicLength = GetMusicTimeLength(*music);
        if(isfinite(musicLength) && musicLength > 0.0f &&
           audioTime >= (double)musicLength - BEATMANAGER_LOOP_EPSILON) {
            BeatManager_Reset(manager);
            return;
        }
    }

    /* BPM pode mudar em onStepHit quando uma seção nova começa. */
    float activeBpm = manager->bpm;
    double crochet = BeatManager_Crochet(activeBpm);
    manager->stepCrochet = crochet;

    int stepsThisFrame = 0;
    while((audioTime - manager->btime) >= crochet &&
          stepsThisFrame < BEATMANAGER_MAX_STEPS_PER_UPDATE) {
        manager->btime += crochet;
        manager->step++;
        stepsThisFrame++;

        if(onStepHit != NULL)
            onStepHit();

        if((manager->step & 3) == 0) {
            manager->beat++;
            if(onBeatHit != NULL)
                onBeatHit();
        }

        /* Recalcula apenas se algum callback alterou o BPM. */
        if(manager->bpm != activeBpm) {
            if(!BeatManager_IsValidBpm(manager->bpm))
                break;

            activeBpm = manager->bpm;
            crochet = BeatManager_Crochet(activeBpm);
            manager->stepCrochet = crochet;
        }
    }

    /* Limita o custo de recuperação depois de travamentos longos. */
    if(stepsThisFrame == BEATMANAGER_MAX_STEPS_PER_UPDATE &&
       (audioTime - manager->btime) >= crochet) {
        manager->btime = audioTime;
    }
}
