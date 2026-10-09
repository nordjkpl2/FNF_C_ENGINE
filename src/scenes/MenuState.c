#include"scenes/AllScenes.h"

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
    // ok
    {
        cam.target = (Vector2) {1280 / 2, 720 / 2};
        cam.zoom = 1;
    }
    // load bg
    {
        Render_DefaultRGT(&bg);  
        bg.objType = RGT_IMAGE;
        bg.image = Render_LoadTexture("assets/images/menu/menuBG.png");
        bg.scaleX = bg.scaleY = 1.03f;
        bg.position.x = 1280 / 2 - GraphicObject_Sizes(&bg).x / 2;
    }

    // initializing buttons
    {
        buttonAnimations = AnimationSet_LoadAnimations("assets/images/menu/menuOptions.animset", Render_LoadTexture("assets/images/menu/menuOptions.png")); 
        for(int i = 0; i < buttonAnimations.animationCount; i++)
            AnimationSet_SetAnimationData(&buttonAnimations, i, 18, 1);

        // make this only happen the first time the animations are loaded
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
} 

// also serves as update
static void MenuState_Draw([[maybe_unused]] RayScene* scene) { 
    Render_SetCamera(&cam);

    ClearBackground(BLACK);
    Render_DrawGraphicObject(&bg);    

    // detectin key presses
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
                return; // do not execute all the rest!!!!
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
                RayAnimatedObject* oldSelectedButton = &oldSelected->button;

                AnimatedObject_SetAnimation(oldSelectedButton, oldSelected->idle);
                Button_SetPosition(selectedButton);
            }

            if(newSelected < 3) {
                MenuButton* newSelectedM = menuButtons + newSelected;
                RayAnimatedObject* newSelectedButton = &newSelectedM->button;

                AnimatedObject_SetAnimation(newSelectedButton, newSelectedM->selected);
                Button_SetPosition(newSelected);
            }

            selectedButton = newSelected;
        }
    }
    
    // updating bg background
    bg.position.y = Lerp(bg.position.y, -selectedButton * 15, RayGame_DeltaTime() * 10);

    // drawing buttons 
    {
        BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
        for(int i = 0; i < 3; i++) {
            MenuButton* button = menuButtons + i;
            RayAnimatedObject* obj = &button->button; 
            AnimatedObject_UpdateFrame(obj);
            Render_DrawAnimatedObject(obj);
        }
        EndBlendMode();
    }

    // 4th item: MODDING (text, art comes later)
    {
        const char* label = "MODDING";
        const int fontSize = 40;
        const int shadow = 3;
        Vector2 size = MeasureTextEx(mainFont, label, fontSize, 4);
        Vector2 pos = {(1280 - size.x) / 2 + shadow, 655 + shadow};

        Color fill = WHITE;
        Color outline = BLACK;
        if(selectedButton == 3) {
            fill = (Color) {42, 209, 86, 255};
            outline.a = 255;
        } else {
            fill.a = 150;
            outline.a = 100;
        }

        DrawTextEx(mainFont, label, pos, fontSize, 4, outline);
        pos.x -= shadow;
        pos.y -= shadow;
        DrawTextEx(mainFont, label, pos, fontSize, 4, fill);
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