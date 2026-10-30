/*
 * Friday Night Funkin' C Engine
 * FreeplayState.c - optimized Freeplay menu
 * (c) 2026 nordjkpl
 */

#include "scenes/AllScenes.h"
#include "scenes/SongList.h"
#include "yyjson.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define FREEPLAY_PATH_MAX       512
#define FREEPLAY_ICON_NAME_MAX   96
#define FREEPLAY_LIST_STEP       92.0f
#define FREEPLAY_ICON_SIZE       224.0f

static SongEntry *songs;
static int songCount;
static int selectedSong;
static float listOffset;

static RayGraphicObject bg;
static Camera2D cam;

/* One selected icon only. Failed lookups are cached too, so no disk I/O per frame. */
static Texture2D selectedIcon;
static bool selectedIconLoaded;
static bool selectedIconResolved;
static int selectedIconSong = -1;
static char selectedOpponent[FREEPLAY_ICON_NAME_MAX];

static void CopyString(char *dst, size_t dstSize, const char *src)
{
    if (!dst || dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t length = strlen(src);
    if (length >= dstSize) length = dstSize - 1;
    memcpy(dst, src, length);
    dst[length] = '\0';
}

static bool IsSafeAssetName(const char *name)
{
    if (!name || !name[0] || strstr(name, "..")) return false;

    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p < 32) return false;
    }

    return true;
}

/*
 * Extracts assets/mods/<mod> from a song directory.
 * Also accepts the legacy assets/mods/mods/<mod>/... layout.
 * Returned paths use '/' which is accepted by Windows and avoids separator
 * mismatches when songDir was created with backslashes.
 */
static bool Freeplay_GetModRoot(const char *songDir, char *out, size_t outSize)
{
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (!songDir || !songDir[0]) return false;

    char normalized[FREEPLAY_PATH_MAX];
    CopyString(normalized, sizeof(normalized), songDir);
    for (char *p = normalized; *p; ++p) {
        if (*p == '\\') *p = '/';
    }

    const char marker[] = "assets/mods/";
    char *mods = strstr(normalized, marker);
    if (!mods) return false;

    char *modName = mods + sizeof(marker) - 1;
    char *separator = strchr(modName, '/');
    if (!separator) return false;

    /* Compatibility with the old duplicated "mods/mods/<name>" root. */
    if ((size_t)(separator - modName) == 4 &&
        memcmp(modName, "mods", 4) == 0) {
        modName = separator + 1;
        separator = strchr(modName, '/');
        if (!separator) return false;
    }

    if (separator == modName) return false;

    size_t length = (size_t)(separator - normalized);
    if (length == 0 || length >= outSize) return false;

    memcpy(out, normalized, length);
    out[length] = '\0';
    return true;
}

static bool ReadCharacterHealthIcon(const char *jsonPath,
                                    char out[FREEPLAY_ICON_NAME_MAX])
{
    out[0] = '\0';

    FILE *file = fopen(jsonPath, "rb");
    if (!file) return false;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long fileSize = ftell(file);
    if (fileSize <= 0 || fileSize > 1024L * 1024L ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    char *buffer = (char *)malloc((size_t)fileSize + 1);
    if (!buffer) {
        fclose(file);
        return false;
    }

    size_t readSize = fread(buffer, 1, (size_t)fileSize, file);
    fclose(file);

    if (readSize != (size_t)fileSize) {
        free(buffer);
        return false;
    }
    buffer[fileSize] = '\0';

    yyjson_doc *doc = yyjson_read(buffer, readSize, 0);
    free(buffer);
    if (!doc) return false;

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *value = yyjson_is_obj(root)
        ? yyjson_obj_get(root, "healthicon")
        : NULL;

    bool found = false;
    if (yyjson_is_str(value)) {
        const char *healthIcon = yyjson_get_str(value);
        if (IsSafeAssetName(healthIcon)) {
            CopyString(out, FREEPLAY_ICON_NAME_MAX, healthIcon);
            found = out[0] != '\0';
        }
    }

    yyjson_doc_free(doc);
    return found;
}

static bool Freeplay_FindHealthIconKey(const char *songDir,
                                       const char *modRoot,
                                       const char *opponent,
                                       char out[FREEPLAY_ICON_NAME_MAX])
{
    CopyString(out, FREEPLAY_ICON_NAME_MAX, opponent);
    if (!IsSafeAssetName(opponent)) return false;

    char candidate[FREEPLAY_PATH_MAX];

    if (modRoot && modRoot[0]) {
        snprintf(candidate, sizeof(candidate), "%s/characters/%s.json", modRoot, opponent);
        if (FileExists(candidate) && ReadCharacterHealthIcon(candidate, out)) return true;

        snprintf(candidate, sizeof(candidate), "%s/images/characters/%s.json", modRoot, opponent);
        if (FileExists(candidate) && ReadCharacterHealthIcon(candidate, out)) return true;
    }

    if (songDir && songDir[0]) {
        snprintf(candidate, sizeof(candidate), "%s/characters/%s.json", songDir, opponent);
        if (FileExists(candidate) && ReadCharacterHealthIcon(candidate, out)) return true;
    }

    snprintf(candidate, sizeof(candidate), "assets/characters/%s.json", opponent);
    if (FileExists(candidate) && ReadCharacterHealthIcon(candidate, out)) return true;

    snprintf(candidate, sizeof(candidate), "assets/images/characters/%s.json", opponent);
    if (FileExists(candidate) && ReadCharacterHealthIcon(candidate, out)) return true;

    return false;
}

static bool TryIconNameInDirectory(const char *directory,
                                   const char *name,
                                   char out[FREEPLAY_PATH_MAX])
{
    if (!directory || !directory[0] || !IsSafeAssetName(name)) return false;

    char path[FREEPLAY_PATH_MAX];
    snprintf(path, sizeof(path), "%s/icon-%s.png", directory, name);
    if (FileExists(path)) {
        CopyString(out, FREEPLAY_PATH_MAX, path);
        return true;
    }

    snprintf(path, sizeof(path), "%s/%s.png", directory, name);
    if (FileExists(path)) {
        CopyString(out, FREEPLAY_PATH_MAX, path);
        return true;
    }

    return false;
}

static bool Freeplay_FindIconPath(const char *songDir,
                                  const char *modRoot,
                                  const char *healthIcon,
                                  const char *opponent,
                                  char out[FREEPLAY_PATH_MAX])
{
    char directories[3][FREEPLAY_PATH_MAX];
    int directoryCount = 0;

    /* Mod assets always win over base-game assets. */
    if (modRoot && modRoot[0]) {
        snprintf(directories[directoryCount++], FREEPLAY_PATH_MAX,
                 "%s/images/icons", modRoot);
        snprintf(directories[directoryCount++], FREEPLAY_PATH_MAX,
                 "%s/icons", modRoot);
    }

    if (songDir && songDir[0]) {
        snprintf(directories[directoryCount++], FREEPLAY_PATH_MAX,
                 "%s/images/icons", songDir);
    }

    const char *names[2] = {healthIcon, opponent};

    for (int d = 0; d < directoryCount; ++d) {
        for (int n = 0; n < 2; ++n) {
            if (!names[n] || !names[n][0]) continue;
            if (n == 1 && names[0] && strcmp(names[0], names[1]) == 0) continue;
            if (TryIconNameInDirectory(directories[d], names[n], out)) return true;
        }
    }

    /* Some mods keep a song-specific icon beside the chart/audio files. */
    if (songDir && songDir[0]) {
        if (TryIconNameInDirectory(songDir, healthIcon, out)) return true;
        if (opponent && strcmp(healthIcon, opponent) != 0 &&
            TryIconNameInDirectory(songDir, opponent, out)) return true;
    }

    /* Global icons are the final asset fallback. */
    if (TryIconNameInDirectory("assets/images/icons", healthIcon, out)) return true;
    if (opponent && strcmp(healthIcon, opponent) != 0 &&
        TryIconNameInDirectory("assets/images/icons", opponent, out)) return true;

    return false;
}

static void Freeplay_UnloadSelectedIcon(void)
{
    if (selectedIconLoaded) UnloadTexture(selectedIcon);

    selectedIcon = (Texture2D){0};
    selectedIconLoaded = false;
    selectedIconResolved = false;
    selectedIconSong = -1;
    selectedOpponent[0] = '\0';
}

/* Called only when selection changes, never from Draw. */
static void Freeplay_LoadSelectedIcon(void)
{
    if (selectedIconResolved && selectedIconSong == selectedSong) return;

    Freeplay_UnloadSelectedIcon();
    selectedIconSong = selectedSong;
    selectedIconResolved = true; /* Negative lookup is cached as well. */

    if (selectedSong < 0 || selectedSong >= songCount) return;

    const char *songDir = songs[selectedSong].dir;
    char opponent[FREEPLAY_ICON_NAME_MAX] = {0};
    SongList_Opponent(songDir, opponent);
    if (!opponent[0]) CopyString(opponent, sizeof(opponent), "bf");
    CopyString(selectedOpponent, sizeof(selectedOpponent), opponent);

    char modRoot[FREEPLAY_PATH_MAX] = {0};
    (void)Freeplay_GetModRoot(songDir, modRoot, sizeof(modRoot));

    char healthIcon[FREEPLAY_ICON_NAME_MAX] = {0};
    (void)Freeplay_FindHealthIconKey(songDir, modRoot, opponent, healthIcon);

    char iconPath[FREEPLAY_PATH_MAX];
    if (Freeplay_FindIconPath(songDir, modRoot, healthIcon, opponent, iconPath)) {
        selectedIcon = Render_LoadTexture(iconPath);
        selectedIconLoaded = selectedIcon.id != 0;
    }

    /* Universal fallback. Do not retry it every frame if it is also missing. */
    if (!selectedIconLoaded && FileExists("assets/images/icons/bf.png")) {
        selectedIcon = Render_LoadTexture("assets/images/icons/bf.png");
        selectedIconLoaded = selectedIcon.id != 0;
    }
}

static void Freeplay_Create([[maybe_unused]] RayScene *scene)
{
    Freeplay_UnloadSelectedIcon();
    SongList_Free(songs);
    songs = NULL;

    Render_DefaultRGT(&bg);
    bg.objType = RGT_IMAGE;
    bg.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");

    cam = (Camera2D){
        .target = {640.0f, 360.0f},
        .offset = {0.0f, 0.0f},
        .rotation = 0.0f,
        .zoom = 1.0f
    };

    songCount = SongList_Scan(&songs);
    if (songCount < 0) songCount = 0;

    selectedSong = 0;
    listOffset = 0.0f;

    if (songCount > 0) Freeplay_LoadSelectedIcon();
}

static void Freeplay_Update([[maybe_unused]] RayScene *scene)
{
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        MenuState_SetScene();
        return;
    }

    if (songCount <= 0) return;

    int previousSelection = selectedSong;

    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
        selectedSong++;
        if (selectedSong >= songCount) selectedSong = 0;
    } else if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
        selectedSong--;
        if (selectedSong < 0) selectedSong = songCount - 1;
    }

    if (selectedSong != previousSelection) {
        Freeplay_LoadSelectedIcon();
    }

    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) ||
        IsKeyPressed(KEY_KP_ENTER)) {
        const char *dir = songs[selectedSong].dir;
        RayGame_ToggleMusic(0);

        if (SongList_IsHeavy(dir)) {
            LoadingState_SetSongDir(dir);
            LoadingState_SetScene();
        } else {
            PlayState_SetSongDir(dir);
            PlayState_SetScene();
        }
        return;
    }

    const float dt = RayGame_DeltaTime();
    const float factor = 1.0f - expf(-10.0f * fmaxf(dt, 0.0f));
    listOffset += ((float)selectedSong - listOffset) * factor;
}

static void Freeplay_DrawSelectedIcon(void)
{
    const float panelX = 850.0f;
    const float iconX = 920.0f;
    const float iconY = 112.0f;
    const float iconSize = FREEPLAY_ICON_SIZE;

    DrawRectangle((int)panelX, 62, 360, 590, (Color){0, 0, 0, 125});
    DrawRectangleLines((int)panelX, 62, 360, 590, (Color){110, 105, 155, 170});

    DrawTextEx(mainFont, "SELECTED TRACK",
               (Vector2){panelX + 24.0f, 82.0f}, 21.0f, 2.0f,
               (Color){190, 185, 220, 255});

    DrawRectangle((int)(iconX - 8.0f), (int)(iconY - 8.0f),
                  (int)(iconSize + 16.0f), (int)(iconSize + 16.0f),
                  (Color){18, 18, 28, 255});
    DrawRectangleLines((int)(iconX - 8.0f), (int)(iconY - 8.0f),
                       (int)(iconSize + 16.0f), (int)(iconSize + 16.0f),
                       (Color){190, 165, 255, 220});

    if (selectedIconLoaded) {
        /* Center-crop to square, preserving the source aspect ratio. */
        float sourceWidth = (float)selectedIcon.width;
        float sourceHeight = (float)selectedIcon.height;
        Rectangle source = {0.0f, 0.0f, sourceWidth, sourceHeight};

        if (sourceWidth > sourceHeight) {
            source.x = (sourceWidth - sourceHeight) * 0.5f;
            source.width = sourceHeight;
        } else if (sourceHeight > sourceWidth) {
            source.y = (sourceHeight - sourceWidth) * 0.5f;
            source.height = sourceWidth;
        }

        const Rectangle destination = {iconX, iconY, iconSize, iconSize};
        DrawTexturePro(selectedIcon, source, destination,
                       (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
    } else {
        DrawRectangle((int)iconX, (int)iconY,
                      (int)iconSize, (int)iconSize,
                      (Color){38, 38, 52, 255});
        const char *missing = "NO ICON";
        Vector2 size = MeasureTextEx(mainFont, missing, 25.0f, 2.0f);
        DrawTextEx(mainFont, missing,
                   (Vector2){iconX + (iconSize - size.x) * 0.5f,
                             iconY + (iconSize - size.y) * 0.5f},
                   25.0f, 2.0f, (Color){150, 145, 170, 255});
    }

    if (songCount > 0 && selectedSong >= 0 && selectedSong < songCount) {
        const char *title = songs[selectedSong].name;
        Vector2 titleSize = MeasureTextEx(mainFont, title, 34.0f, 2.0f);
        float titleX = panelX + (360.0f - titleSize.x) * 0.5f;
        if (titleX < panelX + 16.0f) titleX = panelX + 16.0f;

        DrawTextEx(mainFont, title,
                   (Vector2){titleX + 2.0f, 375.0f}, 34.0f, 2.0f,
                   (Color){0, 0, 0, 210});
        DrawTextEx(mainFont, title,
                   (Vector2){titleX, 373.0f}, 34.0f, 2.0f, WHITE);

        DrawTextEx(mainFont, "OPPONENT",
                   (Vector2){panelX + 24.0f, 435.0f}, 19.0f, 2.0f,
                   (Color){175, 170, 205, 255});
        DrawTextEx(mainFont, selectedOpponent[0] ? selectedOpponent : "unknown",
                   (Vector2){panelX + 24.0f, 462.0f}, 25.0f, 2.0f,
                   (Color){255, 220, 100, 255});

        char counter[48];
        snprintf(counter, sizeof(counter), "TRACK %d / %d",
                 selectedSong + 1, songCount);
        DrawTextEx(mainFont, counter,
                   (Vector2){panelX + 24.0f, 555.0f}, 19.0f, 1.5f,
                   (Color){190, 185, 220, 255});
    }
}

static void Freeplay_Draw([[maybe_unused]] RayScene *scene)
{
    Render_SetCamera(&cam);
    Render_DrawGraphicObject(&bg);

    if (songCount <= 0) {
        DrawTextEx(mainFont, "NO SONGS FOUND",
                   (Vector2){150.0f, 330.0f}, 42.0f, 3.0f, WHITE);
        DrawTextEx(mainFont, "Add songs to assets/songs/ or assets/mods/<mod>/songs/",
                   (Vector2){150.0f, 390.0f}, 21.0f, 1.5f,
                   (Color){210, 210, 220, 255});
        Render_StopCamera();
        return;
    }

    /* Render only the visible part of the song list instead of scanning all songs. */
    int first = (int)floorf(listOffset - 6.0f);
    int last = (int)ceilf(listOffset + 6.0f);
    if (first < 0) first = 0;
    if (last >= songCount) last = songCount - 1;

    for (int i = first; i <= last; ++i) {
        const float y = 360.0f + ((float)i - listOffset) * FREEPLAY_LIST_STEP;
        const bool isSelected = (i == selectedSong);
        const float distance = fabsf((float)i - listOffset);
        const float x = 145.0f - distance * 24.0f;

        if (isSelected) {
            DrawRectangle(105, (int)(y - 8.0f), 675, 78,
                          (Color){20, 18, 38, 170});
            DrawRectangle(105, (int)(y - 8.0f), 5, 78,
                          (Color){255, 215, 90, 255});
        }

        Color textColor = isSelected
            ? WHITE
            : (Color){205, 205, 220, 165};
        Color shadowColor = (Color){0, 0, 0, isSelected ? 230 : 120};

        DrawTextEx(mainFont, songs[i].name,
                   (Vector2){x + 4.0f, y + 4.0f}, 52.0f, 3.0f,
                   shadowColor);
        DrawTextEx(mainFont, songs[i].name,
                   (Vector2){x, y}, 52.0f, 3.0f, textColor);

        if (isSelected) {
            DrawTextEx(mainFont, ">",
                       (Vector2){x - 45.0f, y - 1.0f}, 55.0f, 3.0f,
                       (Color){255, 220, 90, 255});
        }
    }

    Freeplay_DrawSelectedIcon();

    DrawTextEx(mainFont, "UP / DOWN  SELECT TRACK",
               (Vector2){105.0f, 660.0f}, 19.0f, 1.5f,
               (Color){245, 220, 120, 255});
    DrawTextEx(mainFont, "ENTER PLAY     ESC BACK",
               (Vector2){850.0f, 675.0f}, 17.0f, 1.0f,
               (Color){220, 215, 235, 230});

    Render_StopCamera();
}

static void Freeplay_Destroy([[maybe_unused]] RayScene *scene)
{
    Freeplay_UnloadSelectedIcon();
    if (bg.image.id != 0) UnloadTexture(bg.image);
    bg.image = (Texture2D){0};

    SongList_Free(songs);
    songs = NULL;
    songCount = 0;
    selectedSong = 0;
    listOffset = 0.0f;
}

static RayScene scene;
Scene_MakeSceneCode(
    scene,
    Freeplay_SetScene,
    Freeplay_Create,
    Freeplay_Update,
    Freeplay_Draw,
    Freeplay_Destroy
);
