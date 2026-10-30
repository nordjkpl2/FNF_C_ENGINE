#pragma once
#ifndef SONGLIST_H
#define SONGLIST_H

typedef struct {
    char name[64];
    char dir[256];
    char opp[32];       /* player2 do chart; vazio quando nao foi encontrado */
    char weekIcon[64];  /* segundo campo de songs[] no week JSON, ex. icon-charlie */
} SongEntry;

int SongList_Scan(SongEntry **outEntries);
void SongList_Free(SongEntry *entries);
char SongList_IsHeavy(const char *dir);
char SongList_Opponent(const char *dir, char out[32]);

#endif
