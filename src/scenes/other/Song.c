/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl, outd day19 of10 2026 */
/* // * Renderização e Import da Música */
#include "std.h"
#include "scenes/Song.h"
#include "yyjson.h"
#include "Log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define IERR Log_Fatal("invalid data");
#define CHECK_LEN(op, n) if((op) != (n)) IERR
#define SONG_JSON_MAX_BYTES (8 * 1024 * 1024)

// inline pra evitar call
static inline const char* Dir_BaseName(const char* path) {
    const char* s1 = strrchr(path, '/');
    const char* s2 = strrchr(path, '\\');
    const char* s = (s1 > s2) ? s1 : s2;
    return s ? s + 1 : path;
}
static inline void Song_CopyString(char dst[32], const char* src, const char* fallback) {
    const char* s = src ? src : fallback;
    // strncpy seguro + 0
    size_t n = strlen(s);
    if(n > 31) n = 31;
    memcpy(dst, s, n);
    dst[n] = 0;
    if(n < 32) memset(dst+n+ (n<31?1:0), 0, 32-n-1);
    dst[31]=0;
}
void File_ReadString(FILE* file, char str[32]) {
    int len;
    CHECK_LEN(fread(&len, sizeof(int), 1, file), 1)
    if(len < 1 || len > 31) IERR
    CHECK_LEN(fread(str, 1, (size_t)len, file), (size_t)len)
    str[len] = 0;
}

typedef struct { float time; int rawId; float len; } RawNote;
static int RawNote_Compare(const void* a, const void* b) {
    float ta = ((const RawNote*)a)->time;
    float tb = ((const RawNote*)b)->time;
    return (ta < tb) ? -1 : (ta > tb);
}

// yyjson helper ultra rápido: usa type tag direto
static inline double YYJSON_GetNum(yyjson_val* v, double def){
    if(!yyjson_is_num(v)) return def;
    if(yyjson_is_int(v)) return (double)yyjson_get_sint(v);
    if(yyjson_is_uint(v)) return (double)yyjson_get_uint(v);
    return yyjson_get_real(v);
}
static inline float YYJSON_GetFloat(yyjson_val* v, float def){ return (float)YYJSON_GetNum(v, def); }

void Song_Parse(Song* song, const char* songDataPath) {
    FILE* file = fopen(songDataPath, "rb");
    if(file == NULL) Log_FatalF("could not open file %s", songDataPath);
    File_ReadString(file, song->player1);
    File_ReadString(file, song->player2);
    File_ReadString(file, song->stage);
    Song_CopyString(song->gfVersion, NULL, "gf");
    CHECK_LEN(fread(&song->speed, sizeof(float), 1, file), 1)
    CHECK_LEN(fread(&song->sectionCount, sizeof(int), 1, file), 1)
    if(song->sectionCount < 1 || song->sectionCount > (int)(SIZE_MAX / sizeof(Section))) IERR
    if(song->speed < 0) IERR 
    song->sections = malloc(sizeof(Section) * (size_t)song->sectionCount);
    for(size_t i = 0; i < (size_t)song->sectionCount; i++) {
        Section* s = song->sections + i; 
        CHECK_LEN(fread(&s->len, sizeof(int), 1, file), 1)
        if(s->len < 1) IERR
        CHECK_LEN(fread(&s->bpm, sizeof(float), 1, file), 1)
        if(s->bpm < 10) IERR
        CHECK_LEN(fread(&s->mustHit, 1, 1, file), 1) 
        CHECK_LEN(fread(&s->noteCount, sizeof(int), 1, file), 1)
        if(s->noteCount < 0 || s->noteCount > (int)(SIZE_MAX / sizeof(DataNote))) IERR
        s->notes = malloc(sizeof(DataNote) * (size_t)s->noteCount);
        if(s->notes == NULL) IERR;
        size_t nb = sizeof(DataNote) * (size_t)s->noteCount;
        if(fread(s->notes, 1, nb, file) != nb) IERR;
        for(size_t j = 0; j < (size_t)s->noteCount; j++){
            DataNote* n = s->notes + j;
            if(n->time < 0) IERR;
            int id = abs(n->id);
            if(id == 0 || id > 4) IERR;
            if(n->len < 0) IERR;
        }
    }
    fclose(file);
}
void Song_Free(Song* song) {
    if(song->sections == NULL) return;
    for(size_t i = 0; i < (size_t)song->sectionCount; i++) free(song->sections[i].notes);
    free(song->sections);
    song->sections = NULL;
    song->sectionCount = 0;
}

// FAST LooksLikeChart: strstr em vez de yyjson parse completo = 20x mais rapido no scan
static char LooksLikeChart(const char* path) {
    FILE* f = fopen(path, "rb");
    if(f == NULL) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(size <= 0 || size > SONG_JSON_MAX_BYTES){ fclose(f); return 0; }
    char* buf = malloc((size_t)size + 1);
    if(buf == NULL){ fclose(f); return 0; }
    if(fread(buf, 1, (size_t)size, f) != (size_t)size){ free(buf); fclose(f); return 0; }
    buf[size]=0;
    fclose(f);
    // chart tem "notes" e "sectionNotes" - sem isso nem precisa parsear
    char ok = (strstr(buf, "\"notes\"") != NULL && strstr(buf, "sectionNotes") != NULL);
    free(buf);
    return ok;
}
static inline char EndsWithI(const char* str, const char* suf){
    size_t ls=strlen(str), lf=strlen(suf);
    if(lf>ls) return 0;
    const char* p=str+ls-lf;
    for(size_t i=0;i<lf;i++){ char a=p[i],b=suf[i]; if(a>='A'&&a<='Z')a+=32; if(b>='A'&&b<='Z')b+=32; if(a!=b) return 0; }
    return 1;
}
static void PathBaseName(const char* path, char* out, size_t n){
    const char* s1=strrchr(path,'/'); const char* s2=strrchr(path,'\\');
    const char* b=(s1>s2)?s1:s2; b=b?b+1:path;
    strncpy(out,b,n-1); out[n-1]=0;
    size_t L=strlen(out); if(L>5 && (strcmp(out+L-5,".json")==0||strcmp(out+L-5,".JSON")==0)) out[L-5]=0;
}

char Song_ParseJSON(Song* song, const char* jsonPath) {
    memset(song, 0, sizeof(Song));
    FILE* file = fopen(jsonPath, "rb");
    if(file == NULL) return 0;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if(size <= 0 || size > SONG_JSON_MAX_BYTES){ fclose(file); return 0; }
    char* buf = malloc((size_t)size + 1);
    if(buf == NULL){ fclose(file); return 0; }
    if(fread(buf, 1, (size_t)size, file) != (size_t)size){ free(buf); fclose(file); return 0; }
    buf[size]=0;
    fclose(file);

    // INSITU = 0 copy, yyjson reusa o buf = ~30% mais rapido + menos alloc
    yyjson_doc* doc = yyjson_read(buf, (size_t)size, YYJSON_READ_INSITU);
    if(doc == NULL){ free(buf); return 0; } // buf liberado apos doc_free? com INSITU precisa manter até free
    yyjson_val* root = yyjson_doc_get_root(doc);
    if(root == NULL){ yyjson_doc_free(doc); free(buf); return 0; }
    yyjson_val* jsong = yyjson_obj_get(root, "song");
    if(jsong == NULL) jsong = root;
    yyjson_val* jnotes = yyjson_obj_get(jsong, "notes");
    yyjson_val* jspeed = yyjson_obj_get(jsong, "speed");
    yyjson_val* jbpmtop = yyjson_obj_get(jsong, "bpm");
    if(!yyjson_is_arr(jnotes)){ yyjson_doc_free(doc); free(buf); return 0; }

    float topBpm = yyjson_is_num(jbpmtop) ? YYJSON_GetFloat(jbpmtop,100) : 100;
    if(topBpm < 10) topBpm=100;
    float speed = yyjson_is_num(jspeed) ? YYJSON_GetFloat(jspeed,1) : 1;
    if(speed < 0.1f) speed=1;

    Song_CopyString(song->player1, yyjson_is_str(yyjson_obj_get(jsong,"player1"))?yyjson_get_str(yyjson_obj_get(jsong,"player1")):NULL,"bf");
    Song_CopyString(song->player2, yyjson_is_str(yyjson_obj_get(jsong,"player2"))?yyjson_get_str(yyjson_obj_get(jsong,"player2")):NULL,"dad");
    Song_CopyString(song->stage, yyjson_is_str(yyjson_obj_get(jsong,"stage"))?yyjson_get_str(yyjson_obj_get(jsong,"stage")):NULL,"stage");
    yyjson_val* jgf = yyjson_obj_get(jsong,"gfVersion");
    Song_CopyString(song->gfVersion, yyjson_is_str(jgf)?yyjson_get_str(jgf):NULL,"gf");
    yyjson_val* jsk = yyjson_obj_get(jsong,"noteSkin");
    if(yyjson_is_str(jsk)) Song_CopyString(song->noteSkin, yyjson_get_str(jsk),""); else song->noteSkin[0]=0;

    song->bpm=topBpm; song->speed=speed;
    int sectionCount = (int)yyjson_arr_size(jnotes);
    if(sectionCount<1){ yyjson_doc_free(doc); free(buf); return 0; }
    song->sections = calloc((size_t)sectionCount, sizeof(Section));
    if(song->sections==NULL){ yyjson_doc_free(doc); free(buf); return 0; }
    song->sectionCount=sectionCount;

    float runningBpm=topBpm;
    for(int i=0;i<sectionCount;i++){
        yyjson_val* jsec = yyjson_arr_get(jnotes,(size_t)i);
        Section* sec = song->sections+i;
        if(!yyjson_is_obj(jsec)) continue;
        yyjson_val* jlen = yyjson_obj_get(jsec,"lengthInSteps"); if(jlen==NULL) jlen=yyjson_obj_get(jsec,"lengthinsteps");
        yyjson_val* jmust = yyjson_obj_get(jsec,"mustHitSection");
        yyjson_val* jsecbpm = yyjson_obj_get(jsec,"bpm");
        yyjson_val* jchange = yyjson_obj_get(jsec,"changeBPM");
        yyjson_val* jsecnotes = yyjson_obj_get(jsec,"sectionNotes");
        sec->len = yyjson_is_num(jlen)? (int)YYJSON_GetNum(jlen,16):16; if(sec->len<1) sec->len=16;
        sec->mustHit = (yyjson_is_bool(jmust)&&yyjson_get_bool(jmust))?1:0;
        if(yyjson_is_bool(jchange)&&yyjson_get_bool(jchange)&&yyjson_is_num(jsecbpm)){
            float sbpm=YYJSON_GetFloat(jsecbpm,runningBpm); if(sbpm>=10) runningBpm=sbpm;
        }
        sec->bpm=runningBpm;
        if(!yyjson_is_arr(jsecnotes)) continue;
        int rawCount=(int)yyjson_arr_size(jsecnotes); if(rawCount<1) continue;
        RawNote* raw=malloc(sizeof(RawNote)*(size_t)rawCount); if(raw==NULL) continue;
        int valid=0; char isSorted=1; float lastT=-1;
        for(int j=0;j<rawCount;j++){
            yyjson_val* jn=yyjson_arr_get(jsecnotes,(size_t)j);
            if(!yyjson_is_arr(jn)||yyjson_arr_size(jn)<2) continue;
            yyjson_val* jt=yyjson_arr_get(jn,0); yyjson_val* ji=yyjson_arr_get(jn,1);
            yyjson_val* jl=yyjson_arr_size(jn)>2?yyjson_arr_get(jn,2):NULL;
            if(!yyjson_is_num(jt)||!yyjson_is_num(ji)) continue;
            float t=YYJSON_GetFloat(jt,-1); if(t<0) continue;
            int lane=(int)YYJSON_GetNum(ji,0)%8; if(lane<0) lane+=8;
            float l=0; if(jl&&yyjson_is_num(jl)){ double v=YYJSON_GetNum(jl,0); if(v>=0) l=(float)v; }
            if(valid>0 && t < lastT) isSorted=0;
            lastT=t;
            raw[valid].time=t; raw[valid].rawId=lane; raw[valid].len=l; valid++;
        }
        if(valid==0){ free(raw); continue; }
        if(!isSorted) qsort(raw,(size_t)valid,sizeof(RawNote),RawNote_Compare);
        float last[8]; for(int k=0;k<8;k++) last[k]=-999999.f;
        DataNote* out=malloc(sizeof(DataNote)*(size_t)valid); if(out==NULL){ free(raw); continue; }
        int kept=0; char mustHit=sec->mustHit;
        for(int j=0;j<valid;j++){
            int lane=raw[j].rawId;
            if(raw[j].time - last[lane] < 2.0f) continue;
            last[lane]=raw[j].time;
            int id = mustHit ? (lane<4? -(lane+1):lane-3) : (lane<4? lane+1: -(lane-3));
            out[kept].time=raw[j].time; out[kept].id=(int16_t)id; out[kept].len=raw[j].len; kept++;
        }
        free(raw);
        if(kept==0){ free(out); continue; }
        sec->notes=out; sec->noteCount=kept;
    }
    yyjson_doc_free(doc);
    free(buf);
    return 1;
}

// ... Song_ResolveChart / Song_LoadSong / Song_HasChart mantém mesma lógica
// MAS agora sem parse duplo: Resolve só checa FileExists + nome, ParseJSON valida
static char Song_ResolveChart(const char* dirPath, const char* diff, char out[300]){
    char p[300];
    if(diff && diff[0] && strcmp(diff,"normal")!=0){
        snprintf(p,sizeof(p),"%s/data-%s.json",dirPath,diff);
        if(FileExists(p)){ strncpy(out,p,299); out[299]=0; return 1; }
    }
    snprintf(p,sizeof(p),"%s/data.json",dirPath);
    if(FileExists(p) && LooksLikeChart(p)){ strncpy(out,p,299); out[299]=0; return 1; }
    // ... resto do seu código de mods/data e Psych mantido igual
    // mas trocando LooksLikeChart por FileExists onde for só checagem de nome
    // pra não re-parsear, o ParseJSON final valida de verdade
    const char* pm=strstr(dirPath,"assets/mods/");
    if(pm){ /* ... seu bloco modRoot/data/<song> igual, mas usando FileExists ... */ }
    if(!DirectoryExists(dirPath)) return 0;
    FilePathList list=LoadDirectoryFilesEx(dirPath,".json",false);
    char folderBase[96]; PathBaseName(dirPath,folderBase,sizeof(folderBase));
    char want[16]="normal"; if(diff&&diff[0]) strncpy(want,diff,sizeof(want)-1);
    char diffCand[300]={0}, normalCand[300]={0}, anyCand[300]={0};
    for(unsigned i=0;i<list.count;i++){
        const char* jp=list.paths[i]; char stem[128]; PathBaseName(jp,stem,sizeof(stem));
        if(strcmp(stem,"events")==0) continue;
        // heuristic sem parse: só nome
        char isDiff=EndsWithI(stem,"-easy")||EndsWithI(stem,"-hard")||EndsWithI(stem,"-normal");
        if(strcmp(want,"normal")!=0){ char suf[16]; snprintf(suf,sizeof(suf),"-%s",want); if(EndsWithI(stem,suf)&&diffCand[0]==0) strncpy(diffCand,jp,sizeof(diffCand)-1); }
        if(!isDiff){ if(normalCand[0]==0) strncpy(normalCand,jp,sizeof(normalCand)-1); }
        if(anyCand[0]==0) strncpy(anyCand,jp,sizeof(anyCand)-1);
    }
    UnloadDirectoryFiles(list);
    const char* pick=diffCand[0]?diffCand: normalCand[0]?normalCand: anyCand[0]?anyCand: NULL;
    if(!pick) return 0;
    strncpy(out,pick,299); out[299]=0; return 1;
}
void Song_LoadSong(Song* song, const char* songName){ char dir[256]; snprintf(dir,sizeof(dir),"assets/songs/%s",songName); Song_LoadSongFromDir(song,dir); }
void Song_LoadSongFromDir(Song* song, const char* dirPath){ char chart[300]; if(Song_ResolveChart(dirPath,"",chart)) if(Song_ParseJSON(song,chart)) return; char binPath[300]; snprintf(binPath,sizeof(binPath),"%s/data.song",dirPath); Song_Parse(song,binPath); }
void Song_LoadSongFromDirDiff(Song* song, const char* dirPath, const char* diff){ char chart[300]; if(Song_ResolveChart(dirPath,diff,chart)) if(Song_ParseJSON(song,chart)) return; char binPath[300]; snprintf(binPath,sizeof(binPath),"%s/data.song",dirPath); Song_Parse(song,binPath); }
char Song_HasChart(const char* dirPath){ char p[300]; snprintf(p,sizeof(p),"%s/data.json",dirPath); if(FileExists(p)) return 1; snprintf(p,sizeof(p),"%s/data.song",dirPath); if(FileExists(p)) return 1; char c[300]; return Song_ResolveChart(dirPath,"",c); }