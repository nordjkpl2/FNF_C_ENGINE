#include "scenes/AllScenes.h"

typedef struct {
    RayAnimationHandler animations;
    char* animationPath;
    char* texturePath;

    char isLoaded;
} CachedAnimation; 

Font mainFont;  
Vector2 VECTOR_ZERO; 

static CachedAnimation notes;  

void AllScenes_StartGame(void) {
    VECTOR_ZERO = (Vector2) {0, 0};
    
    // music
    RayGame_SetMusic("assets/music/freakyMenu.ogg", 0.3f, 1);
    RayGame_ToggleMusic(1);

    // fonts
    {
        mainFont = LoadFontEx("assets/fonts/main.ttf", 64, 0, 0);
        SetTextureFilter(mainFont.texture, TEXTURE_FILTER_BILINEAR);  
    }

    // cached items
    {
        notes.animationPath = "assets/images/NOTE_assets.animset";
        notes.texturePath   = "assets/images/NOTE_assets.png";
        
        notes.isLoaded = 0; 
    }
}

RayAnimationHandler Cache_GetNoteAnimations(void) {
    if(!notes.isLoaded) {
        // load animations with fps and stuff like that
        notes.animations = AnimationSet_LoadAnimations(notes.animationPath, Render_LoadTexture(notes.texturePath));

        int _;

        AnimationSet_SetData(_, notes.animations, "arrowDOWN", 1, 0, 0, 0);
        AnimationSet_SetData(_, notes.animations, "arrowUP", 1, 0, 0, 0);
        AnimationSet_SetData(_, notes.animations, "arrowLEFT", 1, 0, 0, 0);
        AnimationSet_SetData(_, notes.animations, "arrowRIGHT", 1, 0, 0, 0); 
        AnimationSet_SetData(_, notes.animations, "left press", 24, 0, -4, -4);
        AnimationSet_SetData(_, notes.animations, "right press", 24, 0, -4, -4);
        AnimationSet_SetData(_, notes.animations, "up press", 24, 0, -4, -4);
        AnimationSet_SetData(_, notes.animations, "down press", 24, 0, -4, -4);
        AnimationSet_SetData(_, notes.animations, "left confirm", 24, 0, 37, 37);
        AnimationSet_SetData(_, notes.animations, "right confirm", 24, 0, 37, 37);
        AnimationSet_SetData(_, notes.animations, "up confirm", 24, 0, 37, 37);
        AnimationSet_SetData(_, notes.animations, "down confirm", 24, 0, 37, 37);
        
        notes.isLoaded = 1;
    }
    return notes.animations;
} 

void AllScenes_DestroyGame(void) {
    if(notes.isLoaded)
        AnimationSet_FreeAll(&notes.animations);
     
    UnloadFont(mainFont);  
}