#include "GameData.h" 

#define IERR {puts("\n[ SAVE ] invalid data"); exit(1);}
#define READ(type, count, output) if(fread(&output, sizeof(type), count, saveFile) != count) IERR
#define WRITE(type, count, source) if(fwrite(&source, sizeof(type), count, saveFile) != count) { puts("\n[ SAVE ] unable to write to save file!"); exit(1); }

#define SREAD(type, output) READ(type, 1, output)
#define SWRITE(type, source) WRITE(type, 1, source)

char    game_options_showScore;
char    game_options_bopIcons;
char    game_options_vsync;
int     game_options_scheme;
int     game_options_fps;
float   game_options_zoomFactorUI;
float   game_options_zoomFactorGAME;

static FILE* saveFile;

void GameData_Start(void) {
    // read or set to default (what I am doing currently)

    saveFile = fopen("assets/save.data", "r+b");
    if(saveFile == NULL) { 
        // save not found, default values

        game_options_showScore  = 1;
        game_options_bopIcons   = 1;
        game_options_vsync      = 0;
        game_options_fps        = 120;
        game_options_scheme     = 0;
        game_options_zoomFactorUI   = 1;
        game_options_zoomFactorGAME = 1;

        saveFile = fopen("assets/save.data", "w+b");
        GameData_Flush();

        return;
    }

    // the order has to be the same as the flushing order
    SREAD(char, game_options_showScore)
    SREAD(char, game_options_bopIcons)
    SREAD(char, game_options_vsync)
    SREAD(int,  game_options_scheme) 
    SREAD(int,  game_options_fps) 
    SREAD(float,game_options_zoomFactorUI) 
    SREAD(float,game_options_zoomFactorGAME) 

    // some checks
    if(game_options_scheme < 0 || game_options_scheme > 3) IERR 
    if(game_options_fps < 0) IERR
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

    SWRITE(char, game_options_showScore)
    SWRITE(char, game_options_bopIcons)
    SWRITE(char, game_options_vsync)
    SWRITE(int,  game_options_scheme) 
    SWRITE(int,  game_options_fps)
    SWRITE(float,game_options_zoomFactorUI) 
    SWRITE(float,game_options_zoomFactorGAME) 
}

void GameData_Stop(void) {
    GameData_Flush();
    fclose(saveFile);
}