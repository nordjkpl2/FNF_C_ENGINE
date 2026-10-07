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

char DataNote_ShouldSpawn(DataNote* dataNote, float time, float speed);

char Note_CanBeHit(Note* note, float time);
char Note_TooEarly(Note* note, float time);
char Note_TooLate(Note* note, float time);
char Note_ShouldHold(Note* note, float time); 

int Note_Animation(int id);
int Note_TrailAnimation(int id);
int Note_EndAnimation(int id);

int TrailNote_GetAnimation(int id);
#endif