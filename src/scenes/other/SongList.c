/* // * Friday Night Funkin´ c engine (c) 2026 nordjkp, outd day19 of10 2026 */
#include "std.h"
#include "scenes/Song.h"
#include "scenes/SongList.h"
#include "yyjson.h"

// ================================================================
// [1] ESTADO
// ================================================================
static SongEntry* entries = NULL;
static int entryCount = 0;
static int entryCap = 0;

// ================================================================
// [2] HELPERS PATH - inline pra não pagar call
// ================================================================
static inline const char* Dir_BaseName(const char* path) {
    const char* s1 = strrchr(path, '/');
    const char* s2 = strrchr(path, '\\');
    const char* s = (s1 > s2) ? s1 : s2;
    return (s != NULL) ? s + 1 : path;
}

static inline void Path_Normalize(const char* src, char out[256]) {
    size_t L = strlen(src);
    if(L >= 256) L = 255;
    for(size_t i=0; i<L; i++) out[i] = (src[i] == '\\') ? '/' : src[i];
    out[L]=0;
}

static inline long File_SizeFast(const char* path) {
    FILE* f = fopen(path, "rb");
    if(!f) return -1;
    fseek(f,0,SEEK_END);
    long s = ftell(f);
    fclose(f);
    return s;
}

static int Entry_Compare(const void* a, const void* b) {
    return strcmp(((const SongEntry*)a)->name, ((const SongEntry*)b)->name);
}

// ================================================================
// [3] SCAN
// ================================================================
static void Scan_Add(const char* dirPath, const char* displayName) {
    // fast-reject: precisa de Inst.ogg
    char instPath[300];
    snprintf(instPath, sizeof(instPath), "%s/Inst.ogg", dirPath);
    FILE* f = fopen(instPath,"rb");
    if(!f) return;
    fclose(f);
    if(!Song_HasChart(dirPath)) return;

    char norm[256];
    Path_Normalize(dirPath, norm);

    for(int i=0; i<entryCount; i++) if(strcmp(entries[i].dir, norm)==0) return;

    if(entryCount >= entryCap) {
        int newCap = entryCap ? entryCap*2 : 16;
        SongEntry* grown = realloc(entries, sizeof(SongEntry)*(size_t)newCap);
        if(!grown) return;
        entries = grown;
        entryCap = newCap;
    }
    SongEntry* e = entries + entryCount++;
    const char* base = displayName ? displayName : Dir_BaseName(dirPath);
    strncpy(e->name, base, sizeof(e->name)-1); e->name[31]=0;
    strncpy(e->dir, norm, sizeof(e->dir)-1); e->dir[255]=0;
    e->opp[0]=0;
}

static void Scan_Subdirs(const char* base, char isMods) {
    if(!DirectoryExists(base)) return;
    FilePathList list = LoadDirectoryFilesEx(base, "DIR", false);
    for(unsigned int i=0; i<list.count; i++) {
        const char* sub = list.paths[i];
        if(!isMods) Scan_Add(sub, NULL);
        else {
            if(Song_HasChart(sub)) Scan_Add(sub, Dir_BaseName(sub));
            else {
                char songsDir[300]; snprintf(songsDir,sizeof(songsDir),"%s/songs",sub);
                if(!DirectoryExists(songsDir)) continue;
                FilePathList inner = LoadDirectoryFilesEx(songsDir, "DIR", false);
                for(unsigned int j=0;j<inner.count;j++) Scan_Add(inner.paths[j], Dir_BaseName(inner.paths[j]));
                UnloadDirectoryFiles(inner);
            }
        }
    }
    UnloadDirectoryFiles(list);
}

int SongList_Scan(SongEntry** outEntries) {
    free(entries); entries=NULL; entryCount=0; entryCap=0;
    Scan_Subdirs("assets/songs", 0);
    Scan_Subdirs("assets/mods", 1);
    if(entryCount > 1) qsort(entries, (size_t)entryCount, sizeof(SongEntry), Entry_Compare);
    *outEntries = entries;
    int n = entryCount;
    entries=NULL; entryCount=0; entryCap=0;
    return n;
}

void SongList_Free(SongEntry* list) { free(list); }

// ================================================================
// [4] HEAVY - 1 syscall em vez de 3
// ================================================================
char SongList_IsHeavy(const char* dir) {
    char p[300];
    snprintf(p,sizeof(p),"%s/Inst.ogg",dir);
    if(File_SizeFast(p) > 1024*1024) return 1;
    snprintf(p,sizeof(p),"%s/data.json",dir);
    if(File_SizeFast(p) > 512*1024) return 1;
    snprintf(p,sizeof(p),"%s/data.song",dir);
    if(File_SizeFast(p) > 512*1024) return 1;
    return 0;
}

// ================================================================
// [5] OPPONENT - yyjson + lê só 16KB
// ================================================================
static char Opp_FromJsonFile(const char* path, char out[32]) {
    FILE* f = fopen(path,"rb");
    if(!f) return 0;
    fseek(f,0,SEEK_END);
    long size = ftell(f); fseek(f,0,SEEK_SET);
    if(size < 12 || size > 8*1024*1024) { fclose(f); return 0; }
    // OTIMIZAÇÃO: player2 tá no header, não precisa ler 8MB
    long need = size > 16384 ? 16384 : size;
    char* buf = malloc((size_t)need+1);
    if(!buf){ fclose(f); return 0; }
    size_t n = fread(buf,1,(size_t)need,f);
    fclose(f); buf[n]=0;

    yyjson_doc* doc = yyjson_read(buf, n, 0);
    free(buf);
    if(!doc) return 0;
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* js = yyjson_obj_get(root,"song");
    if(!js) js=root;
    yyjson_val* j = yyjson_obj_get(js,"player2");
    char ok=0;
    if(yyjson_is_str(j)){
        const char* s = yyjson_get_str(j);
        if(s && s[0] && !strchr(s,'/') && !strchr(s,'\\') && !strstr(s,"..")){
            strncpy(out,s,31); out[31]=0; ok=1;
        }
    }
    yyjson_doc_free(doc);
    return ok;
}

static char Opp_FromSongBin(const char* path, char out[32]) {
    FILE* f=fopen(path,"rb"); if(!f) return 0;
    char tmp[3][32];
    for(int s=0;s<3;s++){
        int len=0; if(fread(&len,4,1,f)!=1){fclose(f);return 0;}
        if(len<1||len>31){fclose(f);return 0;}
        if(fread(tmp[s],1,(size_t)len,f)!=(size_t)len){fclose(f);return 0;}
        tmp[s][len]=0;
    }
    fclose(f); if(!tmp[1][0]) return 0;
    strncpy(out,tmp[1],31); out[31]=0; return 1;
}

char SongList_Opponent(const char* dir, char out[32]) {
    strncpy(out,"dad",32);
    if(!dir || !dir[0]) return 0;
    char p[300];
    snprintf(p,sizeof(p),"%s/data.json",dir);
    if(Opp_FromJsonFile(p,out)) return 1;

    const char* b = Dir_BaseName(dir);
    if(b[0]){
        snprintf(p,sizeof(p),"%s/%s.json",dir,b);
        if(Opp_FromJsonFile(p,out)) return 1;
    }
    // só varre diffs se não achou nos 2 principais
    if(DirectoryExists(dir)){
        FilePathList list = LoadDirectoryFilesEx(dir,".json",false);
        for(unsigned int i=0;i<list.count;i++){
            const char* qb = Dir_BaseName(list.paths[i]);
            if(qb[0]=='e' && strcmp(qb,"events.json")==0) continue; // evita ToLower
            if(Opp_FromJsonFile(list.paths[i],out)){ UnloadDirectoryFiles(list); return 1; }
        }
        UnloadDirectoryFiles(list);
    }
    snprintf(p,sizeof(p),"%s/data.song",dir);
    return Opp_FromSongBin(p,out);
}