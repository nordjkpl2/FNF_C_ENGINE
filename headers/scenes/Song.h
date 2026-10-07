#ifndef SONG_H
#define SONG_H

typedef struct {
    int id; // sign based instead of that weird wrapping id thing fnf does
    float time;
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

    Section* sections;
    int sectionCount; 

    float bpm;
    float speed;
} Song;

void Song_Parse(Song* song, const char* songDataPath);
void Song_Free(Song* song);

#endif