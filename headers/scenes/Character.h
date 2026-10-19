/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl, outd day19 of10 2026 */
#pragma once
#ifndef CHARACTER_H
#define CHARACTER_H

#include "Render.h" 
#include <stdbool.h>

typedef struct {
    RayAnimatedObject object;
    Vector2 cameraOffset;
    Texture2D icon;
    float idleTimer; 
    bool idled;
    struct { int idle; int notes[4]; } animations;
    struct { int notes[4]; } missAnimations;
} Character; 

typedef struct {
    RayAnimatedObject object;
    char f; 
    int anims[2];
} Girlfriend;

void Character_SetSongDir(const char* restrict songDir);
void Character_Load(Character* restrict character, const char* restrict characterName, bool isDad);
void Character_LoadDeathAnimations(Character* restrict character, const char* restrict characterName, int* restrict anims);
void Girlfriend_Load(Girlfriend* restrict gf, const char* restrict name);

#endif
