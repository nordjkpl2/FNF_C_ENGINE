/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl - Note.h Optimized, outd day19 of10 2026 */
#pragma once
#ifndef NOTE_H
#define NOTE_H

#include "Render.h"
#include "scenes/Song.h"
#include <math.h>

typedef struct {
    RayAnimatedObject object;  
    int id;
} StrumNote;

typedef struct { 
    int id;
    float time;
    float length;
    char mustHit;
    char pressed;
    char missed;
} Note;

/* // * Animações */
void StrumNote_Load(StrumNote* note, int id);
int StrumNote_IdleAnimation(int id);
int StrumNote_PressAnimation(int id);
int StrumNote_ConfirmAnimation(int id);

void Note_Load(Note* note, DataNote* dataNote);
void Note_SetCurrentSkin(const RayAnimationHandler* skin);
const RayAnimationHandler* Note_GetCurrentSkin(void);
void NoteStuff_LoadAnimations(void);

void Note_SetWindow(float w);
float Note_Window(void);

/* // ? API legado - mantida por compatibilidade (chama 1x, não use no loop) */
char Note_CanBeHit(Note* note, float time);
char Note_TooEarly(Note* note, float time);
char Note_TooLate(Note* note, float time);
char Note_ShouldHold(Note* note, float time); 

int Note_Animation(int id);
int Note_TrailAnimation(int id);
int Note_EndAnimation(int id);

// --- HOT PATH INLINES - ZERO CALL - USE ESSES NO PlayState.c ---
// Versão 1: compatível (ainda chama Note_Window, mas inlineada)
static inline int Note_CanBeHit_inline(const Note* n, float t) { 
    return fabsf(n->time - t) < Note_Window(); 
}
static inline int Note_TooLate_inline(const Note* n, float t) { 
    return n->time <= t - Note_Window(); 
}
static inline int Note_TooEarly_inline(const Note* n, float t) { 
    return n->time >= t + Note_Window(); 
}
static inline int Note_ShouldHold_inline(const Note* n, float t) { 
    return t > n->time && t < n->time + n->length; 
}

// Versão 2: PERFORMANCE ABSOLUTA - hoista window fora do loop (RECOMENDADA)
static inline int Note_CanBeHit_win(const Note* n, float t, float win) { 
    return fabsf(n->time - t) < win; 
}
static inline int Note_TooLate_win(const Note* n, float t, float win) { 
    return n->time <= t - win; 
}
static inline int Note_ShouldHold_win(const Note* n, float t) { 
    return t > n->time && t < n->time + n->length; 
}

#endif