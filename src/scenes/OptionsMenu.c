/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */

#include "scenes/AllScenes.h"
/* // * Código dos Textos da aba de Opções, OUTD 18 of10 2026 */
typedef struct {
    char* text;
    enum OptionValueType { 
        OPTION_VALUE_INT, 
        OPTION_VALUE_FLOAT, 
        OPTION_VALUE_BOOL, 
        OPTION_VALUE_LIST,
        OPTION_VALUE_HEADER 
    } type;

    const char** options;
    float value;
    float min, max;
    float step;
    float altStep;
    char active;

    // CACHE - performance absoluta
    char cachedText[96];
    bool dirty;
} Option;

typedef enum {
    __OPTION_HEADER_WIN,
    OPTION_FPS,
    OPTION_UNCAPPED_FPS,
    OPTION_VSYNC,
    __OPTION_HEADER_GAMEPLAY,
    OPTION_KEYBOARD_SCHEME,
    OPTION_SCROLL_STYLE,
    OPTION_GHOST,
    OPTION_HITSOUND,
    __OPTION_HEADER_UI,
    OPTION_SHOW_SCORE,
    OPTION_BOPPING_ICONS,
    OPTION_ZOOMFACTOR_UI,
    OPTION_ZOOMFACTOR_GAME,
    OPTION_FILL_SCREEN,
    OPTION_BOTPLAY
} OPTION_ORDER;

#define OPTION_COUNT 16

static const char* ghost_names[3] = {"On", "50/50", "Off"};
static const char* scroll_names[3] = {"Normal", "Downscroll", "Middlescroll"};

static Option options[OPTION_COUNT] = {
    {.text = "[ Window Options ]", .type = OPTION_VALUE_HEADER},
    {.text = "Framerate", .type = OPTION_VALUE_INT, .min=60, .max=2000, .step=10, .altStep=100},
    {.text = "Uncapped FPS", .type = OPTION_VALUE_BOOL},
    {.text = "V-SYNC", .type = OPTION_VALUE_BOOL},
    {.text = "[ Gameplay Options ]", .type = OPTION_VALUE_HEADER},
    {.text = "Scheme", .type = OPTION_VALUE_LIST, .options=game_available_schemes, .step=1, .max=3, .min=0},
    {.text = "Scroll Style", .type = OPTION_VALUE_LIST, .options=scroll_names, .step=1, .max=2, .min=0},
    {.text = "Ghost Tapping", .type = OPTION_VALUE_LIST, .options=ghost_names, .step=1, .max=2, .min=0},
    {.text = "Hit Sound", .type = OPTION_VALUE_BOOL},
    {.text = "[ Other Options ]", .type = OPTION_VALUE_HEADER},
    {.text = "Show Score", .type = OPTION_VALUE_BOOL},
    {.text = "Bopping Icons", .type = OPTION_VALUE_BOOL},
    {.text = "UI beat zoom", .type = OPTION_VALUE_FLOAT, .max=3, .min=0, .step=0.1f, .altStep=1},
    {.text = "Game beat zoom", .type = OPTION_VALUE_FLOAT, .max=3, .min=0, .step=0.1f, .altStep=1},
    {.text = "Stretch", .type = OPTION_VALUE_BOOL},
    {.text = "Botplay", .type = OPTION_VALUE_BOOL},
};

static float offset; 
static int currentOption;
static Camera2D cam;
static RayGraphicObject bg;

// --- CACHE: só formata quando suja ---
static inline void Option_RebuildText(Option* o) {
    if (o->type == OPTION_VALUE_HEADER) return;
    if (!o->active) {
        snprintf(o->cachedText, sizeof(o->cachedText), "%s", o->text);
        return;
    }
    switch(o->type) {
        case OPTION_VALUE_INT:  snprintf(o->cachedText, sizeof(o->cachedText), "%s: < %d >", o->text, (int)o->value); break;
        case OPTION_VALUE_FLOAT:snprintf(o->cachedText, sizeof(o->cachedText), "%s: < %.2f >", o->text, (double)o->value); break;
        case OPTION_VALUE_BOOL: snprintf(o->cachedText, sizeof(o->cachedText), "%s: [ %s ]", o->text, o->value ? "ON" : "OFF"); break;
        case OPTION_VALUE_LIST: snprintf(o->cachedText, sizeof(o->cachedText), "%s: << %s >>", o->text, o->options[(int)o->value]); break;
        default: break;
    }
    o->dirty = false;
}

static inline void ChangedNumValue() {
    Option* o = &options[currentOption];
    o->value = Clamp(o->value, o->min, o->max);

    if(currentOption == OPTION_FPS) { 
        if(o->value > 500 && o->value < 900) o->value = (o->value < 700) ? 900 : 500;
        RayGame_SetFPS((int)o->value); 
    }
    else if(currentOption == OPTION_KEYBOARD_SCHEME) game_options_scheme = (int)o->value;
    else if(currentOption == OPTION_SCROLL_STYLE) game_options_scroll = (char)(int)o->value;
    else if(currentOption == OPTION_GHOST) game_options_ghost = (char)(int)o->value;
    else if(currentOption == OPTION_ZOOMFACTOR_GAME) game_options_zoomFactorGAME = o->value;
    else if(currentOption == OPTION_ZOOMFACTOR_UI) game_options_zoomFactorUI = o->value;
    
    o->dirty = true;
    Option_RebuildText(o);
}

static inline void ChangedBoolValue() {
    Option* o = &options[currentOption];
    switch(currentOption) {
        case OPTION_UNCAPPED_FPS: 
            RayGame_SetFPS(o->value ? 0 : (int)options[OPTION_FPS].value);
            options[OPTION_FPS].active = !o->value;
            options[OPTION_FPS].dirty = true;
            Option_RebuildText(&options[OPTION_FPS]);
            break;
        case OPTION_SHOW_SCORE: game_options_showScore = o->value; break;
        case OPTION_BOPPING_ICONS: game_options_bopIcons = o->value; break;
        case OPTION_FILL_SCREEN: game_options_fillScreen = o->value; break;
        case OPTION_BOTPLAY: game_options_botplay = o->value; break;
        case OPTION_HITSOUND: game_options_hitSound = o->value; break;
        case OPTION_VSYNC:
            RayGame_SetVsync(o->value); 
            options[OPTION_UNCAPPED_FPS].active = !o->value;
            options[OPTION_FPS].active = !o->value && !options[OPTION_UNCAPPED_FPS].value;
            break;
    }
    o->dirty = true;
    Option_RebuildText(o);
}

// Navegação que já pula HEADER e inativo - elimina o goto
static inline void OptionsMenu_Move(int dir) {
    for(int i=0; i < OPTION_COUNT; i++) {
        currentOption = (currentOption + dir + OPTION_COUNT) % OPTION_COUNT;
        if(options[currentOption].type != OPTION_VALUE_HEADER && options[currentOption].active) break;
    }
}

static void OptionsMenu_Create([[maybe_unused]] RayScene* scene) {
    Render_DefaultRGT(&bg);
    bg.objType = RGT_IMAGE;
    bg.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");
    Render_DefaultCamera(&cam);

    for(int i = 0; i < OPTION_COUNT; i++) {
        options[i].active = 1;  
        options[i].dirty = true;
    }
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
    options[OPTION_SCROLL_STYLE].value = game_options_scroll;
    options[OPTION_GHOST].value = game_options_ghost;
    options[OPTION_HITSOUND].value = game_options_hitSound;
    options[OPTION_ZOOMFACTOR_GAME].value = game_options_zoomFactorGAME;
    options[OPTION_ZOOMFACTOR_UI].value = game_options_zoomFactorUI;
    options[OPTION_FILL_SCREEN].value = game_options_fillScreen;
    options[OPTION_BOTPLAY].value = game_options_botplay;

    for(int i=0; i<OPTION_COUNT; i++) Option_RebuildText(&options[i]);

    // garante começar em opção válida
    if(options[currentOption].type == OPTION_VALUE_HEADER || !options[currentOption].active)
        OptionsMenu_Move(1);
}

static void OptionsMenu_Draw([[maybe_unused]] RayScene* scene) { 
    if(IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE)) {
        GameData_Flush();
        MenuState_SetScene();
        return;
    }

    // --- UPDATE ---
    offset = Lerp(offset, (float)currentOption, RayGame_DeltaTime() * 10.0f);
    
    if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) OptionsMenu_Move(1);
    else if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) OptionsMenu_Move(-1);

    Option* cur = &options[currentOption];
    if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
        if(cur->type == OPTION_VALUE_BOOL) {
            cur->value = !cur->value; 
            ChangedBoolValue();
        }
    } else if(cur->type != OPTION_VALUE_BOOL && cur->type != OPTION_VALUE_HEADER) {
        float speed = IsKeyDown(KEY_LEFT_ALT) ? cur->altStep : cur->step;
        if(IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) { cur->value -= speed; ChangedNumValue(); }
        else if(IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) { cur->value += speed; ChangedNumValue(); } 
    }

    // --- DRAW ---
    Render_SetCamera(&cam);
    {
        Render_DrawGraphicObject(&bg);
        
        // constantes fora do loop
        const int fontSize = 70;
        const int headerSize = 90;
        const int fontPadding = 30;
        const int headerPadding = 10;
        const int shadow = 5;
        const float spacing = 5.0f;

        float y = -offset * 100.0f; // 100 = size antigo

        for(int i = 0; i < OPTION_COUNT; i++) { 
            Option* o = &options[i];
            const bool isHeader = (o->type == OPTION_VALUE_HEADER);
            const float h = isHeader ? (headerSize + headerPadding) : (fontSize + fontPadding);
            
            float drawY = 720.0f / 2.0f + y;
            
            // CULLING VERTICAL CORRETO (header e opções)
            if(drawY < -100.0f || drawY > 850.0f) {
                y += h;
                continue;
            }

            Vector2 pos = {isHeader ? 50.0f : 150.0f, drawY + shadow};

            if(isHeader) {
                DrawTextEx(mainFont, o->text, pos, headerSize, spacing, BLACK);
                pos.x -= shadow; pos.y -= shadow;
                DrawTextEx(mainFont, o->text, pos, headerSize, spacing, YELLOW);
            } else {
                if(o->dirty) Option_RebuildText(o);
                
                Color clr = WHITE;
                Color outline = BLACK;
                if(!o->active) clr = RED;
                
                clr.a = (currentOption == i) ? 255 : 150;
                outline.a = (currentOption == i) ? 255 : 100;

                DrawTextEx(mainFont, o->cachedText, pos, fontSize, spacing, outline);
                pos.x -= shadow; pos.y -= shadow;
                DrawTextEx(mainFont, o->cachedText, pos, fontSize, spacing, clr);
            }
            y += h;
        }
    }
    Render_StopCamera();
}

static void OptionsMenu_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bg.image);
}

static RayScene scene;
Scene_MakeSceneCode(scene, OptionsMenu_SetScene, OptionsMenu_Create, NULL, OptionsMenu_Draw, OptionsMenu_Destroy)