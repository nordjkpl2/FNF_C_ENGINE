/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */

#include "scenes/AllScenes.h"
/* //* Main Menu */
typedef struct {
    RayAnimatedObject button;
    int idle;
    int selected;
} MenuButton;

static RayGraphicObject bg;
static MenuButton menuButtons[3];

static RayAnimationHandler buttonAnimations;
static int selectedButton;

static Camera2D cam;

// Posições em cache (calculadas uma única vez)
static Vector2 moddingPos;
static const Vector2 versionPos = { 12.0f, 694.0f }; // 720 - 18 - 8
static const char* const versionText = "FNF C Engine v0.6.4.2";
static const char* const moddingText = "MODDING";

static void Button_SetPosition(int i) {
    MenuButton* button = menuButtons + i;
    RayAnimatedObject* obj = &button->button;
    Vector2 sizes = AnimatedObject_Sizes(obj);
 
    obj->position = (Vector2) {
        .x = 1280 / 2 - sizes.x / 2,
        .y = 200 + i * 175 - sizes.y / 2
    };
}

static void MenuState_Create([[maybe_unused]] RayScene* scene) { 
    // Camera
    {
        cam.target = (Vector2) {1280 / 2, 720 / 2};
        cam.zoom = 1;
    }

    // Load BG
    {
        Render_DefaultRGT(&bg);  
        bg.objType = RGT_IMAGE;
        bg.image = Render_LoadTexture("assets/images/menu/menuBG.png");
        bg.scaleX = bg.scaleY = 1.03f;
        bg.position.x = 1280 / 2 - GraphicObject_Sizes(&bg).x / 2;
    }

    // Initializing buttons
    {
        buttonAnimations = AnimationSet_LoadAnimations("assets/images/menu/menuOptions.animset", Render_LoadTexture("assets/images/menu/menuOptions.png")); 
        for(int i = 0; i < buttonAnimations.animationCount; i++)
            AnimationSet_SetAnimationData(&buttonAnimations, i, 18, 1);

        menuButtons[0] = (MenuButton) {
            .idle = AnimationSet_FindAnimation(&buttonAnimations, "story mode idle"),
            .selected = AnimationSet_FindAnimation(&buttonAnimations, "story mode selected")
        };
        menuButtons[1] = (MenuButton) {
            .idle = AnimationSet_FindAnimation(&buttonAnimations, "freeplay idle "),
            .selected = AnimationSet_FindAnimation(&buttonAnimations, "freeplay selected ")
        };
        menuButtons[2] = (MenuButton) {
            .idle = AnimationSet_FindAnimation(&buttonAnimations, "options idle"),
            .selected = AnimationSet_FindAnimation(&buttonAnimations, "options selected ")
        };
    
        for(int i = 0; i < 3; i++) {
            MenuButton* button = menuButtons + i;
            RayAnimatedObject* obj = &button->button;

            Render_DefaultAnimated(obj);
            obj->animationSet = buttonAnimations;

            if(i == selectedButton)
                AnimatedObject_SetAnimation(obj, button->selected);
            else 
                AnimatedObject_SetAnimation(obj, button->idle);

            Button_SetPosition(i);
        }
    }

    // Pré-calcula a posição centralizada do texto MODDING (zero custo por frame)
    {
        Vector2 size = MeasureTextEx(mainFont, moddingText, 40, 4);
        moddingPos = (Vector2){ (1280 - size.x) / 2.0f, 655.0f };
    }
} 

static void MenuState_Draw([[maybe_unused]] RayScene* scene) { 
    Render_SetCamera(&cam);

    ClearBackground(BLACK);
    Render_DrawGraphicObject(&bg);    

    // Detecção de teclado e navegação
    {
        int newSelected = selectedButton;
        
        if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
            newSelected--;
            if(newSelected < 0)
                newSelected = 0;
        }
        else if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
            newSelected++;
            if(newSelected > 3)
                newSelected = 3;
        }
        else if(IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
            if(selectedButton == 0) {
                StoryState_SetScene();
                return;
            }
            else if(selectedButton == 1) {
                Freeplay_SetScene();
                return;
            }
            else if(selectedButton == 2) {
                OptionsMenu_SetScene();
                return;
            }
            else if(selectedButton == 3) {
                RayGame_ToggleMusic(0);
                ModdingState_SetScene();
                return;
            }
        }

        if(selectedButton != newSelected) {
            if(selectedButton < 3) {
                MenuButton* oldSelected = menuButtons + selectedButton;
                AnimatedObject_SetAnimation(&oldSelected->button, oldSelected->idle);
                Button_SetPosition(selectedButton);
            }

            if(newSelected < 3) {
                MenuButton* newSelectedM = menuButtons + newSelected;
                AnimatedObject_SetAnimation(&newSelectedM->button, newSelectedM->selected);
                Button_SetPosition(newSelected);
            }

            selectedButton = newSelected;
        }
    }
    
    // Atualização suave da posição Y do fundo
    bg.position.y = Lerp(bg.position.y, -selectedButton * 15, RayGame_DeltaTime() * 10);

    // Atualiza lógica dos frames antes de abrir o blend mode
    for(int i = 0; i < 3; i++) {
        AnimatedObject_UpdateFrame(&menuButtons[i].button);
    }

    // Desenha os botões
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    for(int i = 0; i < 3; i++) {
        Render_DrawAnimatedObject(&menuButtons[i].button);
    }
    EndBlendMode();

    // 4th item: MODDING (renderização com posições pré-computadas)
    {
        const int fontSize = 40;
        const int shadow = 3;

        Color fill = WHITE;
        Color outline = BLACK;
        if(selectedButton == 3) {
            fill = (Color) {42, 209, 86, 255};
            outline.a = 255;
        } else {
            fill.a = 150;
            outline.a = 100;
        }

        DrawTextEx(mainFont, moddingText, (Vector2){moddingPos.x + shadow, moddingPos.y + shadow}, fontSize, 4, outline);
        DrawTextEx(mainFont, moddingText, moddingPos, fontSize, 4, fill);
    }

    // Watermark de versão no canto inferior esquerdo
    {
        const int fontSize = 18;
        const float shadow = 1.5f;

        DrawTextEx(mainFont, versionText, (Vector2){versionPos.x + shadow, versionPos.y + shadow}, fontSize, 1, BLACK);
        DrawTextEx(mainFont, versionText, versionPos, fontSize, 1, (Color){ 255, 255, 255, 180 });
    }

    Render_StopCamera();
}

static void MenuState_Destroy([[maybe_unused]] RayScene* scene) {
    AnimationSet_FreeAll(&buttonAnimations);
    UnloadTexture(bg.image);
}

static RayScene menuState;
Scene_MakeSceneCode(
    menuState, 
    MenuState_SetScene,
    MenuState_Create,
    NULL,
    MenuState_Draw,
    MenuState_Destroy
)