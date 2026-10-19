/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl - Oct 19 2026 */
#include "std.h"
#include "scenes/Week.h"
#include "scenes/SongList.h"
#include "yyjson.h"

static const char* DIFFS[3] = {"easy","normal","hard"};

// ---------- util HOT ----------
static inline void StrCopy(char* dst, size_t n, const char* src, const char* fallback){
    const char* s = src ? src : fallback;
    if(!s) s = "";
    strncpy(dst, s, n-1);
    dst[n-1]=0;
}
static inline const char* WK_Base(const char* p){
    const char* s1=strrchr(p,'/'); const char* s2=strrchr(p,'\\');
    const char* b=(s1>s2)?s1:s2; return b?b+1:p;
}
static inline char WK_CiEq(const char* a, const char* b){
    for(;*a&&*b;a++,b++){
        char ca=*a, cb=*b;
        if(ca>='A'&&ca<='Z') ca+=32;
        if(cb>='A'&&cb<='Z') cb+=32;
        if(ca!=cb) return 0;
    }
    return *a==0 && *b==0;
}
static inline void StripNewline(char* s){
    size_t n=strlen(s);
    while(n>0 && (s[n-1]=='\n' || s[n-1]=='\r')) s[--n]=0;
}
static char* WK_ReadFile(const char* path, long* outSz, long maxSz){
    FILE* f=fopen(path,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    if(sz<=0||sz>maxSz){ fclose(f); return NULL; }
    char* buf=malloc((size_t)sz+1); if(!buf){ fclose(f); return NULL; }
    if(fread(buf,1,(size_t)sz,f)!=(size_t)sz){ free(buf); fclose(f); return NULL; }
    buf[sz]=0; fclose(f); if(outSz) *outSz=sz; return buf;
}

char Week_ParseFile(const char* path, const char* dir, const char* fileBase, WeekEntry* out){
    memset(out,0,sizeof(*out));
    StrCopy(out->file,sizeof(out->file),fileBase,"week");
    StrCopy(out->dir,sizeof(out->dir),dir,"");

    long sz=0; char* buf=WK_ReadFile(path,&sz,64*1024); if(!buf) return 0;
    yyjson_doc* doc=yyjson_read(buf,(size_t)sz,0); free(buf); if(!doc) return 0;
    yyjson_val* root=yyjson_doc_get_root(doc); if(!root){ yyjson_doc_free(doc); return 0; }

    yyjson_val* jhide=yyjson_obj_get(root,"hideStoryMode");
    if(yyjson_is_bool(jhide) && yyjson_get_bool(jhide)){ yyjson_doc_free(doc); return 0; }

    yyjson_val* jstory=yyjson_obj_get(root,"storyName");
    yyjson_val* jweek=yyjson_obj_get(root,"weekName");
    if(yyjson_is_str(jstory) && yyjson_get_str(jstory)[0]) StrCopy(out->title,sizeof(out->title),yyjson_get_str(jstory),fileBase);
    else if(yyjson_is_str(jweek) && yyjson_get_str(jweek)[0]) StrCopy(out->title,sizeof(out->title),yyjson_get_str(jweek),fileBase);
    else StrCopy(out->title,sizeof(out->title),fileBase,"week");

    yyjson_val* jflavor=yyjson_obj_get(root,"flavor");
    if(!yyjson_is_str(jflavor)) jflavor=yyjson_obj_get(root,"description");
    if(yyjson_is_str(jflavor) && yyjson_get_str(jflavor)[0]) StrCopy(out->flavor,sizeof(out->flavor),yyjson_get_str(jflavor),"");
    else out->flavor[0]=0;

    yyjson_val* jsongs=yyjson_obj_get(root,"songs");
    if(yyjson_is_arr(jsongs)){
        size_t n=yyjson_arr_size(jsongs);
        for(size_t i=0;i<n && out->songCount<WEEK_MAX_SONGS;i++){
            yyjson_val* js=yyjson_arr_get(jsongs,i);
            if(!yyjson_is_arr(js) || yyjson_arr_size(js)<1) continue;
            yyjson_val* jn=yyjson_arr_get(js,0);
            yyjson_val* jc=yyjson_arr_size(js)>1 ? yyjson_arr_get(js,1) : NULL;
            if(!yyjson_is_str(jn) || !yyjson_get_str(jn)[0]) continue;
            WeekSong* w=out->songs+out->songCount;
            StrCopy(w->song,sizeof(w->song),yyjson_get_str(jn),"?");
            if(yyjson_is_str(jc)) StrCopy(w->character,sizeof(w->character),yyjson_get_str(jc),"");
            else w->character[0]=0;
            out->songCount++;
        }
    }
    yyjson_doc_free(doc);
    return out->songCount>0;
}

// ---------- lista ----------
static WeekEntry* entries=NULL;
static int entryCount=0, entryCap=0;

static inline int Entry_HasFile(const char* dir, const char* file){
    for(int i=0;i<entryCount;i++) if(strcmp(entries[i].dir,dir)==0 && strcmp(entries[i].file,file)==0) return 1;
    return 0;
}
static void Scan_Add(const char* path, const char* dir, const char* fileBase, char fromMods){
    (void)fromMods;
    WeekEntry e;
    if(!Week_ParseFile(path,dir,fileBase,&e)) return;
    if(Entry_HasFile(dir,fileBase)) return;
    if(entryCount>=entryCap){
        int nc=(entryCap==0)?8:entryCap*2;
        WeekEntry* g=realloc(entries,sizeof(WeekEntry)*(size_t)nc);
        if(!g) return;
        entries=g; entryCap=nc;
    }
    entries[entryCount++]=e;
}
static int WK_CmpPath(const void* a, const void* b){
    const char* const* pa=(const char* const*)a;
    const char* const* pb=(const char* const*)b;
    return strcmp(*pa,*pb);
}
static void Scan_WeeksDir(const char* base, const char* listFile, char fromMods){
    if(!DirectoryExists(base)) return;
    if(listFile){
        char lp[300]; snprintf(lp,sizeof(lp),"%s/%s",base,listFile);
        FILE* f=fopen(lp,"rb");
        if(f){
            char line[128];
            while(fgets(line,sizeof(line),f)){
                StripNewline(line); if(!line[0]) continue;
                char name[128]; strncpy(name,line,sizeof(name)-1); name[sizeof(name)-1]=0;
                size_t L=strlen(name); if(L>5 && strcmp(name+L-5,".json")==0) name[L-5]=0;
                char p[300]; snprintf(p,sizeof(p),"%s/%s.json",base,name);
                Scan_Add(p,base,name,fromMods);
            }
            fclose(f);
        }
    }
    FilePathList list=LoadDirectoryFilesEx(base,".json",false);
    if(list.count>1) qsort(list.paths,list.count,sizeof(char*),WK_CmpPath); // O(n log n) vs bubble O(n²)
    for(unsigned i=0;i<list.count;i++){
        const char* b=WK_Base(list.paths[i]);
        char fileBase[96]; strncpy(fileBase,b,sizeof(fileBase)-1); fileBase[sizeof(fileBase)-1]=0;
        size_t L=strlen(fileBase); if(L>5 && strcmp(fileBase+L-5,".json")==0) fileBase[L-5]=0;
        Scan_Add(list.paths[i],base,fileBase,fromMods);
    }
    UnloadDirectoryFiles(list);
}
int WeekList_Scan(WeekEntry** outEntries){
    *outEntries=NULL; free(entries); entries=NULL; entryCount=0; entryCap=0;
    Scan_WeeksDir("assets/weeks","weekList.txt",0);
    if(DirectoryExists("assets/mods")){
        FilePathList mods=LoadDirectoryFilesEx("assets/mods","DIR",false);
        for(unsigned i=0;i<mods.count;i++){ char wd[300]; snprintf(wd,sizeof(wd),"%s/weeks",mods.paths[i]); Scan_WeeksDir(wd,NULL,1); }
        UnloadDirectoryFiles(mods);
    }
    *outEntries=entries; int n=entryCount; entries=NULL; entryCount=0; entryCap=0; return n;
}
void WeekList_Free(WeekEntry* list){ free(list); }
char Week_Delete(const WeekEntry* e){
    char p[300]; snprintf(p,sizeof(p),"%s/%s.json",e->dir,e->file);
    if(remove(p)!=0) return 0;
    if(!e->fromMods){
        char lp[300]; snprintf(lp,sizeof(lp),"%s/weekList.txt",e->dir);
        FILE* f=fopen(lp,"rb");
        if(f){
            char kept[64][128]; int nk=0; char line[128];
            while(nk<64 && fgets(line,sizeof(line),f)){
                StripNewline(line); if(!line[0]) continue;
                char name[128]; strncpy(name,line,sizeof(name)-1); name[sizeof(name)-1]=0;
                size_t L=strlen(name); if(L>5 && strcmp(name+L-5,".json")==0) name[L-5]=0;
                if(strcmp(name,e->file)==0) continue;
                strncpy(kept[nk],line,sizeof(kept[nk])-1); kept[nk][sizeof(kept[nk])-1]=0; nk++;
            }
            fclose(f); f=fopen(lp,"wb");
            if(f){ for(int i=0;i<nk;i++) fprintf(f,"%s\n",kept[i]); fclose(f); }
        }
    }
    return 1;
}

// ---------- campanha ----------
static WeekEntry runWeek;
static char runActive=0;
static int runDiff=1, runPos=0, runTotal=0, lastTotal=0;
static char runDirs[WEEK_MAX_SONGS][256];
static char runMissing[64];

static inline int DirBaseMatch(const char* dirPath, const char* songName){
    const char* b=WK_Base(dirPath);
    return WK_CiEq(b,songName);
}
void WeekRun_Start(const WeekEntry* e, int diff){
    runWeek=*e; runDiff=(diff<0)?0:(diff>2?2:diff); runPos=0; runTotal=0; runMissing[0]=0; memset(runDirs,0,sizeof(runDirs));
    SongEntry* songs=NULL; int n=SongList_Scan(&songs);
    int resolved=0;
    for(int i=0;i<e->songCount;i++){
        for(int k=0;k<n;k++) if(DirBaseMatch(songs[k].dir,e->songs[i].song)){ strncpy(runDirs[i],songs[k].dir,sizeof(runDirs[i])-1); resolved++; break; }
    }
    SongList_Free(songs); runActive=(resolved>0);
}
char WeekRun_Active(void){ return runActive; }
void WeekRun_Stop(void){ runActive=0; }
static inline void SkipMissing(void){
    while(runPos<runWeek.songCount && runDirs[runPos][0]==0){ strncpy(runMissing,runWeek.songs[runPos].song,sizeof(runMissing)-1); runMissing[sizeof(runMissing)-1]=0; runPos++; }
}
const char* WeekRun_Dir(void){ SkipMissing(); return runPos<runWeek.songCount?runDirs[runPos]:""; }
const char* WeekRun_Char(void){ SkipMissing(); return runPos<runWeek.songCount?runWeek.songs[runPos].character:""; }
const char* WeekRun_DiffSuffix(void){ return DIFFS[runDiff]; }
int WeekRun_Advance(void){ runPos++; SkipMissing(); return runPos<runWeek.songCount; }
void WeekRun_AddScore(int s){ runTotal+=s; }
int WeekRun_Total(void){ return runTotal; }
int WeekRun_Pos(void){ return runPos; }
int WeekRun_TotalSongs(void){ return runWeek.songCount; }
int WeekRun_LastTotal(void){ return lastTotal; }
const char* WeekRun_Missing(void){ return runMissing; }
void WeekRun_Finish(void){ lastTotal=runTotal; runActive=0; }