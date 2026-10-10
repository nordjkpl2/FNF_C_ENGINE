#ifndef STAGE_H
#define STAGE_H

// Parser subset de stage .lua da Psych, sem LuaJIT/VM (so no load, zero por frame).
// Cobre: makeLuaSprite, setLuaSpriteScrollFactor/setScrollFactor,
// addLuaSprite, scaleObject/scaleLuaSprite. Ignora shaders/anim.

// Retangulo de fundo p/ cobertura (sem tipos raylib: testavel isolado).
typedef struct {
    float x, y; // posicao mundo (topo-esq)
    float imgW, imgH; // tamanho da textura
    float scaleX, scaleY;
} StageBg;

// Cobre a caixa que a camera percorre com os fundos: escala uniforme minima +
// deslocamento minimo. So cresce, nunca reduz. So no load, zero por frame.
void Stage_CoverBackgrounds(StageBg* bgs, int n, float zoom,
    float dadX, float dadY, float dadOffX, float dadOffY,
    float bfX, float bfY, float bfOffX, float bfOffY);
#define STAGE_MAX_SPRITES 32

typedef struct {
    char image[128];
    float x, y;
    float scrollX, scrollY;
    float scaleX, scaleY;
    int front; // 0 = atras (padrao Psych), 1 = frente
} StageSprite;

// Acha <nome>.lua em: <mod>/stages/, assets/stages/, assets/data/stages/, songDir/.
// 1 = achou (out = caminho), 0 = nao.
char Stage_FindLua(const char* stageName, const char* songDir, char out[300]);

// Faz parse do texto .lua para out[cap]. Retorna count.
// ignoredGlitch/ignoredAnim podem ser NULL.
int Stage_ParseLua(const char* luaText, StageSprite* out, int cap, int* ignoredGlitch, int* ignoredAnim);

#endif
