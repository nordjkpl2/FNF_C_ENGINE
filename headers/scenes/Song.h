/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl, outd day19 of10 2026 */
//!OUTD day19/10/2026
#pragma once
#ifndef SONG_H
#define SONG_H
#include <stdint.h>
typedef struct {
    float time;
    int16_t id;
    int16_t _pad;
    float len;
} DataNote;
typedef struct {
    char mustHit;
    float bpm; 
    int len; 
    DataNote* notes;
    int noteCount;
} Section;
typedef struct {
    char player1[32];
    char player2[32];
    char stage[32];
    char gfVersion[32];
    char noteSkin[32];
    Section* sections;
    int sectionCount; 
    float bpm;
    float speed;
} Song;
void Song_Parse(Song* song, const char* songDataPath);
char Song_ParseJSON(Song* song, const char* jsonPath);
void Song_LoadSong(Song* song, const char* songName);
void Song_LoadSongFromDir(Song* song, const char* dirPath);
void Song_LoadSongFromDirDiff(Song* song, const char* dirPath, const char* diff);
char Song_HasChart(const char* dirPath);
void Song_Free(Song* song);
#endif