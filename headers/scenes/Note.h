#ifndef NOTE_H
#define NOTE_H

#include "Render.h"
#include "scenes/Song.h"

typedef struct {
    RayAnimatedObject object;  
    int id;
} StrumNote; // yuh

typedef struct { 
    int id;
     
    float time;
    float length;
    
    char mustHit;
    char pressed;
    char missed;
} Note;

void StrumNote_Load(StrumNote* note, int id);
int StrumNote_IdleAnimation(int id);
int StrumNote_PressAnimation(int id);
int StrumNote_ConfirmAnimation(int id);

void Note_Load(Note* note, DataNote* dataNote);

// define skin atual da musica (chamado 1x no load da musica)
void Note_SetCurrentSkin(const RayAnimationHandler* skin);
const RayAnimationHandler* Note_GetCurrentSkin(void);

// reload animations for current skin (chamado apos trocar skin)
void NoteStuff_LoadAnimations(void);

// janela de hit (setada 1x por musica via diffSuf; default 0.200 = normal)
void Note_SetWindow(float w);
float Note_Window(void);

char Note_CanBeHit(Note* note, float time);
char Note_TooEarly(Note* note, float time);
char Note_TooLate(Note* note, float time);
char Note_ShouldHold(Note* note, float time); 

int Note_Animation(int id);
int Note_TrailAnimation(int id);
int Note_EndAnimation(int id);
#endif