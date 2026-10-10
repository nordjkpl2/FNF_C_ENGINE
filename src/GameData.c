#include "GameData.h" 
#include "Log.h"

#define IERR Log_Fatal("[ SAVE ] invalid data");
#define READ(type, count, output) if(fread(&output, sizeof(type), count, saveFile) != count) IERR
#define WRITE(type, count, source) if(fwrite(&source, sizeof(type), count, saveFile) != count) Log_Fatal("[ SAVE ] unable to write to save file!");

#define SREAD(type, output) READ(type, 1, output)
#define SWRITE(type, source) WRITE(type, 1, source)

char    game_options_showScore;
char    game_options_bopIcons;
char    game_options_vsync;
char    game_options_fillScreen; // preenche a tela (corta) em vez de barras pretas
char    game_options_botplay; // joga sozinho (fun)
char    game_options_ghost; // 0=On(free) 1=50/50(lane-lock) 2=Off(strict)
char    game_options_scroll; // 0=Normal 1=Downscroll 2=Middlescroll
char    game_options_hitSound; // som ao acertar nota (session-only, default OFF)
int     game_options_scheme;
int     game_options_fps;
float   game_options_zoomFactorUI;
float   game_options_zoomFactorGAME;

static FILE* saveFile;

#define SAVE_VERSION 4
#define SAVE_V0_SIZE 19 // 3 char + 2 int + 2 float (sem versao)
#define SAVE_V1_SIZE 21 // versao + 4 char + 2 int + 2 float
#define SAVE_V2_SIZE 22 // versao + 5 char + 2 int + 2 float
#define SAVE_V3_SIZE 23 // versao + 6 char + 2 int + 2 float

static void GameData_Defaults(void) {
    game_options_showScore  = 1;
    game_options_bopIcons   = 1;
    game_options_vsync      = 0;
    game_options_fillScreen = 0;
    game_options_botplay    = 0;
    game_options_ghost      = 0; // On = free, preserva feel atual
    game_options_scroll     = 0; // 0=Normal 1=Downscroll 2=Middlescroll
    game_options_hitSound   = 0; // OFF por padrao (session-only)
    game_options_fps        = 120;
    game_options_scheme     = 0;
    game_options_zoomFactorUI   = 1;
    game_options_zoomFactorGAME = 1;
}

static void GameData_Checks(void) {
    if(game_options_scheme < 0 || game_options_scheme > 3) IERR 
    if(game_options_fps < 0) IERR
    if(game_options_ghost < 0 || game_options_ghost > 2) IERR
    if(game_options_scroll < 0 || game_options_scroll > 2) IERR
    if(game_options_hitSound < 0 || game_options_hitSound > 1) IERR
}

void GameData_Start(void) {
    // read or set to default (what I am doing currently)

    saveFile = fopen("assets/save.data", "r+b");
    if(saveFile == NULL) { 
        // save not found, default values
        GameData_Defaults();

        saveFile = fopen("assets/save.data", "w+b");
        GameData_Flush();

        return;
    }

    // save v0 nao tinha versao: migra pelo tamanho
    fseek(saveFile, 0, SEEK_END);
    long size = ftell(saveFile);
    fseek(saveFile, 0, SEEK_SET);

    if(size == SAVE_V0_SIZE) {
        SREAD(char, game_options_showScore)
        SREAD(char, game_options_bopIcons)
        SREAD(char, game_options_vsync)
        SREAD(int,  game_options_scheme) 
        SREAD(int,  game_options_fps) 
        SREAD(float,game_options_zoomFactorUI) 
        SREAD(float,game_options_zoomFactorGAME) 
        game_options_fillScreen = 0;
        game_options_botplay = 0;
        game_options_ghost = 0;
        GameData_Checks();
        GameData_Flush(); // regrava ja no formato novo
        return;
    }

    char version = 0;
    SREAD(char, version)
    if(version == 1) {
        // v1 -> v3: faltam botplay + ghost
        SREAD(char, game_options_showScore)
        SREAD(char, game_options_bopIcons)
        SREAD(char, game_options_vsync)
        SREAD(char, game_options_fillScreen)
        SREAD(int,  game_options_scheme)
        SREAD(int,  game_options_fps)
        SREAD(float,game_options_zoomFactorUI)
        SREAD(float,game_options_zoomFactorGAME)
        game_options_botplay = 0;
        game_options_ghost = 0;
        GameData_Checks();
        GameData_Flush();
        return;
    }
    if(version == 2) {
        // v2 -> v3: mesma ordem, so falta o ghost
        SREAD(char, game_options_showScore)
        SREAD(char, game_options_bopIcons)
        SREAD(char, game_options_vsync)
        SREAD(char, game_options_fillScreen)
        SREAD(char, game_options_botplay)
        SREAD(int,  game_options_scheme)
        SREAD(int,  game_options_fps)
        SREAD(float,game_options_zoomFactorUI)
        SREAD(float,game_options_zoomFactorGAME)
        game_options_ghost = 0; // On = free, preserva feel
        GameData_Checks();
        GameData_Flush();
        return;
    }
    if(version == 3) {
        // v3 -> v4: adiciona scroll
        SREAD(char, game_options_showScore)
        SREAD(char, game_options_bopIcons)
        SREAD(char, game_options_vsync)
        SREAD(char, game_options_fillScreen)
        SREAD(char, game_options_botplay)
        SREAD(char, game_options_ghost)
        SREAD(int,  game_options_scheme)
        SREAD(int,  game_options_fps)
        SREAD(float,game_options_zoomFactorUI)
        SREAD(float,game_options_zoomFactorGAME)
        game_options_scroll = 0; // Normal
        GameData_Checks();
        GameData_Flush();
        return;
    }
    if(version != SAVE_VERSION) {
        GameData_Defaults();
        GameData_Flush();
        return;
    }

    // the order has to be the same as the flushing order
    SREAD(char, game_options_showScore)
    SREAD(char, game_options_bopIcons)
    SREAD(char, game_options_vsync)
    SREAD(char, game_options_fillScreen)
    SREAD(char, game_options_botplay)
    SREAD(char, game_options_ghost)
    SREAD(char, game_options_scroll)
    SREAD(int,  game_options_scheme) 
    SREAD(int,  game_options_fps) 
    SREAD(float,game_options_zoomFactorUI) 
    SREAD(float,game_options_zoomFactorGAME) 

    GameData_Checks();
}

const char* game_available_schemes[4] = {"Arrows", "WASD", "DFJK", "ASKL"};
const int   game_scheme_keys[4][4] = {
    {KEY_LEFT, KEY_DOWN, KEY_UP, KEY_RIGHT},
    {KEY_A, KEY_S, KEY_W, KEY_D},
    {KEY_D, KEY_F, KEY_J, KEY_K},
    {KEY_A, KEY_S, KEY_K, KEY_L}
};

void GameData_Flush(void) {  
    rewind(saveFile);

    char version = SAVE_VERSION;
    SWRITE(char, version)
    SWRITE(char, game_options_showScore)
    SWRITE(char, game_options_bopIcons)
    SWRITE(char, game_options_vsync)
    SWRITE(char, game_options_fillScreen)
    SWRITE(char, game_options_botplay)
    SWRITE(char, game_options_ghost)
    SWRITE(char, game_options_scroll)
    SWRITE(int,  game_options_scheme) 
    SWRITE(int,  game_options_fps)
    SWRITE(float,game_options_zoomFactorUI) 
    SWRITE(float,game_options_zoomFactorGAME) 
}

void GameData_Stop(void) {
    GameData_Flush();
    fclose(saveFile);
}