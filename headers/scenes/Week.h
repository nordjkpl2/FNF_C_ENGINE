#ifndef WEEK_H
#define WEEK_H

#define WEEK_MAX_SONGS 16

typedef struct {
    char song[64];
    char character[32];
} WeekSong;

typedef struct {
    char file[64];   // nome base (sem .json)
    char title[64];  // storyName > weekName > file
    char flavor[192]; // "flavor"/"description" opcional (so mostra se tiver)
    char dir[256];   // pasta da week (assets/weeks ou assets/mods/X/weeks)
    char fromMods;
    WeekSong songs[WEEK_MAX_SONGS];
    int songCount;
} WeekEntry;

// lista assets/weeks (ordem do weekList.txt) + assets/mods/*/weeks
// *outEntries com malloc; liberar com WeekList_Free. Retorna quantidade.
int WeekList_Scan(WeekEntry** outEntries);
void WeekList_Free(WeekEntry* entries);

// apaga a week (arquivo + linha do weekList.txt quando houver). 1 = ok.
char Week_Delete(const WeekEntry* e);

// parse de um arquivo week estilo Psych (subset: ignora cor, arte, unlock)
char Week_ParseFile(const char* path, const char* dir, const char* fileBase, WeekEntry* out);

// campanha: playlist da week com score acumulado
void WeekRun_Start(const WeekEntry* e, int diff);
char WeekRun_Active(void);
void WeekRun_Stop(void);
// pasta da musica atual (resolvida) ou "" se faltar; avanca, 1 = tem proxima
const char* WeekRun_Dir(void);
const char* WeekRun_Char(void);
const char* WeekRun_DiffSuffix(void);
int WeekRun_Advance(void);
void WeekRun_AddScore(int s);
int WeekRun_Total(void);
int WeekRun_Pos(void);
int WeekRun_TotalSongs(void);
int WeekRun_LastTotal(void);
// nome da ultima musica pulada por nao achar a pasta ("" = nenhuma)
const char* WeekRun_Missing(void);
// fecha campanha guardando total pra mostrar
void WeekRun_Finish(void);

#endif
