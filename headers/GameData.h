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
extern int      game_options_scheme;
extern int      game_options_fps;
extern float    game_options_zoomFactorUI;
extern float    game_options_zoomFactorGAME; 

void GameData_Start(void);
void GameData_Flush(void);
void GameData_Stop(void);
#endif