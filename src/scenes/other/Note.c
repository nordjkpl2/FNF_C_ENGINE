#include "scenes/Note.h" 
#include "scenes/AllScenes.h"

#define FIND_ANIMS(arr, L, D, U, R) {\
        arr[0] = AnimationSet_FindAnimation(&anims, L);\
        arr[1] = AnimationSet_FindAnimation(&anims, D);\
        arr[2] = AnimationSet_FindAnimation(&anims, U);\
        arr[3] = AnimationSet_FindAnimation(&anims, R);\
    }\

static int idles[4];
static int presses[4];
static int confirm[4];
static int notes[4];
static int trails[4];
static int ends[4];

static char ready = 0;

void NoteStuff_LoadAnimations() {
    RayAnimationHandler anims = Cache_GetNoteAnimations();

    FIND_ANIMS(idles, "arrowLEFT", "arrowDOWN", "arrowUP", "arrowRIGHT");
    FIND_ANIMS(presses, "left press", "down press", "up press", "right press");
    FIND_ANIMS(confirm, "left confirm", "down confirm", "up confirm", "right confirm");
    FIND_ANIMS(notes, "purple", "blue", "green", "red");
    FIND_ANIMS(trails, "purple hold piece", "blue hold piece", "green hold piece", "red hold piece");
    FIND_ANIMS(ends, "purple hold end", "blue hold end", "green hold end", "red hold end");

    ready = 1;
}

void StrumNote_Load(StrumNote* note, int id) {
    if(!ready) 
        NoteStuff_LoadAnimations();

    Render_DefaultAnimated(&note->object);

    note->object.animationSet = Cache_GetNoteAnimations(); 
    note->object.scaleX = 0.7f;
    note->object.scaleY = 0.7f; 
    note->id = id;

    AnimatedObject_SetAnimation(&note->object, idles[id]);
}

int StrumNote_IdleAnimation(int id) {
    return idles[id];
}

int StrumNote_PressAnimation(int id) {
    return presses[id];
}

int StrumNote_ConfirmAnimation(int id) {
    return confirm[id];
}
 
void Note_Load(Note* note, DataNote* dataNote) { 
    note->time = dataNote->time / 1000.0f; // to seconds
    note->length = dataNote->len / 1000.0f;
    note->mustHit = dataNote->id < 0;
    note->id = dataNote->id;
    note->pressed = 0;
    note->missed = 0; 
    
    if(note->mustHit)
        note->id *= -1;

    note->id--;  
}

char DataNote_ShouldSpawn(DataNote* dataNote, float time, float speed) {
    return dataNote->time / 1000.0f - time < 1.8f / speed;
}

char Note_CanBeHit(Note* note, float time) {
    return note->time > time - 0.167f && note->time < time + 0.167f;
} 

char Note_TooLate(Note* note, float time) {
    return note->time <= time - 0.167f;
}

char Note_TooEarly(Note* note, float time) {
    return note->time >= time + 0.167;
}

char Note_ShouldHold(Note* note, float time) {
    return time > note->time && time < note->time + note->length;
}
 
int Note_Animation(int id) {
    return notes[id];
}

int Note_TrailAnimation(int id) {
    return trails[id];
}

int Note_EndAnimation(int id) {
    return ends[id];
}