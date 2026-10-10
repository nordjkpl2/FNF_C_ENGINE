#ifndef SONGLIST_H
#define SONGLIST_H

typedef struct {
    char name[64];
    char dir[256];
    char opp[32]; // aditivo p/ icone do freeplay; "" = resolver lazy (scan nao parseia)
} SongEntry;

// Scans assets/songs/*, assets/mods/* and assets/mods/*/songs/*.
// Returns count, *outEntries holds malloc'd array (NULL when 0).
// Caller frees with SongList_Free.
int SongList_Scan(SongEntry** outEntries);
void SongList_Free(SongEntry* entries);
// heuristic: 1 if load is expected to hitch (big audio/chart) -> use loading screen
char SongList_IsHeavy(const char* dir);
// resolve leve do oponente (player2) p/ icone: so le cabecalho, sem parse full.
// 1 = achou em out (senao out = "dad"). Fora do scan e fora do frame.
char SongList_Opponent(const char* dir, char out[32]);

#endif
