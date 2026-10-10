#include "std.h"
#include "scenes/ModImport.h"
#include "cJSON.h"
#include <io.h> // rmdir (apagar pastas vazias no delete)

// ---------- util ----------
static const char* MI_Base(const char* p) {
    const char* s1 = strrchr(p, '/');
    const char* s2 = strrchr(p, '\\');
    const char* b = (s1 > s2) ? s1 : s2;
    return (b != NULL) ? b + 1 : p;
}

static char MI_CiEq(const char* a, const char* b) {
    size_t i = 0;
    for(; a[i] != 0 && b[i] != 0; i++) {
        char ca = a[i], cb = b[i];
        if(ca >= 'A' && ca <= 'Z') ca += 32;
        if(cb >= 'A' && cb <= 'Z') cb += 32;
        if(ca != cb)
            return 0;
    }
    return (a[i] == 0 && b[i] == 0);
}

static char MI_EndsWithI(const char* s, const char* suf) {
    size_t ls = strlen(s), lf = strlen(suf);
    if(lf > ls)
        return 0;
    return MI_CiEq(s + ls - lf, suf);
}

// nome seguro p/ pasta/arquivo de destino (nada de traversal)
static char MI_SafeName(const char* s) {
    if(s == NULL || s[0] == 0)
        return 0;
    if(strcmp(s, ".") == 0 || strcmp(s, "..") == 0)
        return 0;
    if(strchr(s, '/') != NULL || strchr(s, '\\') != NULL || strstr(s, "..") != NULL)
        return 0;
    return 1;
}

// saneia nome do mod: so [a-zA-Z0-9_-], resto vira '_'
static void MI_SanitizeMod(char* dst, size_t n, const char* src) {
    size_t w = 0;
    for(size_t i = 0; src[i] != 0 && w + 1 < n; i++) {
        char c = src[i];
        dst[w++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') ? c : '_';
    }
    dst[w] = 0;
    if(w == 0) {
        strncpy(dst, "mod", n - 1);
        dst[n - 1] = 0;
    }
}

static char MI_Copy(const char* src, const char* dst, long* bytes) {
    FILE* in = fopen(src, "rb");
    if(in == NULL)
        return 0;
    FILE* out = fopen(dst, "wb");
    if(out == NULL) {
        fclose(in);
        return 0;
    }
    char buf[8192];
    size_t n;
    long total = 0;
    while((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if(fwrite(buf, 1, n, out) != n)
            break;
        total += (long)n;
    }
    fclose(in);
    fclose(out);
    if(bytes != NULL)
        *bytes += total;
    return 1;
}

static char MI_Stem(const char* path, char* out, size_t n) {
    const char* b = MI_Base(path);
    strncpy(out, b, n - 1);
    out[n - 1] = 0;
    char* dot = strrchr(out, '.');
    if(dot != NULL && dot != out)
        *dot = 0;
    return out[0] != 0;
}

// healthicon do character .json, ou fallback (nome do char)
static void MI_HealthIcon(const char* jsonPath, const char* fallback, char* out, size_t n) {
    strncpy(out, fallback, n - 1);
    out[n - 1] = 0;
    FILE* f = fopen(jsonPath, "rb");
    if(f == NULL)
        return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0 || size > 1024 * 1024) {
        fclose(f);
        return;
    }
    char* buf = malloc((size_t)size + 1);
    if(buf == NULL) {
        fclose(f);
        return;
    }
    if(fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return;
    }
    buf[size] = 0;
    fclose(f);
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if(root == NULL)
        return;
    cJSON* jh = cJSON_GetObjectItem(root, "healthicon");
    if(cJSON_IsString(jh) && jh->valuestring[0] != 0 && MI_SafeName(jh->valuestring)) {
        strncpy(out, jh->valuestring, n - 1);
        out[n - 1] = 0;
    }
    cJSON_Delete(root);
}

// p1/p2/stage do chart .json (p/ mapear icones de quem nao tem character .json)
static void MI_ChartActors(const char* jsonPath, char actors[][32], int* count) {
    FILE* f = fopen(jsonPath, "rb");
    if(f == NULL)
        return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0 || size > 8 * 1024 * 1024) {
        fclose(f);
        return;
    }
    char* buf = malloc((size_t)size + 1);
    if(buf == NULL) {
        fclose(f);
        return;
    }
    if(fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return;
    }
    buf[size] = 0;
    fclose(f);
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if(root == NULL)
        return;
    cJSON* js = cJSON_GetObjectItem(root, "song");
    if(js == NULL)
        js = root;
    const char* keys[3] = {"player1", "player2", "stage"};
    for(int k = 0; k < 3; k++) {
        cJSON* j = cJSON_GetObjectItem(js, keys[k]);
        if(!cJSON_IsString(j) || j->valuestring[0] == 0 || !MI_SafeName(j->valuestring))
            continue;
        char dup = 0;
        for(int i = 0; i < *count; i++) {
            if(MI_CiEq(actors[i], j->valuestring)) {
                dup = 1;
                break;
            }
        }
        if(!dup && *count < 64) {
            strncpy(actors[*count], j->valuestring, 31);
            actors[*count][31] = 0;
            (*count)++;
        }
    }
    cJSON_Delete(root);
}

// copia icone icon-<key>.png (src/icons) -> <name>.png (icons do mod). 1 = copiou.
static char MI_IconFor(const char* srcMod, const char* modIcons, const char* key,
    const char* name, ModImportStats* st) {
    char cand[4][300];
    snprintf(cand[0], sizeof(cand[0]), "%s/images/icons/icon-%s.png", srcMod, key);
    snprintf(cand[1], sizeof(cand[1]), "%s/images/icons/icon-%s.png", srcMod, name);
    snprintf(cand[2], sizeof(cand[2]), "%s/images/icons/%s.png", srcMod, key);
    snprintf(cand[3], sizeof(cand[3]), "%s/images/icons/%s.png", srcMod, name);
    char dst[300];
    snprintf(dst, sizeof(dst), "%s/%s.png", modIcons, name);
    for(int i = 0; i < 4; i++) {
        FILE* f = fopen(cand[i], "rb");
        if(f == NULL)
            continue;
        fclose(f);
        if(MI_Copy(cand[i], dst, &st->bytes)) {
            st->icons++;
            return 1;
        }
        return 0;
    }
    return 0;
}

// ---------- uso + apagar todos (tudo dentro de dstRoot) ----------
static char MI_RootIsMods(const char* dstRoot) {
    if(dstRoot == NULL || dstRoot[0] == 0)
        return 0;
    if(strstr(dstRoot, "..") != NULL)
        return 0;
    size_t L = strlen(dstRoot);
    while(L > 0 && (dstRoot[L - 1] == '/' || dstRoot[L - 1] == '\\'))
        L--;
    if(L < 4)
        return 0;
    char tail[5];
    tail[0] = dstRoot[L - 4];
    tail[1] = dstRoot[L - 3];
    tail[2] = dstRoot[L - 2];
    tail[3] = dstRoot[L - 1];
    tail[4] = 0;
    for(int i = 0; i < 4; i++) {
        if(tail[i] >= 'A' && tail[i] <= 'Z')
            tail[i] += 32;
    }
    return strcmp(tail, "mods") == 0;
}

static long MI_FileSize(const char* path) {
    FILE* f = fopen(path, "rb");
    if(f == NULL)
        return 0;
    fseek(f, 0, SEEK_END);
    long s = ftell(f);
    fclose(f);
    return (s > 0) ? s : 0;
}

// soma bytes de tudo abaixo de path (arquivos; dirs sem custo)
static void MI_WalkSize(const char* path, long* bytes, int* files) {
    if(DirectoryExists(path)) {
        FilePathList l = LoadDirectoryFilesEx(path, NULL, false);
        for(unsigned int i = 0; i < l.count; i++)
            MI_WalkSize(l.paths[i], bytes, files);
        UnloadDirectoryFiles(l);
        return;
    }
    if(FileExists(path)) {
        *bytes += MI_FileSize(path);
        (*files)++;
    }
}

// apaga tudo abaixo de path (inclusive); retorna arquivos removidos
static int MI_Wipe(const char* path) {
    int removed = 0;
    if(DirectoryExists(path)) {
        FilePathList l = LoadDirectoryFilesEx(path, NULL, false);
        for(unsigned int i = 0; i < l.count; i++)
            removed += MI_Wipe(l.paths[i]);
        UnloadDirectoryFiles(l);
        (void)rmdir(path); // pasta ja vazia (filhos removidos acima)
        return removed;
    }
    if(FileExists(path) && remove(path) == 0)
        removed++;
    return removed;
}

char ModImport_ScanMods(const char* dstRoot, ModImportUsage* out) {
    ModImportUsage u;
    memset(&u, 0, sizeof(u));
    if(out != NULL)
        *out = u;
    if(!MI_RootIsMods(dstRoot) || !DirectoryExists(dstRoot)) {
        if(out != NULL)
            *out = u;
        return 1;
    }
    FilePathList l = LoadDirectoryFilesEx(dstRoot, NULL, false);
    for(unsigned int i = 0; i < l.count; i++) {
        if(!DirectoryExists(l.paths[i]))
            continue;
        u.mods++;
        long b = 0;
        int f = 0;
        MI_WalkSize(l.paths[i], &b, &f);
        u.bytes += b;
        // musicas = subpastas de songs/ (layout pack)
        char sd[300];
        snprintf(sd, sizeof(sd), "%s/songs", l.paths[i]);
        if(DirectoryExists(sd)) {
            FilePathList s = LoadDirectoryFilesEx(sd, NULL, false);
            for(unsigned int j = 0; j < s.count; j++) {
                if(DirectoryExists(s.paths[j]))
                    u.songs++;
            }
            UnloadDirectoryFiles(s);
        }
    }
    UnloadDirectoryFiles(l);
    if(out != NULL)
        *out = u;
    return 1;
}

char ModImport_DeleteAll(const char* dstRoot, char* msg, size_t msgSize,
    int* removedMods, long* freedBytes) {
    if(removedMods != NULL)
        *removedMods = 0;
    if(freedBytes != NULL)
        *freedBytes = 0;
    if(!MI_RootIsMods(dstRoot)) {
        if(msg != NULL)
            snprintf(msg, msgSize, "delete: raiz recusada (so *mods)");
        return 0;
    }
    if(!DirectoryExists(dstRoot)) {
        if(msg != NULL)
            snprintf(msg, msgSize, "delete: nada em %s", dstRoot);
        return 1;
    }
    int mods = 0;
    long freed = 0;
    int files = 0;
    FilePathList l = LoadDirectoryFilesEx(dstRoot, NULL, false);
    for(unsigned int i = 0; i < l.count; i++) {
        char isDir = DirectoryExists(l.paths[i]);
        long b = 0;
        int f = 0;
        MI_WalkSize(l.paths[i], &b, &f);
        files += MI_Wipe(l.paths[i]);
        freed += b;
        if(isDir)
            mods++;
    }
    UnloadDirectoryFiles(l);
    if(removedMods != NULL)
        *removedMods = mods;
    if(freedBytes != NULL)
        *freedBytes = freed;
    if(msg != NULL) {
        snprintf(msg, msgSize, "delete: %d mods, %d arquivos (%ld KB) apagados de %s",
            mods, files, freed / 1024, dstRoot);
    }
    return 1;
}

// ---------- Sparrow .xml + animations[] do .json -> .animset ----------
// Layout de escrita espelha AnimationSet_LoadAnimations byte a byte:
// int count; por anim: int nameLen, name, int dataCount(=frames*6), Frame{x,y,w,h,fx,fy}.
// Offsets NAO vao no arquivo (o loader zera); o Character_Load aplica do .json.
#define MI_MAX_XMLFRAMES 2048
#define MI_MAX_CHARANIMS 64
#define MI_MAX_ANIMFRAMES 256

typedef struct { char name[128]; int x, y, w, h, fx, fy; } MI_XmlFrame;

static char MI_XmlStr(const char* tag, const char* tagEnd, const char* attr, char* out, size_t n) {
    // attr com espaco inicial e =" (ex: " name=") p/ nao casar frameX ao buscar x
    size_t al = strlen(attr);
    const char* p = tag;
    while(p + al < tagEnd) {
        if(strncmp(p, attr, al) == 0) {
            p += al;
            size_t w = 0;
            while(p < tagEnd && *p != '"' && w + 1 < n)
                out[w++] = *p++;
            out[w] = 0;
            return w > 0;
        }
        p++;
    }
    out[0] = 0;
    return 0;
}

static int MI_XmlFrames(const char* xml, MI_XmlFrame* out, int cap) {
    int n = 0;
    const char* p = xml;
    while(n < cap) {
        p = strstr(p, "<SubTexture");
        if(p == NULL)
            break;
        const char* end = strchr(p, '>');
        if(end == NULL)
            break;
        MI_XmlFrame* f = out + n;
        memset(f, 0, sizeof(*f));
        char tmp[32];
        if(!MI_XmlStr(p, end, " name=\"", f->name, sizeof(f->name))) {
            p = end + 1;
            continue;
        }
        if(!MI_XmlStr(p, end, " x=\"", tmp, sizeof(tmp))) {
            p = end + 1;
            continue;
        }
        f->x = atoi(tmp);
        if(!MI_XmlStr(p, end, " y=\"", tmp, sizeof(tmp))) {
            p = end + 1;
            continue;
        }
        f->y = atoi(tmp);
        if(!MI_XmlStr(p, end, " width=\"", tmp, sizeof(tmp))) {
            p = end + 1;
            continue;
        }
        f->w = atoi(tmp);
        if(!MI_XmlStr(p, end, " height=\"", tmp, sizeof(tmp))) {
            p = end + 1;
            continue;
        }
        f->h = atoi(tmp);
        if(f->w < 1 || f->h < 1) {
            p = end + 1;
            continue;
        }
        if(MI_XmlStr(p, end, " frameX=\"", tmp, sizeof(tmp)))
            f->fx = atoi(tmp);
        if(MI_XmlStr(p, end, " frameY=\"", tmp, sizeof(tmp)))
            f->fy = atoi(tmp);
        n++;
        p = end + 1;
    }
    return n;
}

typedef struct {
    char role[16];
    char sprite[64];
    int idx[MI_MAX_ANIMFRAMES];
    int idxCount; // 0 = todos do prefixo
} MI_CharAnim;

static char MI_RestIsDigits(const char* s) {
    if(s[0] == 0)
        return 1;
    for(; *s != 0; s++) {
        if(*s < '0' || *s > '9')
            return 0;
    }
    return 1;
}

// resto apos o prefixo: vazio, so digitos ("Dan_Left0001") ou extensao
// (frame unico tipo "dave.png" — a Psych tbm casa por prefixo puro).
static char MI_RestOk(const char* s) {
    if(s[0] == 0 || s[0] == '.')
        return 1;
    return MI_RestIsDigits(s);
}

static int MI_CharAnims(const char* jsonPath, MI_CharAnim* out, int cap) {
    FILE* f = fopen(jsonPath, "rb");
    if(f == NULL)
        return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0 || size > 1024 * 1024) {
        fclose(f);
        return 0;
    }
    char* buf = malloc((size_t)size + 1);
    if(buf == NULL) {
        fclose(f);
        return 0;
    }
    if(fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return 0;
    }
    buf[size] = 0;
    fclose(f);
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if(root == NULL)
        return 0;
    int n = 0;
    cJSON* ja = cJSON_GetObjectItem(root, "animations");
    if(cJSON_IsArray(ja)) {
        int total = cJSON_GetArraySize(ja);
        for(int i = 0; i < total && n < cap; i++) {
            cJSON* a = cJSON_GetArrayItem(ja, i);
            cJSON* jrole = cJSON_GetObjectItem(a, "anim");
            cJSON* jname = cJSON_GetObjectItem(a, "name");
            if(!cJSON_IsString(jrole) || !cJSON_IsString(jname))
                continue;
            if(jrole->valuestring[0] == 0 || jname->valuestring[0] == 0)
                continue;
            if(strlen(jname->valuestring) > 31)
                continue; // loader rejeita >31 (fatal); pula
            MI_CharAnim* o = out + n;
            strncpy(o->role, jrole->valuestring, sizeof(o->role) - 1);
            o->role[sizeof(o->role) - 1] = 0;
            strncpy(o->sprite, jname->valuestring, sizeof(o->sprite) - 1);
            o->sprite[sizeof(o->sprite) - 1] = 0;
            o->idxCount = 0;
            cJSON* jidx = cJSON_GetObjectItem(a, "indices");
            if(cJSON_IsArray(jidx)) {
                int ni = cJSON_GetArraySize(jidx);
                for(int k = 0; k < ni && o->idxCount < MI_MAX_ANIMFRAMES; k++) {
                    cJSON* ji = cJSON_GetArrayItem(jidx, k);
                    if(cJSON_IsNumber(ji) && ji->valueint >= 0)
                        o->idx[o->idxCount++] = ji->valueint;
                }
            }
            n++;
        }
    }
    cJSON_Delete(root);
    return n;
}

// base do "image": "characters/dan" -> "dan"
static void MI_CharImageBase(const char* jsonPath, const char* fallback, char* out, size_t n) {
    strncpy(out, fallback, n - 1);
    out[n - 1] = 0;
    FILE* f = fopen(jsonPath, "rb");
    if(f == NULL)
        return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0 || size > 1024 * 1024) {
        fclose(f);
        return;
    }
    char* buf = malloc((size_t)size + 1);
    if(buf == NULL) {
        fclose(f);
        return;
    }
    if(fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return;
    }
    buf[size] = 0;
    fclose(f);
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if(root == NULL)
        return;
    cJSON* ji = cJSON_GetObjectItem(root, "image");
    if(cJSON_IsString(ji) && ji->valuestring[0] != 0) {
        const char* b = ji->valuestring;
        const char* s1 = strrchr(b, '/');
        const char* s2 = strrchr(b, '\\');
        const char* s = (s1 > s2) ? s1 : s2;
        b = (s != NULL) ? s + 1 : b;
        if(b[0] != 0 && strchr(b, '/') == NULL && strchr(b, '\\') == NULL && strstr(b, "..") == NULL) {
            strncpy(out, b, n - 1);
            out[n - 1] = 0;
        }
    }
    cJSON_Delete(root);
}

typedef struct { int x, y, w, h, fx, fy; } MI_FrameOut; // igual a Frame (Render.h)

// Gera .animset a partir do .json (animations[]) + .xml (Sparrow).
// Retorna anims escritas (0 = nao escreveu nada).
static int MI_WriteAnimset(const char* charJson, const char* xmlPath, const char* animsetPath) {
    FILE* xf = fopen(xmlPath, "rb");
    if(xf == NULL)
        return 0;
    fseek(xf, 0, SEEK_END);
    long xsize = ftell(xf);
    fseek(xf, 0, SEEK_SET);
    if(xsize <= 0 || xsize > 4 * 1024 * 1024) {
        fclose(xf);
        return 0;
    }
    char* xbuf = malloc((size_t)xsize + 1);
    if(xbuf == NULL) {
        fclose(xf);
        return 0;
    }
    if(fread(xbuf, 1, (size_t)xsize, xf) != (size_t)xsize) {
        free(xbuf);
        fclose(xf);
        return 0;
    }
    xbuf[xsize] = 0;
    fclose(xf);

    MI_XmlFrame* frames = malloc(sizeof(MI_XmlFrame) * MI_MAX_XMLFRAMES);
    MI_CharAnim* anims = malloc(sizeof(MI_CharAnim) * MI_MAX_CHARANIMS);
    if(frames == NULL || anims == NULL) {
        free(xbuf);
        free(frames);
        free(anims);
        return 0;
    }
    int nf = MI_XmlFrames(xbuf, frames, MI_MAX_XMLFRAMES);
    free(xbuf);
    int na = MI_CharAnims(charJson, anims, MI_MAX_CHARANIMS);
    if(nf < 1 || na < 1) {
        free(frames);
        free(anims);
        return 0;
    }

    // resolve frames por anim (prefixo + resto so-digitos; indices[] filtra)
    static short picked[MI_MAX_CHARANIMS][MI_MAX_ANIMFRAMES];
    static short pickCount[MI_MAX_CHARANIMS];
    int kept = 0;
    for(int a = 0; a < na; a++) {
        pickCount[a] = 0;
        size_t pl = strlen(anims[a].sprite);
        short all[MI_MAX_ANIMFRAMES];
        int nall = 0;
        for(int i = 0; i < nf && nall < MI_MAX_ANIMFRAMES; i++) {
            if(strncmp(frames[i].name, anims[a].sprite, pl) != 0)
                continue;
            if(!MI_RestOk(frames[i].name + pl))
                continue;
            all[nall++] = (short)i;
        }
        if(nall == 0)
            continue;
        if(anims[a].idxCount > 0) {
            for(int k = 0; k < anims[a].idxCount && pickCount[a] < MI_MAX_ANIMFRAMES; k++) {
                int want = anims[a].idx[k];
                if(want >= 0 && want < nall)
                    picked[a][pickCount[a]++] = all[want];
            }
        } else {
            for(int k = 0; k < nall; k++)
                picked[a][pickCount[a]++] = all[k];
        }
        if(pickCount[a] > 0)
            kept++;
    }
    if(kept == 0) {
        free(frames);
        free(anims);
        return 0;
    }

    FILE* o = fopen(animsetPath, "wb");
    if(o == NULL) {
        free(frames);
        free(anims);
        return 0;
    }
    int written = 0;
    fwrite(&kept, sizeof(int), 1, o);
    for(int a = 0; a < na; a++) {
        if(pickCount[a] == 0)
            continue;
        int nl = (int)strlen(anims[a].sprite);
        int dc = (int)pickCount[a] * 6;
        fwrite(&nl, sizeof(int), 1, o);
        fwrite(anims[a].sprite, 1, (size_t)nl, o);
        fwrite(&dc, sizeof(int), 1, o);
        for(int k = 0; k < pickCount[a]; k++) {
            MI_XmlFrame* fr = frames + picked[a][k];
            MI_FrameOut fo = {fr->x, fr->y, fr->w, fr->h, fr->fx, fr->fy};
            fwrite(&fo, sizeof(fo), 1, o);
        }
        written++;
    }
    fclose(o);
    free(frames);
    free(anims);
    if(written == 0)
        remove(animsetPath);
    return written;
}

char ModImport_Run(const char* srcMod, const char* dstRoot,
    const char* modName, char* msg, size_t msgSize, ModImportStats* stats) {
    ModImportStats st;
    memset(&st, 0, sizeof(st));
    if(msg != NULL && msgSize > 0)
        msg[0] = 0;
    if(stats != NULL)
        memset(stats, 0, sizeof(*stats));
    if(srcMod == NULL || dstRoot == NULL || modName == NULL) {
        if(msg != NULL)
            snprintf(msg, msgSize, "import: caminho invalido");
        return 0;
    }

    char mod[64];
    MI_SanitizeMod(mod, sizeof(mod), modName);
    char dst[300];
    snprintf(dst, sizeof(dst), "%s/%s", dstRoot, mod);

    char srcSongs[300], srcData[300], srcWeeks[300];
    snprintf(srcSongs, sizeof(srcSongs), "%s/songs", srcMod);
    snprintf(srcData, sizeof(srcData), "%s/data", srcMod);
    snprintf(srcWeeks, sizeof(srcWeeks), "%s/weeks", srcMod);
    if(!DirectoryExists(srcSongs)) {
        if(msg != NULL)
            snprintf(msg, msgSize, "import: sem songs/ em %s", srcMod);
        return 0;
    }
    // cria arvore de destino
    {
        char d[300];
        snprintf(d, sizeof(d), "%s/songs", dst);
        if(MakeDirectory(d) != 0) {
            if(msg != NULL)
                snprintf(msg, msgSize, "import: nao criou %s", d);
            return 0;
        }
        snprintf(d, sizeof(d), "%s/weeks", dst);
        MakeDirectory(d);
        snprintf(d, sizeof(d), "%s/stages", dst);
        MakeDirectory(d);
        snprintf(d, sizeof(d), "%s/images/characters", dst);
        MakeDirectory(d);
        snprintf(d, sizeof(d), "%s/characters", dst);
        MakeDirectory(d);
        snprintf(d, sizeof(d), "%s/images/icons", dst);
        MakeDirectory(d);
    }

    // nomes p/ icones: characters + actors dos charts (dedup, max 64)
    char iconNames[64][32];
    int iconNameCount = 0;

    // ---------- musicas ----------
    FilePathList songs = LoadDirectoryFilesEx(srcSongs, "DIR", false);
    for(unsigned int i = 0; i < songs.count; i++) {
        const char* songBase = MI_Base(songs.paths[i]);
        if(!MI_SafeName(songBase))
            continue;
        char dstSong[300];
        snprintf(dstSong, sizeof(dstSong), "%s/songs/%s", dst, songBase);
        // Inst.ogg obrigatorio (scan da engine exige)
        char inst[300];
        snprintf(inst, sizeof(inst), "%s/Inst.ogg", songs.paths[i]);
        FILE* fi = fopen(inst, "rb");
        if(fi == NULL)
            continue;
        fclose(fi);
        MakeDirectory(dstSong);
        char dstInst[300];
        snprintf(dstInst, sizeof(dstInst), "%s/Inst.ogg", dstSong);
        if(!MI_Copy(inst, dstInst, &st.bytes))
            continue;
        // vozes: Voices.ogg unico e/ou par separado (igual assets/mods/high/)
        const char* vox[3] = {"Voices.ogg", "Voices-Player.ogg", "Voices-Opponent.ogg"};
        for(int v = 0; v < 3; v++) {
            char sv[300], dv[300];
            snprintf(sv, sizeof(sv), "%s/%s", songs.paths[i], vox[v]);
            snprintf(dv, sizeof(dv), "%s/%s", dstSong, vox[v]);
            FILE* fv = fopen(sv, "rb");
            if(fv == NULL)
                continue;
            fclose(fv);
            MI_Copy(sv, dv, &st.bytes);
        }
        // charts: data/<musica>/*.json (match case-insensitive), menos events.json
        char dataDir[300] = {0};
        FilePathList datas = LoadDirectoryFilesEx(srcData, "DIR", false);
        for(unsigned int d = 0; d < datas.count; d++) {
            if(MI_CiEq(MI_Base(datas.paths[d]), songBase)) {
                strncpy(dataDir, datas.paths[d], sizeof(dataDir) - 1);
                break;
            }
        }
        UnloadDirectoryFiles(datas);
        int chartsHere = 0;
        if(dataDir[0] != 0) {
            FilePathList charts = LoadDirectoryFilesEx(dataDir, ".json", false);
            for(unsigned int c = 0; c < charts.count; c++) {
                char stem[128];
                MI_Stem(charts.paths[c], stem, sizeof(stem));
                if(MI_CiEq(stem, "events")) {
                    st.skippedEvents++;
                    continue;
                }
                const char* cb = MI_Base(charts.paths[c]);
                if(!MI_SafeName(cb))
                    continue;
                char dc[300];
                snprintf(dc, sizeof(dc), "%s/%s", dstSong, cb);
                if(MI_Copy(charts.paths[c], dc, &st.bytes)) {
                    chartsHere++;
                    st.charts++;
                    MI_ChartActors(dc, iconNames, &iconNameCount);
                }
            }
            UnloadDirectoryFiles(charts);
        } else {
            // fallback: charts soltos junto do audio (menos events.json)
            FilePathList charts = LoadDirectoryFilesEx(songs.paths[i], ".json", false);
            for(unsigned int c = 0; c < charts.count; c++) {
                char stem[128];
                MI_Stem(charts.paths[c], stem, sizeof(stem));
                if(MI_CiEq(stem, "events")) {
                    st.skippedEvents++;
                    continue;
                }
                const char* cb = MI_Base(charts.paths[c]);
                if(!MI_SafeName(cb))
                    continue;
                char dc[300];
                snprintf(dc, sizeof(dc), "%s/%s", dstSong, cb);
                if(MI_Copy(charts.paths[c], dc, &st.bytes)) {
                    chartsHere++;
                    st.charts++;
                    MI_ChartActors(dc, iconNames, &iconNameCount);
                }
            }
            UnloadDirectoryFiles(charts);
        }
        // .lua solto em data/ nao entra (sem runtime Lua)
        if(dataDir[0] != 0) {
            FilePathList luas = LoadDirectoryFilesEx(dataDir, ".lua", false);
            st.skippedLua += (int)luas.count;
            UnloadDirectoryFiles(luas);
        }
        if(chartsHere > 0)
            st.songs++;
    }
    UnloadDirectoryFiles(songs);
    if(st.songs == 0) {
        if(msg != NULL)
            snprintf(msg, msgSize, "import: nenhuma musica com Inst.ogg+chart em %s", srcMod);
        return 0;
    }

    // ---------- weeks (main > raiz > extra > joke, sem duplicar base) ----------
    {
        char seen[32][64];
        int seenCount = 0;
        const char* subs[4] = {"weeks/main", "weeks", "weeks/extra", "weeks/joke"};
        for(int s = 0; s < 4; s++) {
            char wd[300];
            snprintf(wd, sizeof(wd), "%s/%s", srcMod, subs[s]);
            if(!DirectoryExists(wd))
                continue;
            FilePathList wj = LoadDirectoryFilesEx(wd, ".json", false);
            for(unsigned int j = 0; j < wj.count; j++) {
                char stem[128];
                MI_Stem(wj.paths[j], stem, sizeof(stem));
                if(!MI_SafeName(stem))
                    continue;
                char dup = 0;
                for(int k = 0; k < seenCount; k++) {
                    if(MI_CiEq(seen[k], stem)) {
                        dup = 1;
                        break;
                    }
                }
                if(dup || seenCount >= 32)
                    continue;
                strncpy(seen[seenCount], stem, 63);
                seen[seenCount][63] = 0;
                seenCount++;
                const char* wb = MI_Base(wj.paths[j]);
                char dw[300];
                snprintf(dw, sizeof(dw), "%s/weeks/%s", dst, wb);
                if(MI_Copy(wj.paths[j], dw, &st.bytes))
                    st.weeks++;
            }
            UnloadDirectoryFiles(wj);
        }
    }

    // ---------- stages (.lua + .json irmao) ----------
    {
        char sd[300];
        snprintf(sd, sizeof(sd), "%s/stages", srcMod);
        if(DirectoryExists(sd)) {
            FilePathList sl = LoadDirectoryFilesEx(sd, ".lua", false);
            for(unsigned int j = 0; j < sl.count; j++) {
                char stem[128];
                MI_Stem(sl.paths[j], stem, sizeof(stem));
                if(!MI_SafeName(stem))
                    continue;
                const char* lb = MI_Base(sl.paths[j]);
                char dl[300];
                snprintf(dl, sizeof(dl), "%s/stages/%s", dst, lb);
                if(MI_Copy(sl.paths[j], dl, &st.bytes))
                    st.stages++;
                // json irmao (posicoes): mesmo nome, .json
                char sj[300], dj[300];
                snprintf(sj, sizeof(sj), "%s/%s.json", sd, stem);
                FILE* fj = fopen(sj, "rb");
                if(fj != NULL) {
                    fclose(fj);
                    snprintf(dj, sizeof(dj), "%s/stages/%s.json", dst, stem);
                    MI_Copy(sj, dj, &st.bytes);
                }
            }
            UnloadDirectoryFiles(sl);
            // .json sem .lua irmao (stage so-json)
            FilePathList sj2 = LoadDirectoryFilesEx(sd, ".json", false);
            for(unsigned int j = 0; j < sj2.count; j++) {
                char stem[128];
                MI_Stem(sj2.paths[j], stem, sizeof(stem));
                if(!MI_SafeName(stem))
                    continue;
                char sl2[300];
                snprintf(sl2, sizeof(sl2), "%s/%s.lua", sd, stem);
                FILE* fl = fopen(sl2, "rb");
                if(fl != NULL) {
                    fclose(fl);
                    continue; // ja veio com o .lua
                }
                const char* jb = MI_Base(sj2.paths[j]);
                char dj[300];
                snprintf(dj, sizeof(dj), "%s/stages/%s", dst, jb);
                if(MI_Copy(sj2.paths[j], dj, &st.bytes))
                    st.stages++;
            }
            UnloadDirectoryFiles(sj2);
        }
    }

    // ---------- imagens (bg + personagens) ----------
    {
        char id[300];
        snprintf(id, sizeof(id), "%s/images", srcMod);
        if(DirectoryExists(id)) {
            // UMA listagem (filtro raylib e case-insensitive: .png/.PNG
            // na mesma chamada duplicariam tudo). So arquivos .png.
            FilePathList all = LoadDirectoryFilesEx(id, NULL, false);
            for(unsigned int j = 0; j < all.count; j++) {
                if(DirectoryExists(all.paths[j]))
                    continue;
                const char* b = MI_Base(all.paths[j]);
                if(!MI_SafeName(b))
                    continue;
                if(!MI_EndsWithI(b, ".png"))
                    continue;
                char di[300];
                snprintf(di, sizeof(di), "%s/images/%s", dst, b);
                if(MI_Copy(all.paths[j], di, &st.bytes))
                    st.images++;
            }
            UnloadDirectoryFiles(all);
            char cd[300];
            snprintf(cd, sizeof(cd), "%s/images/characters", srcMod);
            if(DirectoryExists(cd)) {
                FilePathList ch = LoadDirectoryFilesEx(cd, NULL, false);
                for(unsigned int j = 0; j < ch.count; j++) {
                    if(DirectoryExists(ch.paths[j]))
                        continue;
                    const char* b = MI_Base(ch.paths[j]);
                    if(!MI_SafeName(b))
                        continue;
                    if(!MI_EndsWithI(b, ".png") && !MI_EndsWithI(b, ".xml"))
                        continue;
                    char di[300];
                    snprintf(di, sizeof(di), "%s/images/characters/%s", dst, b);
                    if(MI_Copy(ch.paths[j], di, &st.bytes))
                        st.images++;
                }
                UnloadDirectoryFiles(ch);
            }
        }
    }

    // ---------- characters (.json + png/xml + .animset gerado) + icones no mod ----------
    {
        char modIcons[300];
        snprintf(modIcons, sizeof(modIcons), "%s/images/icons", dst);
        char modChars[300];
        snprintf(modChars, sizeof(modChars), "%s/images/characters", dst);
        char cd[300];
        snprintf(cd, sizeof(cd), "%s/characters", srcMod);
        char srcChars[300];
        snprintf(srcChars, sizeof(srcChars), "%s/images/characters", srcMod);
        if(DirectoryExists(cd)) {
            FilePathList ch = LoadDirectoryFilesEx(cd, ".json", false);
            for(unsigned int j = 0; j < ch.count; j++) {
                char stem[128];
                MI_Stem(ch.paths[j], stem, sizeof(stem));
                if(!MI_SafeName(stem))
                    continue;
                const char* cb = MI_Base(ch.paths[j]);
                char dc[300];
                snprintf(dc, sizeof(dc), "%s/characters/%s", dst, cb);
                if(!MI_Copy(ch.paths[j], dc, &st.bytes))
                    continue;
                st.characters++;
                // nome p/ icone
                char dup = 0;
                for(int k = 0; k < iconNameCount; k++) {
                    if(MI_CiEq(iconNames[k], stem)) {
                        dup = 1;
                        break;
                    }
                }
                if(!dup && iconNameCount < 64) {
                    strncpy(iconNames[iconNameCount], stem, 31);
                    iconNames[iconNameCount][31] = 0;
                    iconNameCount++;
                }
                // lookup da engine: .animset com o NOME do char; o png resolve
                // pela campo "image" do .json no load (sem duplicar PNG).
                char ibase[64];
                MI_CharImageBase(dc, stem, ibase, sizeof(ibase));
                // .animset a partir do xml da base (Psych: image + .xml irmao).
                // Sempre reconverte (barato; evita .animset velho de reimport).
                char sx[300], sa[300];
                snprintf(sx, sizeof(sx), "%s/%s.xml", srcChars, ibase);
                snprintf(sa, sizeof(sa), "%s/%s.animset", modChars, stem);
                if(MI_WriteAnimset(dc, sx, sa) > 0) {
                    st.animsets++;
                    st.bytes += MI_FileSize(sa);
                }
            }
            UnloadDirectoryFiles(ch);
        }
        // materializa icones: icon-<healthicon>.png -> <mod>/images/icons/<nome>.png
        for(int k = 0; k < iconNameCount; k++) {
            char hpath[300];
            snprintf(hpath, sizeof(hpath), "%s/characters/%s.json", dst, iconNames[k]);
            char h[32];
            MI_HealthIcon(hpath, iconNames[k], h, sizeof(h));
            // stage nao e personagem: icon ausente so nao copia (sem erro).
            MI_IconFor(srcMod, modIcons, h, iconNames[k], &st);
        }
    }

    if(stats != NULL)
        *stats = st;
    if(msg != NULL) {
        snprintf(msg, msgSize, "import %s: %d musicas, %d charts, %d weeks, %d stages, %d imgs, %d chars, %d animsets, %d icones (%ld KB)",
            mod, st.songs, st.charts, st.weeks, st.stages, st.images, st.characters, st.animsets, st.icons, st.bytes / 1024);
    }
    return 1;
}
