#ifndef GAME_DATA_H
#define GAME_DATA_H

#include "raylib.h"
#include <stdlib.h>
#include <stdio.h>

extern const char* game_available_schemes[4];
extern const int   game_scheme_keys[4][4];

extern char     game_options_showScore;
extern char     game_options_bopIcons;
extern char     game_options_vsync;
extern char     game_options_fillScreen;
extern char     game_options_botplay;
extern char     game_options_ghost; // 0=On(free) 1=50/50(lane-lock) 2=Off(strict)
extern char     game_options_scroll; // 0=Normal 1=Downscroll 2=Middlescroll
extern char     game_options_hitSound; // som ao acertar nota
extern int      game_options_scheme;
extern int      game_options_fps;
extern float    game_options_zoomFactorUI;
extern float    game_options_zoomFactorGAME; 

void GameData_Start(void);
void GameData_Flush(void);
void GameData_Stop(void);
#endif