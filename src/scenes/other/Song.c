#include"std.h"
#include"scenes/Song.h"
#include"cJSON.h"

#define IERR  {puts("\ninvalid data");exit(1);}
#define CHECK_LEN(readOperation, expectedSize) if(readOperation != expectedSize) IERR

#define SONG_JSON_MAX_BYTES (8 * 1024 * 1024)

void File_ReadString(FILE* file, char str[32]) {
    int len;

    CHECK_LEN(fread(&len, sizeof(int), 1, file), 1)
    if(len < 1 || len > 31) IERR

    CHECK_LEN(fread(str, 1, len, file), len)

    str[len] = 0;
}

static void Song_CopyString(char dst[32], const char* src, const char* fallback) {
    const char* s = (src != NULL) ? src : fallback;
    strncpy(dst, s, 31);
    dst[31] = 0;
}

typedef struct {
    float time;
    int rawId; // 0-7
    float len;
} RawNote;

static int RawNote_Compare(const void* a, const void* b) {
    float ta = ((const RawNote*)a)->time;
    float tb = ((const RawNote*)b)->time;
    if(ta < tb) return -1;
    if(ta > tb) return 1;
    return 0;
}

void Song_Parse(Song* song, const char* songDataPath) {
    FILE* file = fopen(songDataPath, "rb");
    
    if(file == NULL) {
        printf("could not open file %s\n", songDataPath);
        exit(1);
    }

    File_ReadString(file, song->player1);
    File_ReadString(file, song->player2);
    File_ReadString(file, song->stage);

    CHECK_LEN(fread(&song->speed, sizeof(float), 1, file), 1)
    CHECK_LEN(fread(&song->sectionCount, sizeof(int), 1, file), 1)

    if(song->sectionCount < 1 || song->sectionCount > SIZE_MAX / sizeof(Section) /* overflow */) IERR
    if(song->speed < 0) IERR 

    song->sections = malloc(sizeof(Section) * song->sectionCount);

    for(size_t i = 0; i < song->sectionCount; i++) {
        Section* section = song->sections + i; 
        
        CHECK_LEN(fread(&section->len, sizeof(int), 1, file), 1)
        if(section->len < 1) IERR
        CHECK_LEN(fread(&section->bpm, sizeof(float), 1, file), 1)
        if(section->bpm < 10) IERR
        CHECK_LEN(fread(&section->mustHit, 1, 1, file), 1) 
        CHECK_LEN(fread(&section->noteCount, sizeof(int), 1, file), 1)
        if(section->noteCount < 0 || section->noteCount > SIZE_MAX / sizeof(DataNote) /* overflow */) IERR

       // printf("> section %d\n  len %d\n   bpm %f\n   mustHit %c\n   noteCount %d\n", i, section->len, section->bpm, section->mustHit, section->noteCount);

        section->notes = malloc(sizeof(DataNote) * section->noteCount);

        for(size_t j = 0; j < section->noteCount; j++) {
            DataNote* note = section->notes + j;

            CHECK_LEN(fread(&note->time, sizeof(float), 1, file), 1)
            if(note->time < 0) IERR
            CHECK_LEN(fread(&note->id, sizeof(int), 1, file), 1)
            int id = abs(note->id);
            if(id == 0 || id > 4) IERR
            CHECK_LEN(fread(&note->len, sizeof(float), 1, file), 1)
            if(note->len < 0) IERR 
        }
    }

    fclose(file);
}

void Song_Free(Song* song) {
    if(song->sections == NULL)
        return;
    for(size_t i = 0; i < (size_t)song->sectionCount; i++) {
        Section* section = song->sections + i; 
        free(section->notes);
    }
    free(song->sections);
    song->sections = NULL;
    song->sectionCount = 0;
}

// returns 1 on success, 0 if file missing or invalid (caller tries legacy)
char Song_ParseJSON(Song* song, const char* jsonPath) {
    memset(song, 0, sizeof(Song));

    FILE* file = fopen(jsonPath, "rb");
    if(file == NULL)
        return 0;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if(size <= 0 || size > SONG_JSON_MAX_BYTES) {
        fclose(file);
        return 0;
    }

    char* buf = malloc((size_t)size + 1);
    if(buf == NULL) {
        fclose(file);
        return 0;
    }
    if(fread(buf, 1, (size_t)size, file) != (size_t)size) {
        free(buf);
        fclose(file);
        return 0;
    }
    buf[size] = 0;
    fclose(file);

    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if(root == NULL)
        return 0;

    // FNF charts wrap in { "song": {...} }, accept bare object too
    cJSON* jsong = cJSON_GetObjectItem(root, "song");
    if(jsong == NULL)
        jsong = root;

    cJSON* jnotes = cJSON_GetObjectItem(jsong, "notes");
    cJSON* jspeed = cJSON_GetObjectItem(jsong, "speed");
    cJSON* jbpmtop = cJSON_GetObjectItem(jsong, "bpm");
    if(!cJSON_IsArray(jnotes)) {
        cJSON_Delete(root);
        return 0;
    }

    float topBpm = cJSON_IsNumber(jbpmtop) ? (float)jbpmtop->valuedouble : 100.0f;
    if(topBpm < 10.0f) topBpm = 100.0f;
    float speed = cJSON_IsNumber(jspeed) ? (float)jspeed->valuedouble : 1.0f;
    if(speed < 0.1f) speed = 1.0f;

    cJSON* jplayer1 = cJSON_GetObjectItem(jsong, "player1");
    cJSON* jplayer2 = cJSON_GetObjectItem(jsong, "player2");
    cJSON* jstage = cJSON_GetObjectItem(jsong, "stage");

    Song_CopyString(song->player1, cJSON_IsString(jplayer1) ? jplayer1->valuestring : NULL, "bf");
    Song_CopyString(song->player2, cJSON_IsString(jplayer2) ? jplayer2->valuestring : NULL, "dad");
    Song_CopyString(song->stage, cJSON_IsString(jstage) ? jstage->valuestring : NULL, "stage");

    song->bpm = topBpm;
    song->speed = speed;

    int sectionCount = cJSON_GetArraySize(jnotes);
    if(sectionCount < 1) {
        cJSON_Delete(root);
        return 0;
    }

    song->sections = calloc((size_t)sectionCount, sizeof(Section));
    if(song->sections == NULL) {
        cJSON_Delete(root);
        return 0;
    }
    song->sectionCount = sectionCount;

    // mirrors fix/Chart.hx: running bpm, changeBPM gate, dedup window 2ms
    float runningBpm = topBpm;
    for(int i = 0; i < sectionCount; i++) {
        cJSON* jsec = cJSON_GetArrayItem(jnotes, i);
        Section* section = song->sections + i;
        section->notes = NULL;
        section->noteCount = 0;

        if(!cJSON_IsObject(jsec))
            continue;

        cJSON* jlen = cJSON_GetObjectItem(jsec, "lengthInSteps");
        if(jlen == NULL) jlen = cJSON_GetObjectItem(jsec, "lengthinsteps");
        cJSON* jmust = cJSON_GetObjectItem(jsec, "mustHitSection");
        cJSON* jsecbpm = cJSON_GetObjectItem(jsec, "bpm");
        cJSON* jchange = cJSON_GetObjectItem(jsec, "changeBPM");
        cJSON* jsecnotes = cJSON_GetObjectItem(jsec, "sectionNotes");

        section->len = cJSON_IsNumber(jlen) ? (int)jlen->valuedouble : 16;
        if(section->len < 1) section->len = 16;
        section->mustHit = cJSON_IsTrue(jmust) ? 1 : 0;

        if(cJSON_IsTrue(jchange) && cJSON_IsNumber(jsecbpm) && (float)jsecbpm->valuedouble >= 10.0f)
            runningBpm = (float)jsecbpm->valuedouble;
        section->bpm = runningBpm;

        if(!cJSON_IsArray(jsecnotes))
            continue;

        int rawCount = cJSON_GetArraySize(jsecnotes);
        if(rawCount < 1)
            continue;

        RawNote* raw = malloc(sizeof(RawNote) * (size_t)rawCount);
        if(raw == NULL)
            continue;
        int valid = 0;
        for(int j = 0; j < rawCount; j++) {
            cJSON* jn = cJSON_GetArrayItem(jsecnotes, j);
            if(!cJSON_IsArray(jn) || cJSON_GetArraySize(jn) < 2)
                continue;
            cJSON* jt = cJSON_GetArrayItem(jn, 0);
            cJSON* ji = cJSON_GetArrayItem(jn, 1);
            cJSON* jl = cJSON_GetArraySize(jn) > 2 ? cJSON_GetArrayItem(jn, 2) : NULL;
            if(!cJSON_IsNumber(jt) || !cJSON_IsNumber(ji))
                continue;
            float t = (float)jt->valuedouble;
            int id = ((int)ji->valuedouble) % 8;
            if(id < 0) id += 8;
            float l = (cJSON_IsNumber(jl) && (float)jl->valuedouble >= 0.0f) ? (float)jl->valuedouble : 0.0f;
            if(t < 0.0f)
                continue;
            raw[valid].time = t;
            raw[valid].rawId = id;
            raw[valid].len = l;
            valid++;
        }
        if(valid == 0) {
            free(raw);
            continue;
        }

        qsort(raw, (size_t)valid, sizeof(RawNote), RawNote_Compare);

        // dedup same lane within 2ms, same as Chart.hx idiot[8]
        float last[8];
        for(int k = 0; k < 8; k++) last[k] = -999999.0f;
        DataNote* out = malloc(sizeof(DataNote) * (size_t)valid);
        if(out == NULL) {
            free(raw);
            continue;
        }
        int kept = 0;
        char mustHit = section->mustHit;
        for(int j = 0; j < valid; j++) {
            int lane = raw[j].rawId;
            if(raw[j].time - last[lane] < 2.0f)
                continue;
            last[lane] = raw[j].time;

            int id;
            if(mustHit) {
                if(lane < 4) id = -(lane + 1);
                else id = lane - 3;
            } else {
                if(lane < 4) id = lane + 1;
                else id = -(lane - 3);
            }
            out[kept].time = raw[j].time;
            out[kept].id = id;
            out[kept].len = raw[j].len;
            kept++;
        }
        free(raw);

        if(kept == 0) {
            free(out);
            continue;
        }
        section->notes = out;
        section->noteCount = kept;
    }

    cJSON_Delete(root);
    return 1;
}

void Song_LoadSong(Song* song, const char* songName) {
    char dir[256];
    snprintf(dir, sizeof(dir), "assets/songs/%s", songName);
    Song_LoadSongFromDir(song, dir);
}

// ---- descoberta de chart (nosso data.json + nomes estilo Psych) ----
static char EndsWithI(const char* str, const char* suf) {
    size_t ls = strlen(str);
    size_t lf = strlen(suf);
    if(lf > ls)
        return 0;
    const char* p = str + ls - lf;
    for(size_t i = 0; i < lf; i++) {
        char a = p[i], b = suf[i];
        if(a >= 'A' && a <= 'Z') a += 32;
        if(b >= 'A' && b <= 'Z') b += 32;
        if(a != b)
            return 0;
    }
    return 1;
}

// 1 = tem cara de chart (array de sections com notes)
static char LooksLikeChart(const char* path) {
    FILE* f = fopen(path, "rb");
    if(f == NULL)
        return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0 || size > SONG_JSON_MAX_BYTES) {
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
    cJSON* jsong = cJSON_GetObjectItem(root, "song");
    if(jsong == NULL)
        jsong = root;
    cJSON* jnotes = cJSON_GetObjectItem(jsong, "notes");
    char ok = cJSON_IsArray(jnotes) && cJSON_GetArraySize(jnotes) > 0;
    cJSON_Delete(root);
    return ok;
}

static void PathBaseName(const char* path, char* out, size_t n) {
    const char* s1 = strrchr(path, '/');
    const char* s2 = strrchr(path, '\\');
    const char* b = (s1 > s2) ? s1 : s2;
    b = (b != NULL) ? b + 1 : path;
    strncpy(out, b, n - 1);
    out[n - 1] = 0;
    size_t L = strlen(out);
    if(L > 5 && (strcmp(out + L - 5, ".json") == 0 || strcmp(out + L - 5, ".JSON") == 0))
        out[L - 5] = 0;
}

// diff: "" ou "easy"/"normal"/"hard". resolve o caminho do chart.
static char Song_ResolveChart(const char* dirPath, const char* diff, char out[300]) {
    char p[300];

    // 1. nossa convencao
    if(diff != NULL && diff[0] != 0 && strcmp(diff, "normal") != 0) {
        snprintf(p, sizeof(p), "%s/data-%s.json", dirPath, diff);
        if(LooksLikeChart(p)) {
            strncpy(out, p, 299);
            out[299] = 0;
            return 1;
        }
    }
    snprintf(p, sizeof(p), "%s/data.json", dirPath);
    if(LooksLikeChart(p)) {
        strncpy(out, p, 299);
        out[299] = 0;
        return 1;
    }

    // 2. estilo Psych: <musica>.json / <musica>-easy.json / <musica>-hard.json
    if(!DirectoryExists(dirPath))
        return 0;
    FilePathList list = LoadDirectoryFilesEx(dirPath, ".json", false);

    char folderBase[96];
    PathBaseName(dirPath, folderBase, sizeof(folderBase));
    // tira trailing slash do base se houver
    size_t fl = strlen(folderBase);
    while(fl > 0 && (folderBase[fl - 1] == '/' || folderBase[fl - 1] == '\\'))
        folderBase[--fl] = 0;

    char want[16] = "normal";
    if(diff != NULL && diff[0] != 0)
        strncpy(want, diff, sizeof(want) - 1);

    char diffCand[300] = {0};
    char normalCand[300] = {0};
    char anyCand[300] = {0};
    for(unsigned int i = 0; i < list.count; i++) {
        const char* jp = list.paths[i];
        // ignora o nosso data.json (ja tentado) e events.json (nao e chart)
        char stem[128];
        PathBaseName(jp, stem, sizeof(stem));
        if(!LooksLikeChart(jp))
            continue;
        if(anyCand[0] == 0) {
            strncpy(anyCand, jp, sizeof(anyCand) - 1);
        }
        char isDiff = EndsWithI(stem, "-easy") || EndsWithI(stem, "-hard") || EndsWithI(stem, "-normal");
        if(strcmp(want, "normal") != 0) {
            char suf[16];
            snprintf(suf, sizeof(suf), "-%s", want);
            if(EndsWithI(stem, suf) && diffCand[0] == 0)
                strncpy(diffCand, jp, sizeof(diffCand) - 1);
        }
        if(!isDiff) {
            // nome igual ao da pasta ganha; senao vale o primeiro sem sufixo
            size_t k = 0;
            for(; folderBase[k] != 0 && stem[k] != 0; k++) {
                char a = folderBase[k], b = stem[k];
                if(a >= 'A' && a <= 'Z') a += 32;
                if(b >= 'A' && b <= 'Z') b += 32;
                if(a != b)
                    break;
            }
            if((folderBase[k] == 0 && stem[k] == 0) || normalCand[0] == 0)
                strncpy(normalCand, jp, sizeof(normalCand) - 1);
        }
    }
    UnloadDirectoryFiles(list);

    const char* pick = NULL;
    if(diffCand[0] != 0)
        pick = diffCand;
    else if(normalCand[0] != 0)
        pick = normalCand;
    else if(anyCand[0] != 0)
        pick = anyCand;
    if(pick == NULL)
        return 0;
    strncpy(out, pick, 299);
    out[299] = 0;
    return 1;
}

void Song_LoadSongFromDir(Song* song, const char* dirPath) {
    char chart[300];
    if(Song_ResolveChart(dirPath, "", chart)) {
        if(Song_ParseJSON(song, chart))
            return;
    }
    char binPath[300];
    snprintf(binPath, sizeof(binPath), "%s/data.song", dirPath);
    Song_Parse(song, binPath);
}

// diff: "easy"|"normal"|"hard" -> tenta o chart da diff, cai no padrao
void Song_LoadSongFromDirDiff(Song* song, const char* dirPath, const char* diff) {
    char chart[300];
    if(Song_ResolveChart(dirPath, diff, chart)) {
        if(Song_ParseJSON(song, chart))
            return;
    }
    char binPath[300];
    snprintf(binPath, sizeof(binPath), "%s/data.song", dirPath);
    Song_Parse(song, binPath);
}

char Song_HasChart(const char* dirPath) {
    char path[300];
    FILE* f;

    snprintf(path, sizeof(path), "%s/data.json", dirPath);
    f = fopen(path, "rb");
    if(f != NULL) {
        fclose(f);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/data.song", dirPath);
    f = fopen(path, "rb");
    if(f != NULL) {
        fclose(f);
        return 1;
    }
    {
        char chart[300];
        if(Song_ResolveChart(dirPath, "", chart))
            return 1;
    }
    return 0;
}