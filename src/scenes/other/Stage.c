#include "std.h"
#include "scenes/Stage.h"
#include <ctype.h>

static const char* ModRoot(const char* songDir, char out[256]) {
    const char* pm = strstr(songDir, "assets/mods/");
    if (pm == NULL) { out[0] = 0; return out; }
    const char* after = pm + 12;
    const char* slash = strchr(after, '/');
    if (slash == NULL) {
        strncpy(out, songDir, 255);
        out[255] = 0;
        return out;
    }
    const char* songs = strstr(slash, "/songs/");
    size_t n = (size_t)((songs != NULL ? songs : slash) - songDir);
    if (n >= 256) n = 255;
    memcpy(out, songDir, n);
    out[n] = 0;
    return out;
}

char Stage_FindLua(const char* stageName, const char* songDir, char out[300]) {
    if (stageName == NULL || stageName[0] == 0 || strcmp(stageName, "stage") == 0) return 0;
    if (strchr(stageName, '/') != NULL || strchr(stageName, '\\') != NULL || strstr(stageName, "..") != NULL) return 0;
    char modRoot[256] = {0};
    ModRoot(songDir, modRoot);
    char p[300];
    const char* cands[4];
    char b0[300], b1[300], b2[300], b3[300];
    int n = 0;
    if (modRoot[0] != 0) { snprintf(b0, sizeof(b0), "%s/stages/%s.lua", modRoot, stageName); cands[n++] = b0; }
    snprintf(b1, sizeof(b1), "assets/stages/%s.lua", stageName); cands[n++] = b1;
    snprintf(b2, sizeof(b2), "assets/data/stages/%s.lua", stageName); cands[n++] = b2;
    snprintf(b3, sizeof(b3), "%s/%s.lua", songDir, stageName); cands[n++] = b3;
    for (int i = 0; i < n; i++) {
        FILE* f = fopen(cands[i], "rb");
        if (f != NULL) { fclose(f); strncpy(out, cands[i], 299); out[299] = 0; return 1; }
    }
    (void)p;
    return 0;
}

typedef struct {
    char tag[64];
    int idx; // indice no array de saida, -1 = so citado (add/scroll antes do make)
} TagMap;

static int FindTag(TagMap* maps, int mapCount, const char* tag) {
    for (int i = 0; i < mapCount; i++) {
        if (strcmp(maps[i].tag, tag) == 0) return i;
    }
    return -1;
}

static int parseQuoted(const char** p, char* out, size_t n) {
    const char* s = *p;
    while (*s && *s != '\'' && *s != '"') s++;
    if (!*s) return 0;
    char q = *s++;
    size_t w = 0;
    while (*s && *s != q && w + 1 < n) out[w++] = *s++;
    out[w] = 0;
    if (*s == q) s++;
    *p = s;
    return w > 0;
}

static int parseFloat(const char** p, float* out) {
    const char* s = *p;
    while (*s && (isspace((unsigned char)*s) || *s == ',')) s++;
    char* end = NULL;
    float v = strtof(s, &end);
    if (end == s) return 0;
    *out = v;
    *p = end;
    return 1;
}

static void ArgsOf(const char* p, char* out, size_t n) {
    const char* a = strchr(p, '(');
    if (a == NULL) { out[0] = 0; return; }
    a++;
    size_t w = 0;
    int depth = 1;
    while (*a && depth > 0 && w + 1 < n) {
        if (*a == '(') depth++;
        else if (*a == ')') { depth--; if (depth == 0) break; }
        out[w++] = *a++;
    }
    out[w] = 0;
}

int Stage_ParseLua(const char* luaText, StageSprite* out, int cap, int* ignoredGlitch, int* ignoredAnim) {
    TagMap maps[STAGE_MAX_SPRITES];
    int mapCount = 0;
    int count = 0;
    int glitch = 0, anim = 0;
    const char* p = luaText;
    while (*p) {
        if (strncmp(p, "makeLuaSprite", 13) == 0) {
            char args[512] = {0};
            ArgsOf(p, args, sizeof(args));
            const char* a = args;
            char tag[64] = {0}, img[128] = {0};
            float x = 0, y = 0;
            if (parseQuoted(&a, tag, sizeof(tag)) && parseQuoted(&a, img, sizeof(img)) &&
                parseFloat(&a, &x) && parseFloat(&a, &y)) {
                int mi = FindTag(maps, mapCount, tag);
                int idx;
                if (mi >= 0 && maps[mi].idx >= 0) {
                    idx = maps[mi].idx;
                } else {
                    if (count >= cap || count >= STAGE_MAX_SPRITES) { p++; continue; }
                    idx = count++;
                    memset(out + idx, 0, sizeof(*out));
                    out[idx].scrollX = 1.0f; out[idx].scrollY = 1.0f;
                    out[idx].scaleX = 1.0f; out[idx].scaleY = 1.0f;
                    if (mi >= 0) maps[mi].idx = idx;
                    else if (mapCount < STAGE_MAX_SPRITES) {
                        strncpy(maps[mapCount].tag, tag, 63);
                        maps[mapCount].idx = idx;
                        mapCount++;
                    }
                }
                strncpy(out[idx].image, img, 127);
                out[idx].x = x; out[idx].y = y;
            }
        } else if (strncmp(p, "setLuaSpriteScrollFactor", 24) == 0 || strncmp(p, "setScrollFactor", 15) == 0) {
            char args[256] = {0};
            ArgsOf(p, args, sizeof(args));
            const char* a = args;
            char tag[64] = {0};
            float sx = 1, sy = 1;
            if (parseQuoted(&a, tag, sizeof(tag)) && parseFloat(&a, &sx)) {
                if (!parseFloat(&a, &sy)) sy = sx;
                int mi = FindTag(maps, mapCount, tag);
                int idx = (mi >= 0) ? maps[mi].idx : -1;
                if (idx < 0) {
                    if (count >= cap || count >= STAGE_MAX_SPRITES) { p++; continue; }
                    idx = count++;
                    memset(out + idx, 0, sizeof(*out));
                    out[idx].scrollX = 1.0f; out[idx].scrollY = 1.0f;
                    out[idx].scaleX = 1.0f; out[idx].scaleY = 1.0f;
                    if (mi >= 0) maps[mi].idx = idx;
                    else if (mapCount < STAGE_MAX_SPRITES) {
                        strncpy(maps[mapCount].tag, tag, 63);
                        maps[mapCount].idx = idx;
                        mapCount++;
                    }
                }
                out[idx].scrollX = sx; out[idx].scrollY = sy;
            }
        } else if (strncmp(p, "addLuaSprite", 12) == 0) {
            char args[256] = {0};
            ArgsOf(p, args, sizeof(args));
            const char* a = args;
            char tag[64] = {0};
            if (parseQuoted(&a, tag, sizeof(tag))) {
                int mi = FindTag(maps, mapCount, tag);
                int idx = (mi >= 0) ? maps[mi].idx : -1;
                if (idx < 0) {
                    if (count >= cap || count >= STAGE_MAX_SPRITES) { p++; continue; }
                    idx = count++;
                    memset(out + idx, 0, sizeof(*out));
                    out[idx].scrollX = 1.0f; out[idx].scrollY = 1.0f;
                    out[idx].scaleX = 1.0f; out[idx].scaleY = 1.0f;
                    if (mi >= 0) maps[mi].idx = idx;
                    else if (mapCount < STAGE_MAX_SPRITES) {
                        strncpy(maps[mapCount].tag, tag, 63);
                        maps[mapCount].idx = idx;
                        mapCount++;
                    }
                }
                char low[256] = {0};
                size_t w = 0;
                for (; a[w] && w + 1 < sizeof(low); w++) low[w] = (char)tolower((unsigned char)a[w]);
                out[idx].front = (strstr(low, "true") != NULL) ? 1 : 0;
            }
        } else if (strncmp(p, "scaleObject", 11) == 0 || strncmp(p, "scaleLuaSprite", 14) == 0) {
            char args[256] = {0};
            ArgsOf(p, args, sizeof(args));
            const char* a = args;
            char tag[64] = {0};
            float sx = 1, sy = 1;
            if (parseQuoted(&a, tag, sizeof(tag)) && parseFloat(&a, &sx)) {
                if (!parseFloat(&a, &sy)) sy = sx;
                int mi = FindTag(maps, mapCount, tag);
                int idx = (mi >= 0) ? maps[mi].idx : -1;
                if (idx < 0) {
                    if (count >= cap || count >= STAGE_MAX_SPRITES) { p++; continue; }
                    idx = count++;
                    memset(out + idx, 0, sizeof(*out));
                    out[idx].scrollX = 1.0f; out[idx].scrollY = 1.0f;
                    out[idx].scaleX = 1.0f; out[idx].scaleY = 1.0f;
                    if (mi >= 0) maps[mi].idx = idx;
                    else if (mapCount < STAGE_MAX_SPRITES) {
                        strncpy(maps[mapCount].tag, tag, 63);
                        maps[mapCount].idx = idx;
                        mapCount++;
                    }
                }
                out[idx].scaleX = sx; out[idx].scaleY = sy;
            }
        } else if (strncmp(p, "addGlitchEffect", 15) == 0) {
            glitch++;
        } else if (strncmp(p, "onUpdate", 8) == 0 || strncmp(p, "setProperty", 11) == 0) {
            anim++;
        }
        p++;
    }
    // tira entradas sem imagem (citadas mas nunca criadas com makeLuaSprite)
    int w = 0;
    for (int i = 0; i < count; i++) {
        if (out[i].image[0] == 0) continue;
        if (w != i) out[w] = out[i];
        w++;
    }
    if (ignoredGlitch) *ignoredGlitch = glitch;
    if (ignoredAnim) *ignoredAnim = anim;
    return w;
}

void Stage_CoverBackgrounds(StageBg* bgs, int n, float zoom,
    float dadX, float dadY, float dadOffX, float dadOffY,
    float bfX, float bfY, float bfOffX, float bfOffY) {
    if (bgs == NULL || n < 1 || zoom < 0.3f || zoom > 2.0f) return;
    float uminx = 1e30f, uminy = 1e30f, umaxx = -1e30f, umaxy = -1e30f;
    for (int i = 0; i < n; i++) {
        float w = bgs[i].imgW * bgs[i].scaleX;
        float h = bgs[i].imgH * bgs[i].scaleY;
        if (w <= 0 || h <= 0) continue;
        if (bgs[i].x < uminx) uminx = bgs[i].x;
        if (bgs[i].y < uminy) uminy = bgs[i].y;
        if (bgs[i].x + w > umaxx) umaxx = bgs[i].x + w;
        if (bgs[i].y + h > umaxy) umaxy = bgs[i].y + h;
    }
    if (uminx > umaxx || uminy > umaxy) return;
    // a camera passeia dad <-> bf (com offsets); cobre a uniao das views + margem
    float vw = 1280.0f / zoom, vh = 720.0f / zoom;
    float mx = vw * 0.10f, my = vh * 0.10f;
    float ax0 = dadX, ay0 = dadY, ax1 = dadX, ay1 = dadY;
    float px[2] = {bfX + bfOffX, dadX + dadOffX};
    float py[2] = {bfY + bfOffY, dadY + dadOffY};
    for (int i = 0; i < 2; i++) {
        if (px[i] < ax0) ax0 = px[i];
        if (px[i] > ax1) ax1 = px[i];
        if (py[i] < ay0) ay0 = py[i];
        if (py[i] > ay1) ay1 = py[i];
    }
    float qx0 = ax0 - vw / 2 - mx, qy0 = ay0 - vh / 2 - my;
    float qx1 = ax1 + vw / 2 + mx, qy1 = ay1 + vh / 2 + my;
    float uw = umaxx - uminx, uh = umaxy - uminy;
    if (uw <= 0 || uh <= 0) return;
    float s = (qx1 - qx0) / uw;
    float s2 = (qy1 - qy0) / uh;
    if (s2 > s) s = s2;
    if (s < 1) s = 1;
    for (int i = 0; i < n; i++) {
        bgs[i].x = uminx + (bgs[i].x - uminx) * s;
        bgs[i].y = uminy + (bgs[i].y - uminy) * s;
        bgs[i].scaleX *= s;
        bgs[i].scaleY *= s;
    }
    float vx0 = uminx, vy0 = uminy;
    float vx1 = uminx + uw * s, vy1 = uminy + uh * s;
    float dx = 0, dy = 0;
    if (vx0 > qx0) dx = qx0 - vx0;
    else if (vx1 < qx1) dx = qx1 - vx1;
    if (vy0 > qy0) dy = qy0 - vy0;
    else if (vy1 < qy1) dy = qy1 - vy1;
    for (int i = 0; i < n; i++) {
        bgs[i].x += dx;
        bgs[i].y += dy;
    }
}
