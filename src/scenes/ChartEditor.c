#include"scenes/AllScenes.h"
#include"scenes/Song.h"
#include"scenes/SongList.h"
#include"scenes/Stage.h"
#include"scenes/Note.h"
#include"cJSON.h"

// Chart Editor v1: palavras simples, sem jargao.
// PICKER (escolher musica / abrir json) -> EDIT (grade) -> F5 testa no jogo.

static Color LaneColor(int lane);
static Vector2 MouseWorld(void);
static SongEntry* pickerSongs;
static int pickerCount;

typedef struct {
    float time; // ms
    int lane;   // 0-7 (0-3 oponente, 4-7 voce)
    float len;  // ms de nota longa, 0 = normal
} EdNote;

typedef struct {
    char mustHit; // 1 = sua vez, 0 = vez do oponente
    float bpm;
    char changeBPM;
    int lenSteps;
    EdNote* notes;
    int count;
    int cap;
} EdSec;

typedef struct {
    char name[64];
    char p1[32];
    char p2[32];
    char stage[32];
    char gf[32]; // gfVersion Psych: "gf", "nogf"/"" (so preserva no save, sem UI)
    char noteSkin[32]; // custom note skin (mod > global default)
    char dir[256]; // pasta da musica ("" = ainda nao salva)
    float bpm;
    float speed;
    EdSec* secs;
    int nsec;
    char dirty;
} EdChart;

static EdChart chart;
static char chartLoaded = 0;

// ---------- undo (pilha de copias) ----------
#define UNDO_MAX 50
static EdChart undoStack[UNDO_MAX];
static int undoCount = 0;

static void EdChart_Free(EdChart* c) {
    if(c->secs != NULL) {
        for(int i = 0; i < c->nsec; i++)
            free(c->secs[i].notes);
        free(c->secs);
        c->secs = NULL;
    }
    c->nsec = 0;
}

static void EdChart_CopyInto(EdChart* dst, const EdChart* src) {
    EdChart_Free(dst);
    *dst = *src; // copia escalares + dir/name
    dst->secs = NULL;
    dst->nsec = 0;
    if(src->nsec > 0) {
        dst->secs = malloc(sizeof(EdSec) * (size_t)src->nsec);
        dst->nsec = src->nsec;
        for(int i = 0; i < src->nsec; i++) {
            dst->secs[i] = src->secs[i];
            dst->secs[i].notes = NULL;
            dst->secs[i].count = 0;
            dst->secs[i].cap = 0;
            if(src->secs[i].count > 0) {
                dst->secs[i].notes = malloc(sizeof(EdNote) * (size_t)src->secs[i].count);
                dst->secs[i].cap = src->secs[i].count;
                dst->secs[i].count = src->secs[i].count;
                memcpy(dst->secs[i].notes, src->secs[i].notes, sizeof(EdNote) * (size_t)src->secs[i].count);
            }
        }
    }
}

static void Undo_Push(void) {
    if(undoCount >= UNDO_MAX) {
        EdChart_Free(undoStack);
        memmove(undoStack, undoStack + 1, sizeof(EdChart) * (UNDO_MAX - 1));
        undoCount = UNDO_MAX - 1;
    }
    memset(undoStack + undoCount, 0, sizeof(EdChart));
    EdChart_CopyInto(undoStack + undoCount, &chart);
    undoCount++;
}

static void Undo_Clear(void) {
    for(int i = 0; i < undoCount; i++)
        EdChart_Free(undoStack + i);
    undoCount = 0;
}

static void Undo_Pop(void) {
    if(undoCount <= 0)
        return;
    undoCount--;
    EdChart_Free(&chart);
    memset(&chart, 0, sizeof(chart));
    EdChart_CopyInto(&chart, undoStack + undoCount);
    EdChart_Free(undoStack + undoCount);
}

// ---------- notas ----------
static int EdNote_Cmp(const void* a, const void* b) {
    float ta = ((const EdNote*)a)->time;
    float tb = ((const EdNote*)b)->time;
    if(ta < tb) return -1;
    if(ta > tb) return 1;
    return 0;
}

static void EdSec_Sort(EdSec* s) {
    if(s->count > 1)
        qsort(s->notes, (size_t)s->count, sizeof(EdNote), EdNote_Cmp);
}

static void EdSec_Add(EdSec* s, float time, int lane, float len) {
    // tira duplicada (mesma casa, <2ms) igual ao jogo
    for(int i = 0; i < s->count; i++) {
        if(s->notes[i].lane == lane && fabsf(s->notes[i].time - time) < 2.0f)
            return;
    }
    if(s->count >= s->cap) {
        int nc = (s->cap == 0) ? 8 : s->cap * 2;
        EdNote* g = realloc(s->notes, sizeof(EdNote) * (size_t)nc);
        if(g == NULL) return;
        s->notes = g;
        s->cap = nc;
    }
    s->notes[s->count].time = time;
    s->notes[s->count].lane = lane;
    s->notes[s->count].len = (len < 0) ? 0 : len;
    s->count++;
    EdSec_Sort(s);
}

// bulk: anexa sem dedup/sort (import/paste em massa). Fecha com FinishBulk.
static void EdSec_AddBulk(EdSec* s, float time, int lane, float len) {
    if(s->count >= s->cap) {
        int nc = (s->cap == 0) ? 1024 : s->cap * 2;
        EdNote* g = realloc(s->notes, sizeof(EdNote) * (size_t)nc);
        if(g == NULL) return;
        s->notes = g;
        s->cap = nc;
    }
    s->notes[s->count].time = time;
    s->notes[s->count].lane = lane;
    s->notes[s->count].len = (len < 0) ? 0 : len;
    s->count++;
}

// ordena + dedup adjacente (mesma casa <2ms). Equivale ao Add um-a-um:
// contra o set ja filtrado, so o vizinho anterior da mesma casa importa.
static void EdSec_FinishBulk(EdSec* s) {
    if(s->count < 2) return;
    EdSec_Sort(s);
    float lastT[8];
    char seen[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for(int i = 0; i < 8; i++)
        lastT[i] = -1000000.0f;
    int w = 0;
    for(int r = 0; r < s->count; r++) {
        int lane = s->notes[r].lane;
        if(lane >= 0 && lane < 8) {
            if(seen[lane] && fabsf(s->notes[r].time - lastT[lane]) < 2.0f)
                continue;
            seen[lane] = 1;
            lastT[lane] = s->notes[r].time;
        }
        if(w != r)
            s->notes[w] = s->notes[r];
        w++;
    }
    s->count = w;
}

// lado da tela: no editor VOCE fica sempre a direita (igual ao jogo).
// vira a casa pra exibir; a funcao e a propria inversa (vale pros 2 lados)
static int DispLane(const EdSec* s, int lane) {
    if(s->mustHit)
        return (lane < 4) ? lane + 4 : lane - 4;
    return lane;
}

// indice da nota perto, ou -1
static int EdSec_FindNear(EdSec* s, float time, int lane, float windowMs) {
    int best = -1;
    float bestD = windowMs;
    for(int i = 0; i < s->count; i++) {
        if(s->notes[i].lane != lane)
            continue;
        float d = fabsf(s->notes[i].time - time);
        if(d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

static char EdSec_RemoveAt(EdSec* s, int idx) {
    if(idx < 0 || idx >= s->count)
        return 0;
    memmove(s->notes + idx, s->notes + idx + 1, sizeof(EdNote) * (size_t)(s->count - idx - 1));
    s->count--;
    return 1;
}

// devolve 1 se removeu
static char EdSec_RemoveNear(EdSec* s, float time, int lane, float windowMs) {
    return EdSec_RemoveAt(s, EdSec_FindNear(s, time, lane, windowMs));
}

// ---------- carregar ----------
static void EdChart_Default(EdChart* c, const char* name) {
    memset(c, 0, sizeof(*c));
    strncpy(c->name, name, sizeof(c->name) - 1);
    strncpy(c->p1, "bf", sizeof(c->p1) - 1);
    strncpy(c->p2, "dad", sizeof(c->p2) - 1);
    strncpy(c->stage, "stage", sizeof(c->stage) - 1);
    strncpy(c->gf, "gf", sizeof(c->gf) - 1);
    c->bpm = 100.0f;
    c->speed = 1.0f;
    c->secs = calloc(1, sizeof(EdSec));
    c->nsec = 1;
    c->secs[0].mustHit = 1;
    c->secs[0].bpm = 100.0f;
    c->secs[0].changeBPM = 1;
    c->secs[0].lenSteps = 16;
}

// json FNF cru (lendo lanes 0-7 direto, sem converter)
static char EdChart_LoadJSON(EdChart* c, const char* jsonPath, const char* dir, const char* fallbackName) {
    FILE* f = fopen(jsonPath, "rb");
    if(f == NULL)
        return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0 || size > 8 * 1024 * 1024) {
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
    cJSON* js = cJSON_GetObjectItem(root, "song");
    if(js == NULL)
        js = root;

    cJSON* jnotes = cJSON_GetObjectItem(js, "notes");
    if(!cJSON_IsArray(jnotes)) {
        cJSON_Delete(root);
        return 0;
    }

    EdChart tmp;
    EdChart_Default(&tmp, fallbackName);
    strncpy(tmp.dir, dir, sizeof(tmp.dir) - 1);

    cJSON* j = cJSON_GetObjectItem(js, "song");
    if(cJSON_IsString(j)) strncpy(tmp.name, j->valuestring, sizeof(tmp.name) - 1);
    j = cJSON_GetObjectItem(js, "bpm");
    if(cJSON_IsNumber(j) && j->valuedouble >= 10) tmp.bpm = (float)j->valuedouble;
    j = cJSON_GetObjectItem(js, "speed");
    if(cJSON_IsNumber(j) && j->valuedouble > 0) tmp.speed = (float)j->valuedouble;
    j = cJSON_GetObjectItem(js, "player1");
    if(cJSON_IsString(j)) strncpy(tmp.p1, j->valuestring, sizeof(tmp.p1) - 1);
    j = cJSON_GetObjectItem(js, "player2");
    if(cJSON_IsString(j)) strncpy(tmp.p2, j->valuestring, sizeof(tmp.p2) - 1);
    j = cJSON_GetObjectItem(js, "stage");
    if(cJSON_IsString(j)) strncpy(tmp.stage, j->valuestring, sizeof(tmp.stage) - 1);
    j = cJSON_GetObjectItem(js, "gfVersion");
    if(cJSON_IsString(j)) strncpy(tmp.gf, j->valuestring, sizeof(tmp.gf) - 1);
    // noteSkin: custom note skin name
    j = cJSON_GetObjectItem(js, "noteSkin");
    if(cJSON_IsString(j)) strncpy(tmp.noteSkin, j->valuestring, sizeof(tmp.noteSkin) - 1);
    else
        tmp.noteSkin[0] = 0;

    int nsec = cJSON_GetArraySize(jnotes);
    if(nsec > 0) {
        EdChart_Free(&tmp);
        tmp.secs = calloc((size_t)nsec, sizeof(EdSec));
        tmp.nsec = nsec;
        float runBpm = tmp.bpm;
        for(int i = 0; i < nsec; i++) {
            cJSON* jsec = cJSON_GetArrayItem(jnotes, i);
            EdSec* s = tmp.secs + i;
            s->lenSteps = 16;
            s->bpm = runBpm;
            if(!cJSON_IsObject(jsec))
                continue;
            cJSON* jl = cJSON_GetObjectItem(jsec, "lengthInSteps");
            if(jl == NULL) jl = cJSON_GetObjectItem(jsec, "lengthinsteps");
            if(cJSON_IsNumber(jl) && jl->valuedouble >= 1) s->lenSteps = (int)jl->valuedouble;
            if(cJSON_IsTrue(cJSON_GetObjectItem(jsec, "mustHitSection"))) s->mustHit = 1;
            cJSON* jb = cJSON_GetObjectItem(jsec, "bpm");
            if(cJSON_IsTrue(cJSON_GetObjectItem(jsec, "changeBPM")) && cJSON_IsNumber(jb) && jb->valuedouble >= 10) {
                runBpm = (float)jb->valuedouble;
                s->changeBPM = 1;
            }
            s->bpm = runBpm;
            cJSON* jarr = cJSON_GetObjectItem(jsec, "sectionNotes");
            if(!cJSON_IsArray(jarr))
                continue;
            int rn = cJSON_GetArraySize(jarr);
            for(int k = 0; k < rn; k++) {
                cJSON* jn = cJSON_GetArrayItem(jarr, k);
                if(!cJSON_IsArray(jn) || cJSON_GetArraySize(jn) < 2)
                    continue;
                cJSON* jt = cJSON_GetArrayItem(jn, 0);
                cJSON* ji = cJSON_GetArrayItem(jn, 1);
                cJSON* jln = cJSON_GetArraySize(jn) > 2 ? cJSON_GetArrayItem(jn, 2) : NULL;
                if(!cJSON_IsNumber(jt) || !cJSON_IsNumber(ji) || jt->valuedouble < 0)
                    continue;
                int lane = ((int)ji->valuedouble) % 8;
                if(lane < 0) lane += 8;
                float len = (cJSON_IsNumber(jln) && jln->valuedouble > 0) ? (float)jln->valuedouble : 0;
                EdSec_AddBulk(s, (float)jt->valuedouble, lane, len);
            }
            EdSec_FinishBulk(s);
        }
    }

    cJSON_Delete(root);
    EdChart_Free(c);
    *c = tmp;
    return 1;
}

// binario .song antigo -> converte ids de volta pra lanes 0-7
static char EdChart_LoadBinary(EdChart* c, const char* binPath, const char* dir, const char* fallbackName) {
    Song s;
    memset(&s, 0, sizeof(s));
    // Song_Parse sai do jogo se o arquivo for invalido; checa antes
    FILE* t = fopen(binPath, "rb");
    if(t == NULL)
        return 0;
    fclose(t);
    Song_Parse(&s, binPath);

    EdChart tmp;
    EdChart_Default(&tmp, fallbackName);
    strncpy(tmp.dir, dir, sizeof(tmp.dir) - 1);
    strncpy(tmp.p1, s.player1, sizeof(tmp.p1) - 1);
    strncpy(tmp.p2, s.player2, sizeof(tmp.p2) - 1);
    strncpy(tmp.stage, s.stage, sizeof(tmp.stage) - 1);
    strncpy(tmp.gf, s.gfVersion, sizeof(tmp.gf) - 1);
    strncpy(tmp.noteSkin, s.noteSkin, sizeof(tmp.noteSkin) - 1);
    tmp.speed = (s.speed > 0) ? s.speed : 1.0f;
    tmp.bpm = (s.sectionCount > 0) ? s.sections[0].bpm : 100.0f;

    if(s.sectionCount > 0) {
        EdChart_Free(&tmp);
        tmp.secs = calloc((size_t)s.sectionCount, sizeof(EdSec));
        tmp.nsec = s.sectionCount;
        for(int i = 0; i < s.sectionCount; i++) {
            Section* src = s.sections + i;
            EdSec* dst = tmp.secs + i;
            dst->mustHit = src->mustHit;
            dst->bpm = src->bpm;
            dst->changeBPM = (i == 0) || (src->bpm != s.sections[i - 1].bpm);
            dst->lenSteps = (src->len >= 1) ? src->len : 16;
            for(int k = 0; k < src->noteCount; k++) {
                DataNote* dn = src->notes + k;
                int lane;
                if(src->mustHit) {
                    lane = (dn->id < 0) ? (-dn->id - 1) : (dn->id + 3);
                } else {
                    lane = (dn->id > 0) ? (dn->id - 1) : (3 - dn->id);
                }
                if(lane < 0 || lane > 7)
                    continue;
                EdSec_AddBulk(dst, dn->time, lane, dn->len);
            }
            EdSec_FinishBulk(dst);
        }
    }

    Song_Free(&s);
    EdChart_Free(c);
    *c = tmp;
    return 1;
}

static const char* DirName(const char* path) {
    const char* a = strrchr(path, '/');
    const char* b = strrchr(path, '\\');
    const char* s = (a > b) ? a : b;
    return (s != NULL) ? s + 1 : path;
}

// abre pasta de musica (data.json, senao Psych <pasta>.json, senao qualquer chart, senao binario)
// corrige NAO ABRIA mods tipo milf/milf.json e satin-panties/*.json
static char EdLooksLikeChart(const char* path) {
    FILE* f = fopen(path, "rb");
    if(f == NULL) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0 || size > 8 * 1024 * 1024) { fclose(f); return 0; }
    char* buf = malloc((size_t)size + 1);
    if(buf == NULL) { fclose(f); return 0; }
    if(fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    buf[size] = 0;
    fclose(f);
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if(root == NULL) return 0;
    cJSON* js = cJSON_GetObjectItem(root, "song");
    if(js == NULL) js = root;
    cJSON* jn = cJSON_GetObjectItem(js, "notes");
    char ok = (cJSON_IsArray(jn) && cJSON_GetArraySize(jn) > 0);
    cJSON_Delete(root);
    return ok;
}

static char EdChart_OpenDir(EdChart* c, const char* dir) {
    char p[300];
    snprintf(p, sizeof(p), "%s/data.json", dir);
    if(EdChart_LoadJSON(c, p, dir, DirName(dir)))
        return 1;
    // Psych: <pasta>.json (milf/milf.json)
    snprintf(p, sizeof(p), "%s/%s.json", dir, DirName(dir));
    if(EdLooksLikeChart(p) && EdChart_LoadJSON(c, p, dir, DirName(dir)))
        return 1;
    // qualquer outro *.json que pareca chart (satin-panties-*.json, etc)
    if(DirectoryExists(dir)) {
        FilePathList list = LoadDirectoryFilesEx(dir, ".json", false);
        for(unsigned int i = 0; i < list.count; i++) {
            const char* jp = list.paths[i];
            char stem[128];
            const char* s1 = strrchr(jp, '/');
            const char* s2 = strrchr(jp, '\\');
            const char* b = (s1 > s2) ? s1 : s2;
            b = (b != NULL) ? b + 1 : jp;
            strncpy(stem, b, sizeof(stem) - 1);
            stem[sizeof(stem) - 1] = 0;
            if(strcmp(stem, "events.json") == 0 || strcmp(stem, "data.json") == 0)
                continue;
            if(EdLooksLikeChart(jp) && EdChart_LoadJSON(c, jp, dir, DirName(dir))) {
                UnloadDirectoryFiles(list);
                return 1;
            }
        }
        UnloadDirectoryFiles(list);
    }
    snprintf(p, sizeof(p), "%s/data.song", dir);
    if(EdChart_LoadBinary(c, p, dir, DirName(dir)))
        return 1;
    return 0;
}

// forward
static char EdChart_SaveEx(const EdChart* c, char compressed);

// ---------- salvar (json FNF) ----------
static char EdChart_Save(const EdChart* c) {
    return EdChart_SaveEx(c, 0);
}

// ---------- salvar compacto (json minificado, campos padrão omitidos) ----------
static char EdChart_SaveCompressed(const EdChart* c) {
    return EdChart_SaveEx(c, 1);
}

static char EdChart_SaveEx(const EdChart* c, char compressed) {
    if(c->dir[0] == 0)
        return 0;
    cJSON* root = cJSON_CreateObject();
    cJSON* js = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "song", js);
    cJSON_AddStringToObject(js, "song", c->name);
    if(!compressed || c->bpm != 100.0f) cJSON_AddNumberToObject(js, "bpm", c->bpm);
    if(!compressed || c->speed != 1.0f) cJSON_AddNumberToObject(js, "speed", c->speed);
    cJSON_AddStringToObject(js, "player1", c->p1);
    cJSON_AddStringToObject(js, "player2", c->p2);
    cJSON_AddStringToObject(js, "stage", c->stage);
    if(!compressed || (c->gf[0] != 0 && strcmp(c->gf, "gf") != 0))
        cJSON_AddStringToObject(js, "gfVersion", c->gf[0] != 0 ? c->gf : "gf");
    if(!compressed || c->noteSkin[0] != 0)
        cJSON_AddStringToObject(js, "noteSkin", c->noteSkin);
    cJSON* jnotes = cJSON_CreateArray();
    cJSON_AddItemToObject(js, "notes", jnotes);
    float runBpm = c->bpm;
    for(int i = 0; i < c->nsec; i++) {
        const EdSec* s = c->secs + i;
        cJSON* jsec = cJSON_CreateObject();
        cJSON_AddItemToArray(jnotes, jsec);
        if(!compressed || s->lenSteps != 16)
            cJSON_AddNumberToObject(jsec, "lengthInSteps", s->lenSteps);
        if(!compressed || s->mustHit)
            cJSON_AddBoolToObject(jsec, "mustHitSection", s->mustHit);
        char change = (s->bpm != runBpm);
        if(change) runBpm = s->bpm;
        if(!compressed || change)
            cJSON_AddNumberToObject(jsec, "bpm", s->bpm);
        if(!compressed || change)
            cJSON_AddBoolToObject(jsec, "changeBPM", change);
        cJSON* jarr = cJSON_CreateArray();
        cJSON_AddItemToObject(jsec, "sectionNotes", jarr);
        for(int k = 0; k < s->count; k++) {
            cJSON* jn = cJSON_CreateArray();
            cJSON_AddItemToArray(jarr, jn);
            cJSON_AddItemToArray(jn, cJSON_CreateNumber(s->notes[k].time));
            cJSON_AddItemToArray(jn, cJSON_CreateNumber(s->notes[k].lane));
            if(!compressed || s->notes[k].len != 0.0f)
                cJSON_AddItemToArray(jn, cJSON_CreateNumber(s->notes[k].len));
        }
    }
    char* txt = compressed ? cJSON_PrintUnformatted(root) : cJSON_Print(root);
    cJSON_Delete(root);
    if(txt == NULL)
        return 0;
    char p[300];
    snprintf(p, sizeof(p), "%s/data.json", c->dir);
    FILE* f = fopen(p, "w");
    if(f == NULL) {
        cJSON_free(txt);
        return 0;
    }
    fwrite(txt, 1, strlen(txt), f);
    fclose(f);
    cJSON_free(txt);
    return 1;
}

// ---------- util ----------
static void StatusMsg(const char* m);

static char CopyFile(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if(in == NULL) return 0;
    FILE* out = fopen(dst, "wb");
    if(out == NULL) {
        fclose(in);
        return 0;
    }
    char buf[8192];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 1;
}

static void SanitizeName(char* dst, size_t dstSize, const char* src) {
    size_t w = 0;
    for(size_t i = 0; src[i] != 0 && w + 1 < dstSize; i++) {
        char ch = src[i];
        if((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')
            dst[w++] = ch;
    }
    dst[w] = 0;
}

// ---------- caixinha de texto (clica e digita) ----------
typedef struct {
    char buf[128];
} TxtBox;
static TxtBox* focusBox = NULL;
static float backTimer = 0;

// caixinha de numero (digita e ENTER confirma)
typedef struct {
    char buf[16];
} NumBox;
static NumBox* focusNum = NULL;
static NumBox nbSecBpm;
static NumBox nbTopBpm;
static NumBox nbSpeed;
static NumBox nbLen;

static void NumBox_Refresh(NumBox* nb, float v, char isInt) {
    if(isInt)
        snprintf(nb->buf, sizeof(nb->buf), "%d", (int)v);
    else if(v == (int)v)
        snprintf(nb->buf, sizeof(nb->buf), "%.0f", (double)v);
    else
        snprintf(nb->buf, sizeof(nb->buf), "%.1f", (double)v);
}

static void TxtBox_Update(TxtBox* box, Rectangle r, char blocked) {
    if(blocked)
        return;
    Vector2 m = MouseWorld();
    if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if(m.x >= r.x && m.x <= r.x + r.width && m.y >= r.y && m.y <= r.y + r.height) {
            focusBox = box;
            focusNum = NULL;
        } else if(focusBox == box) {
            focusBox = NULL;
        }
    }
    if(focusBox != box)
        return;
    int ch;
    while((ch = GetCharPressed()) > 0) {
        size_t len = strlen(box->buf);
        if(ch >= 32 && ch < 127 && len + 1 < sizeof(box->buf)) {
            box->buf[len] = (char)ch;
            box->buf[len + 1] = 0;
        }
    }
    int ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if(ctrl && IsKeyPressed(KEY_V)) {
        const char* clip = GetClipboardText();
        if(clip != NULL) {
            for(size_t ci = 0; clip[ci] != 0; ci++) {
                size_t len = strlen(box->buf);
                if(len + 1 >= sizeof(box->buf))
                    break;
                if(clip[ci] >= 32 && clip[ci] < 127) {
                    box->buf[len] = clip[ci];
                    box->buf[len + 1] = 0;
                }
            }
        }
    }
    if(ctrl && IsKeyPressed(KEY_C) && box->buf[0] != 0)
        SetClipboardText(box->buf);
    if(IsKeyDown(KEY_BACKSPACE)) {
        backTimer -= RayGame_DeltaTime();
        if(IsKeyPressed(KEY_BACKSPACE) || backTimer <= 0) {
            size_t len = strlen(box->buf);
            if(len > 0) box->buf[len - 1] = 0;
            backTimer = 0.05f;
        }
    } else {
        backTimer = 0;
    }
}

static void TxtBox_Draw(TxtBox* box, Rectangle r, const char* hint) {
    DrawRectangleRec(r, (Color) {20, 20, 20, 255});
    DrawRectangleLinesEx(r, 1, (focusBox == box) ? GREEN : GRAY);
    const char* txt = (box->buf[0] != 0) ? box->buf : hint;
    Color c = (box->buf[0] != 0) ? WHITE : (Color) {120, 120, 120, 255};
    DrawTextEx(mainFont, txt, (Vector2) {r.x + 6, r.y + 6}, 20, 1, c);
}

static void NumBox_Update(NumBox* nb, Rectangle r, char blocked) {
    if(blocked)
        return;
    Vector2 m = MouseWorld();
    if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if(m.x >= r.x && m.x <= r.x + r.width && m.y >= r.y && m.y <= r.y + r.height) {
            focusNum = nb;
            focusBox = NULL;
        } else if(focusNum == nb) {
            focusNum = NULL;
        }
    }
    if(focusNum != nb)
        return;
    int ch;
    while((ch = GetCharPressed()) > 0) {
        size_t len = strlen(nb->buf);
        if(((ch >= '0' && ch <= '9') || ch == '.') && len + 1 < sizeof(nb->buf)) {
            if(ch == '.' && strchr(nb->buf, '.') != NULL)
                continue;
            nb->buf[len] = (char)ch;
            nb->buf[len + 1] = 0;
        }
    }
    int ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if(ctrl && IsKeyPressed(KEY_V)) {
        const char* clip = GetClipboardText();
        if(clip != NULL) {
            for(size_t ci = 0; clip[ci] != 0; ci++) {
                size_t len = strlen(nb->buf);
                if(len + 1 >= sizeof(nb->buf))
                    break;
                char c = clip[ci];
                if((c >= '0' && c <= '9') || (c == '.' && strchr(nb->buf, '.') == NULL)) {
                    nb->buf[len] = c;
                    nb->buf[len + 1] = 0;
                }
            }
        }
    }
    if(ctrl && IsKeyPressed(KEY_C) && nb->buf[0] != 0)
        SetClipboardText(nb->buf);
    if(IsKeyDown(KEY_BACKSPACE)) {
        backTimer -= RayGame_DeltaTime();
        if(IsKeyPressed(KEY_BACKSPACE) || backTimer <= 0) {
            size_t len = strlen(nb->buf);
            if(len > 0) nb->buf[len - 1] = 0;
            backTimer = 0.05f;
        }
    } else if(focusNum != nb) {
        backTimer = 0;
    }
}

static void NumBox_Draw(NumBox* nb, Rectangle r) {
    DrawRectangleRec(r, (Color) {20, 20, 20, 255});
    DrawRectangleLinesEx(r, 1, (focusNum == nb) ? GREEN : GRAY);
    const char* txt = (nb->buf[0] != 0) ? nb->buf : "0";
    DrawTextEx(mainFont, txt, (Vector2) {r.x + 6, r.y + 6}, 20, 1, YELLOW);
}

// lista de cantores (artes que existem): pro seletor Bf/Opponent
static char charNames[32][32];
static int charCount = 0;
static char charDrop = 0; // 0 = fechado, 1 = Bf, 2 = Opponent, 3 = Stage
static char dropJustOpened = 0;
static float dropX = 0, dropY = 0, dropW = 0;

// stages que existem nas musicas (pro seletor de Stage)
static char stageNames[32][32];
static int stageCount = 0;

static char StageOfDir(const char* dir, char out[32]) {
    char p[300];
    snprintf(p, sizeof(p), "%s/data.json", dir);
    FILE* f = fopen(p, "rb");
    if(f != NULL) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if(size > 0 && size < 8 * 1024 * 1024) {
            char* buf = malloc((size_t)size + 1);
            if(buf != NULL) {
                if(fread(buf, 1, (size_t)size, f) == (size_t)size) {
                    buf[size] = 0;
                    cJSON* root = cJSON_Parse(buf);
                    if(root != NULL) {
                        cJSON* js = cJSON_GetObjectItem(root, "song");
                        if(js == NULL) js = root;
                        cJSON* jst = cJSON_GetObjectItem(js, "stage");
                        if(cJSON_IsString(jst)) {
                            strncpy(out, jst->valuestring, 31);
                            out[31] = 0;
                            cJSON_Delete(root);
                            free(buf);
                            fclose(f);
                            return 1;
                        }
                        cJSON_Delete(root);
                    }
                }
                free(buf);
            }
        }
        fclose(f);
    }
    // binario: pula 2 strings, le a 3a (stage)
    snprintf(p, sizeof(p), "%s/data.song", dir);
    f = fopen(p, "rb");
    if(f == NULL)
        return 0;
    for(int s = 0; s < 3; s++) {
        int len = 0;
        if(fread(&len, sizeof(int), 1, f) != 1 || len < 1 || len > 31) {
            fclose(f);
            return 0;
        }
        char tmp[32];
        if(fread(tmp, 1, (size_t)len, f) != (size_t)len) {
            fclose(f);
            return 0;
        }
        tmp[len] = 0;
        if(s == 2) {
            strncpy(out, tmp, 31);
            out[31] = 0;
        }
    }
    fclose(f);
    return 1;
}

// raiz do mod de um songDir (assets/mods/<mod>/...), "" = base
static void EdModRoot(const char* songDir, char out[256]) {
    out[0] = 0;
    if(songDir == NULL)
        return;
    const char* pm = strstr(songDir, "assets/mods/");
    if(pm == NULL)
        return;
    const char* after = pm + 12;
    const char* slash = strchr(after, '/');
    const char* songs = (slash != NULL) ? strstr(slash, "/songs/") : NULL;
    size_t n = (size_t)((songs != NULL ? songs : slash) - songDir);
    if(slash == NULL)
        n = strlen(songDir);
    if(n >= 256)
        n = 255;
    memcpy(out, songDir, n);
    out[n] = 0;
}

// so entra na lista quem tem png + animset + icone (sem fantasma).
// fallback de preview/load continua bf/dad via Editor_TryIcon e Character_Load.
static char CharacterAsset_Exists(const char* name) {
    if(name == NULL || name[0] == 0) return 0;
    if(strchr(name, '/') != NULL || strchr(name, '\\') != NULL || strstr(name, "..") != NULL) return 0;
    char p[300];
    snprintf(p, sizeof(p), "assets/images/characters/%s.png", name);
    FILE* f = fopen(p, "rb");
    if(f == NULL) return 0;
    fclose(f);
    snprintf(p, sizeof(p), "assets/images/characters/%s.animset", name);
    f = fopen(p, "rb");
    if(f == NULL) return 0;
    fclose(f);
    snprintf(p, sizeof(p), "assets/images/icons/%s.png", name);
    f = fopen(p, "rb");
    if(f == NULL) return 0;
    fclose(f);
    return 1;
}

// vale no mod da musica (png+animset+icone no mod) ou no global
static char CharacterAssetForSong_Exists(const char* name, const char* songDir) {
    if(CharacterAsset_Exists(name))
        return 1;
    if(name == NULL || name[0] == 0)
        return 0;
    if(strchr(name, '/') != NULL || strchr(name, '\\') != NULL || strstr(name, "..") != NULL)
        return 0;
    char modRoot[256];
    EdModRoot(songDir, modRoot);
    if(modRoot[0] == 0)
        return 0;
    char p[300];
    snprintf(p, sizeof(p), "%s/images/characters/%s.png", modRoot, name);
    FILE* f = fopen(p, "rb");
    if(f == NULL)
        return 0;
    fclose(f);
    snprintf(p, sizeof(p), "%s/images/characters/%s.animset", modRoot, name);
    f = fopen(p, "rb");
    if(f == NULL)
        return 0;
    fclose(f);
    snprintf(p, sizeof(p), "%s/images/icons/%s.png", modRoot, name);
    f = fopen(p, "rb");
    if(f == NULL)
        return 0;
    fclose(f);
    return 1;
}
// json: assets/stages/<nome>.json, assets/data/stages/<nome>.json.
// "stage" builtin continua valido. Fantasmas sem arquivo caem aqui.
static char StageJsonGlobal_Exists(const char* name) {
    char p[300];
    snprintf(p, sizeof(p), "assets/stages/%s.json", name);
    FILE* f = fopen(p, "rb");
    if(f != NULL) { fclose(f); return 1; }
    snprintf(p, sizeof(p), "assets/data/stages/%s.json", name);
    f = fopen(p, "rb");
    if(f != NULL) { fclose(f); return 1; }
    return 0;
}

// json do mod da musica: <mod>/stages/<nome>.json (layout Psych mods/stages)
static char StageJsonForSong_Exists(const char* name, const char* songDir) {    const char* pm = strstr(songDir, "assets/mods/");
    if(pm == NULL) return 0;
    const char* after = pm + 12;
    const char* slash = strchr(after, '/');
    char modRoot[256] = {0};
    if(slash != NULL) {
        const char* songs = strstr(slash, "/songs/");
        size_t n = (size_t)((songs != NULL ? songs : slash) - songDir);
        if(n >= sizeof(modRoot)) return 0;
        memcpy(modRoot, songDir, n);
        modRoot[n] = 0;
    } else {
        return 0;
    }
    char p[300];
    snprintf(p, sizeof(p), "%s/stages/%s.json", modRoot, name);
    FILE* f = fopen(p, "rb");
    if(f != NULL) { fclose(f); return 1; }
    return 0;
}

// .lua vale porque o exe converte na hora (Stage_ParseLua, sem LuaJIT)
static char StageFileForSong_Exists(const char* name, const char* songDir) {
    if(StageJsonForSong_Exists(name, songDir)) return 1;
    char lua[300];
    return Stage_FindLua(name, songDir, lua);
}

static char StageAsset_Exists(const char* name) {
    if(name == NULL || name[0] == 0) return 0;
    if(strchr(name, '/') != NULL || strchr(name, '\\') != NULL || strstr(name, "..") != NULL) return 0;
    if(strcmp(name, "stage") == 0) {
        FILE* a = fopen("assets/images/stage/stageback.png", "rb");
        FILE* b = fopen("assets/images/stage/stagefront.png", "rb");
        char ok = (a != NULL && b != NULL);
        if(a != NULL) fclose(a);
        if(b != NULL) fclose(b);
        return ok;
    }
    if(StageJsonGlobal_Exists(name)) return 1;
    {
        // .lua global tambem vale: o exe parseia na hora
        char p[300];
        snprintf(p, sizeof(p), "assets/stages/%s.lua", name);
        FILE* f = fopen(p, "rb");
        if(f != NULL) { fclose(f); return 1; }
        snprintf(p, sizeof(p), "assets/data/stages/%s.lua", name);
        f = fopen(p, "rb");
        if(f != NULL) { fclose(f); return 1; }
    }
    char p[300];
    snprintf(p, sizeof(p), "assets/images/stage/%sback.png", name);
    FILE* f = fopen(p, "rb");
    if(f != NULL) { fclose(f); return 1; }
    snprintf(p, sizeof(p), "assets/images/stage/%s.png", name);
    f = fopen(p, "rb");
    if(f != NULL) { fclose(f); return 1; }
    snprintf(p, sizeof(p), "assets/images/stage/%sfront.png", name);
    f = fopen(p, "rb");
    if(f != NULL) { fclose(f); return 1; }
    return 0;
}

static void StageList_Scan(void) {
    stageCount = 0;
    for(int i = 0; i < pickerCount && stageCount < 32; i++) {
        char st[32] = {0};
        if(!StageOfDir(pickerSongs[i].dir, st) || st[0] == 0)
            continue;
        if(!StageAsset_Exists(st) && !StageFileForSong_Exists(st, pickerSongs[i].dir))
            continue;
        char dup = 0;
        for(int k = 0; k < stageCount; k++) {
            if(strcmp(stageNames[k], st) == 0) {
                dup = 1;
                break;
            }
        }
        if(!dup) {
            strncpy(stageNames[stageCount], st, 31);
            stageNames[stageCount][31] = 0;
            stageCount++;
        }
    }
    if(stageCount == 0 && StageAsset_Exists("stage") && stageCount < 32) {
        strncpy(stageNames[0], "stage", 31);
        stageNames[0][31] = 0;
        stageCount = 1;
    }
}
static void CharList_ScanDir(const char* charDir, const char* iconDir) {
    if(!DirectoryExists(charDir))
        return;
    FilePathList list = LoadDirectoryFiles(charDir);
    for(unsigned int i = 0; i < list.count && charCount < 32; i++) {
        const char* p = list.paths[i];
        size_t L = strlen(p);
        if(L < 5 || strcmp(p + L - 4, ".png") != 0)
            continue;
        const char* s1 = strrchr(p, '/');
        const char* s2 = strrchr(p, '\\');
        const char* b = (s1 > s2) ? s1 : s2;
        b = (b != NULL) ? b + 1 : p;
        size_t nlen = L - (size_t)(b - p) - 4;
        if(nlen < 1 || nlen > 31)
            continue;
        if(b[0] == '_' || strstr(b, "Title") != NULL)
            continue;
        char dup = 0;
        for(int k = 0; k < charCount; k++) {
            if(strncmp(charNames[k], b, nlen) == 0 && charNames[k][nlen] == 0) {
                dup = 1;
                break;
            }
        }
        if(dup)
            continue;
        // so vale se tem .animset + icone juntos (sem fantasma, ex: gf sem icone cai fora)
        char ap[300];
        snprintf(ap, sizeof(ap), "%s/%.*s.animset", charDir, (int)nlen, b);
        FILE* f = fopen(ap, "rb");
        if(f == NULL)
            continue;
        fclose(f);
        char ip[300];
        snprintf(ip, sizeof(ip), "%s/%.*s.png", iconDir, (int)nlen, b);
        f = fopen(ip, "rb");
        if(f == NULL)
            continue;
        fclose(f);
        memcpy(charNames[charCount], b, nlen);
        charNames[charCount][nlen] = 0;
        charCount++;
    }
    UnloadDirectoryFiles(list);
}

static void CharList_Scan(void) {
    charCount = 0;
    CharList_ScanDir("assets/images/characters", "assets/images/icons");
    // chars do mod da musica (importador gera .animset + icones no mod)
    char modRoot[256];
    EdModRoot(chart.dir, modRoot);
    if(modRoot[0] != 0) {
        char cd[300], id[300];
        snprintf(cd, sizeof(cd), "%s/images/characters", modRoot);
        snprintf(id, sizeof(id), "%s/images/icons", modRoot);
        CharList_ScanDir(cd, id);
    }
}

// note skin list (scan assets/images/notes/ e mod/images/notes/)
static char skinNames[32][32];
static int skinCount = 0;
static char skinDrop = 0;
static TxtBox skinBox;

static void NoteSkinList_ScanDir(const char* notesDir) {
    if(!DirectoryExists(notesDir))
        return;
    FilePathList list = LoadDirectoryFiles(notesDir);
    for(unsigned int i = 0; i < list.count && skinCount < 32; i++) {
        const char* p = list.paths[i];
        // so considera diretorios (pastas de skin)
        if(!DirectoryExists(p))
            continue;
        const char* s1 = strrchr(p, '/');
        const char* s2 = strrchr(p, '\\');
        const char* b = (s1 > s2) ? s1 : s2;
        b = (b != NULL) ? b + 1 : p;
        size_t nlen = strlen(b);
        if(nlen < 1 || nlen > 31)
            continue;
        // verifica se tem pelo menos as 4 sprites basicas
        char fp[300];
        int ok = 1;
        const char* req[4] = {"left.png", "down.png", "up.png", "right.png"};
        for(int r = 0; r < 4 && ok; r++) {
            snprintf(fp, sizeof(fp), "%s/%s", p, req[r]);
            FILE* f = fopen(fp, "rb");
            if(f == NULL) ok = 0;
            else fclose(f);
        }
        if(!ok) continue;
        char dup = 0;
        for(int k = 0; k < skinCount; k++) {
            if(strcmp(skinNames[k], b) == 0) { dup = 1; break; }
        }
        if(!dup) {
            memcpy(skinNames[skinCount], b, nlen);
            skinNames[skinCount][nlen] = 0;
            skinCount++;
        }
    }
    UnloadDirectoryFiles(list);
}

static void NoteSkinList_Scan(void) {
    skinCount = 0;
    NoteSkinList_ScanDir("assets/images/notes");
    // skins do mod da musica
    char modRoot[256];
    EdModRoot(chart.dir, modRoot);
    if(modRoot[0] != 0) {
        char nd[300];
        snprintf(nd, sizeof(nd), "%s/images/notes", modRoot);
        NoteSkinList_ScanDir(nd);
    }
    // garante "default" no topo se existir
    for(int i = 0; i < skinCount; i++) {
        if(strcmp(skinNames[i], "default") == 0) {
            if(i != 0) {
                char tmp[32];
                memcpy(tmp, skinNames[i], 32);
                memmove(skinNames[1], skinNames[0], i * 32);
                memcpy(skinNames[0], tmp, 32);
            }
            break;
        }
    }
}

// ---------- estado da cena ----------
typedef enum { CE_PICKER, CE_EDIT } CeMode;
typedef enum { DLG_NONE, DLG_OPEN, DLG_SAVEAS } CeDlg;
// abas estilo Psych (uma ativa por vez, resto colapsado)
typedef enum { TAB_CHARTING, TAB_DATA, TAB_EVENTS, TAB_NOTE, TAB_SECTION, TAB_SONG, TAB_COUNT } ChartTab;
static const char* ChartTab_Name(int t) {
    switch(t) {
        case TAB_CHARTING: return "Charting";
        case TAB_DATA: return "Data";
        case TAB_EVENTS: return "Events";
        case TAB_NOTE: return "Note";
        case TAB_SECTION: return "Section";
        case TAB_SONG: return "Song";
        default: return "?";
    }
}

static CeMode ceMode;
static CeDlg ceDlg;
static int activeTab = TAB_SECTION;
static int menuOpen = 0; // 0=fechado 1=File 2=Edit 3=View
static SongEntry* pickerSongs;
static int pickerCount;
static int pickerSel = 0; // 0 = open file, 1..N = songs
static float pickerOffset = 0;
static TxtBox openBox;
static TxtBox saveAsBox;
static TxtBox p1Box;
static TxtBox p2Box;
static TxtBox stageBox;
static TxtBox skinBox;
static char pendingDir[256]; // entrar direto no editor (volta do teste)
static char statusText[128];
static float statusTimer = 0;

// editor
static int secIdx = 0;
static int snapDiv = 4; // 1, 2 ou 4 (por tempo)
static double cursorMs = 0;
static double viewTopMs = 0;
static char playing = 0;
static char playFollow = 1; // View: trava a grade no receptor durante o OUVIR, igual ao jogo
static float edVolApplied = -1.0f;
static Music edInst;
static char edHasAudio = 0;
static Texture2D edIconO;
static Texture2D edIconP;
static char edIconOOk = 0;
static char edIconPOk = 0;

static void Editor_UnloadIcons(void) {
    if(edIconOOk) {
        UnloadTexture(edIconO);
        edIconOOk = 0;
    }
    if(edIconPOk) {
        UnloadTexture(edIconP);
        edIconPOk = 0;
    }
}

static Texture2D Editor_TryIcon(const char* name, const char* fallback, char* ok) {
    char p[300];
    const char* want = (name != NULL && name[0] != 0) ? name : fallback;
    // mod da musica antes do global (importador guarda tudo no mod)
    char modRoot[256];
    EdModRoot(chart.dir, modRoot);
    if(modRoot[0] != 0 && want != NULL) {
        snprintf(p, sizeof(p), "%s/images/icons/%s.png", modRoot, want);
        FILE* f = fopen(p, "rb");
        if(f != NULL) {
            fclose(f);
            *ok = 1;
            return Render_LoadTexture(p);
        }
    }
    snprintf(p, sizeof(p), "assets/images/icons/%s.png", want);
    FILE* f = fopen(p, "rb");
    if(f == NULL && fallback != NULL) {
        snprintf(p, sizeof(p), "assets/images/icons/%s.png", fallback);
        f = fopen(p, "rb");
    }
    if(f == NULL) {
        *ok = 0;
        return (Texture2D) {0};
    }
    fclose(f);
    *ok = 1;
    return Render_LoadTexture(p);
}

static void Editor_LoadIcons(void) {
    Editor_UnloadIcons();
    // p2 vazio (tutorial): sem icone de oponente
    if(chart.p2[0] == 0) {
        edIconOOk = 0;
    } else {
        edIconO = Editor_TryIcon(chart.p2, "dad", &edIconOOk);
    }
    edIconP = Editor_TryIcon(chart.p1, "bf", &edIconPOk);
}
static Music edVoices;
static char edHasVoices = 0;
static Music edSplitO;
static Music edSplitP;
static char edHasSplit = 0;
static EdSec copyBuf;
static char copyValid = 0;
static RayGraphicObject bgEd;
static Camera2D camEd;

// setas do proprio jogo (NOTE_assets) pras cabecas das notas
static RayAnimatedObject edArrows[4];
static Vector2 edArrowAdj[4];
static char edArrowsReady = 0;

static void EdArrows_Init(void) {
    if(edArrowsReady)
        return;
    const RayAnimationHandler* nah = Note_GetCurrentSkin();
    // sprites claros das notas (iguais aos que descem no jogo), sem tinta
    const char* names[4] = {"purple", "blue", "green", "red"};
    for(int i = 0; i < 4; i++) {
        Render_DefaultAnimated(edArrows + i);
        edArrows[i].animationSet = *nah;
        int ai = AnimationSet_FindAnimation(nah, names[i]);
        if(ai >= 0) {
            AnimatedObject_SetAnimation(edArrows + i, ai);
            Frame* fr = nah->animations[ai].frames;
            float big = (float)((fr->w > fr->h) ? fr->w : fr->h);
            float s = (big > 1) ? 56.0f / big : 1.0f;
            edArrows[i].scaleX = edArrows[i].scaleY = s;
            edArrows[i].color = WHITE;
            RayAnimation* an = nah->animations + ai;
            edArrowAdj[i].x = (an->animationOffset.x + fr->fx) * s - fr->w * s / 2;
            edArrowAdj[i].y = (an->animationOffset.y + fr->fy) * s - fr->h * s / 2;
        }
    }
    edArrowsReady = 1;
}

static void StatusMsg(const char* m) {
    strncpy(statusText, m, sizeof(statusText) - 1);
    statusText[sizeof(statusText) - 1] = 0;
    statusTimer = 3.0f;
}

static void Picker_Refresh(void) {
    SongList_Free(pickerSongs);
    pickerSongs = NULL;
    pickerCount = SongList_Scan(&pickerSongs);
    pickerSel = 0;
    pickerOffset = 0;
}

// tempo (ms) onde cada trecho comeca (pela duracao dos anteriores)
static double* secTimes = NULL; // prefixo: [i] = inicio do trecho i, [nsec] = fim
static int secTimesN = -1;      // nsec coberto; -1 = invalido

static void SecTimes_Drop(void) {
    free(secTimes);
    secTimes = NULL;
    secTimesN = -1;
}

static void SecTimes_Invalidate(void) {
    secTimesN = -1;
}

static void SecTimes_Ensure(void) {
    if(secTimesN == chart.nsec && secTimes != NULL)
        return;
    free(secTimes);
    secTimes = NULL;
    secTimesN = -1;
    if(chart.nsec < 0)
        return;
    secTimes = malloc(sizeof(double) * ((size_t)chart.nsec + 1));
    if(secTimes == NULL)
        return;
    double t = 0;
    for(int i = 0; i < chart.nsec; i++) {
        secTimes[i] = t;
        t += chart.secs[i].lenSteps * (60000.0 / chart.secs[i].bpm) / 4.0;
    }
    secTimes[chart.nsec] = t;
    secTimesN = chart.nsec;
}

static double SecStartMs(int idx) {
    SecTimes_Ensure();
    if(secTimes == NULL || secTimesN < 0) {
        // sem cache (sem memoria): loop antigo
        double t = 0;
        for(int i = 0; i < idx && i < chart.nsec; i++)
            t += chart.secs[i].lenSteps * (60000.0 / chart.secs[i].bpm) / 4.0;
        return t;
    }
    if(idx < 0)
        return 0;
    if(idx > secTimesN)
        idx = secTimesN;
    return secTimes[idx];
}

static double SongEndMs(void) {
    return SecStartMs(chart.nsec);
}

static int SecAtTime(double ms) {
    SecTimes_Ensure();
    if(secTimes == NULL || secTimesN <= 0)
        return 0;
    // ultimo trecho com inicio <= ms (igual ao linear antigo, O(log n))
    if(ms < secTimes[0])
        return (chart.nsec > 0) ? chart.nsec - 1 : 0;
    int lo = 0, hi = secTimesN;
    while(lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if(secTimes[mid] <= ms)
            lo = mid;
        else
            hi = mid - 1;
    }
    if(lo >= chart.nsec)
        lo = chart.nsec - 1;
    return lo;
}

// painel segue o cursor (a barra que move)
static void SecFollowCursor(void) {
    if(chart.nsec > 0) {
        int si = SecAtTime(cursorMs < 0 ? 0 : cursorMs);
        if(si >= 0 && si < chart.nsec)
            secIdx = si;
    }
}

static void Editor_EnterDir(const char* dir) {
    if(!EdChart_OpenDir(&chart, dir)) {
        StatusMsg("NAO ABRIU ESSA MUSICA");
        return;
    }
    chartLoaded = 1;
    chart.dirty = 0;
    Undo_Clear();
    if(copyValid) {
        free(copyBuf.notes);
        copyValid = 0;
    }
    secIdx = 0;
    cursorMs = 0;
    viewTopMs = 0;
    playing = 0;
    snapDiv = 4;
    strncpy(p1Box.buf, chart.p1, sizeof(p1Box.buf) - 1);
    strncpy(p2Box.buf, chart.p2, sizeof(p2Box.buf) - 1);
    strncpy(stageBox.buf, chart.stage, sizeof(stageBox.buf) - 1);
    strncpy(skinBox.buf, chart.noteSkin, sizeof(skinBox.buf) - 1);
    CharList_Scan();
    StageList_Scan();
    NoteSkinList_Scan();
    charDrop = 0;
    menuOpen = 0;
    activeTab = TAB_SECTION;
    Editor_LoadIcons();
    // audio pra ouvir (se nao tiver, anda no silencio)
    // voz: Voices.ogg, ou o par separado Voices-Opponent/Voices-Player
    char p[300];
    if(edHasAudio) {
        UnloadMusicStream(edInst);
        edHasAudio = 0;
    }
    if(edHasVoices) {
        UnloadMusicStream(edVoices);
        edHasVoices = 0;
    }
    if(edHasSplit) {
        UnloadMusicStream(edSplitO);
        UnloadMusicStream(edSplitP);
        edHasSplit = 0;
    }
    FILE* f;
    snprintf(p, sizeof(p), "%s/Inst.ogg", dir);
    f = fopen(p, "rb");
    if(f != NULL) {
        fclose(f);
        edInst = LoadMusicStream(p);
        SetMusicVolume(edInst, RayGame_MusicVolumeLevel());
        edHasAudio = 1;
    }
    snprintf(p, sizeof(p), "%s/Voices.ogg", dir);
    f = fopen(p, "rb");
    if(f != NULL) {
        fclose(f);
        edVoices = LoadMusicStream(p);
        SetMusicVolume(edVoices, RayGame_MusicVolumeLevel());
        edHasVoices = 1;
    } else {
        char po[300], pp[300];
        snprintf(po, sizeof(po), "%s/Voices-Opponent.ogg", dir);
        snprintf(pp, sizeof(pp), "%s/Voices-Player.ogg", dir);
        FILE* fo = fopen(po, "rb");
        FILE* fp = fopen(pp, "rb");
        if(fo != NULL && fp != NULL) {
            fclose(fo);
            fclose(fp);
            edSplitO = LoadMusicStream(po);
            edSplitP = LoadMusicStream(pp);
            SetMusicVolume(edSplitO, RayGame_MusicVolumeLevel());
            SetMusicVolume(edSplitP, RayGame_MusicVolumeLevel());
            edHasSplit = 1;
        } else {
            if(fo != NULL) fclose(fo);
            if(fp != NULL) fclose(fp);
        }
    }
    ceMode = CE_EDIT;
    ceDlg = DLG_NONE;
    focusBox = NULL;

    // set custom note skin for editor preview
    if(chart.noteSkin[0] != 0) {
        RayAnimationHandler skin = Cache_GetNoteSkin(chart.noteSkin, dir);
        Note_SetCurrentSkin(&skin);
    } else {
        Note_SetCurrentSkin(NULL);
    }

    StatusMsg("opened :)");
}

static void Editor_Save(void) {
    strncpy(chart.p1, (p1Box.buf[0] != 0) ? p1Box.buf : "bf", sizeof(chart.p1) - 1);
    // vazio = sem oponente (tutorial): preserva "" no save (sem virar "dad")
    if(p2Box.buf[0] == 0) {
        strncpy(chart.p2, "", sizeof(chart.p2) - 1);
    } else {
        strncpy(chart.p2, p2Box.buf, sizeof(chart.p2) - 1);
    }
    chart.p2[sizeof(chart.p2) - 1] = 0;
    strncpy(chart.stage, (stageBox.buf[0] != 0) ? stageBox.buf : "stage", sizeof(chart.stage) - 1);
    chart.p1[sizeof(chart.p1) - 1] = 0;
    chart.stage[sizeof(chart.stage) - 1] = 0;
    // sem fantasma no save: so entra quem tem png+animset+icone (mod ou global), senao bf/dad/stage
    // p2 vazio e valido (tutorial, so bf) e nao cai na validacao de asset
    if(!CharacterAssetForSong_Exists(chart.p1, chart.dir)) {
        strncpy(chart.p1, "bf", sizeof(chart.p1) - 1);
        strncpy(p1Box.buf, "bf", sizeof(p1Box.buf) - 1);
    }
    if(chart.p2[0] != 0 && !CharacterAssetForSong_Exists(chart.p2, chart.dir)) {
        strncpy(chart.p2, "dad", sizeof(chart.p2) - 1);
        strncpy(p2Box.buf, "dad", sizeof(p2Box.buf) - 1);
    }
    if(!StageAsset_Exists(chart.stage) && !StageFileForSong_Exists(chart.stage, chart.dir)) {
        strncpy(chart.stage, "stage", sizeof(chart.stage) - 1);
        strncpy(stageBox.buf, "stage", sizeof(stageBox.buf) - 1);
    }
    strncpy(chart.noteSkin, skinBox.buf, sizeof(chart.noteSkin) - 1);
    chart.noteSkin[sizeof(chart.noteSkin) - 1] = 0;
    if(EdChart_Save(&chart)) {
        chart.dirty = 0;
        Editor_LoadIcons();
        StatusMsg("SALVO!");
    } else {
        StatusMsg("ERRO AO SALVAR");
    }
}

// importa json solto -> vira pasta de mod -> abre (1 = ok)
static char ImportJSON(const char* jsonPath, const char* modName) {
    char clean[64];
    SanitizeName(clean, sizeof(clean), modName);
    if(clean[0] == 0) {
        StatusMsg("NOME INVALIDO");
        return 0;
    }
    char dir[256];
    snprintf(dir, sizeof(dir), "assets/mods/%s", clean);
    if(Song_HasChart(dir)) {
        StatusMsg("JA EXISTE ESSE NOME");
        return 0;
    }
    MakeDirectory(dir);
    char dst[300];
    snprintf(dst, sizeof(dst), "%s/data.json", dir);
    if(!CopyFile(jsonPath, dst)) {
        StatusMsg("NAO DEU PRA COPIAR");
        return 0;
    }
    Editor_EnterDir(dir);
    return 1;
}

// SALVAR COMO: copia audio + salva json com outro nome (1 = ok)
static char Editor_SaveAs(const char* modName) {
    char clean[64];
    SanitizeName(clean, sizeof(clean), modName);
    if(clean[0] == 0) {
        StatusMsg("NOME INVALIDO");
        return 0;
    }
    char dir[256];
    snprintf(dir, sizeof(dir), "assets/mods/%s", clean);
    if(strcmp(dir, chart.dir) == 0) {
        Editor_Save();
        return 1;
    }
    if(Song_HasChart(dir)) {
        StatusMsg("JA EXISTE ESSE NOME");
        return 0;
    }
    // mesma musica, outro nome: RENOMEIA a pasta ( so vale pros mods )
    if(strncmp(chart.dir, "assets/mods/", 12) == 0 && Song_HasChart(chart.dir)) {
        if(rename(chart.dir, dir) == 0) {
            strncpy(chart.dir, dir, sizeof(chart.dir) - 1);
            chart.dir[sizeof(chart.dir) - 1] = 0;
            strncpy(chart.name, clean, sizeof(chart.name) - 1);
            chart.name[sizeof(chart.name) - 1] = 0;
            Editor_Save();
            Picker_Refresh();
            StatusMsg("PASTA RENOMEADA");
            return 1;
        }
        // rename falhou: cai pro copia normal abaixo
    }
    MakeDirectory(dir);
    char src[300], dst[300];
    snprintf(src, sizeof(src), "%s/Inst.ogg", chart.dir);
    snprintf(dst, sizeof(dst), "%s/Inst.ogg", dir);
    CopyFile(src, dst);
    snprintf(src, sizeof(src), "%s/Voices.ogg", chart.dir);
    snprintf(dst, sizeof(dst), "%s/Voices.ogg", dir);
    CopyFile(src, dst);
    snprintf(src, sizeof(src), "%s/Voices-Opponent.ogg", chart.dir);
    snprintf(dst, sizeof(dst), "%s/Voices-Opponent.ogg", dir);
    CopyFile(src, dst);
    snprintf(src, sizeof(src), "%s/Voices-Player.ogg", chart.dir);
    snprintf(dst, sizeof(dst), "%s/Voices-Player.ogg", dir);
    CopyFile(src, dst);
    strncpy(chart.dir, dir, sizeof(chart.dir) - 1);
    strncpy(chart.name, clean, sizeof(chart.name) - 1);
    Editor_Save();
    Picker_Refresh();
    return 1;
}

// mouse em coordenada do mundo (stretch: proporcao direta; senao
// camera centralizada COM zoom)
static Vector2 MouseWorld(void) {
    Vector2 m = GetMousePosition();
    if(game_options_fillScreen) {
        m.x = m.x * 1280 / GetScreenWidth();
        m.y = m.y * 720 / GetScreenHeight();
        return m;
    }
    float z = RayGame_ZoomFactor();
    if(z <= 0) z = 1;
    m.x = (m.x - GetScreenWidth() / 2) / z + 1280 / 2;
    m.y = (m.y - GetScreenHeight() / 2) / z + 720 / 2;
    return m;
}

// ---------- botoes e textos ----------
static char Button(Rectangle r, const char* label, int fs) {
    if(ceDlg != DLG_NONE || charDrop || menuOpen)
        return 0;
    Vector2 m = MouseWorld();
    char hov = (m.x >= r.x && m.x <= r.x + r.width && m.y >= r.y && m.y <= r.y + r.height);
    DrawRectangleRec(r, hov ? (Color) {60, 60, 60, 255} : (Color) {30, 30, 30, 255});
    DrawRectangleLinesEx(r, 1, hov ? GREEN : GRAY);
    Vector2 s = MeasureTextEx(mainFont, label, fs, 1);
    DrawTextEx(mainFont, label, (Vector2) {r.x + (r.width - s.x) / 2, r.y + (r.height - s.y) / 2}, fs, 1, WHITE);
    return hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// setinha que abre lista (desenha o triangulo na mao)
static TxtBox* dropBox = NULL;
static char (*dropNames)[32] = NULL;
static int dropCount = 0;

static char DropBtn(Rectangle r, char open) {
    if(ceDlg != DLG_NONE || menuOpen)
        return 0;
    Vector2 m = MouseWorld();
    char hov = (m.x >= r.x && m.x <= r.x + r.width && m.y >= r.y && m.y <= r.y + r.height);
    DrawRectangleRec(r, hov ? (Color) {60, 60, 60, 255} : (Color) {30, 30, 30, 255});
    DrawRectangleLinesEx(r, 1, hov ? GREEN : GRAY);
    Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
    if(open)
        DrawTriangle((Vector2) {c.x - 7, c.y + 4}, (Vector2) {c.x + 7, c.y + 4}, (Vector2) {c.x, c.y - 5}, WHITE);
    else
        DrawTriangle((Vector2) {c.x - 7, c.y - 4}, (Vector2) {c.x + 7, c.y - 4}, (Vector2) {c.x, c.y + 5}, WHITE);
    return hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// lista por cima (clica e preenche a caixinha)
static void CharDrop_Draw(void) {
    if(!charDrop || dropBox == NULL || dropNames == NULL)
        return;
    float bx = dropX, by = dropY, bw = dropW;
    TxtBox* box = dropBox;
    int n = dropCount;
    float ih = 30;
    float h = (n > 0 ? n : 1) * ih + 8;
    if(by + h > 700) by = 700 - h;
    Rectangle r = {bx - bw + 30, by, bw, h};
    if(r.x + r.width > 1270) r.x = 1270 - r.width;
    DrawRectangleRec(r, (Color) {15, 15, 15, 245});
    DrawRectangleLinesEx(r, 1, GREEN);
    Vector2 m = MouseWorld();
    if(n == 0) {
        DrawTextEx(mainFont, "(vazio)", (Vector2) {r.x + 8, r.y + 8}, 20, 1, YELLOW);
    }
    for(int i = 0; i < n; i++) {
        Rectangle ir = {r.x + 4, r.y + 4 + i * ih, r.width - 8, ih - 2};
        char hov = (m.x >= ir.x && m.x <= ir.x + ir.width && m.y >= ir.y && m.y <= ir.y + ir.height);
        if(hov)
            DrawRectangleRec(ir, (Color) {50, 90, 50, 255});
        DrawTextEx(mainFont, dropNames[i], (Vector2) {ir.x + 6, ir.y + 4}, 20, 1, WHITE);
        if(hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            strncpy(box->buf, dropNames[i], sizeof(box->buf) - 1);
            box->buf[sizeof(box->buf) - 1] = 0;
            chart.dirty = 1;
            charDrop = 0;
            return;
        }
    }
    // clicou fora: fecha (pula o frame que abriu)
    if(dropJustOpened) {
        dropJustOpened = 0;
    } else if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if(!(m.x >= r.x && m.x <= r.x + r.width && m.y >= r.y && m.y <= r.y + r.height)) {
            charDrop = 0;
            skinDrop = 0;
        }
    }
}

static void TimeFmt(char* out, size_t n, double ms) {
    if(ms < 0) ms = 0;
    int m = (int)(ms / 60000);
    int s = (int)(ms / 1000) % 60;
    int d = (int)(ms / 100) % 10;
    snprintf(out, n, "%d:%02d.%d", m, s, d);
}

// ---------- grade ----------
#define GRID_X0 40.0f
#define LANE_W 64.0f
#define GRID_Y0 112.0f
#define GRID_Y1 680.0f
#define VIEW_MS 2200.0
#define MENU_H 28.0f
#define PANEL_X 586.0f

static float PxPerMs(void) {
    return (GRID_Y1 - GRID_Y0) / (float)VIEW_MS;
}

static Color LaneColor(int lane) {
    switch(lane % 4) {
        case 0: return (Color) {200, 80, 255, 255};
        case 1: return (Color) {80, 180, 255, 255};
        case 2: return (Color) {80, 255, 150, 255};
        default: return (Color) {255, 90, 90, 255};
    }
}

// ---------- PICKER ----------
static char importPath[512];

static void Picker_OpenSelected(void) {
    if(pickerSel == 0) {
        openBox.buf[0] = 0;
        ceDlg = DLG_OPEN;
        focusBox = &openBox;
    } else if(pickerSel >= 1 && pickerSel <= pickerCount) {
        Editor_EnterDir(pickerSongs[pickerSel - 1].dir);
    }
}

static void Picker_NewSong(void) {
    EdChart_Default(&chart, "new-song");
    chart.dir[0] = 0;
    chartLoaded = 1;
    chart.dirty = 1;
    Undo_Clear();
    if(copyValid) { free(copyBuf.notes); memset(&copyBuf, 0, sizeof(copyBuf)); copyValid = 0; }
    secIdx = 0;
    cursorMs = 0;
    viewTopMs = 0;
    playing = 0;
    snapDiv = 4;
    strncpy(p1Box.buf, "bf", sizeof(p1Box.buf) - 1);
    strncpy(p2Box.buf, "dad", sizeof(p2Box.buf) - 1);
    strncpy(stageBox.buf, "stage", sizeof(stageBox.buf) - 1);
    CharList_Scan();
    StageList_Scan();
    charDrop = 0;
    menuOpen = 0;
    activeTab = TAB_SONG;
    Editor_LoadIcons();
    if(edHasAudio) { UnloadMusicStream(edInst); edHasAudio = 0; }
    if(edHasVoices) { UnloadMusicStream(edVoices); edHasVoices = 0; }
    if(edHasSplit) { UnloadMusicStream(edSplitO); UnloadMusicStream(edSplitP); edHasSplit = 0; }
    ceMode = CE_EDIT;
    ceDlg = DLG_NONE;
    focusBox = NULL;
    focusNum = NULL;
    StatusMsg("NEW SONG - SALVAR COMO");
}

static void Picker_Draw(void) {
    Render_SetCamera(&camEd);
    Render_DrawGraphicObject(&bgEd);

    DrawTextEx(mainFont, "song selector", (Vector2) {150, 60}, 48, 4, WHITE);

    int rows = 1 + pickerCount;
    for(int i = 0; i < rows; i++) {
        const int fontSize = (i == 0) ? 40 : 56;
        float y = 720 / 2 + ((float)i - pickerOffset) * (10 + fontSize);
        if(y < -60 || y > 780)
            continue;
        char name[128];
        if(i == 0) {
            strncpy(name, "open file directory...", sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
        } else {
            strncpy(name, pickerSongs[i - 1].name, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            if(strstr(pickerSongs[i - 1].dir, "mods") != NULL)
                strncat(name, "  [mod]", sizeof(name) - strlen(name) - 1);
        }
        float d = fabsf((float)i - pickerOffset);
        Vector2 pos = {(float)(5 + 150 + d * -30), y + 5};
        Color fill = WHITE, out = BLACK;
        fill.a = (pickerSel == i) ? 255 : 150;
        out.a = (pickerSel == i) ? 255 : 100;
        if(i == 0) fill = (pickerSel == 0) ? GREEN : (Color) {150, 255, 150, 150};
        DrawTextEx(mainFont, name, pos, fontSize, 4, out);
        pos.x -= 5;
        pos.y -= 5;
        DrawTextEx(mainFont, name, pos, fontSize, 4, fill);
    }

    DrawTextEx(mainFont, "ENTER open   R update   N new   arraste um .json pra ca   ESC return",
        (Vector2) {150, 690}, 20, 1, (Color) {200, 200, 200, 255});
    if(Button((Rectangle) {150, 630, 200, 40}, "OPEN", 22)) Picker_OpenSelected();
    if(Button((Rectangle) {360, 630, 200, 40}, "NEW", 22)) Picker_NewSong();
    Render_StopCamera();
}

static void Picker_Update(void) {
    // arrastou arquivo pra janela
    if(IsFileDropped()) {
        FilePathList drop = LoadDroppedFiles();
        for(unsigned int i = 0; i < drop.count; i++) {
            const char* p = drop.paths[i];
            size_t L = strlen(p);
            if(L > 5 && (strcmp(p + L - 5, ".json") == 0 || strcmp(p + L - 5, ".JSON") == 0)) {
                EdChart tmp;
                memset(&tmp, 0, sizeof(tmp));
                if(EdChart_LoadJSON(&tmp, p, "", DirName(p))) {
                    EdChart_Free(&tmp);
                    strncpy(importPath, p, sizeof(importPath) - 1);
                    saveAsBox.buf[0] = 0;
                    ceDlg = DLG_SAVEAS;
                    focusBox = &saveAsBox;
                } else {
                    StatusMsg("ESSE JSON NAO E MUSICA");
                }
                break;
            }
        }
        UnloadDroppedFiles(drop);
    }

    if(ceDlg != DLG_NONE)
        return;

    int rows = 1 + pickerCount;
    if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
        pickerSel++;
        if(pickerSel >= rows) pickerSel = 0;
    }
    if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
        pickerSel--;
        if(pickerSel < 0) pickerSel = rows - 1;
    }
    if(IsKeyPressed(KEY_R))
        Picker_Refresh();
    if(IsKeyPressed(KEY_N))
        Picker_NewSong();
    if(IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) {
        RayGame_ResetMusic();
        RayGame_ToggleMusic(1);
        MenuState_SetScene();
        return;
    }
    if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
        Picker_OpenSelected();
        return;
    }
    // clique: 1o seleciona, 2o abre (OPEN ao lado da musica)
    if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ceDlg == DLG_NONE) {
        Vector2 m = MouseWorld();
        for(int i = 0; i < rows; i++) {
            int fs = (i == 0) ? 40 : 56;
            float y = 720 / 2 + ((float)i - pickerOffset) * (10 + fs);
            if(m.x >= 150 && m.x <= 1100 && m.y >= y - 5 && m.y <= y + fs) {
                if(pickerSel == i)
                    Picker_OpenSelected();
                else
                    pickerSel = i;
                break;
            }
        }
    }
    pickerOffset = Lerp(pickerOffset, (float)pickerSel, RayGame_DeltaTime() * 10.0f);
}

// ---------- dialogs (por cima de tudo) ----------
static void Dialogs_Update(void) {
    if(ceDlg == DLG_NONE)
        return;
    if(IsKeyPressed(KEY_ESCAPE)) {
        ceDlg = DLG_NONE;
        focusBox = NULL;
        importPath[0] = 0;
        return;
    }
    if(IsKeyPressed(KEY_ENTER)) {
        if(ceDlg == DLG_OPEN) {
            if(openBox.buf[0] == 0) {
                StatusMsg("COLE O CAMINHO PRIMEIRO");
                return;
            }
            EdChart tmp;
            memset(&tmp, 0, sizeof(tmp));
            if(EdChart_LoadJSON(&tmp, openBox.buf, "", DirName(openBox.buf))) {
                EdChart_Free(&tmp);
                strncpy(importPath, openBox.buf, sizeof(importPath) - 1);
                saveAsBox.buf[0] = 0;
                ceDlg = DLG_SAVEAS;
                focusBox = &saveAsBox;
            } else {
                StatusMsg("NAO ABRIU: confira o caminho");
            }
        } else if(ceDlg == DLG_SAVEAS) {
            char ok = 0;
            if(importPath[0] != 0)
                ok = ImportJSON(importPath, saveAsBox.buf);
            else
                ok = Editor_SaveAs(saveAsBox.buf);
            if(ok) {
                ceDlg = DLG_NONE;
                focusBox = NULL;
                importPath[0] = 0;
            }
        }
    }
}

static void Dialogs_Draw(void) {
    if(ceDlg == DLG_NONE)
        return;
    Render_SetCamera(&camEd);
    DrawRectangle(0, 0, 1280, 720, (Color) {0, 0, 0, 180});
    if(ceDlg == DLG_OPEN) {
        DrawTextEx(mainFont, ".json path", (Vector2) {340, 250}, 40, 3, WHITE);
        Rectangle r = {340, 320, 600, 40};
        TxtBox_Update(&openBox, r, 0);
        TxtBox_Draw(&openBox, r, "C:/.../musica.json");
        DrawTextEx(mainFont, "enter import, esc cancel", (Vector2) {340, 380}, 20, 1, YELLOW);
        DrawTextEx(mainFont, "You can copy and paste the path to the .json file.", (Vector2) {340, 410}, 20, 1, YELLOW);
    } else {
        DrawTextEx(mainFont, "Save (path name):", (Vector2) {140, 190}, 36, 3, WHITE);
        Rectangle r = {140, 260, 600, 40};
        TxtBox_Update(&saveAsBox, r, 0);
        TxtBox_Draw(&saveAsBox, r, "my-music-renameonexplorer");
        DrawTextEx(mainFont, "enter save, esc cancel", (Vector2) {140, 320}, 20, 1, YELLOW);
        DrawTextEx(mainFont, "renames the folder if it already exists", (Vector2) {140, 350}, 20, 1, YELLOW);
        {
            char clean[64];
            SanitizeName(clean, sizeof(clean), saveAsBox.buf);
            char where[192];
            char dstDir[128];
            if(clean[0] != 0) {
                snprintf(dstDir, sizeof(dstDir), "assets/mods/%s", clean);
                snprintf(where, sizeof(where), "saves to: %s/data.json", dstDir);
            } else {
                snprintf(dstDir, sizeof(dstDir), "assets/mods/<your-song>");
                snprintf(where, sizeof(where), "saves to: mods/<your-song>");
            }
            DrawTextEx(mainFont, where, (Vector2) {140, 380}, 20, 1, YELLOW);
            if(importPath[0] != 0) {
                char from[96];
                size_t L = strlen(importPath);
                const char* tail = (L > 52) ? importPath + L - 52 : importPath;
                snprintf(from, sizeof(from), "from: ...%s", (L > 52) ? tail : importPath);
                DrawTextEx(mainFont, from, (Vector2) {140, 410}, 18, 1, GRAY);
            }
            // mini arvore do destino (leve: so re-checa quando o nome muda)
            static char lastTree[64] = {0};
            static char tChart = 0, tInst = 0, tVox = 0;
            if(strcmp(lastTree, clean) != 0) {
                strncpy(lastTree, clean, sizeof(lastTree) - 1);
                lastTree[sizeof(lastTree) - 1] = 0;
                tChart = tInst = tVox = 0;
                if(clean[0] != 0) {
                    tChart = Song_HasChart(dstDir);
                    char p[176];
                    // se o destino ja existe mostra o destino, senao mostra o que sera copiado da origem
                    const char* srcDir = (tChart || chart.dir[0] == 0) ? dstDir : chart.dir;
                    snprintf(p, sizeof(p), "%s/Inst.ogg", srcDir);
                    FILE* f = fopen(p, "rb");
                    if(f != NULL) { fclose(f); tInst = 1; }
                    snprintf(p, sizeof(p), "%s/Voices.ogg", srcDir);
                    f = fopen(p, "rb");
                    if(f != NULL) { fclose(f); tVox = 1; }
                }
            }
            float tx = 780;
            float ty = 260;
            DrawTextEx(mainFont, "tree:", (Vector2) {tx, ty}, 20, 1, GREEN);
            ty += 30;
            DrawTextEx(mainFont, "assets/", (Vector2) {tx, ty}, 18, 1, GRAY);
            ty += 24;
            DrawTextEx(mainFont, "  mods/", (Vector2) {tx, ty}, 18, 1, GRAY);
            ty += 24;
            {
                char mod[80];
                if(clean[0] != 0) snprintf(mod, sizeof(mod), "    %.28s/", clean);
                else snprintf(mod, sizeof(mod), "    <your-song>/");
                DrawTextEx(mainFont, mod, (Vector2) {tx, ty}, 18, 1, WHITE);
                DrawTextEx(mainFont, tChart ? "[existe]" : "[novo]", (Vector2) {tx + 240, ty}, 18, 1, tChart ? YELLOW : GREEN);
            }
            ty += 24;
            DrawTextEx(mainFont, "      data.json", (Vector2) {tx, ty}, 18, 1, WHITE);
            ty += 24;
            DrawTextEx(mainFont, tInst ? "      Inst.ogg [ok]" : "      Inst.ogg [--]", (Vector2) {tx, ty}, 18, 1, tInst ? GREEN : GRAY);
            ty += 24;
            DrawTextEx(mainFont, tVox ? "      Voices.ogg [ok]" : "      Voices.ogg [--]", (Vector2) {tx, ty}, 18, 1, tVox ? GREEN : GRAY);
        }
    }
    Render_StopCamera();
}

// ---------- EDIT ----------
static void Touch(void) {
    Undo_Push();
    chart.dirty = 1;
}

static void TogglePlay(void) {
    if(playing) {
        playing = 0;
        if(edHasAudio) PauseMusicStream(edInst);
        if(edHasVoices) PauseMusicStream(edVoices);
        if(edHasSplit) {
            PauseMusicStream(edSplitO);
            PauseMusicStream(edSplitP);
        }
    } else {
        if(cursorMs >= SongEndMs())
            cursorMs = 0;
        float pos = (float)(cursorMs / 1000.0);
        if(edHasAudio) {
            SeekMusicStream(edInst, pos);
            if(!IsMusicStreamPlaying(edInst))
                PlayMusicStream(edInst);
        }
        if(edHasVoices) {
            SeekMusicStream(edVoices, pos);
            if(!IsMusicStreamPlaying(edVoices))
                PlayMusicStream(edVoices);
        }
        if(edHasSplit) {
            SeekMusicStream(edSplitO, pos);
            SeekMusicStream(edSplitP, pos);
            if(!IsMusicStreamPlaying(edSplitO))
                PlayMusicStream(edSplitO);
            if(!IsMusicStreamPlaying(edSplitP))
                PlayMusicStream(edSplitP);
        }
        playing = 1;
    }
}

static void StopPlay(void) {
    playing = 0;
    if(edHasAudio) PauseMusicStream(edInst);
    if(edHasVoices) PauseMusicStream(edVoices);
    if(edHasSplit) {
        PauseMusicStream(edSplitO);
        PauseMusicStream(edSplitP);
    }
}

static void NumBox_Commit(void) {
    if(focusNum == NULL)
        return;
    if(focusNum == &nbSecBpm) {
        float v = (float)atof(nbSecBpm.buf);
        if(v < 10) v = 10;
        if(v > 300) v = 300;
        if(v != chart.secs[secIdx].bpm) {
            Touch();
            chart.secs[secIdx].bpm = v;
            SecTimes_Invalidate(); // inicio dos trechos seguintes muda
        }
    } else if(focusNum == &nbTopBpm) {
        float v = (float)atof(nbTopBpm.buf);
        if(v < 10) v = 10;
        if(v > 300) v = 300;
        if(v != chart.bpm) {
            Touch();
            chart.bpm = v;
            SecTimes_Invalidate();
        }
    } else if(focusNum == &nbLen) {
        int v = atoi(nbLen.buf);
        if(v < 1) v = 1;
        if(v > 64) v = 64;
        if(v != chart.secs[secIdx].lenSteps) {
            Touch();
            chart.secs[secIdx].lenSteps = v;
            SecTimes_Invalidate(); // duracao do trecho muda
        }
    } else if(focusNum == &nbSpeed) {
        float v = (float)atof(nbSpeed.buf);
        if(v < 0.2f) v = 0.2f;
        if(v > 5) v = 5;
        if(v != chart.speed) {
            Touch();
            chart.speed = v;
        }
    } else if(focusNum == &nbLen) {
        int v = atoi(nbLen.buf);
        if(v < 1) v = 1;
        if(v > 64) v = 64;
        if(v != chart.secs[secIdx].lenSteps) {
            Touch();
            chart.secs[secIdx].lenSteps = v;
        }
    }
    focusNum = NULL;
}

static void Edit_UpdateKeys(void) {
    int ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    int shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if(ctrl && IsKeyPressed(KEY_S)) {
        Editor_Save();
        return;
    }
    if(ctrl && IsKeyPressed(KEY_Z) && focusBox == NULL && focusNum == NULL) {
        Undo_Pop();
        chart.dirty = 1;
        StatusMsg("actions reverted");
        return;
    }
    // digitando numa caixinha: nao dispara atalho
    if(menuOpen && IsKeyPressed(KEY_ESCAPE)) {
        menuOpen = 0;
        return;
    }
    if(charDrop && IsKeyPressed(KEY_ESCAPE)) {
        charDrop = 0;
        return;
    }
    if(focusNum != NULL) {
        if(IsKeyPressed(KEY_ENTER))
            NumBox_Commit();
        else if(IsKeyPressed(KEY_ESCAPE))
            focusNum = NULL;
        return;
    }
    if(focusBox != NULL) {
        if(IsKeyPressed(KEY_ESCAPE))
            focusBox = NULL;
        return;
    }
    if(IsKeyPressed(KEY_SPACE)) {
        TogglePlay();
        return;
    }
    if(IsKeyPressed(KEY_F5)) {
        StopPlay();
        Editor_Save();
        PlayState_SetSongDir(chart.dir);
        PlayState_SetReturnEditor(1);
        PlayState_SetScene();
        return;
    }
    if(IsKeyPressed(KEY_TAB)) {
        Touch();
        chart.secs[secIdx].mustHit = !chart.secs[secIdx].mustHit;
        StatusMsg(chart.secs[secIdx].mustHit ? "Turn: You" : "Turn: Opponent");
        return;
    }
    if(IsKeyPressed(KEY_T)) {
        snapDiv = (snapDiv == 4) ? 1 : (snapDiv == 1) ? 2 : 4;
        return;
    }    if(IsKeyPressed(KEY_N)) {
        Touch();
        EdSec* last = chart.secs + chart.nsec - 1;
        EdSec* g = realloc(chart.secs, sizeof(EdSec) * (size_t)(chart.nsec + 1));
        if(g != NULL) {
            chart.secs = g;
            EdSec* n = chart.secs + chart.nsec;
            memset(n, 0, sizeof(*n));
            n->mustHit = last->mustHit;
            n->bpm = last->bpm;
            n->lenSteps = 16;
            chart.nsec++;
            secIdx = chart.nsec - 1;
            cursorMs = SecStartMs(secIdx);
            viewTopMs = cursorMs - VIEW_MS * 0.3;
            if(viewTopMs < 0) viewTopMs = 0;
            StatusMsg("TRECHO NOVO NO FIM");
        }
        return;
    }
    if(IsKeyPressed(KEY_X)) {
        /* // * part deleter */
        if(chart.nsec > 1) {
            Touch();
            free(chart.secs[secIdx].notes);
            memmove(chart.secs + secIdx, chart.secs + secIdx + 1,
                sizeof(EdSec) * (size_t)(chart.nsec - secIdx - 1));
            chart.nsec--;
            if(secIdx >= chart.nsec) secIdx = chart.nsec - 1;
            StatusMsg("part deleted");
        } else {
            StatusMsg("NOT POSSIBLE TO DELETE HAVE ONLY 1 PART");
        }
        return;
    }
    if(IsKeyPressed(KEY_C)) {
        /* // * part copier */
        if(copyValid) free(copyBuf.notes);
        memset(&copyBuf, 0, sizeof(copyBuf));
        EdSec* s = chart.secs + secIdx;
        copyBuf.mustHit = s->mustHit;
        copyBuf.bpm = s->bpm;
        copyBuf.lenSteps = s->lenSteps;
        if(s->count > 0) {
            copyBuf.notes = malloc(sizeof(EdNote) * (size_t)s->count);
            copyBuf.count = s->count;
            copyBuf.cap = s->count;
            memcpy(copyBuf.notes, s->notes, sizeof(EdNote) * (size_t)s->count);
        }
        copyValid = 1;
        StatusMsg("part Copyed");
        return;
    }
    if(IsKeyPressed(KEY_V)) {
        /* // * if pressing control v without control c */
        if(!copyValid) {
            StatusMsg("V pressed missing Copy Before (C)");
            return;
        }
        Touch();
        double base = SongEndMs();
        double srcStart = SecStartMs(secIdx);
        EdSec* g = realloc(chart.secs, sizeof(EdSec) * (size_t)(chart.nsec + 1));
        if(g != NULL) {
            chart.secs = g;
            EdSec* n = chart.secs + chart.nsec;
            memset(n, 0, sizeof(*n));
            n->mustHit = copyBuf.mustHit;
            n->bpm = chart.secs[chart.nsec - 1].bpm;
            n->lenSteps = copyBuf.lenSteps;
            for(int i = 0; i < copyBuf.count; i++)
                EdSec_Add(n, (float)(base + (copyBuf.notes[i].time - srcStart)), copyBuf.notes[i].lane, copyBuf.notes[i].len);
            chart.nsec++;
            secIdx = chart.nsec - 1;
            StatusMsg("Copyed on End");
        }
        return;
    }
    if(IsKeyPressed(KEY_G)) {
        secIdx--;
        if(secIdx < 0) secIdx = 0;
        cursorMs = SecStartMs(secIdx);
        viewTopMs = cursorMs - VIEW_MS * 0.3;
        if(viewTopMs < 0) viewTopMs = 0;
        return;
    }
    if(IsKeyPressed(KEY_H)) {
        secIdx++;
        if(secIdx >= chart.nsec) secIdx = chart.nsec - 1;
        cursorMs = SecStartMs(secIdx);
        viewTopMs = cursorMs - VIEW_MS * 0.3;
        if(viewTopMs < 0) viewTopMs = 0;
        return;
    }
    if(IsKeyPressed(KEY_HOME)) {
        cursorMs = SecStartMs(secIdx);
        return;
    }
    if(IsKeyPressed(KEY_COMMA) || IsKeyPressed(KEY_PERIOD)) {
        Touch();
        float d = (IsKeyPressed(KEY_PERIOD) ? 1 : -1) * (shift ? 10 : 1);
        chart.secs[secIdx].bpm += d;
        if(chart.secs[secIdx].bpm < 10) chart.secs[secIdx].bpm = 10;
        if(chart.secs[secIdx].bpm > 300) chart.secs[secIdx].bpm = 300;
        return;
    }
    if(IsKeyPressed(KEY_ESCAPE)) {
        if(focusBox != NULL) {
            focusBox = NULL;
            return;
        }
        StopPlay();
        if(chart.dirty)
            Editor_Save();
        if(edHasAudio) {
            UnloadMusicStream(edInst);
            edHasAudio = 0;
        }
        if(edHasVoices) {
            UnloadMusicStream(edVoices);
            edHasVoices = 0;
        }
        if(edHasSplit) {
            UnloadMusicStream(edSplitO);
            UnloadMusicStream(edSplitP);
            edHasSplit = 0;
        }
        Editor_UnloadIcons();
        ceMode = CE_PICKER;
        Picker_Refresh();
        return;
    }
}

static void Edit_Update(void) {
    if(ceDlg != DLG_NONE)
        return;
    // volume global (+/-) vale pro OUVIR tambem (so reaplica se mudou)
    {
        float v = RayGame_MusicVolumeLevel();
        if(v != edVolApplied) {
            edVolApplied = v;
            if(edHasAudio) SetMusicVolume(edInst, v);
            if(edHasVoices) SetMusicVolume(edVoices, v);
            if(edHasSplit) {
                SetMusicVolume(edSplitO, v);
                SetMusicVolume(edSplitP, v);
            }
        }
    }
    Edit_UpdateKeys();

    // roda a musica / relogio (tempo mestre = Inst, senao voz, senao silencio)
    if(playing) {
        if(edHasAudio) UpdateMusicStream(edInst);
        if(edHasVoices) UpdateMusicStream(edVoices);
        if(edHasSplit) {
            UpdateMusicStream(edSplitO);
            UpdateMusicStream(edSplitP);
        }
        if(edHasAudio)
            cursorMs = GetMusicTimePlayed(edInst) * 1000.0;
        else if(edHasVoices)
            cursorMs = GetMusicTimePlayed(edVoices) * 1000.0;
        else if(edHasSplit)
            cursorMs = GetMusicTimePlayed(edSplitO) * 1000.0;
        else
            cursorMs += RayGame_DeltaTime() * 1000.0;
        if(cursorMs >= SongEndMs() + 2000)
            StopPlay();
        else if(playFollow)
            viewTopMs = cursorMs - VIEW_MS * 0.25; // trava no receptor, igual ao jogo
    }

    // setas e W/S sobem e descem a grade
    if(IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
        viewTopMs -= 2600 * RayGame_DeltaTime();
    if(IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
        viewTopMs += 2600 * RayGame_DeltaTime();
    if(viewTopMs < -500) viewTopMs = -500;
    {
        double maxTop = SongEndMs() + 1000;
        if(viewTopMs > maxTop) viewTopMs = maxTop;
    }

    // roda do mouse = sobe/desce
    float wheel = GetMouseWheelMove();
    if(wheel != 0) {
        Vector2 m = MouseWorld();
        int shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        int ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if((shift || ctrl) && m.x >= GRID_X0 && m.x <= GRID_X0 + LANE_W * 8 && m.y >= GRID_Y0 && m.y <= GRID_Y1) {
            //na nota embaixo do mouse: muda a nota longa
            int lane = (int)((m.x - GRID_X0) / LANE_W);
            double t = viewTopMs + (m.y - GRID_Y0) / PxPerMs();
            int si = SecAtTime(t);
            EdSec* s = chart.secs + si;
            lane = DispLane(s, lane); // casa da tela -> casa do arquivo
            float beatMs = 60000.0f / s->bpm;
            for(int i = 0; i < s->count; i++) {
                if(s->notes[i].lane == lane && fabsf(s->notes[i].time - (float)t) < beatMs / 2) {
                    Touch();
                    s->notes[i].len += wheel * beatMs / 4;
                    if(s->notes[i].len < 0) s->notes[i].len = 0;
                    if(s->notes[i].len > 10000) s->notes[i].len = 10000;
                    break;
                }
            }
        } else {
            viewTopMs -= wheel * 150;
            if(viewTopMs < -500) viewTopMs = -500;
        }
    }
}

// barra da musica no topo (arrasta pra andar; substitui os botoes de trecho)
#define SBAR_X GRID_X0
#define SBAR_Y 36.0f
#define SBAR_W (LANE_W * 8)
#define SBAR_H 16.0f
static char barDrag = 0;

static void SeekAllAudio(double ms) {
    float pos = (float)(ms / 1000.0);
    if(edHasAudio) SeekMusicStream(edInst, pos);
    if(edHasVoices) SeekMusicStream(edVoices, pos);
    if(edHasSplit) {
        SeekMusicStream(edSplitO, pos);
        SeekMusicStream(edSplitP, pos);
    }
}

static void Edit_DrawGrid(void) {
    float ppm = PxPerMs();
    double viewBottom = viewTopMs + VIEW_MS;

    // barra da musica (preta e branca, arrasta pra andar)
    {
        double end = SongEndMs();
        if(end < 1000) end = 1000;
        Rectangle bar = {SBAR_X, SBAR_Y, SBAR_W, SBAR_H};
        DrawRectangleRec(bar, BLACK);
        float frac = (float)(cursorMs / end);
        if(frac < 0) frac = 0;
        if(frac > 1) frac = 1;
        float kx = SBAR_X + frac * SBAR_W;
        DrawRectangle((int)SBAR_X, (int)SBAR_Y, (int)(kx - SBAR_X), (int)SBAR_H, WHITE);
        DrawRectangle((int)(kx - 6), (int)SBAR_Y - 2, 12, (int)SBAR_H + 4, RED);
    }

    DrawRectangle((int)GRID_X0 - 10, (int)GRID_Y0 - 34, (int)(LANE_W * 8) + 20, (int)(GRID_Y1 - GRID_Y0) + 44, (Color) {12, 12, 12, 255});

    // casas brancas (da pra ver onde clica)
    for(int l = 0; l < 8; l++) {
        float x = GRID_X0 + l * LANE_W;
        DrawRectangle((int)x + 1, (int)GRID_Y0, (int)LANE_W - 2, (int)(GRID_Y1 - GRID_Y0), (Color) {250, 250, 250, 255});
        if(l == 4)
            DrawRectangle((int)x - 1, (int)GRID_Y0 - 30, 2, (int)(GRID_Y1 - GRID_Y0) + 34, (Color) {60, 60, 60, 255});
    }
    // seu lado (direita) com verde claro
    DrawRectangle((int)(GRID_X0 + LANE_W * 4), (int)GRID_Y0, (int)(LANE_W * 4), (int)(GRID_Y1 - GRID_Y0), (Color) {140, 230, 170, 70});
    // (letras das casas removidas: as setas ja mostram a direcao)
    // icones dos cantores em cima (igual a barra de vida do jogo)
    if(edIconOOk)
        DrawTexturePro(edIconO, (Rectangle) {0, 0, 150, 150},
            (Rectangle) {GRID_X0 + LANE_W * 2 - 22, GRID_Y0 - 52, 44, 44}, (Vector2) {0, 0}, 0, WHITE);
    if(edIconPOk)
        DrawTexturePro(edIconP, (Rectangle) {0, 0, 150, 150},
            (Rectangle) {GRID_X0 + LANE_W * 6 - 22, GRID_Y0 - 52, 44, 44}, (Vector2) {0, 0}, 0, WHITE);

    // linhas de tempo por trecho
    for(int i = 0; i < chart.nsec; i++) {
        double start = SecStartMs(i);
        double end = (i + 1 < chart.nsec) ? SecStartMs(i + 1) : SongEndMs() + 4000;
        if(end < viewTopMs || start > viewBottom)
            continue;
        if(i == secIdx)
            DrawRectangle((int)GRID_X0, (int)(GRID_Y0 + (fmax(start, viewTopMs) - viewTopMs) * ppm),
                (int)(LANE_W * 8), (int)((fmin(end, viewBottom) - fmax(start, viewTopMs)) * ppm),
                (Color) {255, 255, 255, 12});
        float beatMs = 60000.0f / chart.secs[i].bpm;
        float stepMs = beatMs / snapDiv;
        for(double t = start; t <= end + 0.5; t += stepMs) {
            if(t < viewTopMs || t > viewBottom)
                continue;
            float y = GRID_Y0 + (float)(t - viewTopMs) * ppm;
            char isBeat = (fabs(fmod(t - start, beatMs)) < stepMs / 2);
            DrawLine((int)GRID_X0, (int)y, (int)(GRID_X0 + LANE_W * 8), (int)y,
                isBeat ? (Color) {90, 90, 90, 160} : (Color) {160, 160, 160, 130});
        }
        // comeco do trecho (so a linha, sem texto em cima do chart)
        if(start >= viewTopMs && start <= viewBottom) {
            float y = GRID_Y0 + (float)(start - viewTopMs) * ppm;
            DrawLine((int)GRID_X0 - 8, (int)y, (int)(GRID_X0 + LANE_W * 8) + 8, (int)y, (Color) {210, 130, 0, 255});
        }
    }

    // notas (casa virada pra tela; cor/seta pela casa real do arquivo)
    // Note player ON: passado apaga, na agulha acende (so visual, sem tocar no chart)
    for(int i = 0; i < chart.nsec; i++) {
        EdSec* s = chart.secs + i;
        for(int k = 0; k < s->count; k++) {
            EdNote* n = s->notes + k;
            float y = GRID_Y0 + (float)(n->time - viewTopMs) * ppm;
            if(y < GRID_Y0 - 30 || y > GRID_Y1 + 30)
                continue;
            float x = GRID_X0 + DispLane(s, n->lane) * LANE_W;
            Color c = LaneColor(n->lane);
            char isPast = 0;
            char isHit = 0;
            if(playFollow) {
                float dt = n->time - (float)cursorMs;
                if(playing && fabsf(dt) <= 200.0f)
                    isHit = 1;
                else if(dt < 0)
                    isPast = 1;
            }
            if(n->len > 0) {
                float h = n->len * ppm;
                if(h < 6) h = 6;
                float top = y;
                float bot = y + h;
                if(top < GRID_Y0) top = GRID_Y0;
                if(bot > GRID_Y1) bot = GRID_Y1;
                if(bot > top) {
                    Color bc = c;
                    bc.a = isPast ? 50 : (isHit ? 255 : 160);
                    DrawRectangle((int)(x + LANE_W / 2 - 7), (int)top, 14, (int)(bot - top), bc);
                }
            }
            // seta do jogo (bem mais visivel que quadrado)
            {
                int d = n->lane % 4;
                float cx = x + LANE_W / 2;
                if(isHit) {
                    float r = 34.0f;
                    DrawCircle((int)cx, (int)(y + 1), r, (Color) {255, 255, 255, 60});
                    DrawCircle((int)cx, (int)(y + 1), r - 8, (Color) {255, 255, 200, 90});
                }
                Color keepC = edArrows[d].color;
                float keepSx = edArrows[d].scaleX;
                float keepSy = edArrows[d].scaleY;
                if(isPast)
                    edArrows[d].color = (Color) {255, 255, 255, 90};
                else if(isHit) {
                    edArrows[d].color = WHITE;
                    edArrows[d].scaleX = keepSx * 1.25f;
                    edArrows[d].scaleY = keepSy * 1.25f;
                }
                edArrows[d].position.x = cx + edArrowAdj[d].x;
                edArrows[d].position.y = y + 1 + edArrowAdj[d].y;
                Render_DrawAnimatedObject(edArrows + d);
                edArrows[d].color = keepC;
                edArrows[d].scaleX = keepSx;
                edArrows[d].scaleY = keepSy;
            }
        }
    }

    // cursor (play grosso + agulha de vinil; as notas sobem ate ele, igual ao jogo)
    {
        float y = GRID_Y0 + (float)(cursorMs - viewTopMs) * ppm;
        if(y >= GRID_Y0 - 30 && y <= GRID_Y1 + 30) {
            int x0 = (int)GRID_X0 - 8;
            int x1 = (int)(GRID_X0 + LANE_W * 8) + 8;
            int yi = (int)y;
            // linha grossa: sombra preta + vermelho + fio branco no meio
            DrawRectangle(x0 - 2, yi - 4, (x1 - x0) + 4, 8, BLACK);
            DrawRectangle(x0, yi - 3, x1 - x0, 6, RED);
            DrawRectangle(x0, yi - 1, x1 - x0, 2, WHITE);
            // agulha: triangulo apontando pro play (lado esquerdo, estilo vinil)
            DrawTriangle((Vector2) {(float)x0 - 22, (float)yi - 13},
                         (Vector2) {(float)x0 - 22, (float)yi + 13},
                         (Vector2) {(float)x0 + 2, (float)yi}, WHITE);
            DrawTriangle((Vector2) {(float)x0 - 19, (float)yi - 10},
                         (Vector2) {(float)x0 - 19, (float)yi + 10},
                         (Vector2) {(float)x0 - 1, (float)yi}, RED);
        }
    }

    // mouse (travado com janela/drop abertos)
    if(ceDlg == DLG_NONE && !charDrop) {
        Vector2 m = MouseWorld();
        // barra: clica e arrasta pra andar na musica
        {
            double end = SongEndMs();
            if(end < 1000) end = 1000;
            Rectangle bar = {SBAR_X, SBAR_Y, SBAR_W, SBAR_H};
            char inside = (m.x >= bar.x && m.x <= bar.x + bar.width && m.y >= bar.y - 8 && m.y <= bar.y + bar.height + 8);
            if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && inside)
                barDrag = 1;
            if(barDrag) {
                if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    double frac = (m.x - bar.x) / bar.width;
                    if(frac < 0) frac = 0;
                    if(frac > 1) frac = 1;
                    cursorMs = frac * end;
                    viewTopMs = cursorMs - VIEW_MS * 0.5;
                    if(viewTopMs < -500) viewTopMs = -500;
                } else {
                    if(playing)
                        SeekAllAudio(cursorMs);
                    barDrag = 0;
                }
            }
        }
        if(m.x >= GRID_X0 && m.x <= GRID_X0 + LANE_W * 8 && m.y >= GRID_Y0 && m.y <= GRID_Y1) {
            int lane = (int)((m.x - GRID_X0) / LANE_W);
            if(lane >= 0 && lane < 8) {
                // fantasma: mostra onde a nota vai cair (X vermelho = ja tem, direito apaga)
                double t = viewTopMs + (m.y - GRID_Y0) / ppm;
                int si = SecAtTime(fmax(t, 0));
                EdSec* s = chart.secs + si;
                int raw = DispLane(s, lane); // casa da tela -> casa do arquivo
                float beatMs = 60000.0f / s->bpm;
                float stepMs = beatMs / snapDiv;
                float snapped = roundf((float)(t / stepMs)) * stepMs;
                if(snapped < 0) snapped = 0;
                float gy = GRID_Y0 + (float)(snapped - viewTopMs) * ppm;
                float cx = GRID_X0 + lane * LANE_W + LANE_W / 2;
                int exist = EdSec_FindNear(s, snapped, raw, fmaxf(stepMs / 2, 15.0f));
                if(gy >= GRID_Y0 - 26 && gy <= GRID_Y1 + 26) {
                    if(exist >= 0) {
                        DrawLine((int)(cx - 14), (int)(gy - 14), (int)(cx + 14), (int)(gy + 14), RED);
                        DrawLine((int)(cx - 14), (int)(gy + 14), (int)(cx + 14), (int)(gy - 14), RED);
                    } else {
                        Color keep = edArrows[raw % 4].color;
                        edArrows[raw % 4].color = (Color) {255, 255, 255, 150};
                        edArrows[raw % 4].position.x = cx + edArrowAdj[raw % 4].x;
                        edArrows[raw % 4].position.y = gy + 1 + edArrowAdj[raw % 4].y;
                        Render_DrawAnimatedObject(edArrows + (raw % 4));
                        edArrows[raw % 4].color = keep;
                    }
                }
                // esquerdo SO poe, direito SO apaga
                if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if(exist < 0) {
                        Touch();
                        EdSec_Add(s, snapped, raw, 0);
                        secIdx = si;
                        cursorMs = snapped;
                    }
                }
                if(IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                    int idx = EdSec_FindNear(s, (float)t, raw, beatMs);
                    if(idx >= 0 && EdSec_RemoveAt(s, idx)) {
                        Touch();
                        secIdx = si;
                    }
                }
            }
        }
    }
}

// ---------- acoes de trecho (usadas no menu e nas abas, sem duplicar) ----------
static void Edit_DoNewPart(void) {
    Touch();
    EdSec* last = chart.secs + chart.nsec - 1;
    EdSec* g = realloc(chart.secs, sizeof(EdSec) * (size_t)(chart.nsec + 1));
    if(g == NULL) return;
    chart.secs = g;
    EdSec* n = chart.secs + chart.nsec;
    memset(n, 0, sizeof(*n));
    n->mustHit = last->mustHit;
    n->bpm = last->bpm;
    n->lenSteps = 16;
    chart.nsec++;
    secIdx = chart.nsec - 1;
    SecTimes_Invalidate();
    cursorMs = SecStartMs(secIdx);
    viewTopMs = cursorMs - VIEW_MS * 0.3;
    if(viewTopMs < 0) viewTopMs = 0;
    StatusMsg("TRECHO NOVO NO FIM");
}
static void Edit_DoDelPart(void) {
    if(chart.nsec <= 1) { StatusMsg("NOT POSSIBLE TO DELETE HAVE ONLY 1 PART"); return; }
    Touch();
    free(chart.secs[secIdx].notes);
    memmove(chart.secs + secIdx, chart.secs + secIdx + 1, sizeof(EdSec) * (size_t)(chart.nsec - secIdx - 1));
    chart.nsec--;
    if(secIdx >= chart.nsec) secIdx = chart.nsec - 1;
    SecTimes_Invalidate();
    StatusMsg("part deleted");
}
static void Edit_DoCopy(void) {
    if(copyValid) free(copyBuf.notes);
    memset(&copyBuf, 0, sizeof(copyBuf));
    EdSec* s = chart.secs + secIdx;
    copyBuf.mustHit = s->mustHit;
    copyBuf.bpm = s->bpm;
    copyBuf.lenSteps = s->lenSteps;
    if(s->count > 0) {
        copyBuf.notes = malloc(sizeof(EdNote) * (size_t)s->count);
        if(copyBuf.notes != NULL) {
            copyBuf.count = copyBuf.cap = s->count;
            memcpy(copyBuf.notes, s->notes, sizeof(EdNote) * (size_t)s->count);
        }
    }
    copyValid = 1;
    StatusMsg("TRECHO COPIADO");
}
static void Edit_DoPaste(void) {
    if(!copyValid) { StatusMsg("COPIE ANTES (C)"); return; }
    Touch();
    double base = SongEndMs();
    double srcStart = SecStartMs(secIdx);
    EdSec* g = realloc(chart.secs, sizeof(EdSec) * (size_t)(chart.nsec + 1));
    if(g == NULL) return;
    chart.secs = g;
    EdSec* n = chart.secs + chart.nsec;
    memset(n, 0, sizeof(*n));
    n->mustHit = copyBuf.mustHit;
    n->bpm = chart.secs[chart.nsec - 1].bpm;
    n->lenSteps = copyBuf.lenSteps;
    // copy ja vem ordenado+dedupado: anexa puro (mantem ordem no shift)
    for(int i = 0; i < copyBuf.count; i++)
        EdSec_AddBulk(n, (float)(base + (copyBuf.notes[i].time - srcStart)), copyBuf.notes[i].lane, copyBuf.notes[i].len);
    chart.nsec++;
    secIdx = chart.nsec - 1;
    SecTimes_Invalidate();
    cursorMs = SecStartMs(secIdx);
    viewTopMs = cursorMs - VIEW_MS * 0.3;
    if(viewTopMs < 0) viewTopMs = 0;
    StatusMsg("COLADO NO FIM");
}
static void Edit_DoTestPlay(void) {
    StopPlay();
    Editor_Save();
    PlayState_SetSongDir(chart.dir);
    PlayState_SetReturnEditor(1);
    PlayState_SetScene();
}
// menu File/Edit/View estilo Psych (dropdown, sem alloc)
static char MenuItem(Rectangle r, const char* label) {
    Vector2 m = MouseWorld();
    char hov = (m.x >= r.x && m.x <= r.x + r.width && m.y >= r.y && m.y <= r.y + r.height);
    DrawRectangleRec(r, hov ? (Color){60,60,60,255} : (Color){18,18,18,255});
    DrawTextEx(mainFont, label, (Vector2){r.x + 8, r.y + 5}, 20, 1, WHITE);
    return hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
static void Edit_DrawMenuBar(void) {
    DrawRectangle(0, 0, 1280, (int)MENU_H, (Color){16,16,16,255});
    DrawLine(0, (int)MENU_H, 1280, (int)MENU_H, (Color){60,60,60,255});
    Vector2 m = MouseWorld();
    const char* names[3] = {"File", "Edit", "View"};
    for(int i = 0; i < 3; i++) {
        Rectangle r = {(float)(PANEL_X + i * 70), 0, 68, MENU_H};
        char hov = (m.x >= r.x && m.x <= r.x + r.width && m.y >= r.y && m.y <= r.y + r.height);
        char open = (menuOpen == i + 1);
        DrawRectangleRec(r, (open || hov) ? (Color){60,60,60,255} : (Color){16,16,16,255});
        DrawTextEx(mainFont, names[i], (Vector2){r.x + 12, 5}, 20, 1, open ? GREEN : WHITE);
        if(hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            menuOpen = open ? 0 : (i + 1);
            charDrop = 0;
            focusBox = NULL;
            focusNum = NULL;
        }
    }
    if(menuOpen == 0) return;
    float mx = (float)(PANEL_X + (menuOpen - 1) * 70);
    if(menuOpen == 1) {
        Rectangle box = {mx, MENU_H, 230, 5 * 30 + 8};
        DrawRectangleRec(box, (Color){15,15,15,245});
        DrawRectangleLinesEx(box, 1, GREEN);
        if(MenuItem((Rectangle){box.x + 4, box.y + 4, 222, 28}, "Save  Ctrl+S")) { menuOpen = 0; Editor_Save(); }
        else if(MenuItem((Rectangle){box.x + 4, box.y + 32, 222, 28}, "Save As...")) { menuOpen = 0; saveAsBox.buf[0] = 0; importPath[0] = 0; ceDlg = DLG_SAVEAS; focusBox = &saveAsBox; }
        else if(MenuItem((Rectangle){box.x + 4, box.y + 60, 222, 28}, "Save Compressed")) { menuOpen = 0; EdChart_SaveCompressed(&chart); StatusMsg("SALVO COMPRIMIDO"); }
        else if(MenuItem((Rectangle){box.x + 4, box.y + 88, 222, 28}, "Test Play  F5")) { menuOpen = 0; Edit_DoTestPlay(); }
        else if(MenuItem((Rectangle){box.x + 4, box.y + 116, 222, 28}, "Back to list  ESC")) { menuOpen = 0; StopPlay(); if(chart.dirty) Editor_Save(); Editor_UnloadIcons(); ceMode = CE_PICKER; Picker_Refresh(); }
        else if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if(!(m.x >= box.x && m.x <= box.x + box.width && m.y >= 0 && m.y <= box.y + box.height)) menuOpen = 0;
        }
    } else if(menuOpen == 2) {
        Rectangle box = {mx, MENU_H, 230, 5 * 30 + 8};
        DrawRectangleRec(box, (Color){15,15,15,245});
        DrawRectangleLinesEx(box, 1, GREEN);
        if(MenuItem((Rectangle){box.x + 4, box.y + 4, 222, 28}, "Undo  Ctrl+Z")) { menuOpen = 0; Undo_Pop(); chart.dirty = 1; }
        else if(MenuItem((Rectangle){box.x + 4, box.y + 32, 222, 28}, "Copy part  C")) { menuOpen = 0; Edit_DoCopy(); }
        else if(MenuItem((Rectangle){box.x + 4, box.y + 60, 222, 28}, "Paste at end  V")) { menuOpen = 0; Edit_DoPaste(); }
        else if(MenuItem((Rectangle){box.x + 4, box.y + 88, 222, 28}, "New part  N")) { menuOpen = 0; Edit_DoNewPart(); }
        else if(MenuItem((Rectangle){box.x + 4, box.y + 116, 222, 28}, "Delete part  X")) { menuOpen = 0; Edit_DoDelPart(); }
        else if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if(!(m.x >= box.x && m.x <= box.x + box.width && m.y >= 0 && m.y <= box.y + box.height)) menuOpen = 0;
        }
    } else {
        Rectangle box = {mx, MENU_H, 230, 2 * 30 + 8};
        DrawRectangleRec(box, (Color){15,15,15,245});
        DrawRectangleLinesEx(box, 1, GREEN);
        const char* followName = playFollow ? "Note player : ON" : "Note player : OFF";
        if(MenuItem((Rectangle){box.x + 4, box.y + 4, 222, 28}, followName)) { menuOpen = 0; playFollow = !playFollow; }
        else if(MenuItem((Rectangle){box.x + 4, box.y + 32, 222, 28}, "Go to part start")) { menuOpen = 0; cursorMs = SecStartMs(secIdx); }
        else if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if(!(m.x >= box.x && m.x <= box.x + box.width && m.y >= 0 && m.y <= box.y + box.height)) menuOpen = 0;
        }
    }
}
// abas laterais (uma ativa por vez)
static void Edit_DrawTabs(void) {
    float x0 = PANEL_X;
    float y0 = 34;
    float tw = 102;
    Vector2 m = MouseWorld();
    for(int t = 0; t < TAB_COUNT; t++) {
        Rectangle r = {x0 + t * (tw + 4), y0, tw, 26};
        char active = (activeTab == t);
        char hov = (m.x >= r.x && m.x <= r.x + r.width && m.y >= r.y && m.y <= r.y + r.height);
        DrawRectangleRec(r, active ? (Color){45,90,45,255} : hov ? (Color){60,60,60,255} : (Color){28,28,28,255});
        DrawRectangleLinesEx(r, 1, active ? GREEN : GRAY);
        Vector2 s = MeasureTextEx(mainFont, ChartTab_Name(t), 18, 1);
        DrawTextEx(mainFont, ChartTab_Name(t), (Vector2){r.x + (r.width - s.x) / 2, r.y + 4}, 18, 1, active ? GREEN : WHITE);
        if(hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ceDlg == DLG_NONE && !charDrop) {
            activeTab = t;
            menuOpen = 0;
            focusBox = NULL;
            focusNum = NULL;
        }
    }
}

static void Edit_DrawPanel_Song(float x, float y) {
    char blocked = (ceDlg != DLG_NONE || charDrop || menuOpen);
    DrawTextEx(mainFont, "SONG", (Vector2){x, y}, 22, 1, GREEN);
    y += 30;
    char titleBuf[96];
    snprintf(titleBuf, sizeof(titleBuf), "%s%s", chart.name, chart.dirty ? "*" : "");
    DrawTextEx(mainFont, titleBuf, (Vector2){x, y}, 26, 2, WHITE);
    y += 36;
    float w1 = MeasureTextEx(mainFont, p1Box.buf[0] ? p1Box.buf : "bf", 20, 1).x;
    float w2 = MeasureTextEx(mainFont, p2Box.buf[0] ? p2Box.buf : "dad", 20, 1).x;
    float w3 = MeasureTextEx(mainFont, stageBox.buf[0] ? stageBox.buf : "stage", 20, 1).x;
    float bw = w1;
    if(w2 > bw) bw = w2;
    if(w3 > bw) bw = w3;
    bw += 24;
    if(bw < 140) bw = 140;
    if(bw > 240) bw = 240;
    DrawTextEx(mainFont, "Bf:", (Vector2){x, y + 4}, 22, 1, WHITE);
    Rectangle r1 = {x + 120, y, bw, 32};
    TxtBox_Update(&p1Box, r1, blocked);
    TxtBox_Draw(&p1Box, r1, "bf");
    Rectangle d1 = {x + 120 + bw + 6, y, 30, 32};
    if(DropBtn(d1, charDrop == 1)) {
        if(charDrop == 1) charDrop = 0;
        else { charDrop = 1; dropBox = &p1Box; dropNames = charNames; dropCount = charCount; dropX = d1.x; dropY = d1.y + d1.height + 4; dropW = bw + 36; dropJustOpened = 1; }
        focusBox = NULL; focusNum = NULL;
    }
    y += 38;
    DrawTextEx(mainFont, "Opponent:", (Vector2){x, y + 4}, 22, 1, WHITE);
    Rectangle r2 = {x + 120, y, bw, 32};
    TxtBox_Update(&p2Box, r2, blocked);
    TxtBox_Draw(&p2Box, r2, "dad");
    Rectangle d2 = {x + 120 + bw + 6, y, 30, 32};
    if(DropBtn(d2, charDrop == 2)) {
        if(charDrop == 2) charDrop = 0;
        else { charDrop = 2; dropBox = &p2Box; dropNames = charNames; dropCount = charCount; dropX = d2.x; dropY = d2.y + d2.height + 4; dropW = bw + 36; dropJustOpened = 1; }
        focusBox = NULL; focusNum = NULL;
    }
    y += 38;
    DrawTextEx(mainFont, "Stage:", (Vector2){x, y + 4}, 22, 1, WHITE);
    Rectangle r3 = {x + 120, y, bw, 32};
    TxtBox_Update(&stageBox, r3, blocked);
    TxtBox_Draw(&stageBox, r3, "stage");
    Rectangle d3 = {x + 120 + bw + 6, y, 30, 32};
    if(DropBtn(d3, charDrop == 3)) {
        if(charDrop == 3) charDrop = 0;
        else { charDrop = 3; dropBox = &stageBox; dropNames = stageNames; dropCount = stageCount; dropX = d3.x; dropY = d3.y + d3.height + 4; dropW = bw + 36; dropJustOpened = 1; }
        focusBox = NULL; focusNum = NULL;
    }
    y += 38;
    // Note Skin dropdown (mesmo estilo de Bf/Opponent/Stage)
    float w4 = MeasureTextEx(mainFont, skinBox.buf[0] ? skinBox.buf : "default", 20, 1).x;
    if(w4 > bw) bw = w4; // usa mesma largura se skin for mais longa
    DrawTextEx(mainFont, "Note Skin:", (Vector2){x, y + 4}, 22, 1, WHITE);
    Rectangle r4 = {x + 120, y, bw, 32};
    TxtBox_Update(&skinBox, r4, blocked);
    TxtBox_Draw(&skinBox, r4, "default");
    Rectangle d4 = {x + 120 + bw + 6, y, 30, 32};
    if(DropBtn(d4, skinDrop)) {
        if(skinDrop) skinDrop = 0;
        else {
            skinDrop = 1;
            dropBox = &skinBox;
            dropNames = skinNames;
            dropCount = skinCount;
            dropX = d4.x;
            dropY = d4.y + d4.height + 4;
            dropW = bw + 36;
            dropJustOpened = 1;
        }
        focusBox = NULL; focusNum = NULL;
    }
    y += 42;
    DrawTextEx(mainFont, "bpm:", (Vector2){x, y + 4}, 22, 1, WHITE);
    {
        Rectangle r = {x + 120, y, 80, 32};
        if(focusNum != &nbTopBpm) NumBox_Refresh(&nbTopBpm, chart.bpm, 0);
        NumBox_Update(&nbTopBpm, r, blocked);
        NumBox_Draw(&nbTopBpm, r);
    }
    DrawTextEx(mainFont, "speed:", (Vector2){x + 230, y + 4}, 22, 1, WHITE);
    {
        Rectangle r = {x + 310, y, 70, 32};
        if(focusNum != &nbSpeed) NumBox_Refresh(&nbSpeed, chart.speed, 0);
        NumBox_Update(&nbSpeed, r, blocked);
        NumBox_Draw(&nbSpeed, r);
    }
    y += 44;
    if(Button((Rectangle){x, y, 190, 40}, "SALVAR", 22)) Editor_Save();
    if(Button((Rectangle){x + 200, y, 190, 40}, "SALVAR COMO", 20)) {
        saveAsBox.buf[0] = 0; importPath[0] = 0; ceDlg = DLG_SAVEAS; focusBox = &saveAsBox;
    }
}
static void Edit_DrawPanel_Section(float x, float y) {
    char blocked = (ceDlg != DLG_NONE || charDrop || menuOpen);
    char buf[96];
    SecFollowCursor();
    snprintf(buf, sizeof(buf), "SECTION %d/%d", secIdx + 1, chart.nsec);
    DrawTextEx(mainFont, buf, (Vector2){x, y}, 22, 1, GREEN);
    y += 32;
    DrawTextEx(mainFont, "turn (TAB):", (Vector2){x, y + 4}, 22, 1, WHITE);
    if(Button((Rectangle){x + 150, y, 180, 32}, chart.secs[secIdx].mustHit ? "bf" : "opponent", 20)) {
        Touch(); chart.secs[secIdx].mustHit = !chart.secs[secIdx].mustHit;
    }
    y += 42;
    // steps = tamanho do trecho em passos (16 = padrao, 4 tempos). Era "size:".
    DrawTextEx(mainFont, "steps:", (Vector2){x, y + 4}, 22, 1, WHITE);
    {
        Rectangle r = {x + 150, y, 90, 32};
        if(focusNum != &nbLen) NumBox_Refresh(&nbLen, (float)chart.secs[secIdx].lenSteps, 1);
        NumBox_Update(&nbLen, r, blocked);
        NumBox_Draw(&nbLen, r);
    }
    DrawTextEx(mainFont, "16=4 tempos", (Vector2){x + 250, y + 8}, 18, 1, YELLOW);
    y += 42;
    DrawTextEx(mainFont, "bpm:", (Vector2){x, y + 4}, 22, 1, WHITE);
    {
        Rectangle r = {x + 150, y, 90, 32};
        if(focusNum != &nbSecBpm) NumBox_Refresh(&nbSecBpm, chart.secs[secIdx].bpm, 0);
        NumBox_Update(&nbSecBpm, r, blocked);
        NumBox_Draw(&nbSecBpm, r);
    }
    y += 46;
    if(Button((Rectangle){x, y, 120, 36}, "+part", 20)) Edit_DoNewPart();
    if(Button((Rectangle){x + 130, y, 120, 36}, "X part", 20)) Edit_DoDelPart();
    y += 44;
    if(Button((Rectangle){x, y, 120, 36}, "copy", 20)) Edit_DoCopy();
    if(Button((Rectangle){x + 130, y, 120, 36}, "paste", 20)) Edit_DoPaste();
    y += 44;
}
static void Edit_DrawPanel_Note(float x, float y) {
    char buf[128];
    SecFollowCursor();
    DrawTextEx(mainFont, "NOTE", (Vector2){x, y}, 22, 1, GREEN);
    y += 32;
    char tbuf[32];
    TimeFmt(tbuf, sizeof(tbuf), cursorMs);
    snprintf(buf, sizeof(buf), "cursor: %s", tbuf);
    DrawTextEx(mainFont, buf, (Vector2){x, y}, 20, 1, WHITE);
    y += 30;
    snprintf(buf, sizeof(buf), "in part: %d notes", chart.secs[secIdx].count);
    DrawTextEx(mainFont, buf, (Vector2){x, y}, 20, 1, WHITE);
    y += 30;
    int total = 0;
    for(int i = 0; i < chart.nsec; i++) total += chart.secs[i].count;
    snprintf(buf, sizeof(buf), "total: %d notes", total);
    DrawTextEx(mainFont, buf, (Vector2){x, y}, 20, 1, WHITE);
    y += 36;
    DrawTextEx(mainFont, "click adds  right-click deletes", (Vector2){x, y}, 18, 1, YELLOW);
    y += 24;
    DrawTextEx(mainFont, "SHIFT/CTRL+wheel = long note", (Vector2){x, y}, 18, 1, YELLOW);
}
static void Edit_DrawPanel_Data(float x, float y) {
    char buf[160];
    DrawTextEx(mainFont, "DATA", (Vector2){x, y}, 22, 1, GREEN);
    y += 32;
    snprintf(buf, sizeof(buf), "parts: %d", chart.nsec);
    DrawTextEx(mainFont, buf, (Vector2){x, y}, 20, 1, WHITE);
    y += 30;
    int total = 0;
    for(int i = 0; i < chart.nsec; i++) total += chart.secs[i].count;
    snprintf(buf, sizeof(buf), "notes: %d", total);
    DrawTextEx(mainFont, buf, (Vector2){x, y}, 20, 1, WHITE);
    y += 30;
    char tbuf[32];
    TimeFmt(tbuf, sizeof(tbuf), SongEndMs());
    snprintf(buf, sizeof(buf), "length: %s", tbuf);
    DrawTextEx(mainFont, buf, (Vector2){x, y}, 20, 1, WHITE);
    y += 30;
    snprintf(buf, sizeof(buf), "dir: %s", chart.dir[0] ? chart.dir : "(unsaved)");
    DrawTextEx(mainFont, buf, (Vector2){x, y}, 18, 1, YELLOW);
    y += 28;
    snprintf(buf, sizeof(buf), "bpm %.1f  speed %.2f%s", (double)chart.bpm, (double)chart.speed, chart.dirty ? " *" : "");
    DrawTextEx(mainFont, buf, (Vector2){x, y}, 18, 1, YELLOW);
}
static void Edit_DrawPanel_Charting(float x, float y) {
    DrawTextEx(mainFont, "CHARTING", (Vector2){x, y}, 22, 1, GREEN);
    y += 34;
    if(Button((Rectangle){x, y, 190, 40}, playing ? "PAUSAR" : "OUVIR", 22)) TogglePlay();
    y += 50;
    if(Button((Rectangle){x, y, 190, 40}, "play (F5)", 22)) Edit_DoTestPlay();
    y += 50;
    DrawTextEx(mainFont, "space = play/stop", (Vector2){x, y}, 18, 1, YELLOW);
    y += 24;
    DrawTextEx(mainFont, "drag red bar = seek", (Vector2){x, y}, 18, 1, YELLOW);
    y += 24;
    DrawTextEx(mainFont, "W/S or wheel = scroll", (Vector2){x, y}, 18, 1, YELLOW);
}
static void Edit_DrawPanel_Events(float x, float y) {
    DrawTextEx(mainFont, "EVENTS", (Vector2){x, y}, 22, 1, GREEN);
    y += 34;
    DrawTextEx(mainFont, "events.json ignorado", (Vector2){x, y}, 20, 1, YELLOW);
    y += 30;
    DrawTextEx(mainFont, "(Song.c nao le events,", (Vector2){x, y}, 18, 1, YELLOW);
    y += 24;
    DrawTextEx(mainFont, "compat Psych so p/ notes)", (Vector2){x, y}, 18, 1, YELLOW);
}

static void Edit_DrawPanel(void) {
    float x = PANEL_X;
    float y = 70;
    SecFollowCursor();
    // sem bloco de fundo (pedido do dono): painel direto no bg, ajuda vai p/ baixo
    DrawLine((int)(PANEL_X - 8), 62, (int)(PANEL_X - 8), 672, (Color){60,60,60,255});
    char titleBuf[96];
    snprintf(titleBuf, sizeof(titleBuf), "%s%s", chart.name, chart.dirty ? "*" : "");
    // trunca nome longo p/ nao invadir OPEN nem a grade
    for(int guard = 0; guard < 32; guard++) {
        Vector2 ts = MeasureTextEx(mainFont, titleBuf, 24, 2);
        if(ts.x <= 280 || titleBuf[0] == 0) break;
        size_t L = strlen(titleBuf);
        if(L > 4) { titleBuf[L - 4] = '.'; titleBuf[L - 3] = '.'; titleBuf[L - 2] = '.'; titleBuf[L - 1] = 0; }
        else break;
    }
    DrawTextEx(mainFont, titleBuf, (Vector2){x, 66}, 24, 2, WHITE);
    // OPEN ao lado da musica: volta p/ lista p/ abrir outra (mesmo que ESC)
    if(Button((Rectangle){x + 300, 62, 110, 32}, "OPEN", 20)) {
        StopPlay();
        if(chart.dirty) Editor_Save();
        Editor_UnloadIcons();
        ceMode = CE_PICKER;
        Picker_Refresh();
        return;
    }
    if(activeTab == TAB_SONG) Edit_DrawPanel_Song(x, y + 30);
    else if(activeTab == TAB_SECTION) Edit_DrawPanel_Section(x, y + 30);
    else if(activeTab == TAB_NOTE) Edit_DrawPanel_Note(x, y + 30);
    else if(activeTab == TAB_DATA) Edit_DrawPanel_Data(x, y + 30);
    else if(activeTab == TAB_CHARTING) Edit_DrawPanel_Charting(x, y + 30);
    else Edit_DrawPanel_Events(x, y + 30);
    // ajuda no espaco de baixo, sem bloco: limao com sombra p/ destacar no fundo roxo
    {
        Color help = (Color){190, 255, 90, 255};
        Color shade = (Color){0, 0, 0, 200};
        const char* hs[4] = {
            "space play  F5 test  CTRL+S save  TAB turn  T grid",
            "W/S scroll  N/X part  C/V copy/paste  ,/. bpm  ESC list",
            "click adds  right-click deletes  wheel = scroll",
            "G/H prev/next part  HOME goto part start"
        };
        float hy[4] = {648, 668, 688, 708};
        for(int hi = 0; hi < 4; hi++) {
            DrawTextEx(mainFont, hs[hi], (Vector2){PANEL_X + 2, hy[hi] + 2}, 15, 1, shade);
            DrawTextEx(mainFont, hs[hi], (Vector2){PANEL_X, hy[hi]}, 15, 1, help);
        }
    }
}
// (removido: migrado p/ abas Song/Section)

static void Edit_Draw(void) {
    Render_SetCamera(&camEd);
    Render_DrawGraphicObject(&bgEd);

    // tempo no canto + status
    char tbuf[32];
    TimeFmt(tbuf, sizeof(tbuf), cursorMs);
    DrawTextEx(mainFont, tbuf, (Vector2) {1110, 2}, 24, 2, WHITE);
    // status embaixo da grade (antes caia em cima do painel, lado direito)
    if(statusTimer > 0) {
        DrawTextEx(mainFont, statusText, (Vector2) {GRID_X0 + 2, 692}, 20, 1, (Color){0, 0, 0, 200});
        DrawTextEx(mainFont, statusText, (Vector2) {GRID_X0, 690}, 20, 1, YELLOW);
    }

    Edit_DrawGrid();
    Edit_DrawTabs();
    Edit_DrawPanel();
    Edit_DrawMenuBar();
    CharDrop_Draw();
    Render_StopCamera();
}

// ---------- cena ----------
static void ChartEditor_Create([[maybe_unused]] RayScene* scene) {
    Render_DefaultRGT(&bgEd);
    bgEd.objType = RGT_IMAGE;
    bgEd.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");

    camEd.target = (Vector2) {1280 / 2, 720 / 2};
    camEd.zoom = 1;
    camEd.rotation = 0;
    camEd.offset = (Vector2) {0, 0};

    EdArrows_Init();

    if(pendingDir[0] != 0) {
        char tmp[256];
        strncpy(tmp, pendingDir, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
        pendingDir[0] = 0;
        ceMode = CE_PICKER;
        Picker_Refresh();
        Editor_EnterDir(tmp);
    } else {
        ceMode = CE_PICKER;
        ceDlg = DLG_NONE;
        chartLoaded = 0;
        Picker_Refresh();
    }
    openBox.buf[0] = 0;
    saveAsBox.buf[0] = 0;
    importPath[0] = 0;
    focusBox = NULL;
    statusText[0] = 0;
    statusTimer = 0;
}

static void ChartEditor_Update([[maybe_unused]] RayScene* scene) {
    if(statusTimer > 0)
        statusTimer -= RayGame_DeltaTime();
    if(ceMode == CE_PICKER)
        Picker_Update();
    else
        Edit_Update();
    Dialogs_Update();
}

static void ChartEditor_Draw([[maybe_unused]] RayScene* scene) {
    if(ceMode == CE_PICKER)
        Picker_Draw();
    else
        Edit_Draw();
    Dialogs_Draw();
    if(statusTimer > 0 && ceMode == CE_PICKER)
        DrawTextEx(mainFont, statusText, (Vector2) {150, 640}, 24, 2, YELLOW);
}

static void ChartEditor_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bgEd.image);
    SongList_Free(pickerSongs);
    pickerSongs = NULL;
    pickerCount = 0;
    StopPlay();
    if(edHasAudio) {
        UnloadMusicStream(edInst);
        edHasAudio = 0;
    }
    if(edHasVoices) {
        UnloadMusicStream(edVoices);
        edHasVoices = 0;
    }
    if(edHasSplit) {
        UnloadMusicStream(edSplitO);
        UnloadMusicStream(edSplitP);
        edHasSplit = 0;
    }
    Editor_UnloadIcons();
    EdChart_Free(&chart);
    chartLoaded = 0;
    Undo_Clear();
    if(copyValid) {
        free(copyBuf.notes);
        memset(&copyBuf, 0, sizeof(copyBuf));
        copyValid = 0;
    }
    focusBox = NULL;
}

void ChartEditor_SetSongDir(const char* dir) {
    strncpy(pendingDir, dir, sizeof(pendingDir) - 1);
    pendingDir[sizeof(pendingDir) - 1] = 0;
}

static RayScene chartScene;
Scene_MakeSceneCode(
    chartScene,
    ChartEditor_SetScene,
    ChartEditor_Create,
    ChartEditor_Update,
    ChartEditor_Draw,
    ChartEditor_Destroy
)
