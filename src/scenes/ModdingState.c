/*
 * Friday Night Funkin' C Engine
 * (c) 2026 nordjkpl
 *
 * ModdingState.c - Modding hub and mod importer UI.
 * Input/state work is handled in Update; Draw only renders.
 */

#include "scenes/AllScenes.h"
#include "scenes/ModImport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

typedef enum {
    MOD_LIST = 0,
    MOD_IMPORT,
    MOD_STUB
} ModdingMode;

static const char *const tabs[] = {
    "CHART EDITOR",
    "CHARACTER EDITOR",
    "IMPORT MOD"
};

enum { TAB_COUNT = (int)(sizeof(tabs) / sizeof(tabs[0])) };

static ModdingMode mode;
static int selected;
static float offset;
static const char *stubTitle;

static RayGraphicObject bg;
static Camera2D cam;

/* Importer: origem do mod -> assets/mods/<nome>. */
#define MODDING_PATH_CAP 512
#define MODDING_MSG_CAP  256

static char impPath[MODDING_PATH_CAP];
static char impMsg[MODDING_MSG_CAP];
static int impMods;
static int impSongs;
static long impKB;
static char impConfirmDel;

static Color outlineColor = {0, 0, 0, 0};
static Color fontColor = {255, 255, 255, 255};

/* ------------------------------------------------------------------------- */
/* Small utilities                                                           */
/* ------------------------------------------------------------------------- */

static void Imp_RefreshUsage(void)
{
    ModImportUsage usage = {0};

    ModImport_ScanMods("assets/mods", &usage);
    impMods = usage.mods;
    impSongs = usage.songs;
    impKB = usage.bytes / 1024;
}

static void Imp_SetMessage(const char *message)
{
    if (!message) {
        impMsg[0] = '\0';
        return;
    }

    snprintf(impMsg, sizeof(impMsg), "%s", message);
}

/* Append an input codepoint as UTF-8. GetCharPressed returns Unicode codepoints. */
static void Imp_AppendCodepoint(int codepoint)
{
    size_t length = strlen(impPath);
    unsigned char bytes[4];
    size_t count;

    if (codepoint < 32 || codepoint == 127 || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        return;

    if (codepoint <= 0x7F) {
        bytes[0] = (unsigned char)codepoint;
        count = 1;
    } else if (codepoint <= 0x7FF) {
        bytes[0] = (unsigned char)(0xC0 | (codepoint >> 6));
        bytes[1] = (unsigned char)(0x80 | (codepoint & 0x3F));
        count = 2;
    } else if (codepoint <= 0xFFFF) {
        bytes[0] = (unsigned char)(0xE0 | (codepoint >> 12));
        bytes[1] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | (codepoint & 0x3F));
        count = 3;
    } else {
        bytes[0] = (unsigned char)(0xF0 | (codepoint >> 18));
        bytes[1] = (unsigned char)(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = (unsigned char)(0x80 | (codepoint & 0x3F));
        count = 4;
    }

    if (length + count >= sizeof(impPath))
        return;

    memcpy(impPath + length, bytes, count);
    impPath[length + count] = '\0';
}

static void Imp_PasteClipboard(void)
{
    const char *clipboard = GetClipboardText();
    size_t length = strlen(impPath);

    if (!clipboard)
        return;

    while (*clipboard && length + 1 < sizeof(impPath)) {
        unsigned char c = (unsigned char)*clipboard++;

        /* Keep UTF-8 bytes; discard newlines and other control characters. */
        if (c < 32 || c == 127)
            continue;

        impPath[length++] = (char)c;
    }

    impPath[length] = '\0';
}

static void Imp_Backspace(void)
{
    size_t length = strlen(impPath);

    if (length == 0)
        return;

    /* Remove one UTF-8 codepoint, not just one trailing byte. */
    do {
        length--;
    } while (length > 0 && (((unsigned char)impPath[length] & 0xC0) == 0x80));

    impPath[length] = '\0';
}

/* Remove surrounding quotes and trailing separators; return the last folder. */
static const char *Imp_PrepareSourcePath(char path[MODDING_PATH_CAP])
{
    size_t length = strlen(path);
    char *base = path;

    while (length > 0 && (path[length - 1] == '\\' || path[length - 1] == '/'))
        path[--length] = '\0';

    if (length >= 2 && path[0] == '"' && path[length - 1] == '"') {
        memmove(path, path + 1, length - 2);
        length -= 2;
        path[length] = '\0';
    }

    while (length > 0 && (path[length - 1] == '\\' || path[length - 1] == '/'))
        path[--length] = '\0';

    if (length == 0)
        return NULL;

    for (char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }

    return *base ? base : NULL;
}

static void Imp_RunImport(void)
{
    char sourcePath[MODDING_PATH_CAP];
    const char *modName;
    ModImportStats stats = {0};

    memcpy(sourcePath, impPath, sizeof(sourcePath));
    sourcePath[sizeof(sourcePath) - 1] = '\0';

    modName = Imp_PrepareSourcePath(sourcePath);
    if (!modName) {
        Imp_SetMessage("import: caminho invalido");
        return;
    }

    if (!ModImport_Run(sourcePath, "assets/mods", modName,
                       impMsg, sizeof(impMsg), &stats)) {
        impMsg[sizeof(impMsg) - 1] = '\0';
    }

    impConfirmDel = 0;
    Imp_RefreshUsage();
}

static void Imp_UpdateInput(void)
{
    int codepoint;
    const int ctrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    while ((codepoint = GetCharPressed()) > 0)
        Imp_AppendCodepoint(codepoint);

    if (IsKeyPressed(KEY_BACKSPACE))
        Imp_Backspace();

    if (ctrlDown && IsKeyPressed(KEY_V))
        Imp_PasteClipboard();
}

/* ------------------------------------------------------------------------- */
/* Update                                                                    */
/* ------------------------------------------------------------------------- */

static void ModdingState_Update([[maybe_unused]] RayScene *scene)
{
    if (mode == MOD_IMPORT) {
        Imp_UpdateInput();

        if (IsKeyPressed(KEY_ESCAPE)) {
            mode = MOD_LIST;
            impConfirmDel = 0;
            return;
        }

        if (IsKeyPressed(KEY_DELETE) && impPath[0] == '\0') {
            if (!impConfirmDel && impMods > 0) {
                impConfirmDel = 1;
                snprintf(impMsg, sizeof(impMsg),
                         "DELETE again to remove %d mods (%ld KB). ESC cancels.",
                         impMods, impKB);
            } else if (impConfirmDel) {
                int removed = 0;
                long freed = 0;

                ModImport_DeleteAll("assets/mods", impMsg, sizeof(impMsg),
                                    &removed, &freed);
                impMsg[sizeof(impMsg) - 1] = '\0';
                impConfirmDel = 0;
                Imp_RefreshUsage();
            }
            return;
        }

        if (impConfirmDel &&
            (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
            impConfirmDel = 0;
            return;
        }

        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) &&
            impPath[0] != '\0') {
            Imp_RunImport();
            return;
        }

        return;
    }

    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        if (mode == MOD_STUB) {
            mode = MOD_LIST;
            return;
        }

        RayGame_ResetMusic();
        RayGame_ToggleMusic(1);
        MenuState_SetScene();
        return;
    }

    if (mode == MOD_STUB)
        return;

    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
        if (++selected >= TAB_COUNT)
            selected = 0;
    } else if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
        if (--selected < 0)
            selected = TAB_COUNT - 1;
    }

    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) ||
        IsKeyPressed(KEY_KP_ENTER)) {
        switch (selected) {
            case 0:
                ChartEditor_SetScene();
                return;

            case 1:
                stubTitle = tabs[selected];
                mode = MOD_STUB;
                return;

            case 2:
                mode = MOD_IMPORT;
                impConfirmDel = 0;
                Imp_RefreshUsage();
                return;

            default:
                break;
        }
    }

    /* Clamp interpolation to avoid overshoot after a long frame. */
    {
        float factor = RayGame_DeltaTime() * 10.0f;
        if (factor > 1.0f)
            factor = 1.0f;
        if (factor < 0.0f)
            factor = 0.0f;

        offset += ((float)selected - offset) * factor;
    }
}

/* ------------------------------------------------------------------------- */
/* Draw                                                                      */
/* ------------------------------------------------------------------------- */

static void Modding_DrawBackground(void)
{
    Render_SetCamera(&cam);
    Render_DrawGraphicObject(&bg);
}

static void Modding_DrawImport(void)
{
    char usageText[160];
    const Color muted = {120, 120, 120, 255};
    const Color warning = impConfirmDel ? RED : YELLOW;

    Modding_DrawBackground();

    DrawTextEx(mainFont, "IMPORT MOD", (Vector2){150, 120}, 40, 4, fontColor);
    DrawTextEx(mainFont,
               "Mod folder (the directory containing songs/, data/ or weeks/)",
               (Vector2){150, 190}, 20, 1, fontColor);

    DrawRectangle(150, 220, 980, 42, (Color){20, 20, 20, 255});
    DrawRectangleLines(150, 220, 980, 42, GREEN);
    DrawTextEx(mainFont,
               impPath[0] ? impPath : "C:/.../mod-folder",
               (Vector2){160, 231}, 20, 1,
               impPath[0] ? WHITE : muted);

    DrawTextEx(mainFont,
               "ENTER: import   CTRL+V: paste path   BACKSPACE: erase   ESC: back",
               (Vector2){150, 280}, 19, 1, YELLOW);
    DrawTextEx(mainFont,
               "Imports charts, music, weeks, stages, images, characters and icons.",
               (Vector2){150, 314}, 19, 1, fontColor);
    DrawTextEx(mainFont,
               "Destination: assets/mods/<mod-name>/",
               (Vector2){150, 344}, 19, 1, fontColor);

    snprintf(usageText, sizeof(usageText),
             "Mods: %d    Songs: %d    Disk usage: %ld KB    (DELETE twice removes all mods)",
             impMods, impSongs, impKB);
    DrawTextEx(mainFont, usageText, (Vector2){150, 390}, 19, 1, warning);

    if (impMsg[0])
        DrawTextEx(mainFont, impMsg, (Vector2){150, 430}, 19, 1, warning);

    Render_StopCamera();
}

static void Modding_DrawStub(void)
{
    const char *message = "COMING SOON";
    const Vector2 center = {640.0f, 360.0f};
    const Vector2 size = MeasureTextEx(mainFont, message, 80, 5);

    Modding_DrawBackground();
    DrawTextEx(mainFont, message,
               (Vector2){center.x - size.x * 0.5f,
                         center.y - size.y * 0.5f},
               80, 5, fontColor);

    if (stubTitle) {
        const Vector2 sub = MeasureTextEx(mainFont, stubTitle, 32, 4);
        DrawTextEx(mainFont, stubTitle,
                   (Vector2){center.x - sub.x * 0.5f, center.y + 80.0f},
                   32, 4, fontColor);
    }

    Render_StopCamera();
}

static void Modding_DrawList(void)
{
    Modding_DrawBackground();

    for (int i = 0; i < TAB_COUNT; ++i) {
        const float distance = (float)i - offset;
        const float absDistance = (distance < 0.0f) ? -distance : distance;
        const int selectedItem = (selected == i);
        Color textColor = fontColor;
        Color shadowColor = outlineColor;
        const float fontSize = 80.0f;
        const float shadow = 5.0f;
        Vector2 position = {
            shadow + 150.0f - absDistance * 30.0f,
            shadow + 360.0f + distance * 90.0f
        };

        textColor.a = selectedItem ? 255 : 150;
        shadowColor.a = selectedItem ? 255 : 100;

        DrawTextEx(mainFont, tabs[i], position, fontSize, 5, shadowColor);
        position.x -= shadow;
        position.y -= shadow;
        DrawTextEx(mainFont, tabs[i], position, fontSize, 5, textColor);

        if (selectedItem) {
            const Vector2 tip = {position.x - 72.0f, position.y + 14.0f};
            DrawTriangle(tip,
                         (Vector2){tip.x, tip.y + 44.0f},
                         (Vector2){tip.x + 36.0f, tip.y + 22.0f},
                         (Color){80, 220, 100, 255});
        }
    }

    Render_StopCamera();
}

static void ModdingState_Draw([[maybe_unused]] RayScene *scene)
{
    ClearBackground(BLACK);

    switch (mode) {
        case MOD_IMPORT:
            Modding_DrawImport();
            break;

        case MOD_STUB:
            Modding_DrawStub();
            break;

        case MOD_LIST:
        default:
            Modding_DrawList();
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* Scene lifecycle                                                           */
/* ------------------------------------------------------------------------- */

static void ModdingState_Create([[maybe_unused]] RayScene *scene)
{
    Render_DefaultRGT(&bg);
    bg.objType = RGT_IMAGE;
    bg.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");

    cam = (Camera2D){
        .target = {640.0f, 360.0f},
        .offset = {0.0f, 0.0f},
        .rotation = 0.0f,
        .zoom = 1.0f
    };

    mode = MOD_LIST;
    selected = 0;
    offset = 0.0f;
    stubTitle = NULL;

    impPath[0] = '\0';
    impMsg[0] = '\0';
    impMods = 0;
    impSongs = 0;
    impKB = 0;
    impConfirmDel = 0;
}

static void ModdingState_Destroy([[maybe_unused]] RayScene *scene)
{
    if (bg.image.id != 0)
        UnloadTexture(bg.image);

    bg.image = (Texture2D){0};
}

static RayScene moddingScene;
Scene_MakeSceneCode(
    moddingScene,
    ModdingState_SetScene,
    ModdingState_Create,
    ModdingState_Update,
    ModdingState_Draw,
    ModdingState_Destroy
)
