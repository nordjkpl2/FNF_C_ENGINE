#ifndef SONG_H
#define SONG_H

#include <stdint.h>

typedef struct {
    float time;
    int16_t id;   // sign = mustHit; |id| = lane 1..4 (0 = invalid)
    int16_t _pad; // alinhamento p/ float len
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
    char gfVersion[32]; // Psych: "gf" padrao, "nogf"/"" = sem girlfriend
    char noteSkin[32];  // custom note skin (mod > global default), vazio = default

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