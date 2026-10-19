/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl - Oct 19 2026 */
#pragma once
#ifndef WEEK_H
#define WEEK_H
#define WEEK_MAX_SONGS 30

typedef struct {
    char song[64];
    char character[32];
} WeekSong;

typedef struct {
    char file[64];
    char title[64];
    char flavor[192];
    char dir[256];
    char fromMods;
    WeekSong songs[WEEK_MAX_SONGS];
    int songCount;
} WeekEntry;

int WeekList_Scan(WeekEntry** outEntries);
void WeekList_Free(WeekEntry* entries);
char Week_Delete(const WeekEntry* e);
char Week_ParseFile(const char* path, const char* dir, const char* fileBase, WeekEntry* out);
void WeekRun_Start(const WeekEntry* e, int diff);
char WeekRun_Active(void);
void WeekRun_Stop(void);
const char* WeekRun_Dir(void);
const char* WeekRun_Char(void);
const char* WeekRun_DiffSuffix(void);
int WeekRun_Advance(void);
void WeekRun_AddScore(int s);
int WeekRun_Total(void);
int WeekRun_Pos(void);
int WeekRun_TotalSongs(void);
int WeekRun_LastTotal(void);
const char* WeekRun_Missing(void);
void WeekRun_Finish(void);
#endif