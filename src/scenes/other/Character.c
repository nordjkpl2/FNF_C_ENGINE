#include"scenes/Character.h" 

void Character_Load(Character* character, char* characterName, char isDad) {
    Render_DefaultAnimated(&character->object);
    RayAnimationHandler characterAnimations;

    character->idled = 0;
    character->idleTimer = 0; 

    if(strcmp(characterName, "dad") == 0 || isDad) {
        characterAnimations = AnimationSet_LoadAnimations("assets/images/characters/dad.animset", Render_LoadTexture("assets/images/characters/dad.png"));
        
        AnimationSet_SetData(character->animations.idle, characterAnimations, "Dad idle dance", 24, 0, 0, 0);
        AnimationSet_SetData(character->animations.notes[0], characterAnimations, "Dad Sing Note LEFT", 24, 0, -9, 10);
        AnimationSet_SetData(character->animations.notes[1], characterAnimations, "Dad Sing Note DOWN", 24, 0, 0, -30);
        AnimationSet_SetData(character->animations.notes[2], characterAnimations, "Dad Sing Note UP", 24, 0, -6, 50);
        AnimationSet_SetData(character->animations.notes[3], characterAnimations, "Dad Sing Note RIGHT", 24, 0, 0, 27); 

        character->cameraOffset = (Vector2) {400, 300};  
        character->icon = Render_LoadTexture("assets/images/icons/dad.png");
    }
    else if(strcmp(characterName, "bf") == 0 || !isDad) { 
        characterAnimations = AnimationSet_LoadAnimations("assets/images/characters/bf.animset", Render_LoadTexture("assets/images/characters/bf.png"));

        AnimationSet_SetData(character->animations.idle, characterAnimations, "BF idle dance", 24, 0, -5, 0);
        AnimationSet_SetData(character->animations.notes[0], characterAnimations, "BF NOTE LEFT", 24, 0, 5, -6);
        AnimationSet_SetData(character->animations.notes[1], characterAnimations, "BF NOTE DOWN", 24, 0, -20, -51);
        AnimationSet_SetData(character->animations.notes[2], characterAnimations, "BF NOTE UP", 24, 0, -46, 27);
        AnimationSet_SetData(character->animations.notes[3], characterAnimations, "BF NOTE RIGHT", 24, 0, -48, -7);

        AnimationSet_SetData(character->missAnimations.notes[0], characterAnimations, "BF NOTE LEFT MISS", 24, 0, 7, 19);
        AnimationSet_SetData(character->missAnimations.notes[1], characterAnimations, "BF NOTE DOWN MISS", 24, 0, -15, -19);
        AnimationSet_SetData(character->missAnimations.notes[2], characterAnimations, "BF NOTE UP MISS", 24, 0, -46, 27);
        AnimationSet_SetData(character->missAnimations.notes[3], characterAnimations, "BF NOTE RIGHT MISS", 24, 0, -48, 19);

        character->cameraOffset = (Vector2) {50, 50}; 
        character->icon = Render_LoadTexture("assets/images/icons/bf.png");
    }
    character->object.animationSet = characterAnimations;
    AnimatedObject_SetAnimation(&character->object, character->animations.idle);
}

// 3 animations, [ die, loop, confirm ]
void Character_LoadDeathAnimations(Character* character, char* characterName, int* anims) {
    RayAnimationHandler set = character->object.animationSet;
    
    AnimationSet_SetData(anims[0], set, "BF dies", 24, 0, 37, 11);
    AnimationSet_SetData(anims[1], set, "BF Dead Loop", 24, 1, 37, 5);
    AnimationSet_SetData(anims[2], set, "BF Dead confirm", 24, 0, 37, 69);
}

void Girlfriend_Load(Girlfriend* gf, char* name) {
    Render_DefaultAnimated(&gf->object);

    gf->f = 0;
    RayAnimationHandler characterAnimations;

    // this spritesheet has the necessary animations for actual gameplay (the sad and cheer animations are pretty useless imo)
    characterAnimations = AnimationSet_LoadAnimations("assets/images/characters/gf.animset", Render_LoadTexture("assets/images/characters/gf.png"));
    AnimationSet_SetAnimationData(&characterAnimations, 0, 24, 0);
    AnimationSet_SetAnimationData(&characterAnimations, 1, 24, 0);

    gf->object.animationSet = characterAnimations; 
    gf->anims[0] = 0;
    gf->anims[1] = 1;

    AnimatedObject_SetAnimation(&gf->object, gf->anims[0]);
}