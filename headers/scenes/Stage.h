#pragma once
#ifndef STAGE_H
#define STAGE_H

#include <stdint.h>

/* Parser subset de stage .lua da Psych, sem LuaJIT/VM (somente no load).
 * Nao executa Lua: apenas extrai comandos de criacao e propriedades de sprites.
 */

typedef struct {
    float x, y;          /* posicao mundial (topo-esquerda) */
    float imgW, imgH;    /* tamanho original da textura */
    float scaleX, scaleY;
} StageBg;

/* Cobertura do percurso da camera: escala uniforme minima + deslocamento.
 * So cresce, nunca reduz. Executar apenas durante o carregamento do stage.
 */
void Stage_CoverBackgrounds(StageBg *bgs, int n, float zoom,
    float dadX, float dadY, float dadOffX, float dadOffY,
    float bfX, float bfY, float bfOffX, float bfOffY);

#define STAGE_MAX_SPRITES 32

typedef struct {
    char image[128];

    float x, y;
    float scrollX, scrollY;
    float scaleX, scaleY;

    /* makeGraphic(tag, width, height, color). Dimensoes sao independentes
     * de scaleX/scaleY: o tamanho desenhado e graphicWidth*scaleX por
     * graphicHeight*scaleY.
     */
    float graphicWidth, graphicHeight;
    uint8_t colorR, colorG, colorB, colorA;
    uint8_t isGraphic;

    int front;           /* 0 = atras, 1 = frente (addLuaSprite/setObjectOrder) */
} StageSprite;

/* Procura <nome>.lua em <mod>/stages/, <mod>/data/stages/,
 * assets/stages/, assets/data/stages/ e songDir/.
 * Retorna 1 quando encontrado; escreve o caminho em out[300].
 */
char Stage_FindLua(const char *stageName, const char *songDir, char out[300]);

/* Faz parse do subset de Lua suportado para out[capacity]. Retorna quantidade.
 * ignoredGlitch/ignoredAnim podem ser NULL.
 */
int Stage_ParseLua(const char *luaText, StageSprite *out, int capacity,
                   int *ignoredGlitch, int *ignoredAnim);

#endif /* STAGE_H */
