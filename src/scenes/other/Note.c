/* Friday Night Funkin' C Engine (c) 2026 nordjkpl
 * Note.c - animation cache and note timing helpers
 */

#include "scenes/Note.h"
#include "scenes/AllScenes.h"

#include <math.h>
#include <stdint.h>

#define NOTE_LANE_COUNT 4
#define NOTE_WINDOW_DEFAULT 0.400f
#define NOTE_WINDOW_MIN     0.050f
#define NOTE_WINDOW_MAX     0.500f
#define MS_TO_SECONDS       0.001f

/* Animation names are kept in tables to avoid repeating lookup code. */
static const char* const idleNames[NOTE_LANE_COUNT] = {
    "arrowLEFT", "arrowDOWN", "arrowUP", "arrowRIGHT"
};

static const char* const pressNames[NOTE_LANE_COUNT] = {
    "left press", "down press", "up press", "right press"
};

static const char* const confirmNames[NOTE_LANE_COUNT] = {
    "left confirm", "down confirm", "up confirm", "right confirm"
};

static const char* const noteNames[NOTE_LANE_COUNT] = {
    "purple", "blue", "green", "red"
};

static const char* const trailNames[NOTE_LANE_COUNT] = {
    "purple hold piece", "blue hold piece", "green hold piece", "red hold piece"
};

static const char* const endNames[NOTE_LANE_COUNT] = {
    "purple hold end", "blue hold end", "green hold end", "red hold end"
};

static int idles[NOTE_LANE_COUNT]   = {-1, -1, -1, -1};
static int presses[NOTE_LANE_COUNT] = {-1, -1, -1, -1};
static int confirms[NOTE_LANE_COUNT] = {-1, -1, -1, -1};
static int notesAnim[NOTE_LANE_COUNT] = {-1, -1, -1, -1};
static int trails[NOTE_LANE_COUNT]   = {-1, -1, -1, -1};
static int ends[NOTE_LANE_COUNT]     = {-1, -1, -1, -1};

static const RayAnimationHandler* currentSkin = NULL;
static const RayAnimationHandler* defaultSkinPtr = NULL;
static const RayAnimationHandler* loadedSkin = NULL;
static char animationsReady = 0;

static float noteWindow = NOTE_WINDOW_DEFAULT;

static inline int Note_IsValidLane(int lane) {
    return (unsigned)lane < NOTE_LANE_COUNT;
}

static void Note_InvalidateAnimationCache(void) {
    animationsReady = 0;
    loadedSkin = NULL;
}

void Note_SetWindow(float windowSeconds) {
    /* Ignore NaN/Inf rather than poisoning every timing comparison. */
    if(!isfinite(windowSeconds))
        windowSeconds = NOTE_WINDOW_DEFAULT;

    if(windowSeconds < NOTE_WINDOW_MIN)
        windowSeconds = NOTE_WINDOW_MIN;
    else if(windowSeconds > NOTE_WINDOW_MAX)
        windowSeconds = NOTE_WINDOW_MAX;

    noteWindow = windowSeconds;
}

float Note_Window(void) {
    return noteWindow;
}

void Note_SetCurrentSkin(const RayAnimationHandler* skin) {
    /* Same pointer means the cached animation indices are still valid. */
    if(currentSkin == skin)
        return;

    currentSkin = skin;
    Note_InvalidateAnimationCache();
}

const RayAnimationHandler* Note_GetCurrentSkin(void) {
    if(currentSkin != NULL)
        return currentSkin;

    /* Retry while NULL: the default cache may not be initialized yet. */
    if(defaultSkinPtr == NULL)
        defaultSkinPtr = Cache_GetDefaultNoteSkinPtr();

    return defaultSkinPtr;
}

static void NoteStuff_LoadAnimationsInternal(const RayAnimationHandler* animations) {
    if(animations == NULL) {
        for(int i = 0; i < NOTE_LANE_COUNT; ++i) {
            idles[i] = -1;
            presses[i] = -1;
            confirms[i] = -1;
            notesAnim[i] = -1;
            trails[i] = -1;
            ends[i] = -1;
        }

        Note_InvalidateAnimationCache();
        return;
    }

    for(int i = 0; i < NOTE_LANE_COUNT; ++i) {
        idles[i]    = AnimationSet_FindAnimation(animations, idleNames[i]);
        presses[i]  = AnimationSet_FindAnimation(animations, pressNames[i]);
        confirms[i] = AnimationSet_FindAnimation(animations, confirmNames[i]);
        notesAnim[i] = AnimationSet_FindAnimation(animations, noteNames[i]);
        trails[i]   = AnimationSet_FindAnimation(animations, trailNames[i]);
        ends[i]     = AnimationSet_FindAnimation(animations, endNames[i]);
    }

    loadedSkin = animations;
    animationsReady = 1;
}

/* Ensures the cache matches the active skin. Called during setup, not per note. */
static const RayAnimationHandler* Note_EnsureCurrentAnimations(void) {
    const RayAnimationHandler* animations = Note_GetCurrentSkin();
    if(animations == NULL)
        return NULL;

    if(!animationsReady || loadedSkin != animations)
        NoteStuff_LoadAnimationsInternal(animations);

    return animationsReady ? animations : NULL;
}

void NoteStuff_LoadAnimations(void) {
    /* Explicit load also refreshes indices if a skin was modified in place. */
    NoteStuff_LoadAnimationsInternal(Note_GetCurrentSkin());
}

void StrumNote_Load(StrumNote* note, int id) {
    if(note == NULL || !Note_IsValidLane(id))
        return;

    const RayAnimationHandler* animations = Note_EnsureCurrentAnimations();
    if(animations == NULL)
        return;

    Render_DefaultAnimated(&note->object);
    note->object.animationSet = *animations;
    note->object.scaleX = 0.7f;
    note->object.scaleY = 0.7f;
    note->id = id;

    AnimatedObject_SetAnimation(&note->object, idles[id]);
}

int StrumNote_IdleAnimation(int id) {
    return Note_IsValidLane(id) ? idles[id] : -1;
}

int StrumNote_PressAnimation(int id) {
    return Note_IsValidLane(id) ? presses[id] : -1;
}

int StrumNote_ConfirmAnimation(int id) {
    return Note_IsValidLane(id) ? confirms[id] : -1;
}

void Note_Load(Note* note, DataNote* dataNote) {
    if(note == NULL)
        return;

    if(dataNote == NULL) {
        note->time = 0.0f;
        note->length = 0.0f;
        note->mustHit = 0;
        note->id = -1;
        note->pressed = 0;
        note->missed = 0;
        return;
    }

    /* The sign identifies the side; absolute value 1..4 identifies the lane. */
    const int64_t rawId = (int64_t)dataNote->id;
    const int64_t laneId = (rawId < 0) ? -rawId : rawId;

    note->time = isfinite(dataNote->time)
               ? dataNote->time * MS_TO_SECONDS
               : 0.0f;
    note->length = (isfinite(dataNote->len) && dataNote->len > 0.0f)
                 ? dataNote->len * MS_TO_SECONDS
                 : 0.0f;
    note->mustHit = (rawId < 0);
    note->id = (laneId >= 1 && laneId <= NOTE_LANE_COUNT)
             ? (int)(laneId - 1)
             : -1;
    note->pressed = 0;
    note->missed = 0;
}

/* Compatibility functions. The performance-critical call sites should use
 * the static inline versions declared in Note.h, as intended by the engine.
 */
char Note_CanBeHit(Note* note, float time) {
    if(note == NULL)
        return 0;

    const float delta = note->time - time;
    return (delta > -noteWindow && delta < noteWindow);
}

char Note_TooLate(Note* note, float time) {
    return (note != NULL) && (note->time <= time - noteWindow);
}

char Note_TooEarly(Note* note, float time) {
    return (note != NULL) && (note->time >= time + noteWindow);
}

char Note_ShouldHold(Note* note, float time) {
    if(note == NULL || note->length <= 0.0f)
        return 0;

    const float endTime = note->time + note->length;
    return (time > note->time && time < endTime);
}

int Note_Animation(int id) {
    return Note_IsValidLane(id) ? notesAnim[id] : -1;
}

int Note_TrailAnimation(int id) {
    return Note_IsValidLane(id) ? trails[id] : -1;
}

int Note_EndAnimation(int id) {
    return Note_IsValidLane(id) ? ends[id] : -1;
}
