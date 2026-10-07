#include "scenes/BeatManager.h" 

void BeatManager_New(BeatManager* mng) {
    mng->beat = 0;
    mng->step = 0;
    mng->bpm = 0;
    mng->music = NULL;
    mng->time = 0;
    mng->btime = 0;
    mng->loop = 0;
}

void BeatManager_Reset(BeatManager* mng) {
    mng->beat = 0;
    mng->step = 0;
    mng->time = 0;
    mng->btime = 0;
}

void BeatManager_Update(BeatManager* manager, void (*onStepHit)(), void (*onBeatHit)()) { 
    if(manager->music == NULL || !IsMusicStreamPlaying(*manager->music))  
        return;

    float time = GetMusicTimePlayed(*manager->music);
    manager->time += RayGame_DeltaTime(); 

    if(manager->time > GetMusicTimeLength(*manager->music)) {
        if(manager->loop) 
            BeatManager_Reset(manager);
        return;
    }

    float timePerStep = (60.0f / manager->bpm) / 4.0f; 
    
    while(time - manager->btime >= timePerStep) { 
        manager->step++;

        if(onStepHit != NULL)
            onStepHit();

        if(manager->time - time < -0.02f || manager->time - time > 0.02f)
            manager->time = time;

        if(manager->step % 4 == 0) {
            manager->beat++;
            if(onBeatHit != NULL)
                onBeatHit();
        }

        manager->btime += timePerStep;
    }
}