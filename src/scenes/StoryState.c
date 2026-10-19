/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl - StoryState.c Optimized, outd 19 of10 2026 */
#include "scenes/AllScenes.h"
#include "scenes/SongList.h"
#include <string.h>
#include <stdio.h>

static WeekEntry* weeks = NULL;
static int weekCount = 0;
static int selected = 0;
static float offset = 0;
static char delConfirm = 0;
static char statusText[128];
static float statusTimer = 0;

static Texture2D weekIcon;
static char weekIconHas = 0;
static int weekIconIdx = -1;

static float discAngle = 0;
static RayGraphicObject bg;
static Camera2D cam;

static Color outlineColor = {0, 0, 0, 0};
static Color fontColor = {255, 255, 255, 255};

static inline void StatusMsg(const char* m) {
    strncpy(statusText, m, sizeof(statusText)-1);
    statusText[sizeof(statusText)-1]=0;
    statusTimer = 3.0f;
}

// Otimizado: sem alloc extra, sem fopen. Normaliza on-the-fly.
static inline char Story_ModRoot(const char* weekDir, char out[256]) {
    out[0]=0;
    if(!weekDir || !weekDir[0]) return 0;
    // copia normalizando \ -> / direto pro out temp
    char norm[300];
    size_t L = strlen(weekDir);
    if(L >= sizeof(norm)) L = sizeof(norm)-1;
    for(size_t i=0;i<L;i++) norm[i] = (weekDir[i]=='\\') ? '/' : weekDir[i];
    norm[L]=0;

    if(L < 6) return 0;
    if(memcmp(norm+L-6, "/weeks", 6)!=0) return 0;
    size_t n = L-6;
    if(n >= 256) n=255;
    memcpy(out, norm, n);
    out[n]=0;
    return 1;
}

static inline char Story_IconPath(const char* weekDir, const char* opp, char out[300]) {
    if(!opp || !opp[0]) return 0;
    if(strchr(opp,'/') || strchr(opp,'\\') || strstr(opp,"..")) return 0;
    
    char root[256];
    char p[300];
    if(Story_ModRoot(weekDir, root)) {
        snprintf(p, sizeof(p), "%s/images/icons/%s.png", root, opp);
        if(FileExists(p)) { strncpy(out,p,299); out[299]=0; return 1; }
    }
    snprintf(p, sizeof(p), "assets/images/icons/%s.png", opp);
    if(FileExists(p)) { strncpy(out,p,299); out[299]=0; return 1; }
    return 0;
}

static inline void Story_UnloadIcon(void) {
    if(weekIconHas) UnloadTexture(weekIcon);
    weekIconHas=0;
    weekIconIdx=-1;
}

static void Story_LoadWeekIcon(void) {
    if(weekIconIdx==selected && weekIconHas) return;
    Story_UnloadIcon();
    if(selected<0 || selected>=weekCount) return;
    WeekEntry* w = &weeks[selected];
    const char* opp = (w->songCount>0 && w->songs[0].character[0]) ? w->songs[0].character : "dad";
    char ip[300];
    if(!Story_IconPath(w->dir, opp, ip)) return;
    weekIcon = Render_LoadTexture(ip);
    // Render_LoadTexture retorna texture vazia se falhar - checa
    if(weekIcon.id != 0) {
        weekIconHas=1;
        weekIconIdx=selected;
    }
}

static void Story_StartWeek(void) {
    WeekEntry* w = &weeks[selected];
    WeekRun_Start(w, 1);
    const char* dir = WeekRun_Dir();
    if(dir[0]==0) {
        const char* miss = WeekRun_Missing();
        char b[128];
        if(miss[0]) snprintf(b,sizeof(b),"MISSING SONG: %s",miss);
        else snprintf(b,sizeof(b),"EMPTY WEEK");
        StatusMsg(b);
        WeekRun_Stop();
        return;
    }
    RayGame_ToggleMusic(0);
    PlayState_SetSongDir(dir);
    PlayState_SetReturnWeek(1);
    PlayState_SetCharsOverride(WeekRun_Char());
    PlayState_SetDiff(WeekRun_DiffSuffix());
    if(SongList_IsHeavy(dir)) {
        LoadingState_SetSongDir(dir);
        LoadingState_SetScene();
    } else {
        PlayState_SetScene();
    }
}

static void StoryState_Create([[maybe_unused]] RayScene* scene) {
    Render_DefaultRGT(&bg);
    bg.objType = RGT_IMAGE;
    bg.image = Render_LoadTexture("assets/images/freeplay/menuBGBlue.png");
    cam.target = (Vector2){1280/2,720/2};
    cam.zoom=1;
    WeekList_Free(weeks);
    weeks=NULL;
    weekCount = WeekList_Scan(&weeks);
    selected=0; offset=0; delConfirm=0; discAngle=0;
    statusText[0]=0; statusTimer=0;
    Story_UnloadIcon();
    Story_LoadWeekIcon();
    if(WeekRun_Missing()[0]) {
        char b[128];
        snprintf(b,sizeof(b),"SKIPPED (missing): %s",WeekRun_Missing());
        StatusMsg(b);
    }
}

static void StoryState_Draw([[maybe_unused]] RayScene* scene) {
    float dt = RayGame_DeltaTime();
    if(statusTimer>0) statusTimer-=dt;

    // --- input (1 branch só) ---
    if(delConfirm) {
        if(IsKeyPressed(KEY_Y)) {
            StatusMsg(Week_Delete(&weeks[selected]) ? "DELETED" : "NOT DELETED");
            WeekList_Free(weeks); weeks=NULL;
            weekCount = WeekList_Scan(&weeks);
            if(selected>=weekCount) selected=weekCount-1;
            if(selected<0) selected=0;
            delConfirm=0;
            Story_UnloadIcon(); Story_LoadWeekIcon();
            return;
        }
        if(IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) { delConfirm=0; return; }
    } else {
        if(IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) { MenuState_SetScene(); return; }
        if(weekCount>0) {
            if(IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) { selected=(selected+1)%weekCount; Story_LoadWeekIcon(); }
            if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) { selected=(selected-1+weekCount)%weekCount; Story_LoadWeekIcon(); }
            if(IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_SLASH) || IsKeyPressed(KEY_KP_DIVIDE)) { delConfirm=1; return; }
            if(IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) { Story_StartWeek(); return; }
        }
    }

    offset = Lerp(offset, (float)selected, dt*10.0f);
    Render_SetCamera(&cam);
    Render_DrawGraphicObject(&bg);

    // disco + icone (5 draws, 0 VRAM)
    {
        if(weekIconHas) {
            DrawTexturePro(weekIcon, (Rectangle){0,0,150,150}, (Rectangle){955,475,200,200}, VECTOR_ZERO, 0, WHITE);
        }
        discAngle += dt*0.6f;
        Vector2 c={1055,575};
        DrawCircleV(c,135,(Color){16,16,24,255});
        DrawCircleLines((int)c.x,(int)c.y,110,(Color){60,60,80,255});
        DrawCircleLines((int)c.x,(int)c.y,78,(Color){60,60,80,255});
        DrawCircleV(c,46,(Color){130,90,170,255});
        DrawCircleV(c,10,(Color){220,220,230,255});
        // 1 sin/cos só por frame
        float ca=cosf(discAngle), sa=sinf(discAngle);
        Vector2 tip={c.x+126*ca, c.y+126*sa};
        DrawLineEx(c,tip,5,(Color){255,255,255,40});
    }

    if(weekCount==0) {
        DrawTextEx(mainFont,"NO WEEKS - add to assets/weeks/",(Vector2){150,360},32,2,fontColor);
        Render_StopCamera(); return;
    }
    if(delConfirm) {
        char b[128];
        snprintf(b,sizeof(b),"DELETE %s?  Y = yes   N = no",weeks[selected].title);
        Vector2 s=MeasureTextEx(mainFont,b,40,4);
        DrawTextEx(mainFont,b,(Vector2){640-s.x/2,340},40,4,(Color){255,80,80,255});
        Render_StopCamera(); return;
    }

    // lista esquerda - cull vertical agressivo
    const int fontSize=56, shadow=4;
    for(int i=0;i<weekCount;i++) {
        float y = 200 + (i-offset)*(10+fontSize);
        if(y < -80 || y > 800) continue;
        float d = fabsf((float)i-offset);
        Vector2 pos={(float)(shadow+120+d*-24), (float)(shadow+y)};
        char isSel = (selected==i);
        fontColor.a = isSel ? 255 : 150;
        outlineColor.a = isSel ? 255 : 100;
        DrawTextEx(mainFont,weeks[i].title,pos,fontSize,4,outlineColor);
        pos.x-=shadow; pos.y-=shadow;
        DrawTextEx(mainFont,weeks[i].title,pos,fontSize,4,fontColor);
        if(isSel) {
            Vector2 mp={pos.x-56,pos.y};
            DrawTextEx(mainFont,">",(Vector2){mp.x+shadow,mp.y+shadow},fontSize,4,outlineColor);
            DrawTextEx(mainFont,">",mp,fontSize,4,(Color){255,220,80,255});
            Vector2 np={pos.x-116,pos.y+8};
            DrawTriangle((Vector2){np.x,np.y},(Vector2){np.x,np.y+36},(Vector2){np.x+30,np.y+18},(Color){80,220,100,255});
        }
    }

    // header direita - mede só a selecionada
    {
        WeekEntry* w=&weeks[selected];
        Vector2 ts=MeasureTextEx(mainFont,w->title,40,3);
        float tx=1230-ts.x;
        DrawTextEx(mainFont,w->title,(Vector2){tx+3,53},40,3,outlineColor);
        DrawTextEx(mainFont,w->title,(Vector2){tx,50},40,3,fontColor);
        if(w->flavor[0]) {
            const int fs=22,lh=26;
            const char* s=w->flavor;
            char linebuf[48];
            for(int line=0;line<4 && s[0];line++) {
                size_t k=0; while(s[k] && s[k]!='\n' && k<47){linebuf[k]=s[k];k++;} linebuf[k]=0;
                Vector2 ls=MeasureTextEx(mainFont,linebuf,fs,2);
                float lx=1230-ls.x;
                DrawTextEx(mainFont,linebuf,(Vector2){lx+2,102+line*lh},fs,2,outlineColor);
                DrawTextEx(mainFont,linebuf,(Vector2){lx,100+line*lh},fs,2,(Color){255,220,130,255});
                if(s[k]==0) break; s+=k+1;
            }
        }
        float x=800,y=150; char b[128];
        snprintf(b,sizeof(b),"%d songs",w->songCount);
        DrawTextEx(mainFont,b,(Vector2){x,y},28,2,(Color){255,220,80,255}); y+=44;
        for(int i=0;i<w->songCount && y<=440;i++) {
            snprintf(b,sizeof(b),"%d. %s",i+1,w->songs[i].song);
            DrawTextEx(mainFont,b,(Vector2){x,y},24,2,WHITE); y+=32;
        }
        if(WeekRun_LastTotal()>0) {
            snprintf(b,sizeof(b),"LAST: %d",WeekRun_LastTotal());
            DrawTextEx(mainFont,b,(Vector2){x,y+8},24,2,(Color){200,200,200,255});
        }
    }

    if(statusTimer>0) DrawTextEx(mainFont,statusText,(Vector2){150,660},24,2,YELLOW);
    DrawTextEx(mainFont,"ENTER start   DEL or / delete   ESC back",(Vector2){150,692},20,1,YELLOW);
    Render_StopCamera();
}

static void StoryState_Destroy([[maybe_unused]] RayScene* scene) {
    UnloadTexture(bg.image);
    Story_UnloadIcon();
    WeekList_Free(weeks); weeks=NULL; weekCount=0;
}

static RayScene storyScene;
Scene_MakeSceneCode(storyScene,StoryState_SetScene,StoryState_Create,NULL,StoryState_Draw,StoryState_Destroy)