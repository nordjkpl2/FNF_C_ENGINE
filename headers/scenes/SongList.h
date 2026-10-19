/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl - Oct 19 2026 */
#pragma once
#ifndef SONGLIST_H
#define SONGLIST_H
typedef struct {
    char name[64];
    char dir[256];
    char opp[32];
} SongEntry;
int SongList_Scan(SongEntry** outEntries);
void SongList_Free(SongEntry* entries);
char SongList_IsHeavy(const char* dir);
char SongList_Opponent(const char* dir, char out[32]);

#endif
