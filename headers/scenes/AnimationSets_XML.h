#ifndef ANIMATIONSETS_XML_H
#define ANIMATIONSETS_XML_H

#include <stdbool.h>
#include "Render.h"

/*
 * Dados de um personagem definido no formato XML do Codename Engine.
 * `animations` contém as animações já resolvidas contra o atlas Sparrow.
 * A textura recebida em AnimationSet_LoadCharacterXML passa a pertencer
 * a `animations` quando a função retorna true.
 */
typedef struct RayCharacterXMLData {
    char sprite[128];

    float x;
    float y;
    float scale;
    float cameraX;
    float cameraY;
    float holdTime;
    float defaultFPS;

    Color color;

    bool isPlayer;
    bool flipX;
    bool antialiasing;

    bool hasX;
    bool hasY;
    bool hasScale;
    bool hasCameraX;
    bool hasCameraY;
    bool hasColor;

    RayAnimationHandler animations;
} RayCharacterXMLData;

/*
 * Lê um XML de personagem (character/anim) e um XML de atlas Sparrow
 * (TextureAtlas/SubTexture), construindo um RayAnimationHandler.
 * Retorna false em XML inválido, prefixo ausente, índices inválidos ou
 * erro de memória. Em caso de sucesso, `out->animations` passa a possuir
 * a textura `image`; libere com AnimationSet_FreeAll().
 */
bool AnimationSet_LoadCharacterXML(
    const char *characterXMLPath,
    const char *atlasXMLPath,
    Texture2D image,
    RayCharacterXMLData *out
);

#endif /* ANIMATIONSETS_XML_H */
