/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl - Note.c Optimized, outd day19 of10 2026 */
#include "scenes/Note.h" 
#include "scenes/AllScenes.h"
#include <math.h>

#define FIND_ANIMS(arr, L, D, U, R, anims) {\
        arr[0] = AnimationSet_FindAnimation(anims, L);\
        arr[1] = AnimationSet_FindAnimation(anims, D);\
        arr[2] = AnimationSet_FindAnimation(anims, U);\
        arr[3] = AnimationSet_FindAnimation(anims, R);\
    }\

static int idles[4];
static int presses[4];
static int confirm[4];
static int notesAnim[4];
static int trails[4];
static int ends[4];

static char ready = 0;
static const RayAnimationHandler* currentSkin = NULL;
static const RayAnimationHandler* defaultSkinPtr = NULL;

static float noteWindow = 0.400f; 

void Note_SetWindow(float w) {
    if(w < 0.05f) w = 0.05f;
    if(w > 0.50f) w = 0.50f;
    noteWindow = w;
}

float Note_Window(void) { return noteWindow; }

void Note_SetCurrentSkin(const RayAnimationHandler* skin) {
    // fast reject: mesma skin não recarrega
    if (currentSkin == skin) return;
    currentSkin = skin;
    ready = 0;
}

const RayAnimationHandler* Note_GetCurrentSkin(void) {
    if(currentSkin) return currentSkin;
    if(!defaultSkinPtr) defaultSkinPtr = Cache_GetDefaultNoteSkinPtr();
    return defaultSkinPtr;
}

static inline void NoteStuff_LoadAnimationsInternal(const RayAnimationHandler* anims) {
    FIND_ANIMS(idles, "arrowLEFT", "arrowDOWN", "arrowUP", "arrowRIGHT", anims);
    FIND_ANIMS(presses, "left press", "down press", "up press", "right press", anims);
    FIND_ANIMS(confirm, "left confirm", "down confirm", "up confirm", "right confirm", anims);
    FIND_ANIMS(notesAnim, "purple", "blue", "green", "red", anims);
    FIND_ANIMS(trails, "purple hold piece", "blue hold piece", "green hold piece", "red hold piece", anims);
    FIND_ANIMS(ends, "purple hold end", "blue hold end", "green hold end", "red hold end", anims);
    ready = 1;
}

void NoteStuff_LoadAnimations(void) {
    NoteStuff_LoadAnimationsInternal(Note_GetCurrentSkin());
}

void StrumNote_Load(StrumNote* note, int id) {
    if(!ready) NoteStuff_LoadAnimations();
    Render_DefaultAnimated(&note->object);
    const RayAnimationHandler* anims = Note_GetCurrentSkin();
    note->object.animationSet = *anims; // TODO futuro: trocar pra ponteiro pra evitar memcpy
    note->object.scaleX = 0.7f;
    note->object.scaleY = 0.7f; 
    note->id = id;
    AnimatedObject_SetAnimation(&note->object, idles[id]);
}

int StrumNote_IdleAnimation(int id) { return idles[id]; }
int StrumNote_PressAnimation(int id) { return presses[id]; }
int StrumNote_ConfirmAnimation(int id) { return confirm[id]; }
 
void Note_Load(Note* note, DataNote* dataNote) { 
    // 0.001f é mais rápido que / 1000.0f e permite FMA
    note->time = dataNote->time * 0.001f;
    note->length = dataNote->len * 0.001f;
    note->mustHit = dataNote->id < 0;
    note->id = dataNote->id;
    if(note->mustHit) note->id = -note->id;
    note->id--;  
    note->pressed = 0;
    note->missed = 0; 
}

// Mantidos no .c por compatibilidade, mas o hot path DEVE usar os inline do .h
char Note_CanBeHit(Note* note, float time) { return fabsf(note->time - time) < noteWindow; } 
char Note_TooLate(Note* note, float time) { return note->time <= time - noteWindow; }
char Note_TooEarly(Note* note, float time) { return note->time >= time + noteWindow; }
char Note_ShouldHold(Note* note, float time) { return time > note->time && time < note->time + note->length; }
 
int Note_Animation(int id) { return notesAnim[id]; }
int Note_TrailAnimation(int id) { return trails[id]; }
int Note_EndAnimation(int id) { return ends[id]; }