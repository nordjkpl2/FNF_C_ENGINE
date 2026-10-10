#include"scenes/AllScenes.h"
#include"scenes/ModImport.h"

// Modding hub: freeplay-style clickable list.
// Editors land here as tabs; stubs show EM BREVE until implemented.
typedef enum {
    MOD_LIST,
    MOD_IMPORT,
    MOD_STUB
} ModdingMode;

static const char* tabs[] = {
    "CHART EDITOR",
    "CHARACTER EDITOR",
    "IMPORT MOD"
};
#define TAB_COUNT 3

static ModdingMode mode;
static int selected;
static float offset;
static const char* stubTitle;

static RayGraphicObject bg;
static Camera2D cam;

// importador: pasta do mod -> assets/mods/<nome>
static char impPath[256] = {0};
static char impMsg[256] = {0};
static int impMods = 0;
static int impSongs = 0;
static long impKB = 0;
static char impConfirmDel = 0;

static void Imp_RefreshUsage(void) {
    ModImportUsage u;
    ModImport_ScanMods("assets/mods", &u);
    impMods = u.mods;
    impSongs = u.songs;
    impKB = u.bytes / 1024;
}

static Color outlineColor = {0, 0, 0, 0};
static Color fontColor = {255, 255, 255, 255};

static void ModdingState_Create([[maybe_unused]] RayScene* scene) {
    Render_DefaultRGT(&bg);

    bg.objType = RGT_IMAGE;
    bg.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");

    cam.target = (Vector2) {1280 / 2, 720 / 2};
    cam.zoom = 1;

    mode = MOD_LIST;
    selected = 0;
    offset = 0;
    stubTitle = NULL;
    impMsg[0] = 0;
}

static void ModdingState_Draw([[maybe_unused]] RayScene* scene) {
    if(mode == MOD_IMPORT) {
        // digita a pasta (BACKSPACE apaga; ESC volta)
        int ch;
        while((ch = GetCharPressed()) > 0) {
            size_t len = strlen(impPath);
            if(ch >= 32 && ch < 127 && len + 1 < sizeof(impPath)) {
                impPath[len] = (char)ch;
                impPath[len + 1] = 0;
            }
        }
        if(IsKeyPressed(KEY_BACKSPACE) && impPath[0] != 0)
            impPath[strlen(impPath) - 1] = 0;
        int ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if(ctrl && IsKeyPressed(KEY_V)) {
            const char* clip = GetClipboardText();
            if(clip != NULL) {
                size_t len = strlen(impPath);
                for(size_t i = 0; clip[i] != 0 && len + 1 < sizeof(impPath); i++) {
                    if(clip[i] >= 32 && clip[i] < 127)
                        impPath[len++] = clip[i];
                }
                impPath[len] = 0;
            }
        }
        if(IsKeyPressed(KEY_ESCAPE)) {
            mode = MOD_LIST;
            impConfirmDel = 0;
            return;
        }
        if(IsKeyPressed(KEY_DELETE) && impPath[0] == 0) {
            if(!impConfirmDel && impMods > 0) {
                impConfirmDel = 1;
                snprintf(impMsg, sizeof(impMsg), "DEL de novo = APAGA %d mods (%ld KB). ESC cancela.", impMods, impKB);
            } else if(impConfirmDel) {
                int removed = 0;
                long freed = 0;
                ModImport_DeleteAll("assets/mods", impMsg, sizeof(impMsg), &removed, &freed);
                impMsg[sizeof(impMsg) - 1] = 0;
                impConfirmDel = 0;
                Imp_RefreshUsage();
            }
            return;
        }
        if(impConfirmDel && IsKeyPressed(KEY_ENTER)) {
            impConfirmDel = 0; // ENTER com path vazio so desarma
        }
        if((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && impPath[0] != 0) {
            // nome do mod = ultima pasta do caminho
            char tmp[256];
            strncpy(tmp, impPath, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = 0;
            size_t L = strlen(tmp);
            while(L > 0 && (tmp[L - 1] == '/' || tmp[L - 1] == '\\'))
                tmp[--L] = 0;
            const char* s1 = strrchr(tmp, '/');
            const char* s2 = strrchr(tmp, '\\');
            const char* base = (s1 > s2) ? s1 : s2;
            base = (base != NULL) ? base + 1 : tmp;
            ModImportStats st;
            if(!ModImport_Run(tmp, "assets/mods", base, impMsg, sizeof(impMsg), &st))
                impMsg[sizeof(impMsg) - 1] = 0;
            impConfirmDel = 0;
            Imp_RefreshUsage();
        }

        Render_SetCamera(&cam);
        Render_DrawGraphicObject(&bg);
        DrawTextEx(mainFont, "IMPORT MOD", (Vector2) {150, 120}, 40, 4, fontColor);
        DrawTextEx(mainFont, "pasta do mod (a que tem songs/ data/ weeks/)", (Vector2) {150, 190}, 20, 1, fontColor);
        DrawRectangle(150, 220, 980, 40, (Color) {20, 20, 20, 255});
        DrawRectangleLines(150, 220, 980, 40, GREEN);
        DrawTextEx(mainFont, impPath[0] != 0 ? impPath : "C:/.../mods", (Vector2) {160, 230}, 20, 1, impPath[0] != 0 ? WHITE : (Color) {120, 120, 120, 255});
        DrawTextEx(mainFont, "ENTER importa p/ assets/mods/<nome>   CTRL+V cola   ESC volta", (Vector2) {150, 280}, 20, 1, YELLOW);
        {
            char ubuf[128];
            snprintf(ubuf, sizeof(ubuf), "mods: %d   musicas: %d   disco: %ld KB   (DEL 2x apaga tudo)", impMods, impSongs, impKB);
            DrawTextEx(mainFont, ubuf, (Vector2) {150, 370}, 20, 1, impConfirmDel ? RED : fontColor);
        }
        DrawTextEx(mainFont, "traz: charts+Inst/Voices, weeks, stages lua+json, imgs, chars+animset, icones.", (Vector2) {150, 310}, 20, 1, fontColor);
        DrawTextEx(mainFont, "tudo dentro de assets/mods/<nome> (apagar a pasta desfaz).", (Vector2) {150, 340}, 20, 1, fontColor);
        if(impMsg[0] != 0)
            DrawTextEx(mainFont, impMsg, (Vector2) {150, 400}, 20, 1, YELLOW);
        Render_StopCamera();
        return;
    }

    if(IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        if(mode == MOD_STUB) {
            mode = MOD_LIST;
            return;
        }
        RayGame_ResetMusic();
        RayGame_ToggleMusic(1);
        MenuState_SetScene();
        return;
    }

    if(mode == MOD_LIST) {
        if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
            selected++;
            if(selected >= TAB_COUNT)
                selected = 0;
        }

        if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
            selected--;
            if(selected < 0)
                selected = TAB_COUNT - 1;
        }

        if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            if(selected == 0) {
                ChartEditor_SetScene();
                return;
            }
            if(selected == 2) {
                mode = MOD_IMPORT;
                impConfirmDel = 0;
                Imp_RefreshUsage();
                return;
            }
            stubTitle = tabs[selected];
            mode = MOD_STUB;
            return;
        }
    }

    offset = Lerp(offset, (float)selected, RayGame_DeltaTime() * 10.0f);

    Render_SetCamera(&cam);
    Render_DrawGraphicObject(&bg);

    if(mode == MOD_STUB) {
        const char* msg = "EM BREVE";
        Vector2 size = MeasureTextEx(mainFont, msg, 80, 5);
        DrawTextEx(mainFont, msg, (Vector2) {1280 / 2 - size.x / 2, 720 / 2 - size.y / 2}, 80, 5, fontColor);
        if(stubTitle != NULL) {
            Vector2 sub = MeasureTextEx(mainFont, stubTitle, 32, 4);
            DrawTextEx(mainFont, stubTitle, (Vector2) {1280 / 2 - sub.x / 2, 720 / 2 + 80}, 32, 4, fontColor);
        }
        Render_StopCamera();
        return;
    }

    for(int i = 0; i < TAB_COUNT; i++) {
        const int fontSize = 80;
        const int shadow = 5;

        float d = fabsf((float)i - offset);

        Vector2 pos = {(float)(shadow + 150 + d * -30), (float)(shadow + 720 / 2 + (i - offset) * (10 + fontSize))};

        fontColor.a = 150;
        outlineColor.a = 100;

        if(selected == i)
            fontColor.a = outlineColor.a = 255;

        DrawTextEx(mainFont, tabs[i], pos, fontSize, 5, outlineColor);
        pos.x -= shadow;
        pos.y -= shadow;
        DrawTextEx(mainFont, tabs[i], pos, fontSize, 5, fontColor);

        // agulha verde no selecionado (2 draws a menos que texto: 1 triangulo)
        if(selected == i) {
            Vector2 np = {pos.x - 72, pos.y + 14};
            DrawTriangle((Vector2) {np.x, np.y}, (Vector2) {np.x, np.y + 44},
                (Vector2) {np.x + 36, np.y + 22}, (Color) {80, 220, 100, 255});
        }
    }
    Render_StopCamera();
}

static void ModdingState_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bg.image);
}

static RayScene moddingScene;
Scene_MakeSceneCode(
    moddingScene,
    ModdingState_SetScene,
    ModdingState_Create,
    NULL,
    ModdingState_Draw,
    ModdingState_Destroy
)
