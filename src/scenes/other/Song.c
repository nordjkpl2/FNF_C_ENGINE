#include"std.h"
#include"scenes/Song.h"
 
#define IERR  {puts("\ninvalid data");exit(1);}
#define CHECK_LEN(readOperation, expectedSize) if(readOperation != expectedSize) IERR

void File_ReadString(FILE* file, char str[32]) {
    int len;

    CHECK_LEN(fread(&len, sizeof(int), 1, file), 1)
    if(len < 1 || len > 31) IERR

    CHECK_LEN(fread(str, 1, len, file), len)

    str[len] = 0;
}

void Song_Parse(Song* song, const char* songDataPath) {
    FILE* file = fopen(songDataPath, "rb");
    
    if(file == NULL) {
        printf("could not open file %s\n", songDataPath);
        exit(1);
    }

    File_ReadString(file, song->player1);
    File_ReadString(file, song->player2);
    File_ReadString(file, song->stage);

    CHECK_LEN(fread(&song->speed, sizeof(float), 1, file), 1)
    CHECK_LEN(fread(&song->sectionCount, sizeof(int), 1, file), 1)

    if(song->sectionCount < 1 || song->sectionCount > SIZE_MAX / sizeof(Section) /* overflow */) IERR
    if(song->speed < 0) IERR 

    song->sections = malloc(sizeof(Section) * song->sectionCount);

    for(size_t i = 0; i < song->sectionCount; i++) {
        Section* section = song->sections + i; 
        
        CHECK_LEN(fread(&section->len, sizeof(int), 1, file), 1)
        if(section->len < 1) IERR
        CHECK_LEN(fread(&section->bpm, sizeof(float), 1, file), 1)
        if(section->bpm < 10) IERR
        CHECK_LEN(fread(&section->mustHit, 1, 1, file), 1) 
        CHECK_LEN(fread(&section->noteCount, sizeof(int), 1, file), 1)
        if(section->noteCount < 0 || section->noteCount > SIZE_MAX / sizeof(DataNote) /* overflow */) IERR

       // printf("> section %d\n  len %d\n   bpm %f\n   mustHit %c\n   noteCount %d\n", i, section->len, section->bpm, section->mustHit, section->noteCount);

        section->notes = malloc(sizeof(DataNote) * section->noteCount);

        for(size_t j = 0; j < section->noteCount; j++) {
            DataNote* note = section->notes + j;

            CHECK_LEN(fread(&note->time, sizeof(float), 1, file), 1)
            if(note->time < 0) IERR
            CHECK_LEN(fread(&note->id, sizeof(int), 1, file), 1)
            int id = abs(note->id);
            if(id == 0 || id > 4) IERR
            CHECK_LEN(fread(&note->len, sizeof(float), 1, file), 1)
            if(note->len < 0) IERR 
        }
    }

    fclose(file);
}

void Song_Free(Song* song) {
    for(size_t i = 0; i < song->sectionCount; i++) {
        Section* section = song->sections + i; 
        free(section->notes);
    }
    free(song->sections);
}