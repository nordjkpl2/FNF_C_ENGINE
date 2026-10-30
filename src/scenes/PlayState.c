/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#include "scenes/AllScenes.h"
#include "scenes/Character.h"
#include "scenes/Note.h"
#include "scenes/Song.h"
#include <stdlib.h>

//!OUTD day30/10/2026
/* // * Gameplay Score Misses e Tempo */

static float smooth(float a, float b, float c) {
    return Lerp(a, b, c * c * c * c);
}

typedef struct { 
    RayGraphicObject* objects;
    float defaultCameraZoom;

    int backgroundObjects;
    int objectCount;
} Stage;  

// intro / ui
static int introHit;
static float introTime, introStep;  

struct { 
    int sick, good, bad, shit;
} UI_ASSETS;

static RayAnimationHandler uiAssets;
static RayAnimatedObject introText;
static Sound introSounds[4];

// characters
static Girlfriend gf;
static Character bf;
static Character dad;
static Character* target = &dad;

// stage
static Stage stage;

// music
static char songDir[256];
static char hasVoices;
static char audioWasPaused;
static char returnToEditor, returnToWeek, diffSuf[16], overrideP2[32];
static Music inst, voices;
static Song song; 

static char restarted;

// death
static int deathAnimations[3];
static float deadTime;
static char justDied;
static Music deathMusic,deathMusicEnd;
static Sound deathSounds;
static char retryin;

// beat
static int currentSteps;
static int currentSection;
static BeatManager songBeat; 

// optimizing
static RayAnimatedObject trail;
static RayAnimatedObject end;
static RayAnimatedObject renderNote;

// note stuff
Note* notes;
size_t noteCount;
size_t firstNote;

// sustain colors
static const Color normalColor = {255, 255, 255, 200};
static const Color missedColor = {150, 150, 150, 200};
static const Vector2 trailOffset = {40, 60}; 

static int nextSectionToSpawn;
static int spawnedSections;

// strumlines
static StrumNote dadStrumline[4];
static StrumNote bfStrumline[4];

// camera
static Vector2 cameraTarget; 
static Camera2D camGame; 
static Camera2D camHUD; 

// sfx
static Sound missSFX[3]; 
static Sound hitSFX;

// ui
static Font vcrFont;
static float bop;
static float bopT;
static Color dadColor;
static Color bfColor;

static Rectangle healthbar;
static int healthbarPadding;

// stats  
static float health; // 0 - 1
static int score;
static int lastScoreDrawn = -1;
static char scoreBuf[32] = "Score: 0";
static Vector2 scorePos;

static int misses;
static int lastMissesDrawn = -1;
static char missBuf[32] = "Misses: 0";
static Vector2 missPos;

static int lastTimeSec = -1;
static char timeBuf[32] = "0:00 / 0:00";
static Vector2 timePos;
static float lastVoiceSync = -10.0f;

static float popupTimer;
static float popupVelocity;
static RayAnimatedObject popupObj; 

// pause menu
static char isPaused;
static int currentOption;
static float optionOffset;
static float pauseTimer;

// Função de ordenação para qsort (Garante que nunca ocorra bug de notas fora de ordem)
static int Note_CompareChronological(const void* a, const void* b) {
    float diff = ((const Note*)a)->time - ((const Note*)b)->time;
    if (diff < 0) return -1;
    if (diff > 0) return 1;
    return 0;
}

// ~ 1. StrumLines  
static float Scroll_StrumY(char mustHit) {
    if (game_options_scroll == 2) return 20.0f;
    if (game_options_scroll == 1) return 670.0f;
    return 50.0f;
}

static float Scroll_NoteVelocity(void) {
    if (game_options_scroll == 1) return 450.0f;
    return -450.0f;
}

static float Scroll_StrumX(int lane, char mustHit) {
    if (game_options_scroll != 2) {
        return mustHit ? (700.0f + 110.0f * (lane - 4)) : (50.0f + 110.0f * lane);
    }
    if (mustHit) {
        int bfLane = (lane >= 4) ? (lane - 4) : lane;
        return 475.0f + 110.0f * bfLane;
    } else {
        return 50.0f + 55.0f * lane;
    }
}

static float Scroll_NoteVelocityX(int lane, char mustHit) {
    if (game_options_scroll != 2) return 0.0f;
    float startX = mustHit ? (475.0f + 110.0f * lane) : (50.0f + 55.0f * lane);
    float targetX = 640.0f;
    return (targetX - startX) * 0.003f * 60.0f;
}

static float Scroll_PlayerStrumY(void) { return Scroll_StrumY(1); }
static float Scroll_OppStrumY(void) { return Scroll_StrumY(0); }
static float Scroll_OppStrumX(int lane) { return Scroll_StrumX(lane, 0); }
static float Scroll_PlayerStrumX(int lane) { return Scroll_StrumX(lane + 4, 1); }
static float Scroll_StrumlineY(void) { return Scroll_StrumY(0); }

static void UpdateSection(); 

// 2. Vozes
static void Voices_Load(void) {
    hasVoices = FileExists(TextFormat("%s/Voices.ogg", songDir));
    if(hasVoices) {
        voices = LoadMusicStream(TextFormat("%s/Voices.ogg", songDir));
        voices.looping = 0;
        SetMusicVolume(voices, RayGame_MusicVolumeLevel());
    }
}

static void Voices_Play(void) {
    if(hasVoices) PlayMusicStream(voices);
}

static void Voices_Pause(void) {
    if(hasVoices) PauseMusicStream(voices);
}

static void Voices_Resume(void) {
    if(hasVoices) ResumeMusicStream(voices);
}

static void Voices_Update(void) {
    if(hasVoices) UpdateMusicStream(voices);
}

static void Voices_Unload(void) {
    if(hasVoices) UnloadMusicStream(voices);
    hasVoices = 0;
}

// 3. PlayState
static void PlayState_Create([[maybe_unused]] RayScene* scene) {
    currentSteps = 0;
    currentSection = 0;  
    nextSectionToSpawn = -1;
    spawnedSections = 0;

    bop = 1;
    bopT = 1;

    score = 0;
    lastScoreDrawn = -1;
    misses = 0;
    lastMissesDrawn = -1;
    lastTimeSec = -1;
    lastVoiceSync = -10.0f;
    audioWasPaused = 1;
    health = 0.5f; 

    camGame = (Camera2D) { 
        .target = VECTOR_ZERO, 
        .rotation = 0,
        .zoom = 1
    }; 

    camHUD = (Camera2D) { 
        .target = (Vector2) {1280 / 2, 720 / 2}, 
        .rotation = 0,
        .zoom = 1
    };  

    popupTimer = 0;
    isPaused = 0;
    currentOption = 0;
    optionOffset = 0;

    deadTime = 0; 
    justDied = 1;
    retryin = 0;

    if(restarted) {
        AnimatedObject_SetAnimation(&dad.object, dad.animations.idle);
        AnimatedObject_SetAnimation(&bf.object, bf.animations.idle);
        AnimatedObject_SetAnimation(&gf.object, gf.anims[gf.f]);

        bf.object.color.a = 255;
        firstNote = 0; 

        for(size_t i = 0; i < noteCount; i++) {
            Note* note = notes + i;
            note->missed = 0;
            note->pressed = 0;
        }
 
        inst = LoadMusicStream(TextFormat("%s/Inst.ogg", songDir)); 
        Voices_Load();
        
        inst.looping = 0;
        SetMusicVolume(inst, RayGame_MusicVolumeLevel());

        PlayMusicStream(inst);
        Voices_Play();

        PauseMusicStream(inst); 
        Voices_Pause(); 
        audioWasPaused = 1;

        BeatManager_New(&songBeat);
        songBeat.music = &inst;
        songBeat.bpm = song.sections[0].bpm;  
        
        introHit = 0;
        introTime = -0.5f; 

        introStep = 60.0f / songBeat.bpm;
        songBeat.time = introTime + -5 * introStep;

        camGame.target.x = dad.object.position.x + 1280 / 2;
        camGame.target.y = dad.object.position.y + 720 / 2;
 
        UpdateSection(); 

        introText.currentAnim.isValid = 0;
        restarted = 0;
        return;
    }

    // UI
    { 
        vcrFont = LoadFontEx("assets/fonts/vcr.ttf", 32, 0, 0); 
        SetTextureFilter(vcrFont.texture, TEXTURE_FILTER_BILINEAR);

        healthbarPadding = 4;
        healthbar.width = 600;
        healthbar.height = 17;
        healthbar.x = 1200 / 2 - healthbar.width / 2;
        healthbar.y = 600;

        dadColor = (Color) { 204, 85, 180, 255 };
        bfColor = (Color) { 0, 217, 204, 255 };

        snprintf(scoreBuf, sizeof(scoreBuf), "Score: 0");
        Vector2 ss = MeasureTextEx(vcrFont, scoreBuf, 16, 1);
        scorePos = (Vector2){ 1270 - ss.x, 672 };

        snprintf(missBuf, sizeof(missBuf), "Misses: 0");
        Vector2 ms = MeasureTextEx(vcrFont, missBuf, 16, 1);
        missPos = (Vector2){ 1270 - ms.x, 690 };

        snprintf(timeBuf, sizeof(timeBuf), "0:00 / 0:00");
        Vector2 ts = MeasureTextEx(vcrFont, timeBuf, 20.0f, 1);
        timePos = (Vector2){ (1200.0f / 2.0f) - (ts.x / 2.0f), 15.0f };
    }

    // Strumlines
    {
        if (game_options_scroll == 2) {
            for(int i = 0; i < 4; i++) {
                StrumNote *dadStrum = dadStrumline + i;
                StrumNote_Load(dadStrum, i);
                dadStrum->object.position.x = Scroll_StrumX(i, 0);
                dadStrum->object.position.y = Scroll_StrumY(0);
                dadStrum->object.scaleX = 0.5f;
                dadStrum->object.scaleY = 0.5f;

                StrumNote *bfStrum = bfStrumline + i;
                StrumNote_Load(bfStrum, i);
                bfStrum->object.position.x = Scroll_StrumX(i + 4, 1);
                bfStrum->object.position.y = Scroll_StrumY(1);
                bfStrum->object.scaleX = 1.0f;
                bfStrum->object.scaleY = 1.0f;
            }
        } else {
            for(int i = 0; i < 4; i++) {
                StrumNote *dadStrum = dadStrumline + i;
                StrumNote_Load(dadStrum, i);

                StrumNote *bfStrum = bfStrumline + i;
                StrumNote_Load(bfStrum, i);

                dadStrum->object.position.y = Scroll_OppStrumY();
                bfStrum->object.position.y = Scroll_PlayerStrumY();

                dadStrum->object.position.x = Scroll_OppStrumX(i);
                bfStrum->object.position.x = Scroll_PlayerStrumX(i);
            }
        }

        Render_DefaultAnimated(&trail); 
        trail.animationSet = Cache_GetNoteAnimations();
        trail.scaleX = 0.7f; 

        Render_DefaultAnimated(&end);
        end.animationSet = Cache_GetNoteAnimations();
        end.scaleX = 0.7f;
        end.scaleY = 0.7f; 

        Render_DefaultAnimated(&renderNote);
        renderNote.animationSet = Cache_GetNoteAnimations();
        renderNote.scaleX = 0.7f;
        renderNote.scaleY = 0.7f;
    }

    // Personagens e sons
    {
        if(diffSuf[0] != 0)
            Song_LoadSongFromDirDiff(&song, songDir, diffSuf);
        else
            Song_LoadSongFromDir(&song, songDir);

        if(strcmp(diffSuf, "easy") == 0)
            Note_SetWindow(0.240f);
        else if(strcmp(diffSuf, "hard") == 0)
            Note_SetWindow(0.180f);
        else
            Note_SetWindow(0.200f);

        Girlfriend_Load(&gf, NULL);
        Character_Load(&dad, overrideP2[0] != 0 ? overrideP2 : song.player2, 1);
        Character_Load(&bf, song.player1, 0);
        Character_LoadDeathAnimations(&bf, song.player1, deathAnimations);

        missSFX[0] = LoadSound("assets/sounds/missnote1.ogg");
        missSFX[1] = LoadSound("assets/sounds/missnote2.ogg");
        missSFX[2] = LoadSound("assets/sounds/missnote3.ogg");

        for(int i = 0; i < 3; i++)
            SetSoundVolume(missSFX[i], 0.4f);

        hitSFX = LoadSound("assets/sounds/hitosu!mania.ogg");
        SetSoundVolume(hitSFX, 0.5f);

        deathSounds = LoadSound("assets/sounds/fnf_loss_sfx.ogg");
        deathMusic = LoadMusicStream("assets/music/gameOver.ogg");
        deathMusic.looping = 1;
        deathMusicEnd = LoadMusicStream("assets/music/gameOverEnd.ogg");
        deathMusicEnd.looping = 0;
    }

    // Cenário
    {
        RayGraphicObject* funkyStageObjects = malloc(sizeof(RayGraphicObject) * 3);

        RayGraphicObject* bg = funkyStageObjects;
        RayGraphicObject* front = funkyStageObjects + 1;
        RayGraphicObject* curtains = funkyStageObjects + 2;

        Render_DefaultRGT(bg);
        bg->objType = RGT_IMAGE;
        bg->position = (Vector2) {-600, -200};
        bg->scrollFactor = (Vector2) {0.9f, 0.9f};
        bg->image = Render_LoadTexture("assets/images/stage/stageback.png"); 
        
        Render_DefaultRGT(front);
        front->objType = RGT_IMAGE;
        front->position = (Vector2) {-650, 600};
        front->scrollFactor = (Vector2) {0.9f, 0.9f};
        front->scaleX = front->scaleY = 1.1f;
        front->image = Render_LoadTexture("assets/images/stage/stagefront.png");

        Render_DefaultRGT(curtains);
        curtains->objType = RGT_IMAGE;
        curtains->position = (Vector2) {-350, -300};
        curtains->scrollFactor = (Vector2) {1.3f, 1.3f};
        curtains->scaleX = curtains->scaleY = 0.9f;
        curtains->image = Render_LoadTexture("assets/images/stage/stagecurtains.png");

        gf.object.scrollFactor = (Vector2) {0.9f, 0.9f};
        gf.object.position = (Vector2) {400, 100};
        dad.object.position = (Vector2) {200, 150};
        bf.object.position = (Vector2) {820, 500};

        stage.objects = funkyStageObjects;
        stage.objectCount = 3;
        stage.backgroundObjects = 2;
        stage.defaultCameraZoom = 0.9f;
    }

    // Música & Notas
    {  
        inst = LoadMusicStream(TextFormat("%s/Inst.ogg", songDir)); 
        Voices_Load();

        inst.looping = 0;
        SetMusicVolume(inst, RayGame_MusicVolumeLevel());
        
        firstNote = 0;
        noteCount = 0;
        for(size_t i = 0; i < song.sectionCount; i++)
            noteCount += song.sections[i].noteCount;

        notes = malloc(sizeof(Note) * noteCount);  
        size_t noteIndex = 0;
        
        for(size_t i = 0; i < song.sectionCount; i++) {
            for(size_t j = 0; j < song.sections[i].noteCount; j++) { 
                Note_Load(notes + noteIndex, song.sections[i].notes + j);
                noteIndex++;
            }
        }

        // ORDENAÇÃO CRÍTICA: Resolve bug de notas passando do receptor e culling prematuro!
        if (noteCount > 0) {
            qsort(notes, noteCount, sizeof(Note), Note_CompareChronological);
        }

        PlayMusicStream(inst);
        Voices_Play(); 
        PauseMusicStream(inst);
        Voices_Pause();

        BeatManager_New(&songBeat);
        songBeat.music = &inst;
        songBeat.bpm = song.sections[0].bpm; 
        
        introHit = 0;
        introTime = -0.5f; 

        introStep = 60.0f / songBeat.bpm;
        songBeat.time = introTime + -5 * introStep;

        camGame.target.x = dad.object.position.x;
        camGame.target.y = dad.object.position.y;
 
        UpdateSection(); 
    }

    // UI assets
    {
        uiAssets = AnimationSet_LoadAnimations("assets/images/ui.animset", Render_LoadTexture("assets/images/ui.png"));

        UI_ASSETS.sick  = AnimationSet_FindAnimation(&uiAssets, "sick"); 
        UI_ASSETS.good  = AnimationSet_FindAnimation(&uiAssets, "good"); 
        UI_ASSETS.bad   = AnimationSet_FindAnimation(&uiAssets, "bad"); 
        UI_ASSETS.shit  = AnimationSet_FindAnimation(&uiAssets, "shit"); 

        Render_DefaultAnimated(&introText); 
        introText.animationSet = uiAssets;
        introText.scaleX = 0.7f;
        introText.scaleY = 0.7f;

        introSounds[0] = LoadSound("assets/sounds/intro3.ogg");
        introSounds[1] = LoadSound("assets/sounds/intro2.ogg");
        introSounds[2] = LoadSound("assets/sounds/intro1.ogg");
        introSounds[3] = LoadSound("assets/sounds/introGo.ogg");

        Render_DefaultAnimated(&popupObj);
        popupObj.animationSet = uiAssets;  
        popupObj.scaleX = popupObj.scaleY = 0.6f;
    }

    camGame.zoom = stage.defaultCameraZoom; 

    {
        ClearBackground(BLACK);
        for(int i = 0; i < 10; i++) {
            UpdateMusicStream(inst);
            Voices_Update();
        }
    }
}

static void BeatHit() {
    gf.f = !gf.f;
    AnimatedObject_SetAnimation(&gf.object, gf.anims[gf.f]);

    if(game_options_bopIcons)
        bopT = 0;

    if(songBeat.beat % 4 == 0) {
        camHUD.zoom += 0.03f * game_options_zoomFactorUI;
        camGame.zoom += 0.015f * game_options_zoomFactorGAME;
    }
    
    if(songBeat.beat % 2 == 0) {
        if(bf.object.currentAnim.animationIndex == bf.animations.idle) { 
            if(!bf.idled || AnimatedObject_FinishedAnimation(&bf.object))
                AnimatedObject_SetAnimation(&bf.object, bf.animations.idle);

            bf.idled = 0;
        }
            
        if(dad.object.currentAnim.animationIndex == dad.animations.idle) {
            if(!dad.idled || AnimatedObject_FinishedAnimation(&dad.object))
                AnimatedObject_SetAnimation(&dad.object, dad.animations.idle);
            
            dad.idled = 0;
        }
    }
}

static void UpdateSection() {
    if(currentSection > (int)song.sectionCount - 1)
        return;

    Section* section = song.sections + currentSection;
    target = section->mustHit ? &bf : &dad;
    songBeat.bpm = section->bpm;
}

static void StepHit() {
    if(currentSection > (int)song.sectionCount - 1)
        return;

    float time = GetMusicTimePlayed(inst);
    if(hasVoices && time - lastVoiceSync > 0.5f &&
       fabsf(time - GetMusicTimePlayed(voices)) > 0.02f) {
        SeekMusicStream(voices, time);
        lastVoiceSync = time;
    }
    
    Section* section = song.sections + currentSection;
    currentSteps++;
    if(currentSteps >= section->len) {
        currentSteps = 0;
        currentSection++; 
        UpdateSection();
    }
}

static void HitNote(Note* note) {
    float diff = fabsf(note->time - songBeat.time);

    if(diff > 0.1503f) {
        score += 50;
        AnimatedObject_SetAnimation(&popupObj, UI_ASSETS.shit);
    }
    else if(diff > 0.126f) {
        score += 100;
        AnimatedObject_SetAnimation(&popupObj, UI_ASSETS.bad);
    }
    else if(diff > 0.033f) {
        score += 200;
        AnimatedObject_SetAnimation(&popupObj, UI_ASSETS.good);
    }
    else {
        score += 350;
        AnimatedObject_SetAnimation(&popupObj, UI_ASSETS.sick);
    }

    Vector2 size = AnimatedObject_Sizes(&popupObj); 
    popupObj.position.x = healthbar.x + healthbar.width + 150 - size.x / 2;
    popupObj.position.y = 600 + GetRandomValue(-1000, 1000) / 200.0f;
    popupVelocity = -150;
    popupTimer = 1.0f;
    health = fminf(1, health + 0.0115f);

    if(game_options_hitSound)
        PlaySound(hitSFX);

    note->pressed = 1;
    bf.idleTimer = 0; 
}

static void GhostMiss(int lane);

static void NoteHits() { 
    // SE BOTPLAY ESTIVER ATIVO: O acerto agora ocorre diretamente no receptor (em PlayState_Draw)
    // exatamente como o oponente, deixando as notas 100% visíveis e imunes a morte!
    if(game_options_botplay) {
        health = 0.5f; // Mantém a vida travada e imortal no botplay
        return;
    }

    char pressed[4] = {0, 0, 0, 0}; 
    int pressedNotes = 0;

    int keyPressed[4];
    char keyDown[4];
    char anyPressed = 0;
    for(int i = 0; i < 4; i++) {
        int key = game_scheme_keys[game_options_scheme][i];
        keyPressed[i] = IsKeyPressed(key);
        keyDown[i] = IsKeyDown(key);
        if(keyPressed[i])
            anyPressed = 1;
    }

    if(game_options_ghost == 1 && anyPressed) {
        char hittable[4] = {0, 0, 0, 0};
        char anyHittable = 0;
        for(size_t i = firstNote; i < noteCount; i++) {
            Note* n = notes + i;
            if(!n->mustHit || n->missed || n->pressed)
                continue;
            if(Note_TooEarly(n, songBeat.time))
                break;
            if(n->id < 0 || n->id > 3)
                continue;
            if(Note_CanBeHit(n, songBeat.time)) {
                hittable[n->id] = 1;
                anyHittable = 1;
            }
        }
        if(anyHittable) {
            anyPressed = 0;
            for(int i = 0; i < 4; i++) {
                if(keyPressed[i] && !hittable[i]) {
                    keyPressed[i] = 0;
                    keyDown[i] = 0;
                }
                if(keyPressed[i])
                    anyPressed = 1;
            }
        }
    }

    if(!anyPressed)
        goto BRUH; 

    for(size_t i = firstNote; i < noteCount; i++) {
        Note* note = notes + i;

        if(!note->mustHit || note->missed) 
            continue;
            
        if(Note_TooEarly(note, songBeat.time))
            goto BRUH;

        if(note->id < 0 || note->id > 3)
            continue;
        
        if(keyPressed[note->id] && Note_CanBeHit(note, songBeat.time) && !note->pressed && !pressed[note->id]) {
            pressed[note->id] = 1;
            HitNote(note); 
            AnimatedObject_SetAnimation(&bf.object, bf.animations.notes[note->id]);

            pressedNotes++;
            if(pressedNotes == 4)
                goto BRUH;
        } 
    }

BRUH:
    if(game_options_ghost == 2) {
        for(int i = 0; i < 4; i++) {
            if(keyPressed[i] && !pressed[i])
                GhostMiss(i);
        }
    }

    for(int i = 0; i < 4; i++) {
        StrumNote* strumNote = bfStrumline + i;

        if(pressed[i])
            AnimatedObject_SetAnimation(&strumNote->object, StrumNote_ConfirmAnimation(i));
        else if(!keyDown[i]) {
            int j = StrumNote_IdleAnimation(i);
            if(strumNote->object.currentAnim.animationIndex != j) {
                AnimatedObject_SetAnimation(&strumNote->object, j);
            }
        } 
        else if(keyPressed[i]) 
            AnimatedObject_SetAnimation(&strumNote->object, StrumNote_PressAnimation(i));  
    }  
} 

static void PlayState_DeadUpdate() {
    PauseMusicStream(inst);
    Voices_Pause();

    if(justDied) {
        AnimatedObject_SetAnimation(&bf.object, deathAnimations[0]);
        deadTime = -AnimationSet_AnimationLength(&bf.object.animationSet, deathAnimations[0]);
        justDied = 0;
        retryin = 0;
        PlaySound(deathSounds);
    }
 
    target = &bf; 
    
    if(retryin) {
        UpdateMusicStream(deathMusicEnd);
        bf.object.color.a = smooth(255.0f, 0, deadTime / 2);

        if(deadTime >= 2.0f) {
            StopMusicStream(deathMusicEnd);
            restarted = 1;
            PlayState_SetScene();
            return;
        }
    }
    else if(deadTime > 0) {
        if(bf.object.currentAnim.animationIndex != deathAnimations[1]) {
            PlayMusicStream(deathMusic);
            AnimatedObject_SetAnimation(&bf.object, deathAnimations[1]);
        }
        else {
            UpdateMusicStream(deathMusic);

            if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
                retryin = 1;
                deadTime = 0;
                AnimatedObject_SetAnimation(&bf.object, deathAnimations[2]);
                StopMusicStream(deathMusic);
                PlayMusicStream(deathMusicEnd);
            }
            else if(IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE)) {
                StopMusicStream(deathMusic);
                if(returnToWeek) {
                    WeekRun_Stop();
                    StoryState_SetScene();
                } else {
                    Freeplay_SetScene();
                }
                return;
            }
        }
    }

    deadTime += RayGame_DeltaTime();
}

static void PauseMenu_Update([[maybe_unused]] RayScene* scene) {
    const char* options[3] = {"Resume", "Restart", "Exit to Menu"};
    const Color bgColor = {0, 0, 0, 100};

    DrawRectangle(0, 0, 1280, 720, bgColor);

    if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
        currentOption++;
        if(currentOption >= 3)
            currentOption = 0;
    }
    else if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
        currentOption--;
        if(currentOption < 0)
            currentOption = 2;
    }
    else if(pauseTimer > 0 && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) {
        if(currentOption == 0)
            isPaused = 0;
        else if(currentOption == 1) {
            restarted = 1;
            PlayState_SetScene(); 
            return;
        }
        else {
            RayGame_ResetMusic();
            RayGame_ToggleMusic(1);

            if(returnToWeek) {
                WeekRun_Stop();
                StoryState_SetScene();
            } else if(returnToEditor) {
                ChartEditor_SetSongDir(songDir);
                ChartEditor_SetScene();
            } else {
                Freeplay_SetScene();
            }
            return;
        }
    }

    optionOffset = Lerp(optionOffset, currentOption, RayGame_DeltaTime() * 10.0f);

    Color color = WHITE;
    for(int i = 0; i < 3; i++) {
        const int fontSize = 85; 
        Vector2 pos = {150 + fabsf(i - optionOffset) * -30, 720 / 2 + (i - optionOffset) * (15 + fontSize)}; 
        color.a = currentOption == i ? 255 : 150;   
        DrawTextEx(mainFont, options[i], pos, fontSize, 5, color);  
    }
}

static void PlayState_Update([[maybe_unused]] RayScene* scene) {
    if(!IsWindowFocused()) {
        PauseMusicStream(inst);
        Voices_Pause();
        audioWasPaused = 1;
        return;
    }

    if(isPaused) {
        pauseTimer += RayGame_DeltaTime();
        return;
    }
    
    // No botplay, vida nunca é menor que 0
    if(!game_options_botplay && health < 0) {
        PlayState_DeadUpdate(); 
    }
    else if(songBeat.time < 0) {
        introTime += RayGame_DeltaTime(); 
        songBeat.time += RayGame_DeltaTime();

        const char* textAnimations[3] = {"ready", "set", "go"};
        while(introTime >= introStep) {
            if(introHit < 4) {
                if(introHit > 0) {
                    AnimatedObject_SetAnimation(&introText, AnimationSet_FindAnimation(&uiAssets, textAnimations[introHit - 1]));
                    Vector2 sizes = AnimatedObject_Sizes(&introText);
                    introText.position.x = 1280 / 2 - sizes.x / 2;
                    introText.position.y = 720  / 2 - sizes.y / 2;
                }
                
                SetSoundVolume(introSounds[introHit], 0.7f);
                PlaySound(introSounds[introHit]);
            }
 
            gf.f = !gf.f;
            AnimatedObject_SetAnimation(&gf.object, gf.anims[gf.f]);

            introHit++;
            introTime -= introStep;
        }

        introText.color.a = smooth(255.0f, 0, introTime / introStep);
    }
    else { 
        UpdateMusicStream(inst);
        Voices_Update();

        if(audioWasPaused) {
            ResumeMusicStream(inst);
            Voices_Resume();
            audioWasPaused = 0;
        }
        
        BeatManager_Update(&songBeat, StepHit, BeatHit); 

        if(!IsMusicStreamPlaying(inst)) {
            RayGame_ResetMusic();
            RayGame_ToggleMusic(1);

            if(returnToWeek) {
                WeekRun_AddScore(score);
                if(WeekRun_Advance()) {
                    PlayState_SetSongDir(WeekRun_Dir());
                    PlayState_SetReturnWeek(1);
                    PlayState_SetCharsOverride(WeekRun_Char());
                    PlayState_SetDiff(WeekRun_DiffSuffix());
                    PlayState_SetScene();
                } else {
                    WeekRun_Finish();
                    StoryState_SetScene();
                }
            } else if(returnToEditor) {
                ChartEditor_SetSongDir(songDir);
                ChartEditor_SetScene();
            } else {
                Freeplay_SetScene();
            }
            return;
        }

        NoteHits(); 
    }

    if(health >= 0 && IsKeyPressed(KEY_ENTER)) {
        pauseTimer = -0.1f;
        isPaused = 1;
        PauseMusicStream(inst);
        Voices_Pause();
        audioWasPaused = 1;
    }

    cameraTarget = target->object.position;
    cameraTarget.x += target->cameraOffset.x;
    cameraTarget.y += target->cameraOffset.y;
    
    if(bopT < 1) {
        bopT += RayGame_DeltaTime() * 3.5f;
        if(bopT > 1) bopT = 1;
    }
    {
        float k = 1 - bopT;
        bop = 1 + 0.22f * k * k;
    }

    camGame.target.x = Lerp(camGame.target.x, cameraTarget.x, RayGame_DeltaTime() * 5);
    camGame.target.y = Lerp(camGame.target.y, cameraTarget.y, RayGame_DeltaTime() * 5);
    camGame.zoom = Lerp(camGame.zoom, stage.defaultCameraZoom, RayGame_DeltaTime() * 8);
    camHUD.zoom = Lerp(camHUD.zoom, 1, RayGame_DeltaTime() * 8);  
}

static void MissNote(Note* note) {
    if(game_options_botplay) return; // BOTPLAY NUNCA DISPARA MISS

    note->missed = 1;
    bf.idleTimer = 0;
    health -= 0.02f;
    misses++;

    PlaySound(missSFX[GetRandomValue(0, 2)]);
    AnimatedObject_SetAnimation(&bf.object, bf.missAnimations.notes[note->id]);
}

static void GhostMiss(int lane) {
    if(game_options_botplay) return; // BOTPLAY NUNCA DISPARA MISS
    if(lane < 0 || lane > 3) return;

    bf.idleTimer = 0;
    health -= 0.02f;
    misses++;

    PlaySound(missSFX[GetRandomValue(0, 2)]);
    AnimatedObject_SetAnimation(&bf.object, bf.missAnimations.notes[lane]);
}

static void UpdateCharacterIdle(Character* character) {
    float uhm = 60.0f / songBeat.bpm;
    character->idleTimer += RayGame_DeltaTime();
    if(character->object.currentAnim.animationIndex != character->animations.idle && character->idleTimer > uhm) {
        character->idled = 1;
        AnimatedObject_SetAnimation(&character->object, character->animations.idle);
    }
}

static void PlayState_Draw([[maybe_unused]] RayScene* scene) {  
    if(!game_options_botplay && health < 0) {
        ClearBackground(BLACK);

        Render_SetCamera(&camGame); 
            AnimatedObject_UpdateFrame(&bf.object);
            Render_DrawAnimatedObject(&bf.object);
        Render_StopCamera();

        return;
    }

    ClearBackground(DARKGRAY);
 
    Render_SetCamera(&camGame); 
    {
        for(int i = 0; i < stage.backgroundObjects; i++) {  
            Render_DrawGraphicObject(stage.objects + i);
        } 

        if(!isPaused) {
            UpdateCharacterIdle(&dad);
            UpdateCharacterIdle(&bf); 

            AnimatedObject_UpdateFrame(&gf.object);
            AnimatedObject_UpdateFrame(&dad.object);
            AnimatedObject_UpdateFrame(&bf.object);
        }

        Render_DrawAnimatedObject(&gf.object);
        Render_DrawAnimatedObject(&dad.object); 
        Render_DrawAnimatedObject(&bf.object); 

        for(int i = stage.backgroundObjects; i < stage.objectCount; i++) {
            Render_DrawGraphicObject(stage.objects + i);
        } 
    }
    Render_StopCamera(); 
 
    Render_SetCamera(&camHUD);
    {
        // Render receptors
        for(int i = 0; i < 4; i++) {
            StrumNote *dadStrum = dadStrumline + i;     
            StrumNote *bfStrum = bfStrumline + i;

            AnimatedObject_UpdateFrame(&dadStrum->object);
            Render_DrawAnimatedObject(&dadStrum->object);

            AnimatedObject_UpdateFrame(&bfStrum->object);
            Render_DrawAnimatedObject(&bfStrum->object);
        }

        // Reset receptors do BF no botplay se a animação de confirm terminou
        if (game_options_botplay) {
            for(int i = 0; i < 4; i++) {
                StrumNote *bfStrum = bfStrumline + i;
                if (AnimatedObject_FinishedAnimation(&bfStrum->object)) {
                    AnimatedObject_SetAnimation(&bfStrum->object, StrumNote_IdleAnimation(i));
                }
            }
        }

        // Render notas
        { 
            rlSetBlendFactorsSeparate(0x0302, 0x0303, 1, 0x0303, 0x8006, 0x8006);
            BeginBlendMode(BLEND_CUSTOM_SEPARATE);

            const float songSpeed = song.speed;
            const float scrollVel = Scroll_NoteVelocity();
            const float speedMult = scrollVel * songSpeed;
            const float scrollSpeed = 450.0f * songSpeed;
            const float cutoffTime = songBeat.time + (900.0f / scrollSpeed);
            const float cullTimeThreshold = 400.0f / scrollSpeed;
            const float endHeight = fmaxf(64.0f, 18.0f * songSpeed);

            char holdDown[4];
            for(int k = 0; k < 4; k++)
                holdDown[k] = game_options_botplay || IsKeyDown(game_scheme_keys[game_options_scheme][k]);

            while(firstNote < noteCount) {
                Note* n = notes + firstNote;
                char resolved = (n->id < 0 || n->id > 3) || n->pressed || n->missed;
                float timePast = songBeat.time - (n->time + n->length);
                if(resolved && timePast > cullTimeThreshold) {
                    firstNote++;
                } else {
                    break;
                }
            }

            for(size_t i = firstNote; i < noteCount; i++) {
                Note* note = notes + i;

                if(note->id < 0 || note->id > 3)
                    continue;

                if(note->time > cutoffTime)
                    break;

                StrumNote* targetStrumNote = (note->mustHit ? bfStrumline : dadStrumline) + note->id;
                float timeDiff = note->time - songBeat.time;

                renderNote.position.x = targetStrumNote->object.position.x;
                renderNote.position.y = targetStrumNote->object.position.y - (speedMult * timeDiff);

                char fullyAbove = (game_options_scroll == 1) ? (renderNote.position.y > 900.0f)
                                                             : (renderNote.position.y < -200.0f);

                if(note->pressed && fullyAbove && songBeat.time >= note->time + note->length)
                    continue;

                int headAnim = Note_Animation(note->id);
                if(!renderNote.currentAnim.isValid || renderNote.currentAnim.animationIndex != headAnim)
                    AnimatedObject_SetAnimation(&renderNote, headAnim);

                // --- ACERTO DO OPONENTE ---
                if(!note->mustHit && !note->pressed && songBeat.time >= note->time) {
                    note->pressed = 1;  
                    dad.idleTimer = 0;
                    AnimatedObject_SetAnimation(&dad.object, dad.animations.notes[note->id]);
                    AnimatedObject_SetAnimation(&dadStrumline[note->id].object, StrumNote_ConfirmAnimation(note->id));
                }

                // --- ACERTO DO BOTPLAY (TOCA EXATAMENTE COMO O OPONENTE NO RECEPTOR) ---
                if(note->mustHit && game_options_botplay && !note->pressed && songBeat.time >= note->time) {
                    note->pressed = 1;
                    score += 350; // Pontuação sick sem travar a CPU
                    bf.idleTimer = 0;
                    AnimatedObject_SetAnimation(&bf.object, bf.animations.notes[note->id]);
                    AnimatedObject_SetAnimation(&bfStrumline[note->id].object, StrumNote_ConfirmAnimation(note->id));
                }

                if(note->length > 0) { 
                    int trailAnim = Note_TrailAnimation(note->id);
                    if(!trail.currentAnim.isValid || trail.currentAnim.animationIndex != trailAnim)
                        AnimatedObject_SetAnimation(&trail, trailAnim);
                    trail.position.x = targetStrumNote->object.position.x + trailOffset.x;
                    trail.position.y = renderNote.position.y + trailOffset.y;
                    trail.scaleY = fmaxf(0.01f, (scrollSpeed * note->length - endHeight) / 44.0f);
                    trail.color = normalColor;

                    int endAnim = Note_EndAnimation(note->id);
                    if(!end.currentAnim.isValid || end.currentAnim.animationIndex != endAnim)
                        AnimatedObject_SetAnimation(&end, endAnim);
                    end.position.x = trail.position.x;
                    end.position.y = trail.position.y + trail.scaleY * 44.0f;   
                    end.scaleY = endHeight / 64.0f;
                    end.color = normalColor;

                    if(!note->missed) {
                        char shouldHold = Note_ShouldHold(note, songBeat.time);

                        if(!note->mustHit) {
                            if(shouldHold) {
                                dad.idleTimer = 0;
                            }
                        }
                        else if(game_options_botplay) {
                            if(shouldHold) {
                                bf.idleTimer = 0;
                            }
                        }
                        else if(!note->pressed)
                            goto SKIP_CLIP;
                        else if(!isPaused && shouldHold) {
                            float tailLeft = (note->time + note->length) - songBeat.time;
                            if(!holdDown[note->id] && tailLeft > 0.12f) {
                                MissNote(note);
                                goto SKIP_CLIP;
                            }

                            health = fminf(1, health + 0.04f * RayGame_DeltaTime());
                            bf.idleTimer = 0;  
                        } 

                        // Clip geométrico
                        if (game_options_scroll == 2) {
                            float receptorX = targetStrumNote->object.position.x + trailOffset.x;
                            float trailRight = trail.position.x + trail.scaleX * 44.0f;
                            float velX = Scroll_NoteVelocityX(note->id, note->mustHit);
                            
                            if (velX > 0) {
                                if (trailRight > receptorX) {
                                    trail.scaleX = fmaxf(0.0f, (trailRight - receptorX) / 44.0f);
                                    trail.position.x = receptorX - trail.scaleX * 44.0f;
                                }
                                if (trail.scaleX > 0.0f)
                                    Render_DrawAnimatedObject(&trail);
                                if (trailRight >= receptorX)
                                    Render_DrawAnimatedObject(&end);
                            } else {
                                if (trail.position.x < receptorX) {
                                    trail.scaleX = fmaxf(0.0f, (receptorX - trail.position.x) / 44.0f);
                                }
                                float origScaleX = trail.scaleX;
                                float origScaleY = trail.scaleY;
                                trail.scaleX *= 0.4f;
                                trail.scaleY *= 0.4f;
                                if(trail.scaleY > 0.0f)
                                    Render_DrawAnimatedObject(&trail);
                                trail.scaleX = origScaleX;
                                trail.scaleY = origScaleY;
                                if (trail.position.x <= receptorX) {
                                    float origEndScaleX = end.scaleX;
                                    float origEndScaleY = end.scaleY;
                                    end.scaleX = origEndScaleX * 0.4f;
                                    end.scaleY = origEndScaleY * 0.4f;
                                    Render_DrawAnimatedObject(&end);
                                    end.scaleX = origEndScaleX;
                                    end.scaleY = origEndScaleY;
                                }
                            }
                        } else if (game_options_scroll == 1) {
                            float receptorY = targetStrumNote->object.position.y + trailOffset.y;
                            float trailBottom = trail.position.y + trail.scaleY * 44.0f;
                            if(trailBottom > receptorY) {
                                trail.scaleY = fmaxf(0.0f, (receptorY - trail.position.y) / 44.0f);
                            }
                            if(trail.scaleY > 0.0f)
                                Render_DrawAnimatedObject(&trail);
                            if(trailBottom >= receptorY)
                                Render_DrawAnimatedObject(&end);
                        } else {
                            float receptorY = targetStrumNote->object.position.y + trailOffset.y;
                            float trailBottom = trail.position.y + trail.scaleY * 44.0f;
                            if(trail.position.y < receptorY) {
                                trail.scaleY = fmaxf(0.0f, (trailBottom - receptorY) / 44.0f);
                                trail.position.y = receptorY;
                            }
                            if(trail.scaleY > 0.0f)
                                Render_DrawAnimatedObject(&trail);
                            if(trailBottom >= receptorY)
                                Render_DrawAnimatedObject(&end);
                        }
                        goto CLIPPED;
                    }
                    else {
                        trail.color = missedColor;
                        end.color = missedColor; 
                    }   

SKIP_CLIP:
                    if(!fullyAbove) {
                        Render_DrawAnimatedObject(&trail);
                        Render_DrawAnimatedObject(&end);
                    }
CLIPPED:
                }

                if(!note->pressed) { 
                    if(!note->missed && Note_TooLate(note, songBeat.time)) {
                        MissNote(note);
                    }

                    if (game_options_scroll == 2 && !note->mustHit) {
                        float origScaleX = renderNote.scaleX;
                        float origScaleY = renderNote.scaleY;
                        renderNote.scaleX = 0.4f;
                        renderNote.scaleY = 0.4f;
                        Render_DrawAnimatedObject(&renderNote);
                        renderNote.scaleX = origScaleX;
                        renderNote.scaleY = origScaleY;
                    } else if (game_options_scroll == 1) {
                        float origScaleX = renderNote.scaleX;
                        float origScaleY = renderNote.scaleY;
                        renderNote.scaleX = 0.85f;
                        renderNote.scaleY = 0.85f;
                        Render_DrawAnimatedObject(&renderNote);
                        renderNote.scaleX = origScaleX;
                        renderNote.scaleY = origScaleY;
                    } else {
                        Render_DrawAnimatedObject(&renderNote);
                    } 
                }  
            }  

            EndBlendMode(); 
        }

        // Barra de Vida e HUD
        { 
            const int C = 35;
            DrawRectangleRec(healthbar, BLACK); 

            float offsetX = healthbar.x + healthbarPadding;
            float availableWidth = healthbar.width - healthbarPadding * 2; 
            float availableHeight = healthbar.height - healthbarPadding * 2;
            float middle = availableWidth * (1 - health);

            if(middle > 0) {
                DrawRectangle((int)offsetX, (int)(healthbar.y + healthbarPadding), (int)middle, (int)availableHeight, dadColor);
            }
            if(middle < availableWidth) {
                DrawRectangle((int)(offsetX + middle), (int)(healthbar.y + healthbarPadding), (int)(availableWidth - middle), (int)availableHeight, bfColor);
            }

            // Tempo
            {
                int currentSec = (int)(songBeat.time < 0 ? 0 : songBeat.time);
                if(currentSec != lastTimeSec) {
                    lastTimeSec = currentSec;
                    float total = GetMusicTimeLength(inst);
                    if(total < 0) total = 0;
                    snprintf(timeBuf, sizeof(timeBuf), "%d:%02d / %d:%02d",
                        currentSec / 60, currentSec % 60, (int)total / 60, (int)total % 60);

                    Vector2 textSize = MeasureTextEx(vcrFont, timeBuf, 20.0f, 1);
                    timePos = (Vector2){ (1200.0f / 2.0f) - (textSize.x / 2.0f), 15.0f };
                }
                
                const float tsh = 1.5f;
                DrawTextEx(vcrFont, timeBuf, (Vector2){timePos.x + tsh, timePos.y}, 20.0f, 1, BLACK);
                DrawTextEx(vcrFont, timeBuf, timePos, 20.0f, 1, WHITE);
            }

            // Score e Misses
            if(game_options_showScore) {
                if(score != lastScoreDrawn) {
                    snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d", score);
                    lastScoreDrawn = score;
                    Vector2 ss = MeasureTextEx(vcrFont, scoreBuf, 16, 1);
                    scorePos = (Vector2){ 1270 - ss.x, 672 };
                }
                const float shadow = 1.5f;
                DrawTextEx(vcrFont, scoreBuf, (Vector2) {scorePos.x + shadow, scorePos.y - shadow}, 16, 1, BLACK);
                DrawTextEx(vcrFont, scoreBuf, scorePos, 16, 1, WHITE);

                if(misses != lastMissesDrawn) {
                    snprintf(missBuf, sizeof(missBuf), "Misses: %d", misses);
                    lastMissesDrawn = misses;
                    Vector2 ms = MeasureTextEx(vcrFont, missBuf, 16, 1);
                    missPos = (Vector2){ 1270 - ms.x, 690 };
                }
                DrawTextEx(vcrFont, missBuf, (Vector2) {missPos.x + shadow, missPos.y - shadow}, 16, 1, BLACK);
                DrawTextEx(vcrFont, missBuf, missPos, 16, 1, WHITE);
            }

            Rectangle frameRect = {0, 0, 150, 150}; 
            float iconHalf = 75 * bop;
            float iconCY = healthbar.y + healthbar.height / 2;
            float kk = 1 - bopT;
            kk *= kk;
            float bobY = -7 * kk;
            float sway = 4 * kk;

            frameRect.x = 0;
            DrawTexturePro(dad.icon, frameRect, (Rectangle) {
                .x = offsetX + middle + C - 75 - iconHalf - sway,
                .y = iconCY - iconHalf + bobY,
                .width = 130 * bop,
                .height = 130 * bop
            }, VECTOR_ZERO, 0, WHITE); 
 
            DrawTexturePro(bf.icon, frameRect, (Rectangle) {
                .x = offsetX + middle - C + 75 - iconHalf + sway,
                .y = iconCY - iconHalf - bobY,
                .width = 145 * bop,
                .height = 145 * bop
            }, VECTOR_ZERO, 0, WHITE);
        }

        if(songBeat.time < 0) 
            Render_DrawAnimatedObject(&introText);
        
        // No botplay não cria popup para não matar o FPS em spam notes
        if(!game_options_botplay && popupTimer > 0) {
            popupVelocity += 500 * RayGame_DeltaTime();
            popupObj.position.y += popupVelocity * RayGame_DeltaTime();
            popupObj.color.a = smooth(255.0f, 0, 1 - popupTimer); 

            Render_DrawAnimatedObject(&popupObj);
            popupTimer -= RayGame_DeltaTime();
        }
    }
    
    if(isPaused)
        PauseMenu_Update(scene);
    Render_StopCamera();  
}

void PlayState_SetSong(char* song) {
    snprintf(songDir, sizeof(songDir), "assets/songs/%s", song);
}

void PlayState_SetSongDir(const char* dir) {
    strncpy(songDir, dir, sizeof(songDir) - 1);
    songDir[sizeof(songDir) - 1] = 0;
    returnToEditor = 0;
    returnToWeek = 0;
    diffSuf[0] = 0;
    overrideP2[0] = 0;
}

void PlayState_SetSongDirKeep(const char* dir) {
    strncpy(songDir, dir, sizeof(songDir) - 1);
    songDir[sizeof(songDir) - 1] = 0;
}

void PlayState_SetReturnEditor(char on) {
    returnToEditor = on;
}

void PlayState_SetReturnWeek(char on) {
    returnToWeek = on;
}

void PlayState_SetDiff(const char* diff) {
    if(diff != NULL)
        strncpy(diffSuf, diff, sizeof(diffSuf) - 1);
    else
        diffSuf[0] = 0;
    diffSuf[sizeof(diffSuf) - 1] = 0;
}

void PlayState_SetCharsOverride(const char* p2) {
    if(p2 != NULL)
        strncpy(overrideP2, p2, sizeof(overrideP2) - 1);
    else
        overrideP2[0] = 0;
    overrideP2[sizeof(overrideP2) - 1] = 0;
}

int PlayState_GetScore(void) {
    return score;
}

static void PlayState_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadMusicStream(inst);
    Voices_Unload();
    if(restarted) return;

    for(int i = 0; i < 3; i++)
        UnloadSound(missSFX[i]); 

    UnloadSound(hitSFX);

    UnloadSound(deathSounds);
    UnloadMusicStream(deathMusic);
    UnloadMusicStream(deathMusicEnd);

    for(int i = 0; i < 4; i++)
        UnloadSound(introSounds[i]);

    free(notes);
    Song_Free(&song);

    UnloadTexture(dad.icon);
    UnloadTexture(bf.icon); 
    
    AnimationSet_FreeAll(&uiAssets);
    AnimationSet_FreeAll(&dad.object.animationSet);
    AnimationSet_FreeAll(&bf.object.animationSet);
    AnimationSet_FreeAll(&gf.object.animationSet);

    for(int i = 0; i < stage.objectCount; i++) {
        RayGraphicObject* obj = stage.objects + i;
        UnloadTexture(obj->image);
    } 

    UnloadFont(vcrFont);
    free(stage.objects);
}

static RayScene scene;
Scene_MakeSceneCode(
    scene, 
    PlayState_SetScene,
    PlayState_Create,
    PlayState_Update,
    PlayState_Draw,
    PlayState_Destroy
)