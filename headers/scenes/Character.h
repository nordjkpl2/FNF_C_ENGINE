#ifndef CHARACTER_H
#define CHARACTER_H

#include "Render.h" 
 
typedef struct {
    RayAnimatedObject object;
    Vector2 cameraOffset;
    Texture2D icon;

    float idleTimer; 
    char idled;

    struct {
        int idle;
        int notes[4];
    } animations;

    struct {
        int notes[4];
    } missAnimations;
} Character; 

typedef struct {
    RayAnimatedObject object;
 
    char f; 
    int anims[2]; // left, right or right, left doesnt matter!!!!
} Girlfriend;

void Character_Load(Character* character, char* characterName, char isDad);
void Character_LoadDeathAnimations(Character* character, char* characterName, int* anims);

void Girlfriend_Load(Girlfriend* gf, char* name);

#endif