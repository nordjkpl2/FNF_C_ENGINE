#include"std.h"
#include"scenes/Week.h"
#include"scenes/SongList.h"
#include"cJSON.h"

static const char* DIFFS[3] = {"easy", "normal", "hard"};

static void StrCopy(char* dst, size_t n, const char* src, const char* fallback) {
    const char* s = (src != NULL) ? src : fallback;
    strncpy(dst, s, n - 1);
    dst[n - 1] = 0;
}

char Week_ParseFile(const char* path, const char* dir, const char* fileBase, WeekEntry* out) {
    memset(out, 0, sizeof(*out));
    StrCopy(out->file, sizeof(out->file), fileBase, "week");
    StrCopy(out->dir, sizeof(out->dir), dir, "");

    FILE* f = fopen(path, "rb");
    if(f == NULL)
        return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0 || size > 64 * 1024) {
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

    cJSON* jhide = cJSON_GetObjectItem(root, "hideStoryMode");
    if(cJSON_IsTrue(jhide)) {
        cJSON_Delete(root);
        return 0;
    }

    cJSON* jstory = cJSON_GetObjectItem(root, "storyName");
    cJSON* jweek = cJSON_GetObjectItem(root, "weekName");
    if(cJSON_IsString(jstory) && jstory->valuestring[0] != 0)
        StrCopy(out->title, sizeof(out->title), jstory->valuestring, fileBase);
    else if(cJSON_IsString(jweek) && jweek->valuestring[0] != 0)
        StrCopy(out->title, sizeof(out->title), jweek->valuestring, fileBase);
    else
        StrCopy(out->title, sizeof(out->title), fileBase, "week");

    cJSON* jsongs = cJSON_GetObjectItem(root, "songs");
    if(cJSON_IsArray(jsongs)) {
        int n = cJSON_GetArraySize(jsongs);
        for(int i = 0; i < n && out->songCount < WEEK_MAX_SONGS; i++) {
            cJSON* js = cJSON_GetArrayItem(jsongs, i);
            if(!cJSON_IsArray(js) || cJSON_GetArraySize(js) < 1)
                continue;
            cJSON* jn = cJSON_GetArrayItem(js, 0);
            cJSON* jc = cJSON_GetArraySize(js) > 1 ? cJSON_GetArrayItem(js, 1) : NULL;
            if(!cJSON_IsString(jn) || jn->valuestring[0] == 0)
                continue;
            WeekSong* w = out->songs + out->songCount;
            StrCopy(w->song, sizeof(w->song), jn->valuestring, "?");
            if(cJSON_IsString(jc))
                StrCopy(w->character, sizeof(w->character), jc->valuestring, "");
            else
                w->character[0] = 0;
            // [2] = [r,g,b]: nosso motor nao usa, ignora de proposito
            out->songCount++;
        }
    }

    cJSON_Delete(root);
    return (out->songCount > 0);
}

// ---------- lista ----------
static WeekEntry* entries = NULL;
static int entryCount = 0;
static int entryCap = 0;

static int Entry_HasFile(const char* dir, const char* file) {
    for(int i = 0; i < entryCount; i++) {
        if(strcmp(entries[i].dir, dir) == 0 && strcmp(entries[i].file, file) == 0)
            return 1;
    }
    return 0;
}

static void Scan_Add(const char* path, const char* dir, const char* fileBase, char fromMods) {
    WeekEntry e;
    if(!Week_ParseFile(path, dir, fileBase, &e))
        return;
    if(Entry_HasFile(dir, fileBase))
        return;
    if(entryCount >= entryCap) {
        int nc = (entryCap == 0) ? 8 : entryCap * 2;
        WeekEntry* g = realloc(entries, sizeof(WeekEntry) * (size_t)nc);
        if(g == NULL)
            return;
        entries = g;
        entryCap = nc;
    }
    entries[entryCount++] = e;
}

static void StripNewline(char* s) {
    size_t n = strlen(s);
    while(n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = 0;
}

static void Scan_WeeksDir(const char* base, const char* listFile, char fromMods) {
    if(!DirectoryExists(base))
        return;
    // 1. ordem do weekList.txt
    if(listFile != NULL) {
        char lp[300];
        snprintf(lp, sizeof(lp), "%s/%s", base, listFile);
        FILE* f = fopen(lp, "rb");
        if(f != NULL) {
            char line[128];
            while(fgets(line, sizeof(line), f) != NULL) {
                StripNewline(line);
                if(line[0] == 0)
                    continue;
                char name[128];
                strncpy(name, line, sizeof(name) - 1);
                name[sizeof(name) - 1] = 0;
                size_t L = strlen(name);
                if(L > 5 && strcmp(name + L - 5, ".json") == 0)
                    name[L - 5] = 0;
                char p[300];
                snprintf(p, sizeof(p), "%s/%s.json", base, name);
                Scan_Add(p, base, name, fromMods);
            }
            fclose(f);
        }
    }
    // 2. resto dos .json (ordem alfabetica pra ser estavel)
    FilePathList list = LoadDirectoryFilesEx(base, ".json", false);
    // bubble simples: poucas weeks
    for(unsigned int i = 0; i < list.count; i++) {
        for(unsigned int j = i + 1; j < list.count; j++) {
            if(strcmp(list.paths[i], list.paths[j]) > 0) {
                char* t = list.paths[i];
                list.paths[i] = list.paths[j];
                list.paths[j] = t;
            }
        }
    }
    for(unsigned int i = 0; i < list.count; i++) {
        const char* p = list.paths[i];
        const char* s1 = strrchr(p, '/');
        const char* s2 = strrchr(p, '\\');
        const char* b = (s1 > s2) ? s1 : s2;
        b = (b != NULL) ? b + 1 : p;
        char fileBase[96];
        strncpy(fileBase, b, sizeof(fileBase) - 1);
        fileBase[sizeof(fileBase) - 1] = 0;
        size_t L = strlen(fileBase);
        if(L > 5 && strcmp(fileBase + L - 5, ".json") == 0)
            fileBase[L - 5] = 0;
        Scan_Add(p, base, fileBase, fromMods);
    }
    UnloadDirectoryFiles(list);
}

int WeekList_Scan(WeekEntry** outEntries) {
    *outEntries = NULL;
    free(entries);
    entries = NULL;
    entryCount = 0;
    entryCap = 0;

    Scan_WeeksDir("assets/weeks", "weekList.txt", 0);

    if(DirectoryExists("assets/mods")) {
        FilePathList mods = LoadDirectoryFilesEx("assets/mods", "DIR", false);
        for(unsigned int i = 0; i < mods.count; i++) {
            char wd[300];
            snprintf(wd, sizeof(wd), "%s/weeks", mods.paths[i]);
            Scan_WeeksDir(wd, NULL, 1);
        }
        UnloadDirectoryFiles(mods);
    }

    *outEntries = entries;
    int n = entryCount;
    entries = NULL;
    entryCount = 0;
    entryCap = 0;
    return n;
}

void WeekList_Free(WeekEntry* list) {
    free(list);
}

// ---------- apagar ----------
char Week_Delete(const WeekEntry* e) {
    char p[300];
    snprintf(p, sizeof(p), "%s/%s.json", e->dir, e->file);
    if(remove(p) != 0)
        return 0;
    // tira do weekList.txt quando houver
    if(!e->fromMods) {
        char lp[300];
        snprintf(lp, sizeof(lp), "%s/weekList.txt", e->dir);
        FILE* f = fopen(lp, "rb");
        if(f != NULL) {
            char kept[32][128];
            int nk = 0;
            char line[128];
            while(nk < 32 && fgets(line, sizeof(line), f) != NULL) {
                StripNewline(line);
                if(line[0] == 0)
                    continue;
                char name[128];
                strncpy(name, line, sizeof(name) - 1);
                name[sizeof(name) - 1] = 0;
                size_t L = strlen(name);
                if(L > 5 && strcmp(name + L - 5, ".json") == 0)
                    name[L - 5] = 0;
                if(strcmp(name, e->file) == 0)
                    continue;
                strncpy(kept[nk], line, sizeof(kept[nk]) - 1);
                kept[nk][sizeof(kept[nk]) - 1] = 0;
                nk++;
            }
            fclose(f);
            f = fopen(lp, "wb");
            if(f != NULL) {
                for(int i = 0; i < nk; i++)
                    fprintf(f, "%s\n", kept[i]);
                fclose(f);
            }
        }
    }
    return 1;
}

// ---------- campanha ----------
static WeekEntry runWeek;
static char runActive = 0;
static int runDiff = 1;
static int runPos = 0;
static int runTotal = 0;
static int lastTotal = 0;
static char runDirs[WEEK_MAX_SONGS][256];
static char runMissing[64];

static int DirBaseMatch(const char* dirPath, const char* songName) {
    const char* s1 = strrchr(dirPath, '/');
    const char* s2 = strrchr(dirPath, '\\');
    const char* b = (s1 > s2) ? s1 : s2;
    b = (b != NULL) ? b + 1 : dirPath;
    // exato ou case-insensitive
    if(strcmp(b, songName) == 0)
        return 1;
    size_t i = 0;
    for(; b[i] != 0 && songName[i] != 0; i++) {
        char a = b[i], c = songName[i];
        if(a >= 'A' && a <= 'Z') a += 32;
        if(c >= 'A' && c <= 'Z') c += 32;
        if(a != c)
            return 0;
    }
    return (b[i] == 0 && songName[i] == 0);
}

void WeekRun_Start(const WeekEntry* e, int diff) {
    runWeek = *e;
    runDiff = (diff < 0) ? 0 : (diff > 2 ? 2 : diff);
    runPos = 0;
    runTotal = 0;
    runMissing[0] = 0;
    memset(runDirs, 0, sizeof(runDirs));

    // resolve pasta de cada musica uma vez
    SongEntry* songs = NULL;
    int n = SongList_Scan(&songs);
    int resolved = 0;
    for(int i = 0; i < e->songCount; i++) {
        for(int k = 0; k < n; k++) {
            if(DirBaseMatch(songs[k].dir, e->songs[i].song)) {
                strncpy(runDirs[i], songs[k].dir, sizeof(runDirs[i]) - 1);
                resolved++;
                break;
            }
        }
    }
    SongList_Free(songs);
    runActive = (resolved > 0);
}

char WeekRun_Active(void) {
    return runActive;
}

void WeekRun_Stop(void) {
    runActive = 0;
}

static void SkipMissing(void) {
    while(runPos < runWeek.songCount && runDirs[runPos][0] == 0) {
        strncpy(runMissing, runWeek.songs[runPos].song, sizeof(runMissing) - 1);
        runPos++;
    }
}

const char* WeekRun_Dir(void) {
    SkipMissing();
    if(runPos >= runWeek.songCount)
        return "";
    return runDirs[runPos];
}

const char* WeekRun_Char(void) {
    SkipMissing();
    if(runPos >= runWeek.songCount)
        return "";
    return runWeek.songs[runPos].character;
}

const char* WeekRun_DiffSuffix(void) {
    return DIFFS[runDiff];
}

int WeekRun_Advance(void) {
    runPos++;
    SkipMissing();
    return (runPos < runWeek.songCount);
}

void WeekRun_AddScore(int s) {
    runTotal += s;
}

int WeekRun_Total(void) {
    return runTotal;
}

int WeekRun_Pos(void) {
    return runPos;
}

int WeekRun_TotalSongs(void) {
    return runWeek.songCount;
}

int WeekRun_LastTotal(void) {
    return lastTotal;
}

const char* WeekRun_Missing(void) {
    return runMissing;
}

// registra total ao fechar campanha (chamado pelo PlayState no fim)
void WeekRun_Finish(void) {
    lastTotal = runTotal;
    runActive = 0;
}
