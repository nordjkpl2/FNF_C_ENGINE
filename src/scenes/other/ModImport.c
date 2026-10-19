/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl - Oct 19 2026 */
#include "std.h"
#include "scenes/ModImport.h"
#include "yyjson.h"
#include <io.h>

// ---------- defines e tipos - UMA VEZ SÓ ----------
#define MI_MAX_XMLFRAMES 2048
#define MI_MAX_CHARANIMS 64
#define MI_MAX_ANIMFRAMES 256
typedef struct { char name[128]; int x,y,w,h,fx,fy; } MI_XmlFrame;
typedef struct { char role[16]; char sprite[64]; int idx[MI_MAX_ANIMFRAMES]; int idxCount; } MI_CharAnim;
typedef struct { char healthicon[32]; char imageBase[64]; MI_CharAnim anims[MI_MAX_CHARANIMS]; int animCount; } MI_CharInfo;
typedef struct { int x,y,w,h,fx,fy; } MI_FrameOut;

// ---------- forwards - UMA VEZ SÓ ----------
static char* MI_ReadFile(const char* path, long* outSz, long maxSz);
static long MI_FileSize(const char* path);
static char MI_XmlStr(const char* tag, const char* tagEnd, const char* attr, char* out, size_t n);
static int MI_XmlFrames(const char* xml, MI_XmlFrame* out, int cap);
static char MI_RestOk(const char* s);

// ---------- util HOT ----------
static inline const char* MI_Base(const char* p) {
    const char* s1 = strrchr(p, '/');
    const char* s2 = strrchr(p, '\\');
    const char* b = (s1 > s2) ? s1 : s2;
    return b ? b + 1 : p;
}
static inline char MI_CiEq(const char* a, const char* b) {
    for(; *a && *b; a++, b++) {
        char ca = *a, cb = *b;
        if(ca >= 'A' && ca <= 'Z') ca += 32;
        if(cb >= 'A' && cb <= 'Z') cb += 32;
        if(ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}
static inline char MI_EndsWithI(const char* s, const char* suf) {
    size_t ls = strlen(s), lf = strlen(suf);
    return lf <= ls && MI_CiEq(s + ls - lf, suf);
}
static char MI_SafeName(const char* s) {
    if(!s || !*s) return 0;
    if(s[0]=='.' && (s[1]==0 || (s[1]=='.' && s[2]==0))) return 0;
    return strchr(s,'/')==NULL && strchr(s,'\\')==NULL && strstr(s,"..")==NULL;
}
static void MI_SanitizeMod(char* dst, size_t n, const char* src) {
    size_t w=0;
    for(size_t i=0; src[i] && w+1<n; i++){
        char c=src[i];
        dst[w++] = ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-') ? c : '_';
    }
    dst[w]=0;
    if(w==0){ strncpy(dst,"mod",n-1); dst[n-1]=0; }
}
static char MI_Stem(const char* path, char* out, size_t n) {
    const char* b = MI_Base(path);
    strncpy(out, b, n-1); out[n-1]=0;
    char* dot=strrchr(out,'.');
    if(dot && dot!=out) *dot=0;
    return out[0]!=0;
}
static char* MI_ReadFile(const char* path, long* outSz, long maxSz){
    FILE* f=fopen(path,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    if(sz<=0||sz>maxSz){ fclose(f); return NULL; }
    char* buf=malloc((size_t)sz+1); if(!buf){ fclose(f); return NULL; }
    if(fread(buf,1,(size_t)sz,f)!=(size_t)sz){ free(buf); fclose(f); return NULL; }
    buf[sz]=0; fclose(f); if(outSz) *outSz=sz; return buf;
}
static char MI_CopyFast(const char* src, const char* dst, long* bytes){
    FILE* in=fopen(src,"rb"); if(!in) return 0;
    FILE* out=fopen(dst,"wb"); if(!out){ fclose(in); return 0; }
    char buf[65536]; size_t n; long tot=0;
    while((n=fread(buf,1,sizeof(buf),in))>0){
        if(fwrite(buf,1,n,out)!=n) break;
        tot+=(long)n;
    }
    fclose(in); fclose(out);
    if(bytes) *bytes+=tot;
    return tot>0 || FileExists(dst);
}
static long MI_FileSize(const char* path){
    FILE* f=fopen(path,"rb"); if(!f) return 0;
    fseek(f,0,SEEK_END); long s=ftell(f); fclose(f);
    return s>0?s:0;
}
static void MI_WalkSize(const char* path, long* bytes, int* files){
    if(DirectoryExists(path)){
        FilePathList l=LoadDirectoryFilesEx(path,NULL,false);
        for(unsigned i=0;i<l.count;i++) MI_WalkSize(l.paths[i],bytes,files);
        UnloadDirectoryFiles(l); return;
    }
    if(FileExists(path)){ *bytes+=MI_FileSize(path); (*files)++; }
}
static int MI_Wipe(const char* path){
    int r=0;
    if(DirectoryExists(path)){
        FilePathList l=LoadDirectoryFilesEx(path,NULL,false);
        for(unsigned i=0;i<l.count;i++) r+=MI_Wipe(l.paths[i]);
        UnloadDirectoryFiles(l); (void)rmdir(path); return r;
    }
    if(FileExists(path) && remove(path)==0) r++;
    return r;
}
static char MI_RootIsMods(const char* dstRoot){
    if(!dstRoot||!*dstRoot||strstr(dstRoot,"..")) return 0;
    size_t L=strlen(dstRoot);
    while(L>0&&(dstRoot[L-1]=='/'||dstRoot[L-1]=='\\')) L--;
    if(L<4) return 0;
    char tail[5]={0}; tail[0]=dstRoot[L-4]; tail[1]=dstRoot[L-3]; tail[2]=dstRoot[L-2]; tail[3]=dstRoot[L-1];
    for(int i=0;i<4;i++) if(tail[i]>='A'&&tail[i]<='Z') tail[i]+=32;
    return strcmp(tail,"mods")==0;
}
char ModImport_ScanMods(const char* dstRoot, ModImportUsage* out){
    ModImportUsage u; memset(&u,0,sizeof(u));
    if(out) *out=u;
    if(!MI_RootIsMods(dstRoot)||!DirectoryExists(dstRoot)) return 1;
    FilePathList l=LoadDirectoryFilesEx(dstRoot,NULL,false);
    for(unsigned i=0;i<l.count;i++){ if(!DirectoryExists(l.paths[i])) continue;
        u.mods++; long b=0; int f=0; MI_WalkSize(l.paths[i],&b,&f); u.bytes+=b;
        char sd[300]; snprintf(sd,sizeof(sd),"%s/songs",l.paths[i]);
        if(DirectoryExists(sd)){ FilePathList s=LoadDirectoryFilesEx(sd,NULL,false);
            for(unsigned j=0;j<s.count;j++) if(DirectoryExists(s.paths[j])) u.songs++;
            UnloadDirectoryFiles(s);
        }
    }
    UnloadDirectoryFiles(l); if(out) *out=u; return 1;
}
char ModImport_DeleteAll(const char* dstRoot, char* msg, size_t msgSize,int* removedMods, long* freedBytes){
    if(removedMods) *removedMods=0; if(freedBytes) *freedBytes=0;
    if(!MI_RootIsMods(dstRoot)){ if(msg) snprintf(msg,msgSize,"delete: raiz recusada (so *mods)"); return 0; }
    if(!DirectoryExists(dstRoot)){ if(msg) snprintf(msg,msgSize,"delete: nada em %s",dstRoot); return 1; }
    int mods=0,files=0; long freed=0;
    FilePathList l=LoadDirectoryFilesEx(dstRoot,NULL,false);
    for(unsigned i=0;i<l.count;i++){ char isDir=DirectoryExists(l.paths[i]); long b=0; int f=0; MI_WalkSize(l.paths[i],&b,&f); files+=MI_Wipe(l.paths[i]); freed+=b; if(isDir) mods++; }
    UnloadDirectoryFiles(l);
    if(removedMods) *removedMods=mods; if(freedBytes) *freedBytes=freed;
    if(msg) snprintf(msg,msgSize,"delete: %d mods, %d arquivos (%ld KB) apagados de %s",mods,files,freed/1024,dstRoot);
    return 1;
}

// ---------- Sparrow + Psych ----------
static char MI_XmlStr(const char* tag, const char* tagEnd, const char* attr, char* out, size_t n){
    size_t al=strlen(attr); const char* p=tag;
    while(p+al<tagEnd){ if(strncmp(p,attr,al)==0){ p+=al; size_t w=0; while(p<tagEnd&&*p!='"'&&w+1<n) out[w++]=*p++; out[w]=0; return w>0; } p++; }
    out[0]=0; return 0;
}
static int MI_XmlFrames(const char* xml, MI_XmlFrame* out, int cap){
    int n=0; const char* p=xml;
    while(n<cap){ p=strstr(p,"<SubTexture"); if(!p) break; const char* end=strchr(p,'>'); if(!end) break;
        MI_XmlFrame* f=out+n; memset(f,0,sizeof(*f)); char tmp[32];
        if(!MI_XmlStr(p,end," name=\"",f->name,sizeof(f->name))){ p=end+1; continue; }
        if(!MI_XmlStr(p,end," x=\"",tmp,sizeof(tmp))){ p=end+1; continue; } f->x=atoi(tmp);
        if(!MI_XmlStr(p,end," y=\"",tmp,sizeof(tmp))){ p=end+1; continue; } f->y=atoi(tmp);
        if(!MI_XmlStr(p,end," width=\"",tmp,sizeof(tmp))){ p=end+1; continue; } f->w=atoi(tmp);
        if(!MI_XmlStr(p,end," height=\"",tmp,sizeof(tmp))){ p=end+1; continue; } f->h=atoi(tmp);
        if(f->w<1||f->h<1){ p=end+1; continue; }
        if(MI_XmlStr(p,end," frameX=\"",tmp,sizeof(tmp))) f->fx=atoi(tmp);
        if(MI_XmlStr(p,end," frameY=\"",tmp,sizeof(tmp))) f->fy=atoi(tmp);
        n++; p=end+1;
    } return n;
}
static char MI_RestOk(const char* s){
    if(s[0]==0||s[0]=='.') return 1;
    for(;*s;s++) if(*s<'0'||*s>'9') return 0;
    return 1;
}
static char MI_ParseCharInfo(const char* jsonPath, const char* fallback, MI_CharInfo* out){
    memset(out,0,sizeof(*out));
    strncpy(out->healthicon,fallback,31); out->healthicon[31]=0;
    strncpy(out->imageBase,fallback,63); out->imageBase[63]=0;
    long sz=0; char* buf=MI_ReadFile(jsonPath,&sz,1024*1024); if(!buf) return 0;
    yyjson_doc* doc=yyjson_read(buf,(size_t)sz,0); free(buf); if(!doc) return 0;
    yyjson_val* root=yyjson_doc_get_root(doc);
    yyjson_val* jh=yyjson_obj_get(root,"healthicon");
    if(yyjson_is_str(jh)){ const char* s=yyjson_get_str(jh); if(s&&*s&&MI_SafeName(s)){ strncpy(out->healthicon,s,31); } }
    yyjson_val* ji=yyjson_obj_get(root,"image");
    if(yyjson_is_str(ji)){ const char* b=yyjson_get_str(ji); if(b&&*b){ const char* s1=strrchr(b,'/'); const char* s2=strrchr(b,'\\'); const char* s=(s1>s2)?s1:s2; b=s?s+1:b; if(*b&&MI_SafeName(b)){ strncpy(out->imageBase,b,63); } } }
    yyjson_val* ja=yyjson_obj_get(root,"animations");
    if(yyjson_is_arr(ja)){ size_t idx,max; yyjson_val* a; yyjson_arr_foreach(ja,idx,max,a){
        if(out->animCount>=MI_MAX_CHARANIMS) break;
        yyjson_val *jr=yyjson_obj_get(a,"anim"), *jn=yyjson_obj_get(a,"name");
        if(!yyjson_is_str(jr)||!yyjson_is_str(jn)) continue;
        const char* r=yyjson_get_str(jr); const char* n=yyjson_get_str(jn);
        if(!r||!*r||!n||!*n||strlen(n)>31) continue;
        MI_CharAnim* o=&out->anims[out->animCount];
        strncpy(o->role,r,15); o->role[15]=0; strncpy(o->sprite,n,63); o->sprite[63]=0; o->idxCount=0;
        yyjson_val* jidx=yyjson_obj_get(a,"indices");
        if(yyjson_is_arr(jidx)){ size_t k,km; yyjson_val* v; yyjson_arr_foreach(jidx,k,km,v) if(o->idxCount<MI_MAX_ANIMFRAMES&&yyjson_is_int(v)){ int val=yyjson_get_int(v); if(val>=0) o->idx[o->idxCount++]=val; } }
        out->animCount++;
    }}
    yyjson_doc_free(doc); return 1;
}
static void MI_ChartActors(const char* jsonPath, char actors[][32], int* count){
    long sz=0; char* buf=MI_ReadFile(jsonPath,&sz,8*1024*1024); if(!buf) return;
    yyjson_doc* doc=yyjson_read(buf,(size_t)sz,0); free(buf); if(!doc) return;
    yyjson_val* root=yyjson_doc_get_root(doc);
    yyjson_val* js=yyjson_obj_get(root,"song"); if(!js||!yyjson_is_obj(js)) js=root;
    const char* keys[3]={"player1","player2","stage"};
    for(int k=0;k<3;k++){ yyjson_val* j=yyjson_obj_get(js,keys[k]); if(!yyjson_is_str(j)) continue;
        const char* val=yyjson_get_str(j); if(!val||!*val||!MI_SafeName(val)) continue;
        char dup=0; for(int i=0;i<*count;i++) if(MI_CiEq(actors[i],val)){ dup=1; break; }
        if(!dup&&*count<64){ strncpy(actors[*count],val,31); actors[*count][31]=0; (*count)++; }
    }
    yyjson_doc_free(doc);
}
static char MI_IconFor(const char* srcMod, const char* modIcons, const char* key, const char* name, ModImportStats* st){
    char cand[4][300];
    snprintf(cand[0],sizeof(cand[0]),"%s/images/icons/icon-%s.png",srcMod,key);
    snprintf(cand[1],sizeof(cand[1]),"%s/images/icons/icon-%s.png",srcMod,name);
    snprintf(cand[2],sizeof(cand[2]),"%s/images/icons/%s.png",srcMod,key);
    snprintf(cand[3],sizeof(cand[3]),"%s/images/icons/%s.png",srcMod,name);
    char dst[300]; snprintf(dst,sizeof(dst),"%s/%s.png",modIcons,name);
    for(int i=0;i<4;i++) if(MI_CopyFast(cand[i],dst,&st->bytes)){ st->icons++; return 1; }
    return 0;
}
static int MI_WriteAnimsetCached(const MI_CharInfo* info, const char* xmlPath, const char* animsetPath){
    long xsize=0; char* xbuf=MI_ReadFile(xmlPath,&xsize,4*1024*1024); if(!xbuf) return 0;
    MI_XmlFrame* frames=malloc(sizeof(MI_XmlFrame)*MI_MAX_XMLFRAMES);
    if(!frames){ free(xbuf); return 0; }
    int nf=MI_XmlFrames(xbuf,frames,MI_MAX_XMLFRAMES); free(xbuf);
    if(nf<1||info->animCount<1){ free(frames); return 0; }
    static short picked[MI_MAX_CHARANIMS][MI_MAX_ANIMFRAMES]; static short pickCount[MI_MAX_CHARANIMS];
    int kept=0;
    for(int a=0;a<info->animCount;a++){ pickCount[a]=0; size_t pl=strlen(info->anims[a].sprite); short all[MI_MAX_ANIMFRAMES]; int nall=0;
        for(int i=0;i<nf&&nall<MI_MAX_ANIMFRAMES;i++){ if(strncmp(frames[i].name,info->anims[a].sprite,pl)!=0) continue; if(!MI_RestOk(frames[i].name+pl)) continue; all[nall++]=(short)i; }
        if(nall==0) continue;
        if(info->anims[a].idxCount>0){ for(int k=0;k<info->anims[a].idxCount&&pickCount[a]<MI_MAX_ANIMFRAMES;k++){ int w=info->anims[a].idx[k]; if(w>=0&&w<nall) picked[a][pickCount[a]++]=all[w]; }
        } else { for(int k=0;k<nall;k++) picked[a][pickCount[a]++]=all[k]; }
        if(pickCount[a]>0) kept++;
    }
    if(kept==0){ free(frames); return 0; }
    FILE* o=fopen(animsetPath,"wb"); if(!o){ free(frames); return 0; }
    int written=0; fwrite(&kept,sizeof(int),1,o);
    for(int a=0;a<info->animCount;a++){ if(pickCount[a]==0) continue;
        int nl=(int)strlen(info->anims[a].sprite); int dc=(int)pickCount[a]*6;
        fwrite(&nl,sizeof(int),1,o); fwrite(info->anims[a].sprite,1,(size_t)nl,o); fwrite(&dc,sizeof(int),1,o);
        for(int k=0;k<pickCount[a];k++){ MI_XmlFrame* fr=frames+picked[a][k]; MI_FrameOut fo={fr->x,fr->y,fr->w,fr->h,fr->fx,fr->fy}; fwrite(&fo,sizeof(fo),1,o); }
        written++;
    }
    fclose(o); free(frames); if(written==0) remove(animsetPath); return written;
}

char ModImport_Run(const char* srcMod, const char* dstRoot, const char* modName, char* msg, size_t msgSize, ModImportStats* stats){
    ModImportStats st; memset(&st,0,sizeof(st));
    if(msg&&msgSize) msg[0]=0; if(stats) memset(stats,0,sizeof(*stats));
    if(!srcMod||!dstRoot||!modName){ if(msg) snprintf(msg,msgSize,"import: caminho invalido"); return 0; }
    char mod[64]; MI_SanitizeMod(mod,sizeof(mod),modName);
    char dst[300]; snprintf(dst,sizeof(dst),"%s/%s",dstRoot,mod);
    char srcSongs[300],srcData[300]; snprintf(srcSongs,sizeof(srcSongs),"%s/songs",srcMod); snprintf(srcData,sizeof(srcData),"%s/data",srcMod);
    if(!DirectoryExists(srcSongs)){ if(msg) snprintf(msg,msgSize,"import: sem songs/ em %s",srcMod); return 0; }
    { char d[300]; snprintf(d,sizeof(d),"%s/songs",dst); if(MakeDirectory(d)!=0){ if(msg) snprintf(msg,msgSize,"import: nao criou %s",d); return 0; }
      snprintf(d,sizeof(d),"%s/weeks",dst); MakeDirectory(d); snprintf(d,sizeof(d),"%s/stages",dst); MakeDirectory(d);
      snprintf(d,sizeof(d),"%s/images/characters",dst); MakeDirectory(d); snprintf(d,sizeof(d),"%s/characters",dst); MakeDirectory(d);
      snprintf(d,sizeof(d),"%s/images/icons",dst); MakeDirectory(d);
    }
    char iconNames[64][32]; int iconNameCount=0;
    FilePathList songs=LoadDirectoryFilesEx(srcSongs,"DIR",false);
    for(unsigned i=0;i<songs.count;i++){
        const char* songBase=MI_Base(songs.paths[i]); if(!MI_SafeName(songBase)) continue;
        char dstSong[300]; snprintf(dstSong,sizeof(dstSong),"%s/songs/%s",dst,songBase);
        const char* instExts[3]={"Inst.ogg","Inst.mp3","Inst.wav"};
        char instSrc[300]={0}, dstInst[300]={0}; char hasInst=0;
        for(int e=0;e<3;e++){ snprintf(instSrc,sizeof(instSrc),"%s/%s",songs.paths[i],instExts[e]); if(FileExists(instSrc)){ hasInst=1; break; } }
        if(!hasInst) continue;
        MakeDirectory(dstSong);
        snprintf(dstInst,sizeof(dstInst),"%s/%s",dstSong,MI_Base(instSrc));
        if(!MI_CopyFast(instSrc,dstInst,&st.bytes)) continue;
        const char* vox[4]={"Voices.ogg","Voices.mp3","Voices-Player.ogg","Voices-Opponent.ogg"};
        for(int v=0;v<4;v++){ char sv[300],dv[300]; snprintf(sv,sizeof(sv),"%s/%s",songs.paths[i],vox[v]); snprintf(dv,sizeof(dv),"%s/%s",dstSong,vox[v]); MI_CopyFast(sv,dv,&st.bytes); }
        char dataDir[300]={0};
        FilePathList datas=LoadDirectoryFilesEx(srcData,"DIR",false);
        for(unsigned d=0;d<datas.count;d++) if(MI_CiEq(MI_Base(datas.paths[d]),songBase)){ strncpy(dataDir,datas.paths[d],sizeof(dataDir)-1); break; }
        UnloadDirectoryFiles(datas);
        int chartsHere=0;
        const char* chartDirs[3]={dataDir, songs.paths[i], NULL};
        char chartsSub[300]; snprintf(chartsSub,sizeof(chartsSub),"%s/charts",songs.paths[i]);
        if(DirectoryExists(chartsSub)) chartDirs[2]=chartsSub;
        for(int cd=0; cd<3; cd++){
            if(!chartDirs[cd]||!chartDirs[cd][0]||!DirectoryExists(chartDirs[cd])) continue;
            FilePathList charts=LoadDirectoryFilesEx(chartDirs[cd],".json",false);
            for(unsigned c=0;c<charts.count;c++){
                char stem[128]; MI_Stem(charts.paths[c],stem,sizeof(stem));
                if(MI_CiEq(stem,"events")){ st.skippedEvents++; continue; }
                const char* cb=MI_Base(charts.paths[c]); if(!MI_SafeName(cb)) continue;
                char dc[300]; snprintf(dc,sizeof(dc),"%s/%s",dstSong,cb);
                if(FileExists(dc)) continue;
                if(MI_CopyFast(charts.paths[c],dc,&st.bytes)){ chartsHere++; st.charts++; MI_ChartActors(dc,iconNames,&iconNameCount); }
            }
            UnloadDirectoryFiles(charts);
        }
        if(dataDir[0]!=0){ FilePathList luas=LoadDirectoryFilesEx(dataDir,".lua",false); st.skippedLua+=(int)luas.count; UnloadDirectoryFiles(luas); }
        if(chartsHere>0) st.songs++;
    }
    UnloadDirectoryFiles(songs);
    if(st.songs==0){ if(msg) snprintf(msg,msgSize,"import: nenhuma musica com Inst.*+chart em %s",srcMod); return 0; }
    { char seen[32][64]; int seenCount=0; const char* subs[4]={"weeks/main","weeks","weeks/extra","weeks/joke"};
      for(int s=0;s<4;s++){ char wd[300]; snprintf(wd,sizeof(wd),"%s/%s",srcMod,subs[s]); if(!DirectoryExists(wd)) continue;
        FilePathList wj=LoadDirectoryFilesEx(wd,".json",false);
        for(unsigned j=0;j<wj.count;j++){ char stem[128]; MI_Stem(wj.paths[j],stem,sizeof(stem)); if(!MI_SafeName(stem)) continue;
          char dup=0; for(int k=0;k<seenCount;k++) if(MI_CiEq(seen[k],stem)){ dup=1; break; } if(dup||seenCount>=32) continue;
          strncpy(seen[seenCount],stem,63); seen[seenCount][63]=0; seenCount++;
          char dw[300]; snprintf(dw,sizeof(dw),"%s/weeks/%s",dst,MI_Base(wj.paths[j])); if(MI_CopyFast(wj.paths[j],dw,&st.bytes)) st.weeks++;
        } UnloadDirectoryFiles(wj);
      }
    }
    { char sd[300]; snprintf(sd,sizeof(sd),"%s/stages",srcMod);
      if(DirectoryExists(sd)){
        FilePathList sl=LoadDirectoryFilesEx(sd,".lua",false);
        for(unsigned j=0;j<sl.count;j++){ char stem[128]; MI_Stem(sl.paths[j],stem,sizeof(stem)); if(!MI_SafeName(stem)) continue;
          char dl[300]; snprintf(dl,sizeof(dl),"%s/stages/%s",dst,MI_Base(sl.paths[j])); if(MI_CopyFast(sl.paths[j],dl,&st.bytes)) st.stages++;
          char sj[300],dj[300]; snprintf(sj,sizeof(sj),"%s/%s.json",sd,stem); if(FileExists(sj)){ snprintf(dj,sizeof(dj),"%s/stages/%s.json",dst,stem); MI_CopyFast(sj,dj,&st.bytes); }
        } UnloadDirectoryFiles(sl);
        FilePathList sj2=LoadDirectoryFilesEx(sd,".json",false);
        for(unsigned j=0;j<sj2.count;j++){ char stem[128]; MI_Stem(sj2.paths[j],stem,sizeof(stem)); if(!MI_SafeName(stem)) continue;
          char sl2[300]; snprintf(sl2,sizeof(sl2),"%s/%s.lua",sd,stem); if(FileExists(sl2)) continue;
          char dj[300]; snprintf(dj,sizeof(dj),"%s/stages/%s",dst,MI_Base(sj2.paths[j])); if(MI_CopyFast(sj2.paths[j],dj,&st.bytes)) st.stages++;
        } UnloadDirectoryFiles(sj2);
      }
    }
    { char id[300]; snprintf(id,sizeof(id),"%s/images",srcMod);
      if(DirectoryExists(id)){
        FilePathList all=LoadDirectoryFilesEx(id,NULL,false);
        for(unsigned j=0;j<all.count;j++){ if(DirectoryExists(all.paths[j])) continue; const char* b=MI_Base(all.paths[j]); if(!MI_SafeName(b)||!MI_EndsWithI(b,".png")) continue;
          char di[300]; snprintf(di,sizeof(di),"%s/images/%s",dst,b); if(MI_CopyFast(all.paths[j],di,&st.bytes)) st.images++; }
        UnloadDirectoryFiles(all);
        char cd[300]; snprintf(cd,sizeof(cd),"%s/images/characters",srcMod);
        if(DirectoryExists(cd)){ FilePathList ch=LoadDirectoryFilesEx(cd,NULL,false);
          for(unsigned j=0;j<ch.count;j++){ if(DirectoryExists(ch.paths[j])) continue; const char* b=MI_Base(ch.paths[j]); if(!MI_SafeName(b)||(!MI_EndsWithI(b,".png")&&!MI_EndsWithI(b,".xml"))) continue;
            char di[300]; snprintf(di,sizeof(di),"%s/images/characters/%s",dst,b); if(MI_CopyFast(ch.paths[j],di,&st.bytes)) st.images++; }
          UnloadDirectoryFiles(ch);
        }
      }
    }
    { char modIcons[300],modChars[300],srcChars[300]; snprintf(modIcons,sizeof(modIcons),"%s/images/icons",dst); snprintf(modChars,sizeof(modChars),"%s/images/characters",dst); snprintf(srcChars,sizeof(srcChars),"%s/images/characters",srcMod);
      char cd[300]; snprintf(cd,sizeof(cd),"%s/characters",srcMod);
      if(DirectoryExists(cd)){ FilePathList ch=LoadDirectoryFilesEx(cd,".json",false);
        for(unsigned j=0;j<ch.count;j++){ char stem[128]; MI_Stem(ch.paths[j],stem,sizeof(stem)); if(!MI_SafeName(stem)) continue;
          char dc[300]; snprintf(dc,sizeof(dc),"%s/characters/%s",dst,MI_Base(ch.paths[j])); if(!MI_CopyFast(ch.paths[j],dc,&st.bytes)) continue;
          st.characters++;
          char dup=0; for(int k=0;k<iconNameCount;k++) if(MI_CiEq(iconNames[k],stem)){ dup=1; break; }
          if(!dup&&iconNameCount<64){ strncpy(iconNames[iconNameCount],stem,31); iconNames[iconNameCount][31]=0; iconNameCount++; }
          MI_CharInfo info; if(MI_ParseCharInfo(dc,stem,&info)){
            char sx[300],sa[300]; snprintf(sx,sizeof(sx),"%s/%s.xml",srcChars,info.imageBase); if(!FileExists(sx)) snprintf(sx,sizeof(sx),"%s/%s.xml",srcMod,info.imageBase);
            snprintf(sa,sizeof(sa),"%s/%s.animset",modChars,stem);
            if(MI_WriteAnimsetCached(&info,sx,sa)>0){ st.animsets++; st.bytes+=MI_FileSize(sa); }
          }
        } UnloadDirectoryFiles(ch);
      }
      for(int k=0;k<iconNameCount;k++){ char hpath[300]; snprintf(hpath,sizeof(hpath),"%s/characters/%s.json",dst,iconNames[k]);
        MI_CharInfo tmp; char h[32]; strncpy(h,iconNames[k],31); h[31]=0;
        if(MI_ParseCharInfo(hpath,iconNames[k],&tmp)) strncpy(h,tmp.healthicon,31);
        MI_IconFor(srcMod,modIcons,h,iconNames[k],&st);
      }
    }
    if(stats) *stats=st;
    if(msg) snprintf(msg,msgSize,"import %s: %d musicas, %d charts, %d weeks, %d stages, %d imgs, %d chars, %d animsets, %d icones (%ld KB)",mod,st.songs,st.charts,st.weeks,st.stages,st.images,st.characters,st.animsets,st.icons,st.bytes/1024);
    return 1;
}