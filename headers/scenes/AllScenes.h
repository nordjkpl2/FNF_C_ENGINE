#ifndef TESTSCENE_H
#define TESTSCENE_H

#include "GameData.h"
#include "BeatManager.h"
#include "scenes/Week.h"

void PlayState_SetScene(void);
void PlayState_SetSongDir(const char* dir);
void PlayState_SetSongDirKeep(const char* dir);
void PlayState_SetReturnEditor(char on);
void PlayState_SetReturnWeek(char on);
void PlayState_SetDiff(const char* diff);
void PlayState_SetCharsOverride(const char* p2);
int PlayState_GetScore(void);
void LoadingState_SetScene(void);
void LoadingState_SetSongDir(const char* dir);
void ModdingState_SetScene(void);
void ChartEditor_SetScene(void);
void ChartEditor_SetSongDir(const char* dir);
void StoryState_SetScene(void);
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
RayAnimationHandler Cache_GetNoteSkin(const char* skinName, const char* songDir);
RayAnimationHandler* Cache_GetDefaultNoteSkinPtr(void);
void Cache_UnloadCustomNoteSkins(void);
#endif
