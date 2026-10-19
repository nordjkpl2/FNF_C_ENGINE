/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl - OPTIMIZED, outd day19 of10 2026 */
#include "std.h"
#include "scenes/Stage.h"
#include <ctype.h>

#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

// ModRoot e FindLua - I/O bound, otimizado pra não alocar FILE*
static inline const char* ModRoot(const char* songDir, char out[256]) {
    const char* pm = strstr(songDir, "assets/mods/");
    if (UNLIKELY(!pm)) { out[0]=0; return out; }
    const char* after = pm + 13; // strlen("assets/mods/")
    const char* slash = strchr(after, '/');
    if (!slash) {
        snprintf(out, 256, "%s", songDir);
        return out;
    }
    const char* songs = strstr(slash, "/songs/");
    size_t n = (size_t)((songs ? songs : slash) - songDir);
    if (n >= 256) n = 255;
    memcpy(out, songDir, n);
    out[n]=0;
    return out;
}

char Stage_FindLua(const char* stageName, const char* songDir, char out[300]) {
    if (!stageName || !stageName[0] || strcmp(stageName,"stage")==0) return 0;
    if (strchr(stageName,'/') || strchr(stageName,'\\') || strstr(stageName,"..")) return 0;
    
    char modRoot[256]={0};
    ModRoot(songDir, modRoot);

    char b0[300],b1[300],b2[300],b3[300];
    const char* cands[4];
    int n=0;
    if(modRoot[0]){ snprintf(b0,sizeof(b0),"%s/stages/%s.lua",modRoot,stageName); cands[n++]=b0; }
    snprintf(b1,sizeof(b1),"assets/stages/%s.lua",stageName); cands[n++]=b1;
    snprintf(b2,sizeof(b2),"assets/data/stages/%s.lua",stageName); cands[n++]=b2;
    snprintf(b3,sizeof(b3),"%s/%s.lua",songDir,stageName); cands[n++]=b3;

    for(int i=0;i<n;i++){
        FILE* f = fopen(cands[i],"rb");
        if(f){ fclose(f); snprintf(out,300,"%s",cands[i]); return 1; }
    }
    return 0;
}

// --- PARSER HOT PATH ---

typedef struct { char tag[64]; int idx; } TagMap;

static inline int FindTag(TagMap* m, int c, const char* tag){
    for(int i=0;i<c;i++) if(m[i].tag[0]==tag[0] && strcmp(m[i].tag,tag)==0) return i;
    return -1;
}

static inline int parseQuoted(const char** p, char* out, size_t n){
    const char* s=*p;
    while(*s && *s!= '\'' && *s!='"') s++;
    if(UNLIKELY(!*s)) return 0;
    char q=*s++;
    size_t w=0;
    while(*s && *s!=q && w+1<n) out[w++]=*s++;
    out[w]=0;
    if(*s==q) s++;
    *p=s;
    return w>0;
}

static inline int parseFloat(const char** p, float* out){
    const char* s=*p;
    while(*s && (*s==',' || isspace((unsigned char)*s))) s++;
    char* end;
    float v=strtof(s,&end);
    if(end==s) return 0;
    *out=v; *p=end;
    return 1;
}

// retorna ponteiro pro inicio dos args e fim, sem copiar
static inline int GetArgsBounds(const char* p, const char** outStart, const char** outEnd){
    const char* a=strchr(p,'(');
    if(!a) return 0;
    a++;
    const char* s=a;
    int depth=1, inS=0, inD=0;
    while(*s && depth>0){
        char c=*s;
        if(!inD && c=='\'') inS=!inS;
        else if(!inS && c=='"') inD=!inD;
        if(!inS && !inD){
            if(c=='(') depth++;
            else if(c==')'){ depth--; if(depth==0) break; }
        }
        s++;
    }
    *outStart=a; *outEnd=s;
    return 1;
}

static inline int startsWith(const char* p, const char* lit, size_t len){
    return memcmp(p, lit, len)==0;
}

int Stage_ParseLua(const char* luaText, StageSprite* out, int cap, int* ignoredGlitch, int* ignoredAnim) {
    TagMap maps[STAGE_MAX_SPRITES];
    int mapCount=0, count=0, glitch=0, anim=0;
    char isGraphic[STAGE_MAX_SPRITES]={0};
    const char* p=luaText;

    while(LIKELY(*p)){
        // 1. comentario -- e --[[ ]]
        if(p[0]=='-' && p[1]=='-'){
            if(p[2]=='[' && p[3]=='['){
                p+=4; while(*p && !(p[0]==']' && p[1]==']')) p++;
                if(*p) p+=2; continue;
            }
            while(*p && *p!='\n') p++; continue;
        }

        // 2. DISPATCH por primeira letra - 1 branch vs 7 strncmp
        switch(*p){
            case 'm': {
                if(startsWith(p,"makeAnimatedLuaSprite",21)){
                    const char *as,*ae; if(!GetArgsBounds(p,&as,&ae)){p+=21;break;}
                    const char* a=as; char tag[64]={0},img[128]={0}; float x=0,y=0;
                    if(parseQuoted(&a,tag,sizeof(tag)) && parseQuoted(&a,img,sizeof(img)) && parseFloat(&a,&x) && parseFloat(&a,&y)){
                        int mi=FindTag(maps,mapCount,tag);
                        int idx=(mi>=0&&maps[mi].idx>=0)?maps[mi].idx:-1;
                        if(idx<0){
                            if(count>=cap||count>=STAGE_MAX_SPRITES){p+=21;break;}
                            idx=count++; memset(out+idx,0,sizeof(*out));
                            out[idx].scrollX=1; out[idx].scrollY=1; out[idx].scaleX=1; out[idx].scaleY=1;
                            if(mi>=0) maps[mi].idx=idx; else{ memcpy(maps[mapCount].tag,tag,64); maps[mapCount].idx=idx; mapCount++; }
                        }
                        memcpy(out[idx].image,img,128); out[idx].x=x; out[idx].y=y;
                    }
                    p+=21; continue;
                }
                if(startsWith(p,"makeLuaSprite",13)){
                    const char *as,*ae; if(!GetArgsBounds(p,&as,&ae)){p+=13;break;}
                    const char* a=as; char tag[64]={0},img[128]={0}; float x=0,y=0;
                    if(parseQuoted(&a,tag,sizeof(tag)) && parseQuoted(&a,img,sizeof(img)) && parseFloat(&a,&x) && parseFloat(&a,&y)){
                        int mi=FindTag(maps,mapCount,tag);
                        int idx=(mi>=0&&maps[mi].idx>=0)?maps[mi].idx:-1;
                        if(idx<0){
                            if(count>=cap||count>=STAGE_MAX_SPRITES){p+=13;break;}
                            idx=count++; memset(out+idx,0,sizeof(*out));
                            out[idx].scrollX=1; out[idx].scrollY=1; out[idx].scaleX=1; out[idx].scaleY=1;
                            if(mi>=0) maps[mi].idx=idx; else{ memcpy(maps[mapCount].tag,tag,64); maps[mapCount].idx=idx; mapCount++; }
                        }
                        memcpy(out[idx].image,img,128); out[idx].x=x; out[idx].y=y;
                    }
                    p+=13; continue;
                }
                if(startsWith(p,"makeGraphic",11)){
                    const char *as,*ae; if(!GetArgsBounds(p,&as,&ae)){p+=11;break;}
                    const char* a=as; char tag[64]={0}; float w=0,h=0;
                    if(parseQuoted(&a,tag,sizeof(tag)) && parseFloat(&a,&w) && parseFloat(&a,&h)){
                        int mi=FindTag(maps,mapCount,tag);
                        int idx=(mi>=0&&maps[mi].idx>=0)?maps[mi].idx:-1;
                        if(idx<0){
                            if(count>=cap||count>=STAGE_MAX_SPRITES){p+=11;break;}
                            idx=count++; memset(out+idx,0,sizeof(*out));
                            out[idx].scrollX=1; out[idx].scrollY=1; out[idx].scaleX=1; out[idx].scaleY=1;
                            if(mi>=0) maps[mi].idx=idx; else{ memcpy(maps[mapCount].tag,tag,64); maps[mapCount].idx=idx; mapCount++; }
                        }
                        isGraphic[idx]=1;
                    }
                    p+=11; continue;
                }
            } break;
            case 's': {
                int isLua = startsWith(p,"setLuaSpriteScrollFactor",24);
                int isShort = startsWith(p,"setScrollFactor",15);
                if(isLua||isShort){
                    int adv=isLua?24:15;
                    const char *as,*ae; if(!GetArgsBounds(p,&as,&ae)){p+=adv;break;}
                    const char* a=as; char tag[64]={0}; float sx=1,sy=1;
                    if(parseQuoted(&a,tag,sizeof(tag)) && parseFloat(&a,&sx)){
                        if(!parseFloat(&a,&sy)) sy=sx;
                        int mi=FindTag(maps,mapCount,tag);
                        int idx=(mi>=0)?maps[mi].idx:-1;
                        if(idx<0){
                            if(count>=cap||count>=STAGE_MAX_SPRITES){p+=adv;break;}
                            idx=count++; memset(out+idx,0,sizeof(*out));
                            out[idx].scrollX=1; out[idx].scrollY=1; out[idx].scaleX=1; out[idx].scaleY=1;
                            if(mi>=0) maps[mi].idx=idx; else{ memcpy(maps[mapCount].tag,tag,64); maps[mapCount].idx=idx; mapCount++; }
                        }
                        out[idx].scrollX=sx; out[idx].scrollY=sy;
                    }
                    p+=adv; continue;
                }
                if(startsWith(p,"scaleObject",11) || startsWith(p,"scaleLuaSprite",14)){
                    int adv=(p[5]=='O')?11:14;
                    const char *as,*ae; if(!GetArgsBounds(p,&as,&ae)){p+=adv;break;}
                    const char* a=as; char tag[64]={0}; float sx=1,sy=1;
                    if(parseQuoted(&a,tag,sizeof(tag)) && parseFloat(&a,&sx)){
                        if(!parseFloat(&a,&sy)) sy=sx;
                        int mi=FindTag(maps,mapCount,tag);
                        int idx=(mi>=0)?maps[mi].idx:-1;
                        if(idx<0){
                            if(count>=cap||count>=STAGE_MAX_SPRITES){p+=adv;break;}
                            idx=count++; memset(out+idx,0,sizeof(*out));
                            out[idx].scrollX=1; out[idx].scrollY=1; out[idx].scaleX=1; out[idx].scaleY=1;
                            if(mi>=0) maps[mi].idx=idx; else{ memcpy(maps[mapCount].tag,tag,64); maps[mapCount].idx=idx; mapCount++; }
                        }
                        out[idx].scaleX=sx; out[idx].scaleY=sy;
                    }
                    p+=adv; continue;
                }
                if(startsWith(p,"setProperty",11)){ anim++; p+=11; continue; }
            } break;
            case 'a': {
                if(startsWith(p,"addLuaSprite",12)){
                    const char *as,*ae; if(!GetArgsBounds(p,&as,&ae)){p+=12;break;}
                    const char* a=as; char tag[64]={0};
                    if(parseQuoted(&a,tag,sizeof(tag))){
                        int mi=FindTag(maps,mapCount,tag);
                        int idx=(mi>=0)?maps[mi].idx:-1;
                        if(idx<0){
                            if(count>=cap||count>=STAGE_MAX_SPRITES){p+=12;break;}
                            idx=count++; memset(out+idx,0,sizeof(*out));
                            out[idx].scrollX=1; out[idx].scrollY=1; out[idx].scaleX=1; out[idx].scaleY=1;
                            if(mi>=0) maps[mi].idx=idx; else{ memcpy(maps[mapCount].tag,tag,64); maps[mapCount].idx=idx; mapCount++; }
                        }
                        while(*a && isspace((unsigned char)*a)) a++;
                        char val[16]={0}; int vi=0;
                        while(*a && vi<15 && isalpha((unsigned char)*a)) val[vi++]=(char)tolower((unsigned char)*a++);
                        out[idx].front=(strcmp(val,"true")==0);
                    }
                    p+=12; continue;
                }
                if(startsWith(p,"addGlitchEffect",15)){ glitch++; p+=15; continue; }
            } break;
            case 'o': if(startsWith(p,"onUpdate",8)){ anim++; p+=8; continue; } break;
            default: break;
        }
        p++;
    }
    
    int w=0;
    for(int i=0;i<count;i++){
        if(out[i].image[0]==0 && !isGraphic[i]) continue;
        if(w!=i){ out[w]=out[i]; isGraphic[w]=isGraphic[i]; }
        w++;
    }
    if(ignoredGlitch) *ignoredGlitch=glitch;
    if(ignoredAnim) *ignoredAnim=anim;
    return w;
}

void Stage_CoverBackgrounds(StageBg* bgs, int n, float zoom,
    float dadX, float dadY, float dadOffX, float dadOffY,
    float bfX, float bfY, float bfOffX, float bfOffY) {
    // ... igual ao seu, já é O(n) e zero por frame, não precisa mexer ...
    if (!bgs || n < 1 || zoom < 0.3f || zoom > 2.0f) return;
    float uminx=1e30f,uminy=1e30f,umaxx=-1e30f,umaxy=-1e30f;
    for(int i=0;i<n;i++){
        float w=bgs[i].imgW*bgs[i].scaleX, h=bgs[i].imgH*bgs[i].scaleY;
        if(w<=0||h<=0) continue;
        if(bgs[i].x<uminx) uminx=bgs[i].x;
        if(bgs[i].y<uminy) uminy=bgs[i].y;
        if(bgs[i].x+w>umaxx) umaxx=bgs[i].x+w;
        if(bgs[i].y+h>umaxy) umaxy=bgs[i].y+h;
    }
    if(uminx>umaxx) return;
    float vw=1280.f/zoom,vh=720.f/zoom,mx=vw*0.10f,my=vh*0.10f;
    float ax0=fminf(dadX,fminf(bfX+bfOffX,dadX+dadOffX)), ax1=fmaxf(dadX,fmaxf(bfX+bfOffX,dadX+dadOffX));
    float ay0=fminf(dadY,fminf(bfY+bfOffY,dadY+dadOffY)), ay1=fmaxf(dadY,fmaxf(bfY+bfOffY,dadY+dadOffY));
    float qx0=ax0-vw/2-mx,qy0=ay0-vh/2-my,qx1=ax1+vw/2+mx,qy1=ay1+vh/2+my;
    float uw=umaxx-uminx,uh=umaxy-uminy; if(uw<=0||uh<=0) return;
    float s=fmaxf((qx1-qx0)/uw,(qy1-qy0)/uh); if(s<1) s=1;
    for(int i=0;i<n;i++){ bgs[i].x=uminx+(bgs[i].x-uminx)*s; bgs[i].y=uminy+(bgs[i].y-uminy)*s; bgs[i].scaleX*=s; bgs[i].scaleY*=s; }
    float vx0=uminx,vy0=uminy,vx1=uminx+uw*s,vy1=uminy+uh*s;
    float dx=(vx0>qx0)?qx0-vx0:(vx1<qx1)?qx1-vx1:0, dy=(vy0>qy0)?qy0-vy0:(vy1<qy1)?qy1-vy1:0;
    for(int i=0;i<n;i++){ bgs[i].x+=dx; bgs[i].y+=dy; }
}