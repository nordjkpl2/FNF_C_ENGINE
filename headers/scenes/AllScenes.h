#ifndef TESTSCENE_H
#define TESTSCENE_H

#include "GameData.h"
#include "BeatManager.h"

void PlayState_SetScene(void);
void PlayState_SetSong(char* song);
void OptionsMenu_SetScene(void);
void Freeplay_SetScene(void); 
void MenuState_SetScene(void); 
void TitleState_SetScene(void);

extern Vector2 VECTOR_ZERO;
extern Font mainFont;  

void AllScenes_StartGame(void);
void AllScenes_DestroyGame(void);
/* cache */
RayAnimationHandler Cache_GetNoteAnimations(void);
#endif
