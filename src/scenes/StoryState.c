/*
 * Friday Night Funkin' C Engine (c) 2026 nordjkpl
 * StoryState.c - Story Week Selection
 *
 * Week cover convention:
 *   assets/mods/<mod>/images/menubackgrounds/menu_<week>.png
 *   assets/images/menubackgrounds/menu_<week>.png
 *
 * The week key is resolved from the first song character and week title.
 */

#include "scenes/AllScenes.h"
#include "scenes/SongList.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yyjson.h"

#define STORY_SCREEN_W          1280
#define STORY_SCREEN_H          720
#define STORY_LIST_X            58.0f
#define STORY_LIST_Y            164.0f
#define STORY_LIST_W            610.0f
#define STORY_ROW_H             72.0f
#define STORY_VISIBLE_MARGIN    100.0f
#define STORY_COVER_SIZE        224.0f
#define STORY_COVER_X           910.0f
#define STORY_COVER_Y           124.0f
#define STORY_PATH_MAX          512
#define STORY_TEXT_MAX          192
#define STORY_STATUS_SECONDS    3.0f
#define STORY_MAX_DISPLAY_SONGS 50

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

static WeekEntry *weeks;
static int weekCount;
static int selected;
static float listOffset;

static char deleteConfirm;
static char statusText[128];
static float statusTimer;

static Texture2D backgroundTexture;
static Texture2D weekCover;
static char weekCoverLoaded;
static int weekCoverIndex = -1;

typedef struct StoryWeekCoverCache {
    char attempted;
    char path[STORY_PATH_MAX];
} StoryWeekCoverCache;

static StoryWeekCoverCache *weekCoverCache;

static float discAngle;
static Camera2D storyCamera;

static const Color COLOR_PANEL       = { 12, 15, 27, 218 };
static const Color COLOR_PANEL_ALT   = { 22, 27, 44, 230 };
static const Color COLOR_BORDER      = { 90, 105, 145, 150 };
static const Color COLOR_TEXT        = { 238, 241, 255, 255 };
static const Color COLOR_MUTED       = { 155, 165, 190, 255 };
static const Color COLOR_ACCENT      = { 255, 205, 92, 255 };
static const Color COLOR_GREEN       = { 100, 225, 155, 255 };
static const Color COLOR_DANGER      = { 255, 105, 115, 255 };

/* ------------------------------------------------------------------------- */
/* Small utilities                                                           */
/* ------------------------------------------------------------------------- */

static void Story_SetStatus(const char *message)
{
    if (!message) message = "";
    snprintf(statusText, sizeof(statusText), "%s", message);
    statusTimer = STORY_STATUS_SECONDS;
}

static int Story_IsSafeAssetName(const char *name)
{
    if (!name || !name[0] || name[0] == '.') return 0;
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\'))
        return 0;
    return 1;
}

static int Story_IsPrefixNoCase(const char *text, const char *prefix)
{
    while (*prefix) {
        unsigned char a = (unsigned char)*text++;
        unsigned char b = (unsigned char)*prefix++;
        if (!a || tolower(a) != tolower(b)) return 0;
    }
    return 1;
}

/*
 * Resolve o diretório do mod a partir de caminhos de WeekEntry.dir.
 * Aceita:
 *   assets/mods/<mod>/weeks
 *   assets/mods/<mod>/weeks/<arquivo>
 *   assets/mods/<mod>/songs/<song>
 *   assets/mods/mods/<mod>/weeks (layout antigo)
 *   caminho absoluto do Windows com os mesmos componentes.
 */
static int Story_FindNoCase(const char *text, const char *needle,
                            const char **found)
{
    size_t n;
    if (!text || !needle || !needle[0]) return 0;
    n = strlen(needle);

    for (const char *p = text; *p; ++p) {
        size_t i = 0;
        while (i < n) {
            unsigned char a = (unsigned char)p[i];
            unsigned char b = (unsigned char)needle[i];
            if (!a) break;
            if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
            if (a != b) break;
            ++i;
        }
        if (i == n) {
            if (found) *found = p;
            return 1;
        }
    }
    return 0;
}

static int Story_GetModRoot(const char *weekDir, char out[STORY_PATH_MAX])
{
    char normalized[STORY_PATH_MAX];
    const char *marker = NULL;
    size_t length;
    const char *modStart;
    const char *modEnd;

    out[0] = '\0';
    if (!weekDir || !weekDir[0]) return 0;

    length = strlen(weekDir);
    if (length >= sizeof(normalized)) return 0;
    for (size_t i = 0; i < length; ++i)
        normalized[i] = weekDir[i] == '\\' ? '/' : weekDir[i];
    normalized[length] = '\0';

    /* Prefer the canonical assets/mods root, including absolute paths. */
    if (Story_FindNoCase(normalized, "assets/mods/", &marker)) {
        modStart = marker + sizeof("assets/mods/") - 1;
    } else if (Story_FindNoCase(normalized, "/mods/", &marker)) {
        /* Compatibility with paths rooted at <game>/mods/<mod>. */
        modStart = marker + sizeof("/mods/") - 1;
    } else {
        return 0;
    }

    modEnd = strchr(modStart, '/');
    if (!modEnd || modEnd == modStart) return 0;

    /* Old imports used assets/mods/mods/<modName>/. */
    if ((size_t)(modEnd - modStart) == 4u &&
        (modStart[0] == 'm' || modStart[0] == 'M') &&
        (modStart[1] == 'o' || modStart[1] == 'O') &&
        (modStart[2] == 'd' || modStart[2] == 'D') &&
        (modStart[3] == 's' || modStart[3] == 'S')) {
        const char *legacyStart = modEnd + 1;
        const char *legacyEnd = strchr(legacyStart, '/');
        if (!legacyEnd || legacyEnd == legacyStart) return 0;
        modEnd = legacyEnd;
    }

    length = (size_t)(modEnd - normalized);
    if (length == 0 || length >= STORY_PATH_MAX) return 0;
    memcpy(out, normalized, length);
    out[length] = '\0';
    return 1;
}

static int Story_NormalizeWeekCoverKey(const char *input, char out[64])
{
    const char *base;
    size_t length;

    out[0] = '\0';
    if (!input || !input[0]) return 0;

    /* Some week JSONs store a basename or even menu_<key>.png. */
    base = input;
    {
        const char *slash = strrchr(base, '/');
        const char *backslash = strrchr(base, '\\');
        if (slash && (!backslash || slash > backslash)) base = slash + 1;
        else if (backslash) base = backslash + 1;
    }

    length = strlen(base);
    if (length >= 4u &&
        (base[length - 4] == '.') &&
        (base[length - 3] == 'p' || base[length - 3] == 'P') &&
        (base[length - 2] == 'n' || base[length - 2] == 'N') &&
        (base[length - 1] == 'g' || base[length - 1] == 'G'))
        length -= 4u;

    if (length >= 5u) {
        char prefix[6];
        memcpy(prefix, base, 5u);
        prefix[5] = '\0';
        if (Story_IsPrefixNoCase(prefix, "menu_")) {
            base += 5;
            length -= 5u;
        }
    }

    if (length == 0 || length >= 64u) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)base[i];
        if (c < 32 || c == ':' || c == '/' || c == '\\') return 0;
    }
    memcpy(out, base, length);
    out[length] = '\0';
    return Story_IsSafeAssetName(out);
}

/*
 * Busca uma capa somente em dois lugares previsíveis:
 * 1) no mod dono da week;
 * 2) nos assets base.
 * Não enumera pastas nem procura em outros mods.
 */
static int Story_TryWeekCoverPath(const char *weekDir, const char *key,
                                  char out[STORY_PATH_MAX])
{
    char modRoot[STORY_PATH_MAX];
    int n;

    if (!Story_IsSafeAssetName(key)) return 0;

    if (Story_GetModRoot(weekDir, modRoot)) {
        n = snprintf(out, STORY_PATH_MAX,
                     "%s/images/menubackgrounds/menu_%s.png", modRoot, key);
        if (n > 0 && (size_t)n < STORY_PATH_MAX && FileExists(out))
            return 1;
    }

    n = snprintf(out, STORY_PATH_MAX,
                 "assets/images/menubackgrounds/menu_%s.png", key);
    return n > 0 && (size_t)n < STORY_PATH_MAX && FileExists(out);
}

static int Story_ReadWeekBackgroundKey(const char *weekDir,
                                       const char *titleKey,
                                       char out[64]);

/* Converts a title such as "week charlie" into "charlie". */
static int Story_MakeTitleKey(const char *title, char out[64])
{
    const unsigned char *source = (const unsigned char *)title;
    size_t used = 0;

    out[0] = '\0';
    if (!source) return 0;

    while (*source && isspace(*source)) ++source;
    if (Story_IsPrefixNoCase((const char *)source, "week "))
        source += 5;

    while (*source && used + 1 < 64) {
        unsigned char c = *source++;
        if (isalnum(c)) {
            out[used++] = (char)tolower(c);
        } else if ((c == ' ' || c == '-' || c == '_') && used > 0) {
            if (out[used - 1] != '_') out[used++] = '_';
        }
    }

    while (used > 0 && out[used - 1] == '_') --used;
    out[used] = '\0';
    return used > 0;
}

/* Reads the weekBackground field from the week JSON generated by Psych-style mods.
 * Typical file: <weekDir>/week-charlie.json -> "weekBackground": "charlie".
 * This happens only when the selected week changes, never in the draw hot path.
 */
static int Story_ReadWeekBackgroundKey(const char *weekDir,
                                       const char *titleKey,
                                       char out[64])
{
    char path[STORY_PATH_MAX];
    int n;
    FILE *file;
    long size;
    char *buffer;
    yyjson_doc *doc;
    yyjson_val *root;
    yyjson_val *value;
    const char *key;

    out[0] = '\0';
    if (!weekDir || !weekDir[0] || !titleKey || !titleKey[0])
        return 0;

    /* Psych-style week filename: week-<key>.json. One file lookup only. */
    n = snprintf(path, sizeof(path), "%s/week-%s.json", weekDir, titleKey);
    if (n <= 0 || (size_t)n >= sizeof(path)) return 0;

    file = fopen(path, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    size = ftell(file);
    if (size <= 0 || size > 256 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    buffer = (char *)malloc((size_t)size + 1u);
    if (!buffer) {
        fclose(file);
        return 0;
    }

    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return 0;
    }
    fclose(file);
    buffer[size] = '\0';

    doc = yyjson_read(buffer, (size_t)size, 0);
    free(buffer);
    if (!doc) return 0;

    root = yyjson_doc_get_root(doc);
    value = yyjson_obj_get(root, "weekBackground");
    key = yyjson_is_str(value) ? yyjson_get_str(value) : NULL;

    if (key && Story_NormalizeWeekCoverKey(key, out)) {
        yyjson_doc_free(doc);
        return 1;
    }

    yyjson_doc_free(doc);
    return 0;
}

static void Story_UnloadWeekCover(void)
{
    if (weekCoverLoaded && weekCover.id != 0)
        UnloadTexture(weekCover);

    weekCoverLoaded = 0;
    weekCoverIndex = -1;
    weekCover = (Texture2D){0};
}

static int Story_FindWeekCoverPath(const WeekEntry *week,
                                   char out[STORY_PATH_MAX])
{
    char titleKey[64] = {0};
    char backgroundKey[64] = {0};
    const char *characterKey = NULL;

    if (!week || !out) return 0;
    out[0] = '\0';

    Story_MakeTitleKey(week->title, titleKey);

    /* Prefer the explicit weekBackground key from this week's JSON. */
    if (titleKey[0] &&
        Story_ReadWeekBackgroundKey(week->dir, titleKey, backgroundKey) &&
        Story_TryWeekCoverPath(week->dir, backgroundKey, out))
        return 1;

    /* Fallback: first song character. */
    if (week->songCount > 0 && week->songs[0].character[0]) {
        characterKey = week->songs[0].character;
        if (Story_IsPrefixNoCase(characterKey, "icon-"))
            characterKey += 5;
        if (Story_TryWeekCoverPath(week->dir, characterKey, out))
            return 1;
    }

    /* Last named fallback: week title. */
    if (titleKey[0] && Story_TryWeekCoverPath(week->dir, titleKey, out))
        return 1;

    /* One built-in fallback; never enumerate installed mods. */
    if (FileExists("assets/images/freeplay/menuBGBlue.png")) {
        snprintf(out, STORY_PATH_MAX, "%s",
                 "assets/images/freeplay/menuBGBlue.png");
        return 1;
    }

    return 0;
}

static void Story_LoadWeekCover(void)
{
    StoryWeekCoverCache *cache = NULL;
    char resolvedPath[STORY_PATH_MAX];

    if (!weeks || selected < 0 || selected >= weekCount)
        return;

    /* Cache success and failure while this week list remains loaded. */
    if (weekCoverIndex == selected)
        return;

    Story_UnloadWeekCover();
    weekCoverIndex = selected;

    if (weekCoverCache)
        cache = &weekCoverCache[selected];

    if (cache) {
        if (!cache->attempted) {
            cache->attempted = 1;
            (void)Story_FindWeekCoverPath(&weeks[selected], cache->path);
        }
        if (!cache->path[0]) {
            weekCoverLoaded = 0;
            return;
        }
        snprintf(resolvedPath, sizeof(resolvedPath), "%s", cache->path);
    } else {
        /* Allocation-failure fallback: one attempt for this selection. */
        if (!Story_FindWeekCoverPath(&weeks[selected], resolvedPath)) {
            weekCoverLoaded = 0;
            return;
        }
    }

    weekCover = Render_LoadTexture(resolvedPath);
    weekCoverLoaded = (weekCover.id != 0);
}

static void Story_RefreshWeekList(void)
{
    free(weekCoverCache);
    weekCoverCache = NULL;

    WeekList_Free(weeks);
    weeks = NULL;
    weekCount = WeekList_Scan(&weeks);

    if (weekCount < 0) weekCount = 0;
    if (weekCount > 0)
        weekCoverCache = (StoryWeekCoverCache *)calloc(
            (size_t)weekCount, sizeof(*weekCoverCache));
    if (selected >= weekCount) selected = weekCount - 1;
    if (selected < 0) selected = 0;

    if (listOffset > (float)selected) listOffset = (float)selected;
    Story_UnloadWeekCover();
    Story_LoadWeekCover();
}

static void Story_StartSelectedWeek(void)
{
    WeekEntry *week;
    const char *songDir;

    if (!weeks || selected < 0 || selected >= weekCount)
        return;

    week = &weeks[selected];
    WeekRun_Start(week, 1);
    songDir = WeekRun_Dir();

    if (!songDir || !songDir[0]) {
        const char *missing = WeekRun_Missing();
        char message[128];

        if (missing && missing[0])
            snprintf(message, sizeof(message), "MISSING SONG: %.96s", missing);
        else
            snprintf(message, sizeof(message), "THIS WEEK HAS NO PLAYABLE SONGS");

        Story_SetStatus(message);
        WeekRun_Stop();
        return;
    }

    RayGame_ToggleMusic(0);
    PlayState_SetSongDir(songDir);
    PlayState_SetReturnWeek(1);
    PlayState_SetCharsOverride(WeekRun_Char());
    PlayState_SetDiff(WeekRun_DiffSuffix());

    if (SongList_IsHeavy(songDir)) {
        LoadingState_SetSongDir(songDir);
        LoadingState_SetScene();
    } else {
        PlayState_SetScene();
    }
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

static void StoryState_Create([[maybe_unused]] RayScene *scene)
{
    backgroundTexture = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");

    storyCamera = (Camera2D){
        .target = {STORY_SCREEN_W * 0.5f, STORY_SCREEN_H * 0.5f},
        .offset = {0.0f, 0.0f},
        .rotation = 0.0f,
        .zoom = 1.0f
    };

    selected = 0;
    listOffset = 0.0f;
    deleteConfirm = 0;
    discAngle = 0.0f;
    statusText[0] = '\0';
    statusTimer = 0.0f;

    Story_UnloadWeekCover();
    Story_RefreshWeekList();

    if (WeekRun_Missing()[0]) {
        char message[128];
        snprintf(message, sizeof(message), "SKIPPED (MISSING): %.96s", WeekRun_Missing());
        Story_SetStatus(message);
    }
}

/* ------------------------------------------------------------------------- */
/* Update                                                                    */
/* ------------------------------------------------------------------------- */

static void StoryState_Update([[maybe_unused]] RayScene *scene)
{
    const float dt = RayGame_DeltaTime();
    const float safeDt = dt > 0.0f ? dt : 0.0f;

    if (statusTimer > 0.0f) {
        statusTimer -= safeDt;
        if (statusTimer <= 0.0f) {
            statusTimer = 0.0f;
            statusText[0] = '\0';
        }
    }

    discAngle += safeDt * 0.65f;
    if (discAngle >= 6.28318530718f)
        discAngle -= 6.28318530718f;

    if (deleteConfirm) {
        if (IsKeyPressed(KEY_Y)) {
            const int deleted = weeks && selected >= 0 && selected < weekCount
                              ? Week_Delete(&weeks[selected]) : 0;
            Story_SetStatus(deleted ? "WEEK DELETED" : "COULD NOT DELETE WEEK");
            deleteConfirm = 0;
            Story_RefreshWeekList();
            return;
        }

        if (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE) ||
            IsKeyPressed(KEY_BACKSPACE)) {
            deleteConfirm = 0;
            return;
        }
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        MenuState_SetScene();
        return;
    }

    if (weekCount <= 0)
        return;

    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
        selected = (selected + 1) % weekCount;
        Story_LoadWeekCover();
    } else if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
        selected = (selected - 1 + weekCount) % weekCount;
        Story_LoadWeekCover();
    }

    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_SLASH) ||
        IsKeyPressed(KEY_KP_DIVIDE)) {
        deleteConfirm = 1;
        return;
    }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        Story_StartSelectedWeek();
        return;
    }

    /* Exponential smoothing avoids overshoot and behaves consistently at FPS changes. */
    {
        const float factor = 1.0f - expf(-12.0f * safeDt);
        listOffset += ((float)selected - listOffset) * factor;
    }
}

/* ------------------------------------------------------------------------- */
/* Drawing helpers                                                           */
/* ------------------------------------------------------------------------- */

static void Story_DrawPanel(Rectangle rect, Color fill, Color border)
{
    DrawRectangleRounded(rect, 0.045f, 8, fill);
    DrawRectangleLinesEx(rect, 1.0f, border);
}

static void Story_DrawCover(void)
{
    const Rectangle card = {STORY_COVER_X - 18.0f, STORY_COVER_Y - 18.0f,
                             STORY_COVER_SIZE + 36.0f, STORY_COVER_SIZE + 36.0f};
    const Rectangle destination = {STORY_COVER_X, STORY_COVER_Y,
                                   STORY_COVER_SIZE, STORY_COVER_SIZE};

    Story_DrawPanel(card, COLOR_PANEL_ALT, COLOR_BORDER);

    if (weekCoverLoaded && weekCover.id != 0) {
        Rectangle source = {0.0f, 0.0f,
                            (float)weekCover.width, (float)weekCover.height};

        /* Center-crop into a square without stretching the week artwork. */
        if (source.width > source.height) {
            source.x = (source.width - source.height) * 0.5f;
            source.width = source.height;
        } else if (source.height > source.width) {
            source.y = (source.height - source.width) * 0.5f;
            source.height = source.width;
        }

        DrawTexturePro(weekCover, source, destination, VECTOR_ZERO, 0.0f, WHITE);
    } else {
        DrawRectangleRec(destination, (Color){28, 33, 52, 255});
        DrawTextEx(mainFont, "NO WEEK ART", (Vector2){STORY_COVER_X + 43.0f,
                   STORY_COVER_Y + 98.0f}, 19.0f, 1.0f, COLOR_MUTED);
    }

    DrawRectangleLinesEx(destination, 3.0f, (Color){235, 220, 180, 230});
}

static void Story_DrawDisc(void)
{
    /* Small vinyl directly below the square week artwork. */
    const Vector2 center = {
        STORY_COVER_X + STORY_COVER_SIZE * 0.5f,
        390.0f
    };
    const float radius = 29.0f;
    const float c = cosf(discAngle);
    const float s = sinf(discAngle);
    const Vector2 marker = {center.x + 24.0f * c, center.y + 24.0f * s};

    DrawCircleV(center, radius + 4.0f, (Color){4, 6, 13, 150});
    DrawCircleV(center, radius, (Color){19, 22, 35, 255});
    DrawCircleLines((int)center.x, (int)center.y, radius - 2.0f,
                    (Color){105, 117, 150, 210});
    DrawCircleLines((int)center.x, (int)center.y, 20.0f,
                    (Color){62, 72, 103, 190});
    DrawCircleLines((int)center.x, (int)center.y, 12.0f,
                    (Color){62, 72, 103, 170});
    DrawCircleV(center, 7.0f, (Color){104, 79, 153, 255});
    DrawCircleV(center, 2.5f, (Color){231, 232, 242, 255});
    DrawLineEx(center, marker, 1.5f, (Color){255, 218, 139, 210});
    DrawCircleV(marker, 2.0f, COLOR_ACCENT);
}

static void Story_DrawWeekList(void)
{
    const float visibleTop = STORY_LIST_Y;
    const float visibleBottom = 642.0f;

    int first = (int)floorf(listOffset - STORY_VISIBLE_MARGIN / STORY_ROW_H) - 1;
    int last = (int)ceilf(listOffset +
                          (visibleBottom - visibleTop + STORY_VISIBLE_MARGIN) / STORY_ROW_H) + 1;
    if (first < 0) first = 0;
    if (last >= weekCount) last = weekCount - 1;

    for (int i = first; i <= last; ++i) {
        const float y = STORY_LIST_Y + ((float)i - listOffset) * STORY_ROW_H;
        const Rectangle row = {STORY_LIST_X, y, STORY_LIST_W, 62.0f};
        const int isSelected = i == selected;

        if (y + row.height < visibleTop - STORY_VISIBLE_MARGIN ||
            y > visibleBottom + STORY_VISIBLE_MARGIN)
            continue;

        if (isSelected) {
            Story_DrawPanel(row, (Color){59, 49, 75, 242},
                            (Color){255, 205, 92, 220});
            DrawRectangle((int)row.x, (int)row.y + 8, 4, 46, COLOR_ACCENT);
        } else {
            DrawRectangleRounded(row, 0.08f, 6, (Color){12, 16, 29, 155});
            DrawRectangleLinesEx(row, 1.0f, (Color){60, 72, 100, 90});
        }

        {
            const char *title = weeks[i].title[0] ? weeks[i].title : "Untitled Week";
            Color color = isSelected ? COLOR_TEXT : COLOR_MUTED;
            const float fontSize = isSelected ? 29.0f : 26.0f;
            Vector2 textSize = MeasureTextEx(mainFont, title, fontSize, 1.5f);
            float maxWidth = STORY_LIST_W - 92.0f;

            if (textSize.x > maxWidth) {
                char shortened[STORY_TEXT_MAX];
                size_t length = strlen(title);
                if (length >= sizeof(shortened)) length = sizeof(shortened) - 1;
                memcpy(shortened, title, length);
                shortened[length] = '\0';

                while (length > 4 && MeasureTextEx(mainFont, shortened,
                       fontSize, 1.5f).x > maxWidth) {
                    shortened[--length] = '\0';
                }
                if (length > 3) {
                    shortened[length - 3] = '.';
                    shortened[length - 2] = '.';
                    shortened[length - 1] = '.';
                }
                DrawTextEx(mainFont, shortened,
                           (Vector2){row.x + 25.0f, row.y + 13.0f},
                           fontSize, 1.5f, color);
            } else {
                DrawTextEx(mainFont, title,
                           (Vector2){row.x + 25.0f, row.y + 13.0f},
                           fontSize, 1.5f, color);
            }

            {
                char countText[32];
                snprintf(countText, sizeof(countText), "%d SONGS", weeks[i].songCount);
                DrawTextEx(mainFont, countText,
                           (Vector2){row.x + STORY_LIST_W - 112.0f, row.y + 22.0f},
                           13.0f, 1.0f, isSelected ? COLOR_ACCENT : COLOR_MUTED);
            }
        }
    }
}

static void Story_DrawWeekDetails(void)
{
    WeekEntry *week;
    char countText[64];
    const int displayCountLimit = STORY_MAX_DISPLAY_SONGS;
    int displayCount;
    int rowsPerColumn;
    const float panelX = 760.0f;
    const float panelY = 430.0f;
    const float panelW = 460.0f;
    const float panelH = 278.0f;
    const float firstX = panelX + 18.0f;
    const float firstY = panelY + 35.0f;
    const float columnW = 148.0f;
    const float rowH = 13.7f;

    if (!weeks || selected < 0 || selected >= weekCount)
        return;

    week = &weeks[selected];
    displayCount = week->songCount;
    if (displayCount > displayCountLimit)
        displayCount = displayCountLimit;
    if (displayCount < 0)
        displayCount = 0;

    {
        const char *title = week->title[0] ? week->title : "Untitled Week";
        Vector2 size = MeasureTextEx(mainFont, title, 36.0f, 2.0f);
        float x = 1210.0f - size.x;
        if (x < 760.0f) x = 760.0f;
        DrawTextEx(mainFont, title, (Vector2){x + 2.0f, 48.0f},
                   36.0f, 2.0f, (Color){0, 0, 0, 180});
        DrawTextEx(mainFont, title, (Vector2){x, 46.0f},
                   36.0f, 2.0f, COLOR_TEXT);
    }

    Story_DrawPanel((Rectangle){panelX, panelY, panelW, panelH},
                    COLOR_PANEL, COLOR_BORDER);

    if (week->songCount > displayCountLimit) {
        snprintf(countText, sizeof(countText), "SHOWING %d OF %d TRACKS",
                 displayCount, week->songCount);
    } else {
        snprintf(countText, sizeof(countText), "%d TRACKS", week->songCount);
    }
    DrawTextEx(mainFont, countText, (Vector2){firstX, panelY + 10.0f},
               14.0f, 1.0f, COLOR_ACCENT);

    /*
     * Use three compact columns. This displays up to 50 tracks without
     * drawing them outside the panel or hiding the final songs below screen.
     */
    rowsPerColumn = (displayCount + 2) / 3;
    if (rowsPerColumn < 1) rowsPerColumn = 1;

    for (int i = 0; i < displayCount; ++i) {
        const int column = i / rowsPerColumn;
        const int row = i % rowsPerColumn;
        const char *songName = week->songs[i].song;
        char line[48];
        const float x = firstX + (float)column * columnW;
        const float y = firstY + (float)row * rowH;

        if (column >= 3)
            break;

        if (!songName || !songName[0])
            songName = "Unknown";

        snprintf(line, sizeof(line), "%02d  %.17s", i + 1, songName);
        DrawTextEx(mainFont, line, (Vector2){x, y},
                   12.5f, 1.0f, COLOR_TEXT);
    }

    if (week->songCount > STORY_MAX_DISPLAY_SONGS) {
        DrawTextEx(mainFont, "Additional tracks hidden (limit: 50)",
                   (Vector2){firstX, panelY + panelH - 17.0f},
                   10.0f, 1.0f, COLOR_MUTED);
    }

    if (WeekRun_LastTotal() > 0) {
        char last[48];
        snprintf(last, sizeof(last), "LAST SCORE  %d", WeekRun_LastTotal());
        DrawTextEx(mainFont, last, (Vector2){panelX + panelW - 170.0f,
                   panelY + 9.0f}, 12.0f, 1.0f, COLOR_GREEN);
    }

    Story_DrawDisc();
}

/* ------------------------------------------------------------------------- */
/* Draw                                                                      */
/* ------------------------------------------------------------------------- */

static void StoryState_Draw([[maybe_unused]] RayScene *scene)
{
    Render_SetCamera(&storyCamera);

    ClearBackground((Color){7, 9, 18, 255});
    if (backgroundTexture.id != 0) {
        DrawTexturePro(backgroundTexture,
                       (Rectangle){0.0f, 0.0f,
                                   (float)backgroundTexture.width,
                                   (float)backgroundTexture.height},
                       (Rectangle){0.0f, 0.0f,
                                   STORY_SCREEN_W, STORY_SCREEN_H},
                       VECTOR_ZERO, 0.0f, (Color){115, 125, 160, 90});
    }
    DrawRectangle(0, 0, STORY_SCREEN_W, STORY_SCREEN_H,
                  (Color){5, 7, 15, 145});

    DrawTextEx(mainFont, "STORY MODE", (Vector2){58.0f, 44.0f},
               19.0f, 2.0f, COLOR_ACCENT);
    DrawTextEx(mainFont, "SELECT A WEEK", (Vector2){58.0f, 72.0f},
               37.0f, 2.0f, COLOR_TEXT);
    DrawLineEx((Vector2){58.0f, 126.0f}, (Vector2){1218.0f, 126.0f},
               1.0f, COLOR_BORDER);

    if (weekCount <= 0) {
        Story_DrawPanel((Rectangle){250.0f, 245.0f, 780.0f, 190.0f},
                        COLOR_PANEL, COLOR_BORDER);
        DrawTextEx(mainFont, "NO WEEKS FOUND", (Vector2){470.0f, 285.0f},
                   34.0f, 2.0f, COLOR_TEXT);
        DrawTextEx(mainFont, "Add week JSON files to assets/weeks or assets/mods/<mod>/weeks.",
                   (Vector2){315.0f, 350.0f}, 18.0f, 1.0f, COLOR_MUTED);
    } else {
        Story_DrawWeekList();
        Story_DrawCover();
        Story_DrawWeekDetails();
    }

    if (deleteConfirm && weeks && selected >= 0 && selected < weekCount) {
        const Rectangle modal = {300.0f, 260.0f, 680.0f, 175.0f};
        char prompt[STORY_TEXT_MAX];

        DrawRectangle(0, 0, STORY_SCREEN_W, STORY_SCREEN_H,
                      (Color){0, 0, 0, 165});
        Story_DrawPanel(modal, (Color){27, 20, 34, 250}, COLOR_DANGER);
        snprintf(prompt, sizeof(prompt), "DELETE %.80s?", weeks[selected].title);
        DrawTextEx(mainFont, prompt, (Vector2){350.0f, 294.0f},
                   30.0f, 1.5f, COLOR_TEXT);
        DrawTextEx(mainFont, "Y  CONFIRM", (Vector2){390.0f, 365.0f},
                   19.0f, 1.0f, COLOR_DANGER);
        DrawTextEx(mainFont, "N  CANCEL", (Vector2){650.0f, 365.0f},
                   19.0f, 1.0f, COLOR_GREEN);
    }

    if (statusTimer > 0.0f && statusText[0]) {
        const float width = MeasureTextEx(mainFont, statusText, 18.0f, 1.0f).x + 36.0f;
        Rectangle status = {58.0f, 646.0f, width, 36.0f};
        Story_DrawPanel(status, (Color){23, 30, 43, 235}, COLOR_BORDER);
        DrawTextEx(mainFont, statusText, (Vector2){75.0f, 654.0f},
                   18.0f, 1.0f, COLOR_ACCENT);
    }

    DrawTextEx(mainFont, "UP / DOWN  SELECT     ENTER  PLAY     DEL  DELETE     ESC  BACK",
               (Vector2){58.0f, 690.0f}, 14.0f, 1.0f, COLOR_MUTED);

    Render_StopCamera();
}

/* ------------------------------------------------------------------------- */
/* Destroy                                                                   */
/* ------------------------------------------------------------------------- */

static void StoryState_Destroy([[maybe_unused]] RayScene *scene)
{
    Story_UnloadWeekCover();

    if (backgroundTexture.id != 0)
        UnloadTexture(backgroundTexture);
    backgroundTexture = (Texture2D){0};

    WeekList_Free(weeks);
    weeks = NULL;
    weekCount = 0;
    free(weekCoverCache);
    weekCoverCache = NULL;
    selected = 0;
}

static RayScene storyScene;
Scene_MakeSceneCode(
    storyScene,
    StoryState_SetScene,
    StoryState_Create,
    StoryState_Update,
    StoryState_Draw,
    StoryState_Destroy
)
