/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#pragma once
#ifndef SONG_H
#define SONG_H
/* // ! Includes */
#include <stdint.h>
/* // code */
typedef struct {
    float time;
    int16_t id;   // sign = mustHit; |id| = lane 1..4 (0 = invalid)
    int16_t _pad; // alinhamento p/ float len
    float len;
} DataNote;
/* // part of song */
typedef struct {
    char mustHit;
    float bpm; 
    int len; 

    DataNote* notes;
    int noteCount;
} Section;
/* // song structure */
typedef struct {
    char player1[32];
    char player2[32];
    char stage[32];
    char gfVersion[32]; // "gf" padrao, "nogf"/"" = sem girlfriend
    char noteSkin[32];  // custom note skin (mod > global default), vazio = default

    Section* sections;
    int sectionCount; 

    float bpm;
    float speed;
} Song;
/* // song code */
void Song_Parse(Song* song, const char* songDataPath);
char Song_ParseJSON(Song* song, const char* jsonPath);
void Song_LoadSong(Song* song, const char* songName);
void Song_LoadSongFromDir(Song* song, const char* dirPath);
void Song_LoadSongFromDirDiff(Song* song, const char* dirPath, const char* diff);
char Song_HasChart(const char* dirPath);
void Song_Free(Song* song);

#endif