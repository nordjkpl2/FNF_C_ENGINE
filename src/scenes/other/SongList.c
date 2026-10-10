#include"std.h"
#include"scenes/Song.h"
#include"scenes/SongList.h"

static SongEntry* entries = NULL;
static int entryCount = 0;
static int entryCap = 0;

static int Entry_Compare(const void* a, const void* b) {
    return strcmp(((const SongEntry*)a)->name, ((const SongEntry*)b)->name);
}

static const char* Dir_BaseName(const char* path) {
    const char* s1 = strrchr(path, '/');
    const char* s2 = strrchr(path, '\\');
    const char* s = (s1 > s2) ? s1 : s2;
    return (s != NULL) ? s + 1 : path;
}

static void Scan_Add(const char* dirPath, const char* displayName) {
    char instPath[300];
    snprintf(instPath, sizeof(instPath), "%s/Inst.ogg", dirPath);

    FILE* f = fopen(instPath, "rb");
    if(f == NULL)
        return;
    fclose(f);

    if(!Song_HasChart(dirPath))
        return;

    // raylib devolve separador misto no Windows (assets/mods\songs/...) e o
    // resto do codigo casa substring "assets/mods/": normaliza p/ '/' aqui,
    // uma vez, que vale p/ songDir/chart.dir/WeekRun em todo lugar.
    char norm[256];
    size_t L = strlen(dirPath);
    if(L >= sizeof(norm))
        L = sizeof(norm) - 1;
    for(size_t i = 0; i < L; i++)
        norm[i] = (dirPath[i] == '\\') ? '/' : dirPath[i];
    norm[L] = 0;

    // dedup by dir
    for(int i = 0; i < entryCount; i++) {
        if(strcmp(entries[i].dir, norm) == 0)
            return;
    }

    if(entryCount >= entryCap) {
        int newCap = (entryCap == 0) ? 16 : entryCap * 2;
        SongEntry* grown = realloc(entries, sizeof(SongEntry) * (size_t)newCap);
        if(grown == NULL)
            return;
        entries = grown;
        entryCap = newCap;
    }

    SongEntry* e = entries + entryCount;
    strncpy(e->name, (displayName != NULL) ? displayName : Dir_BaseName(dirPath), sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = 0;
    strncpy(e->dir, norm, sizeof(e->dir) - 1);
    e->dir[sizeof(e->dir) - 1] = 0;
    e->opp[0] = 0; // lazy: freeplay resolve fora do scan
    entryCount++;
}

// Lists immediate subdirectories of base using raylib (portable).
// When isMods is set, also descends one level into <mod>/songs/* (mod pack layout).
static void Scan_Subdirs(const char* base, char isMods) {
    if(!DirectoryExists(base))
        return;

    FilePathList list = LoadDirectoryFilesEx(base, "DIR", false);
    for(unsigned int i = 0; i < list.count; i++) {
        const char* sub = list.paths[i];
        if(!isMods) {
            Scan_Add(sub, NULL);
        } else {
            const char* modName = Dir_BaseName(sub);
            if(Song_HasChart(sub)) {
                Scan_Add(sub, modName);
            } else {
                // mod pack layout: <mod>/songs/<song>
                char songsDir[300];
                snprintf(songsDir, sizeof(songsDir), "%s/songs", sub);
                if(DirectoryExists(songsDir)) {
                    FilePathList inner = LoadDirectoryFilesEx(songsDir, "DIR", false);
                    for(unsigned int j = 0; j < inner.count; j++) {
                        // mostra so a musica (estilo Psych); dir continua unico p/ match
                        Scan_Add(inner.paths[j], Dir_BaseName(inner.paths[j]));
                    }
                    UnloadDirectoryFiles(inner);
                }
            }
        }
    }
    UnloadDirectoryFiles(list);
}

int SongList_Scan(SongEntry** outEntries) {
    *outEntries = NULL;

    // free previous scan buffer (reentrancy for scene re-enter)
    free(entries);
    entries = NULL;
    entryCount = 0;
    entryCap = 0;

    Scan_Subdirs("assets/songs", 0);
    Scan_Subdirs("assets/mods", 1);

    if(entryCount > 1)
        qsort(entries, (size_t)entryCount, sizeof(SongEntry), Entry_Compare);

    *outEntries = entries;

    int n = entryCount;
    // hand ownership to caller
    entries = NULL;
    entryCount = 0;
    entryCap = 0;
    return n;
}

void SongList_Free(SongEntry* list) {
    free(list);
}

#define HEAVY_INST_BYTES (1024 * 1024)
#define HEAVY_CHART_BYTES (512 * 1024)

static long File_Size(const char* path) {
    FILE* f = fopen(path, "rb");
    if(f == NULL)
        return -1;
    fseek(f, 0, SEEK_END);
    long s = ftell(f);
    fclose(f);
    return s;
}

char SongList_IsHeavy(const char* dir) {
    char p[300];
    snprintf(p, sizeof(p), "%s/Inst.ogg", dir);
    if(File_Size(p) > HEAVY_INST_BYTES)
        return 1;
    snprintf(p, sizeof(p), "%s/data.json", dir);
    if(File_Size(p) > HEAVY_CHART_BYTES)
        return 1;
    snprintf(p, sizeof(p), "%s/data.song", dir);
    if(File_Size(p) > HEAVY_CHART_BYTES)
        return 1;
    return 0;
}

// copia player2 de um buffer json cru (procura "player2" : "nome"). 1 = achou.
static char Opp_FromJsonBuf(const char* buf, size_t n, char out[32]) {
    const char* key = "\"player2\"";
    size_t kl = 9;
    for(size_t i = 0; i + kl < n; i++) {
        size_t k = 0;
        while(k < kl && buf[i + k] == key[k]) k++;
        if(k != kl) continue;
        size_t j = i + kl;
        while(j < n && (buf[j] == ' ' || buf[j] == '\t' || buf[j] == '\r' || buf[j] == '\n')) j++;
        if(j >= n || buf[j] != ':') continue;
        j++;
        while(j < n && (buf[j] == ' ' || buf[j] == '\t' || buf[j] == '\r' || buf[j] == '\n')) j++;
        if(j >= n || buf[j] != '"') continue;
        j++;
        size_t w = 0;
        while(j < n && buf[j] != '"' && w < 31) out[w++] = buf[j++];
        if(j >= n || buf[j] != '"' || w == 0) continue;
        out[w] = 0;
        // valida como nome de char (sem path traversal); invalido = tenta o proximo
        char bad = 0;
        for(size_t t = 0; t < w; t++) {
            if(out[t] == '/' || out[t] == '\\') { bad = 1; break; }
        }
        if(!bad && strstr(out, "..") != NULL) bad = 1;
        if(bad) continue;
        return 1;
    }
    return 0;
}

static char Opp_FromJsonFile(const char* path, char out[32]) {
    FILE* f = fopen(path, "rb");
    if(f == NULL) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size < 12 || size > 8L * 1024 * 1024) { fclose(f); return 0; }
    char* buf = malloc((size_t)size);
    if(buf == NULL) { fclose(f); return 0; }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    char ok = Opp_FromJsonBuf(buf, n, out);
    free(buf);
    return ok;
}

// header do .song binario sem fatal: player1, player2, stage (len+bytes). 1 = leu player2.
static char Opp_FromSongBin(const char* path, char out[32]) {
    FILE* f = fopen(path, "rb");
    if(f == NULL) return 0;
    char tmp[3][32];
    for(int s = 0; s < 3; s++) {
        int len = 0;
        if(fread(&len, sizeof(int), 1, f) != 1) { fclose(f); return 0; }
        if(len < 1 || len > 31) { fclose(f); return 0; }
        if(fread(tmp[s], 1, (size_t)len, f) != (size_t)len) { fclose(f); return 0; }
        tmp[s][len] = 0;
    }
    fclose(f);
    if(tmp[1][0] == 0) return 0;
    strncpy(out, tmp[1], 31);
    out[31] = 0;
    return 1;
}

char SongList_Opponent(const char* dir, char out[32]) {
    strncpy(out, "dad", 32);
    out[31] = 0;
    if(dir == NULL || dir[0] == 0) return 0;
    char p[300];
    // 1. nossa convencao normal (player2 raramente muda por diff)
    snprintf(p, sizeof(p), "%s/data.json", dir);
    if(Opp_FromJsonFile(p, out)) return 1;
    // 2. Psych: <pasta>.json
    {
        const char* s1 = strrchr(dir, '/');
        const char* s2 = strrchr(dir, '\\');
        const char* b = (s1 > s2) ? s1 : s2;
        b = (b != NULL) ? b + 1 : dir;
        if(b[0] != 0) {
            snprintf(p, sizeof(p), "%s/%s.json", dir, b);
            if(Opp_FromJsonFile(p, out)) return 1;
        }
    }
    // 2b. Psych: qualquer chart do dir com player2 (ex. <musica>-insane.json).
    // Le o chart inteiro ate achar; se nao achar, cai no "dad" e resolve.
    if(DirectoryExists(dir)) {
        FilePathList list = LoadDirectoryFilesEx(dir, ".json", false);
        for(unsigned int i = 0; i < list.count; i++) {
            const char* jp = list.paths[i];
            const char* q1 = strrchr(jp, '/');
            const char* q2 = strrchr(jp, '\\');
            const char* qb = (q1 > q2) ? q1 : q2;
            qb = (qb != NULL) ? qb + 1 : jp;
            size_t ql = strlen(qb);
            if(ql == 11) {
                char low[12];
                for(int t = 0; t < 11; t++) {
                    char c = qb[t];
                    low[t] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                }
                low[11] = 0;
                if(strcmp(low, "events.json") == 0) continue;
            }
            if(Opp_FromJsonFile(jp, out)) { UnloadDirectoryFiles(list); return 1; }
        }
        UnloadDirectoryFiles(list);
    }
    // 3. binario .song
    snprintf(p, sizeof(p), "%s/data.song", dir);
    if(Opp_FromSongBin(p, out)) return 1;
    return 0;
}
