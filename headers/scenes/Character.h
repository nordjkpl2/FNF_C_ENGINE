/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#pragma once
#ifndef CHARACTER_H
#define CHARACTER_H
/* // ! Includes */
#include "Render.h" 
 /* //  code */
 /* // Personagems, Cor da Barra de Vida, Animações Idle e Animação de Erro*/
typedef struct {
    RayAnimatedObject object;
    Vector2 cameraOffset;
    Texture2D icon;
    // Color healthColor;
    /* // ? Idle */
    float idleTimer; 
    char idled;
    struct {
        int idle;
        int notes[4];
    } animations;
    /* // ? Miss */
    struct {
        int notes[4];
    } missAnimations;
} Character; 
/* // girlfriend spawn structure */
typedef struct {
    RayAnimatedObject object;
    /* // girlfriend Animations */
    char f; 
    int anims[2]; // left, right or right, left doesnt matter!!!!
} Girlfriend;
// raiz do mod da musica atual (p/ resolver artes do mod antes do global).
// Chamar uma vez por load de musica (PlayState) antes dos Character_Load.
void Character_SetSongDir(const char* songDir);
/* // TODO: CARREGADOR DE PERSONAGENS E ANIMAÇÃO DE MORTE */
void Character_Load(Character* character, char* characterName, char isDad);
void Character_LoadDeathAnimations(Character* character, char* characterName, int* anims);
/* // Girlfriend Load */
void Girlfriend_Load(Girlfriend* gf, char* name);

#endif
