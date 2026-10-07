#include "scenes/AllScenes.h"

typedef struct {
    char* text;
    enum OptionValueType    { 
        OPTION_VALUE_INT, 
        OPTION_VALUE_FLOAT, 
        OPTION_VALUE_BOOL, 
        OPTION_VALUE_LIST,
        OPTION_VALUE_HEADER /*this isnt actually an option*/ 
    } type;

    const char** options;  // used with the list type
    float value;    // cast this to whatever you want ^
    float min, max; // does not apply to bools obviously (max is used as the number of elements in the list btw)
    float step;
    float altStep;
 
    char active;
} Option;

// THIS IS THE OPTION LAYOUT
typedef enum {
    __OPTION_HEADER_WIN,
    OPTION_FPS,
    OPTION_UNCAPPED_FPS,
    OPTION_VSYNC,
    __OPTION_HEADER_GAMEPLAY,
    OPTION_KEYBOARD_SCHEME,
    __OPTION_HEADER_UI,
    OPTION_SHOW_SCORE,
    OPTION_BOPPING_ICONS,
    OPTION_ZOOMFACTOR_UI,
    OPTION_ZOOMFACTOR_GAME
} OPTION_ORDER;

#define OPTION_COUNT 11

// THIS HAS TO HAVE THE SAME ORDER AS THE OPTION_ORDER ENUM
static Option options[OPTION_COUNT] = {
    {
        .text   = "[ Window Options ]",
        .type   = OPTION_VALUE_HEADER
    },
    {
        .text   = "Framerate",
        .type   = OPTION_VALUE_INT, 
        .min    = 60,
        .max    = 2000,
        .step   = 10,  
        .altStep= 100,
    },
    {
        .text   = "Uncapped FPS",
        .type   = OPTION_VALUE_BOOL, 
    },
    {
        .text   = "V-SYNC",
        .type   = OPTION_VALUE_BOOL
    },
    {
        .text   = "[ Gameplay Options ]",
        .type   = OPTION_VALUE_HEADER
    },
    {
        .text   = "Scheme",
        .type   = OPTION_VALUE_LIST,
        .options = game_available_schemes,
        .step   = 1,
        .max    = 3,
        .min    = 0
    },
    {
        .text   = "[ Other Options ]",
        .type   = OPTION_VALUE_HEADER
    },
    {
        .text   = "Show Score",
        .type   = OPTION_VALUE_BOOL,  
    },
    {
        .text   = "Bopping Icons",
        .type   = OPTION_VALUE_BOOL
    },
    {
        .text   = "UI beat zoom",
        .type   = OPTION_VALUE_FLOAT, 
        .max    = 3,
        .min    = 0,
        .step   = 0.1f, 
        .altStep = 1
    },
    {
        .text   = "Game beat zoom",
        .type   = OPTION_VALUE_FLOAT, 
        .max    = 3,
        .min    = 0,
        .step   = 0.1f, 
        .altStep = 1
    },
};

static float offset; 
static float holdTime; 
static int currentOption;

static Camera2D cam;
static RayGraphicObject bg;

static void ChangedNumValue() {
    Option* option = options + currentOption;
    
    if(option->value > option->max)
        option->value = option->max; 
    else if(option->value < option->min)
        option->value = option->min;  

    if(currentOption == OPTION_FPS) { 
        if(option->value > 500) {
            if(option->value < 700) option->value = 900;
            else if(option->value < 900) option->value = 500;

            // weird framerates when 500 < targetFps < 890
        }

        RayGame_SetFPS((int) option->value); 
    }
    else if(currentOption == OPTION_KEYBOARD_SCHEME)
        game_options_scheme = (int) option->value;
    else if(currentOption == OPTION_ZOOMFACTOR_GAME)
        game_options_zoomFactorGAME = option->value;
    else if(currentOption == OPTION_ZOOMFACTOR_UI)
        game_options_zoomFactorUI = option->value;
}

static void ChangedBoolValue() {
    Option* option = options + currentOption;

    if(currentOption == OPTION_UNCAPPED_FPS)  { 
        RayGame_SetFPS(option->value ? 0 : ((int) options[OPTION_FPS].value));
        options[OPTION_FPS].active = !option->value;
    }
    else if(currentOption == OPTION_SHOW_SCORE)
        game_options_showScore = !game_options_showScore;
    else if(currentOption == OPTION_BOPPING_ICONS) 
        game_options_bopIcons = !game_options_bopIcons;
    else if(currentOption == OPTION_VSYNC) {
        RayGame_SetVsync(option->value); 
        
        if(option->value) {
            options[OPTION_UNCAPPED_FPS].active = 0;
            options[OPTION_FPS].active = 0;
        }
        else {
            options[OPTION_UNCAPPED_FPS].active = 1;
            options[OPTION_FPS].active = !options[OPTION_UNCAPPED_FPS].value;
        }
    }
}

static void OptionsMenu_Create([[maybe_unused]] RayScene* scene) {
    holdTime = 0;
    
    Render_DefaultRGT(&bg);

    bg.objType = RGT_IMAGE;
    bg.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");

    Render_DefaultCamera(&cam);

    // default option values
    {
        for(int i = 0; i < OPTION_COUNT; i++)
            options[i].active = 1;  

        options[OPTION_FPS].value = RayGame_GetTargetFPS(); 
        if(options[OPTION_FPS].value == 0) {
            options[OPTION_FPS].active = 0;
            options[OPTION_FPS].value = 120;
        }

        options[OPTION_UNCAPPED_FPS].value = RayGame_GetTargetFPS() == 0;
        options[OPTION_SHOW_SCORE].value = game_options_showScore;
        options[OPTION_BOPPING_ICONS].value = game_options_bopIcons;
        options[OPTION_VSYNC].value = game_options_vsync;
        options[OPTION_KEYBOARD_SCHEME].value = game_options_scheme;
        options[OPTION_ZOOMFACTOR_GAME].value = game_options_zoomFactorGAME;
        options[OPTION_ZOOMFACTOR_UI].value = game_options_zoomFactorUI;
    }
}

static void OptionsMenu_Draw([[maybe_unused]] RayScene* scene) { 
    if(IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE)) {
        GameData_Flush(); // save data
        MenuState_SetScene();
        return;
    }

    // update
    {
        offset = Lerp(offset, currentOption, RayGame_DeltaTime() * 10.0f);
        if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
            currentOption++;
            if(currentOption >= OPTION_COUNT)
                currentOption = 0;
        }
        else if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
            currentOption--;
            
            if(currentOption < 0)
                currentOption = OPTION_COUNT - 1; 
        }

        Option* option = options + currentOption;
        if(!option->active || option->type == OPTION_VALUE_HEADER)
            goto __SOMETHING__GOTO_INSTRUCTION_01_CANNOT_USE_THIS_OPTION;

        if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            holdTime = 0; // what the dog doin
            if(option->type == OPTION_VALUE_BOOL) {
                option->value = !option->value; 
                ChangedBoolValue();
            }
        }
        else if(option->type != OPTION_VALUE_BOOL) {
            float speed = IsKeyDown(KEY_LEFT_ALT) ? option->altStep : option->step;
            if(IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) {
                option->value -= speed; 
                ChangedNumValue();
            }
            else if(IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) {
                option->value += speed;
                ChangedNumValue();
            } 
        }  

        __SOMETHING__GOTO_INSTRUCTION_01_CANNOT_USE_THIS_OPTION:
    }

    Render_SetCamera(&cam);
    {
        Render_DrawGraphicObject(&bg);
        const int size = 100;
        const int fontPadding = 30;
        const int fontSize = 70;
        
        const int headerPadding = 10;
        const int headerSize = 90;

        const int shadow = 5;

        float y = -offset * size;

        Color outlineColor = BLACK;
        for(int i = 0; i < OPTION_COUNT; i++) { 
            Vector2 pos = {150, 720 / 2 + shadow + y};

            if(options[i].type == OPTION_VALUE_HEADER) { 
                pos.x = 50;
                y += headerPadding + headerSize;

                DrawTextEx(mainFont, options[i].text, pos, fontSize, 5, BLACK);
                pos.x -= shadow;
                pos.y -= shadow;
                DrawTextEx(mainFont, options[i].text, pos, fontSize, 5, YELLOW); 
            }
            else {  
                Color clr = WHITE;
                y += fontPadding + fontSize;

                const char* txt;
                if(!options[i].active) {
                    txt = TextFormat("%s", options[i].text);
                    clr = RED;
                }
                else if(options[i].type == OPTION_VALUE_INT)
                    txt = TextFormat("%s: < %d >", options[i].text, (int) options[i].value);
                else if(options[i].type == OPTION_VALUE_FLOAT)
                    txt = TextFormat("%s: < %.2f >", options[i].text, options[i].value);
                else if(options[i].type == OPTION_VALUE_BOOL)
                    txt = TextFormat("%s: [ %s ]", options[i].text, options[i].value ? "ON" : "OFF");
                else if(options[i].type == OPTION_VALUE_LIST)
                    txt = TextFormat("%s: << %s >>", options[i].text, options[i].options[(int) options[i].value]);

                clr.a = 150;
                outlineColor.a = 100; 

                if(currentOption == i)
                    clr.a = outlineColor.a = 255;

                DrawTextEx(mainFont, txt, pos, fontSize, 5, outlineColor);
                pos.x -= shadow;
                pos.y -= shadow;
                DrawTextEx(mainFont, txt, pos, fontSize, 5, clr);  
            }
        }
    }
    Render_StopCamera();
}
static void OptionsMenu_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bg.image);
}

static RayScene scene;
Scene_MakeSceneCode(scene, OptionsMenu_SetScene, OptionsMenu_Create, NULL, OptionsMenu_Draw, OptionsMenu_Destroy)