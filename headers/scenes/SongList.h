#ifndef SONGLIST_H
#define SONGLIST_H

typedef struct {
    char name[64];
    char dir[256];
} SongEntry;

// Scans assets/songs/*, assets/mods/* and assets/mods/*/songs/*.
// Returns count, *outEntries holds malloc'd array (NULL when 0).
// Caller frees with SongList_Free.
int SongList_Scan(SongEntry** outEntries);
void SongList_Free(SongEntry* entries);
// heuristic: 1 if load is expected to hitch (big audio/chart) -> use loading screen
char SongList_IsHeavy(const char* dir);

#endif
