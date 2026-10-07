#include"scenes/AllScenes.h"
#include"scenes/Character.h"
#include"scenes/Note.h"
#include"scenes/Song.h"

static float smooth(float a, float b, float c) {
    return Lerp(a, b, c * c * c * c);
}

// temporary placement probably
typedef struct { 
    RayGraphicObject* objects;
    float defaultCameraZoom;

    int backgroundObjects;
    int objectCount;
} Stage;  

// intro / ui
static int introHit;
static float introTime;
static float introStep;  

struct { 
    int sick;
    int good;
    int bad;
    int shit;
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
static char* __song__;
static Music inst;
static Music voices;
static Song song; 

// 
static char restarted;

// death
static int deathAnimations[3];
static float deadTime;
static char justDied;

// beat
static int currentSteps;
static int currentSection;
static BeatManager songBeat; 

// optimizing this bih
static RayAnimatedObject trail;
static RayAnimatedObject end;
static RayAnimatedObject renderNote;

// note stuff
Note* notes;
size_t noteCount;
size_t firstNote;

// notes for the sustain notes!
static const Color normalColor = {255, 255, 255, 200};
static const Color missedColor = {150, 150, 150, 200};
// trail offset (static for now)
static const Vector2 trailOffset = {40, 60}; 

static int nextSectionToSpawn;
static int spawnedSections;

// strumlines
static StrumNote dadStrumline[4];
static StrumNote bfStrumline[4];

// camera stuff
static Vector2 cameraTarget; 
static Camera2D camGame; 
static Camera2D camHUD; 

// other
static RayAnimationHandler numFrames;
static RayAnimatedObject nums[5]; // can count up to 99,999 
static float numTime;

// miss sounds
static Sound missSFX[3]; 

// ui stuff 
static Font vcrFont;
static float bop; // used to resize the icons
static Color dadColor;
static Color bfColor;

static Rectangle healthbar;
static int healthbarPadding;

// stats  
static float health; // 0 - 1, why would fnf use 0 - 2 ???
static int score;

static float popupTimer;
static float popupVelocity;
static RayAnimatedObject popupObj; 

// pause menu
static char isPaused;
static int currentOption;
static float optionOffset;
static float pauseTimer;

static void UpdateSection(); 
static void PlayState_Create([[maybe_unused]] RayScene* scene) {
    
    // default
    {  
        currentSteps = 0;
        currentSection = 0;  

        numTime = 0;
        nextSectionToSpawn = -1;
        spawnedSections = 0;

        bop = 1;

        score = 0;
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
    }

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
            // resetting notes
        }
 
        inst = LoadMusicStream(TextFormat("assets/songs/%s/Inst.ogg", __song__)); 
        voices = LoadMusicStream(TextFormat("assets/songs/%s/Voices.ogg", __song__)); 
        
        inst.looping = 0;
        voices.looping = 0;

        PlayMusicStream(inst);
        PlayMusicStream(voices);

        PauseMusicStream(inst); 
        PauseMusicStream(voices); 

        BeatManager_New(&songBeat);
        songBeat.music = &inst;
        songBeat.bpm = song.sections[0].bpm;  
        
        introHit = 0;
        introTime = -.5f; 

        introStep = 60.0f / songBeat.bpm;
        songBeat.time = introTime + -5 * introStep;

        camGame.target.x = dad.object.position.x + 1280 / 2;
        camGame.target.y = dad.object.position.y + 720 / 2;
 
        UpdateSection(); 

        introText.currentAnim.isValid = 0;
        restarted = 0;

        return;
    }

    // load random stuff
    { 
        vcrFont = LoadFontEx("assets/fonts/vcr.ttf", 32, 0, 0); 
        SetTextureFilter(vcrFont.texture, TEXTURE_FILTER_BILINEAR);

        numFrames = AnimationSet_LoadAnimations("assets/images/other/nums.animset", Render_LoadTexture("assets/images/other/nums.png"));
        for(int i = 0; i < 5; i++) {
            RayAnimatedObject* num = nums + i;
            Render_DefaultAnimated(num);
        }

        healthbarPadding = 4;
        
        healthbar.width = 601;
        healthbar.height = 19;
        healthbar.x = 1280 / 2 - healthbar.width / 2; 
        healthbar.y = 640;

        dadColor = RED;
        bfColor = (Color) {42, 209, 86, 255};
    }

    // load strumlines
    {
        const float strumlineY = 50;
        const float strumShift = 110;

        for(int i = 0; i < 4; i++) {
            StrumNote *dadStrum = dadStrumline + i;
            StrumNote_Load(dadStrum, i);

            StrumNote *bfStrum = bfStrumline + i;
            StrumNote_Load(bfStrum, i);

            dadStrum->object.position.y = strumlineY;
            bfStrum->object.position.y = strumlineY;

            dadStrum->object.position.x = 50 + strumShift * i;
            bfStrum->object.position.x = 700 + strumShift * i;
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

    // loading characters
    {
        Song_Parse(&song, TextFormat("assets/songs/%s/data.song", __song__));

        Girlfriend_Load(&gf, NULL /* default gf */);
        Character_Load(&dad, song.player2, 1);
        Character_Load(&bf, song.player1, 0);
        Character_LoadDeathAnimations(&bf, song.player1, deathAnimations);

        missSFX[0] = LoadSound("assets/sounds/missnote1.ogg");
        missSFX[1] = LoadSound("assets/sounds/missnote2.ogg");
        missSFX[2] = LoadSound("assets/sounds/missnote3.ogg");

        for(int i = 0; i < 3; i++)
            SetSoundVolume(missSFX[i], 0.4f);
    }

    // load basic stage
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

    // load song
    {  
        inst = LoadMusicStream(TextFormat("assets/songs/%s/Inst.ogg", __song__)); 
        voices = LoadMusicStream(TextFormat("assets/songs/%s/Voices.ogg", __song__)); 

        inst.looping = 0;
        voices.looping = 0;
        
        // loading notes
        {
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
        }

        PlayMusicStream(inst);
        PlayMusicStream(voices); 
        PauseMusicStream(inst);
        PauseMusicStream(voices);

        //SetMusicVolume(inst, 0);
        //SetMusicVolume(voices, 0); 

        BeatManager_New(&songBeat);
        songBeat.music = &inst;
        songBeat.bpm = song.sections[0].bpm; 
        
        introHit = 0;
        introTime = -.5f; 

        introStep = 60.0f / songBeat.bpm;
        songBeat.time = introTime + -5 * introStep;

        camGame.target.x = dad.object.position.x;
        camGame.target.y = dad.object.position.y;
 
        UpdateSection(); 
    }

    // load ui
    {
        // someone could swap these
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
}

static void BeatHit() {
    gf.f = !gf.f;
    AnimatedObject_SetAnimation(&gf.object, gf.anims[gf.f]);

    if(game_options_bopIcons)
        bop = 1.2f;

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
    if(currentSection > song.sectionCount - 1)
        return;

    Section* section = song.sections + currentSection;
    target = section->mustHit ? &bf : &dad;

    songBeat.bpm = section->bpm;
}

static void StepHit() {
    if(currentSection > song.sectionCount - 1)
        return;

    float time = GetMusicTimePlayed(inst);
    if(fabsf(time - GetMusicTimePlayed(voices)) > 0.02f) {
        SeekMusicStream(voices, time);
    }
    
    Section* section = song.sections + currentSection;

    currentSteps++;
    if(currentSteps >= section->len) {
        // updating section
        currentSteps = 0;
        currentSection++; 

        UpdateSection();
    }
}

static void HitNote(Note* note) {
    float diff = fabsf(note->time - songBeat.time); // score

    if(diff > 0.1503f) { // shit
        score += 50;
        AnimatedObject_SetAnimation(&popupObj, UI_ASSETS.shit);
    }
    else if(diff > 0.126f) { // bad
        score += 100;
        AnimatedObject_SetAnimation(&popupObj, UI_ASSETS.bad);
    }
    else if(diff > 0.033f) { // good
        score += 200;
        AnimatedObject_SetAnimation(&popupObj, UI_ASSETS.good);
    }
    else { // sick
        score += 350;
        AnimatedObject_SetAnimation(&popupObj, UI_ASSETS.sick);
    }

    Vector2 size = AnimatedObject_Sizes(&popupObj); 
    popupObj.position.x = healthbar.x + healthbar.width + 150 - size.x / 2;
    popupObj.position.y = 600 + GetRandomValue(-1000, 1000) / 200.0f;

    popupVelocity = -150;

    popupTimer = 1.0f;
	health = fminf(1, health + 0.0115f);

    note->pressed = 1;
    bf.idleTimer = 0; 
}

static void NoteHits() { 
    char pressed[4] = {0, 0, 0, 0}; 
    int pressedNotes = 0;
 
    for(int i = 0; i < 4; i++) {
        if(IsKeyPressed(game_scheme_keys[game_options_scheme][i])) 
            goto HITS;
    }
 
    //... we didnt even try to press anything
    // tspmo
    goto BRUH; 

    HITS: 

    for(size_t i = firstNote; i < noteCount; i++) {
        Note* note = notes + i;

        if(!note->mustHit || note->missed) 
            continue;
            
        if(Note_TooEarly(note, songBeat.time)){
            // if this note is too early, the others below it are as well
            // the notes are sorted btw
            goto BRUH;
        }
        
        if(IsKeyPressed(game_scheme_keys[game_options_scheme][note->id]) && Note_CanBeHit(note, songBeat.time) && !note->pressed && !pressed[note->id]) {
            pressed[note->id] = 1;
            HitNote(note); 

            AnimatedObject_SetAnimation(&bf.object, bf.animations.notes[note->id]);

            pressedNotes++;
            if(pressedNotes == 4) {
                // pressed the most notes possible in a single frame!!!!
                goto BRUH;
            }
        } 
    }
   
    BRUH:

    for(int i = 0; i < 4; i++) {
        StrumNote* strumNote = bfStrumline + i;

        if(pressed[i])
            AnimatedObject_SetAnimation(&strumNote->object, StrumNote_ConfirmAnimation(i));
        else if(IsKeyUp(game_scheme_keys[game_options_scheme][i])) {
            int j = StrumNote_IdleAnimation(i);
            if(strumNote->object.currentAnim.animationIndex != j) {
                AnimatedObject_SetAnimation(&strumNote->object, j);
            }
        } 
        else if(IsKeyPressed(game_scheme_keys[game_options_scheme][i])) 
            AnimatedObject_SetAnimation(&strumNote->object, StrumNote_PressAnimation(i));  
    }  
} 

static Music deathMusic;
static Sound deathSounds;
static char retryin;

static void PlayState_DeadUpdate() {
    // the entire death scene basically
    PauseMusicStream(inst);
    PauseMusicStream(voices);

    if(justDied) {
        AnimatedObject_SetAnimation(&bf.object, deathAnimations[0]);
        deadTime = -AnimationSet_AnimationLength(&bf.object.animationSet, deathAnimations[0]);
        
        justDied = 0;
        retryin = 0;

        deathSounds = LoadSound("assets/sounds/fnf_loss_sfx.ogg");
        PlaySound(deathSounds);
    }
 
    target = &bf; 
    
    if(retryin){
        UpdateMusicStream(deathMusic);
        bf.object.color.a = smooth(255.0f, 0, deadTime / 2);

        if(deadTime >= 2) {
            UnloadMusicStream(deathMusic);

            restarted = 1;
            PlayState_SetScene();
            return;
        }
    }
    else if(deadTime > 0) {
        if(bf.object.currentAnim.animationIndex != deathAnimations[1]) {
            deathMusic = LoadMusicStream("assets/music/gameOver.ogg");
            deathMusic.looping = 1;
            PlayMusicStream(deathMusic);

            AnimatedObject_SetAnimation(&bf.object, deathAnimations[1]);
        }
        else {
            UpdateMusicStream(deathMusic);

            if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
                retryin = 1;

                deadTime = 0; // reusing this timer!!!

                AnimatedObject_SetAnimation(&bf.object, deathAnimations[2]);
                UnloadMusicStream(deathMusic);
                UnloadSound(deathSounds);
                         
                deathMusic = LoadMusicStream("assets/music/gameOverEnd.ogg");
                deathMusic.looping = 0;
                PlayMusicStream(deathMusic); 
            }
            else if(IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE)) {
                UnloadMusicStream(deathMusic);
                UnloadSound(deathSounds);

                Freeplay_SetScene();
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

            Freeplay_SetScene();
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
    // play sound only if the window is focused  

    if(!IsWindowFocused()) {
        PauseMusicStream(inst);
        PauseMusicStream(voices);
        return;
    }

    if(isPaused) {
        pauseTimer += RayGame_DeltaTime();
        return;
    }
    
    if(health < 0)
        PlayState_DeadUpdate(); 
    else if(songBeat.time < 0) {
        // INTRO
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
            // gf dance

            introHit++;
            introTime -= introStep;
        }

        introText.color.a = smooth(255.0f, 0, introTime / introStep);
    }
    else { 
        UpdateMusicStream(inst);
        UpdateMusicStream(voices);

        ResumeMusicStream(inst);
        ResumeMusicStream(voices); 
        
        BeatManager_Update(&songBeat, StepHit, BeatHit); 

        // complete song
        if(!IsMusicStreamPlaying(inst)) {
            RayGame_ResetMusic();
            RayGame_ToggleMusic(1);
            
            Freeplay_SetScene();
            return;
        }

        NoteHits(); 
    }

    if(IsKeyPressed(KEY_ENTER)) {
        pauseTimer = -0.1f; // little cooldown
        isPaused = 1;
    }

    cameraTarget = target->object.position;
    cameraTarget.x += target->cameraOffset.x;
    cameraTarget.y += target->cameraOffset.y;
    
    bop = Lerp(bop, 1, RayGame_DeltaTime() * 15.0f); 

    camGame.target.x = Lerp(camGame.target.x, cameraTarget.x, RayGame_DeltaTime() * 5);
    camGame.target.y = Lerp(camGame.target.y, cameraTarget.y, RayGame_DeltaTime() * 5);
    camGame.zoom = Lerp(camGame.zoom, stage.defaultCameraZoom, RayGame_DeltaTime() * 8);
    camHUD.zoom = Lerp(camHUD.zoom, 1, RayGame_DeltaTime() * 8);  
}

static void MissNote(Note* note) {
    note->missed = 1;
    bf.idleTimer = 0;
	health -= 0.02f;

    PlaySound(missSFX[GetRandomValue(0, 2)]);
    AnimatedObject_SetAnimation(&bf.object, bf.missAnimations.notes[note->id]);
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
    if(health < 0){
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

        // we dont want the characters to change frames or something while paused
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
        // rendering strumnotes
        for(int i = 0; i < 4; i++) {
            StrumNote *dadStrum = dadStrumline + i;     
            StrumNote *bfStrum = bfStrumline + i;

            AnimatedObject_UpdateFrame(&dadStrum->object);
            Render_DrawAnimatedObject(&dadStrum->object);

            AnimatedObject_UpdateFrame(&bfStrum->object);
            Render_DrawAnimatedObject(&bfStrum->object);
        }

        // note rendering
        { 
            // https://stackoverflow.com/questions/77070321/raylib-alpha-blending-not-working-properly 
            rlSetBlendFactorsSeparate(0x0302, 0x0303, 1, 0x0303, 0x8006, 0x8006);
            BeginBlendMode(BLEND_CUSTOM_SEPARATE);

            float endHeight = fmaxf(64.0f, 18.0f * song.speed);   

            char clearing = 1;
            for(size_t i = firstNote; i < noteCount; i++) {
                Note* note = notes + i;

                StrumNote* targetStrumNote = (note->mustHit ? bfStrumline : dadStrumline) + note->id;
                renderNote.position.y = targetStrumNote->object.position.y + 450 * (note->time - songBeat.time) * song.speed; 
                
                if(renderNote.position.y > 720)
                    break;

                AnimatedObject_SetAnimation(&renderNote, Note_Animation(note->id));
                renderNote.position.x = targetStrumNote->object.position.x;

                if(!note->mustHit && !note->pressed && songBeat.time >= note->time) {
                    note->pressed = 1;  
                    dad.idleTimer = 0;
                    AnimatedObject_SetAnimation(&dad.object, dad.animations.notes[note->id]);
                }

                if(note->length > 0) { 
                    AnimatedObject_SetAnimation(&trail, Note_TrailAnimation(note->id));
                    trail.position.x = targetStrumNote->object.position.x + trailOffset.x; // standard offset
                    trail.position.y = renderNote.position.y + trailOffset.y;
                    trail.scaleY = fmaxf(0.01f, (450.0f * song.speed * note->length - endHeight) / 44.0f);

                    trail.color = normalColor;

                    AnimatedObject_SetAnimation(&end, Note_EndAnimation(note->id));
                    end.position.x = trail.position.x;
                    end.position.y = trail.position.y + trail.scaleY * 44.0f;   
                    end.scaleY = endHeight / 64.0f;
                    end.color = normalColor;

                    Vector2 cut = GetWorldToScreen2D(ADD_VEC(targetStrumNote->object.position, trailOffset), camHUD);
                    if(!note->missed) {
                        char shouldHold = Note_ShouldHold(note, songBeat.time);

                        if(!note->mustHit) {
                            if(shouldHold) {
                                dad.idleTimer = 0; // dad holding his note  
                            }
                        }
                        else if(!note->pressed)
                            goto SKIP_CLIP;
                        else if(!isPaused && shouldHold) {
                            if(IsKeyUp(game_scheme_keys[game_options_scheme][note->id])) {
                                MissNote(note);
                                goto SKIP_CLIP;
                                // stopped holding note => miss!!  
                            }

                            health = fminf(1, health + 0.04f * RayGame_DeltaTime());
                            bf.idleTimer = 0;  
                        }  

                        BeginScissorMode(0, cut.y, RayGame_WindowWidth(), RayGame_WindowHeight()); // we only care about the y 
                            Render_DrawAnimatedObject(&trail);
                            Render_DrawAnimatedObject(&end);
                        EndScissorMode(); 
                        goto CLIPPED;
                    }
                    else {
                        trail.color = missedColor;
                        end.color = missedColor; 
                    }   

                    SKIP_CLIP:
                    Render_DrawAnimatedObject(&trail);
                    Render_DrawAnimatedObject(&end); 
                    CLIPPED:
                }

                if(!note->pressed) { 
                    if(!note->missed && Note_TooLate(note, songBeat.time)) {
                        // missed basic note 
                        MissNote(note);
                    }

                    // no need to update frame because it is not animated:) 
                    Render_DrawAnimatedObject(&renderNote); 
                }  

                if(clearing) {
                    if(songBeat.time > note->time + note->length + 0.167f && renderNote.position.y < -100) {
                        firstNote++; 
                    }
                    else 
                        clearing = 0;
                }
            }  

            EndBlendMode(); 
        }


        // healthbar rendering
        { 
            const int C = 25; // this pushes the icons closer to each other
            DrawRectangleRec(healthbar, BLACK); 

            float offsetX = healthbar.x + healthbarPadding;
            float availableWidth = healthbar.width - healthbarPadding * 2; 
            float availableHeight = healthbar.height - healthbarPadding * 2;
            float middle = availableWidth * (1 - health);
            
            DrawRectangle(offsetX, healthbar.y + healthbarPadding, (int) middle + 1 /* fix 1px gap */, availableHeight, dadColor);
            DrawRectangle(offsetX + middle, healthbar.y + healthbarPadding, availableWidth * health, availableHeight, bfColor);

            if(game_options_showScore) {
                const char* txt = TextFormat("Score: %d", score);
                const float shadow = 1.5f;
                
                DrawTextEx(vcrFont, txt, (Vector2) {healthbar.x + healthbar.width - 170 + shadow, healthbar.y + 30 - shadow}, 16, 1, BLACK); 
                DrawTextEx(vcrFont, txt, (Vector2) {healthbar.x + healthbar.width - 170, healthbar.y + 30}, 16, 1, WHITE);
            }

            Rectangle frameRect = {0, 0, 150, 150}; 

            frameRect.x = (health > 0.9f) * 150;
            DrawTexturePro(dad.icon, frameRect, (Rectangle) {
                .x = offsetX + middle + C - 150 * bop,
                .y = healthbar.y + healthbar.height / 2 - bop * 150 / 2,
                .width = 150 * bop,
                .height = 150 * bop
            }, VECTOR_ZERO, 0, WHITE); 
 
            frameRect.x = (health < 0.1f) * 150;
            DrawTexturePro(bf.icon, frameRect, (Rectangle) {
                .x = offsetX + middle - C,
                .y = healthbar.y + healthbar.height / 2 - bop * 150 / 2,
                .width = 150 * bop,
                .height = 150 * bop
            }, VECTOR_ZERO, 0, WHITE);
        } 

        if(songBeat.time < 0) 
            Render_DrawAnimatedObject(&introText);
        
        if(popupTimer > 0) {
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
    __song__ = song;
}

static void PlayState_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadMusicStream(inst);
    UnloadMusicStream(voices);
    if(restarted) return;

    for(int i = 0; i < 3; i++)
        UnloadSound(missSFX[i]); 

    // move this maybe
    for(int i = 0; i < 4; i++)
        UnloadSound(introSounds[i]);

    free(notes);
    Song_Free(&song);
    AnimationSet_FreeAll(&numFrames);

    UnloadTexture(dad.icon);
    UnloadTexture(bf.icon); 
    
    AnimationSet_FreeAll(&uiAssets);
    AnimationSet_FreeAll(&dad.object.animationSet);
    AnimationSet_FreeAll(&bf.object.animationSet);

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