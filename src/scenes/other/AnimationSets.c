/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl, outd day18 of10 2026 */
#include "scenes/AllScenes.h" // já traz Render.h
#include "Log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define IERR Log_Fatal("invalid data");
#define CHECK_LEN(op, exp) if((op) != (exp)) IERR

RayAnimationHandler AnimationSet_LoadAnimations(const char* animset, Texture2D image) {
    FILE *file = fopen(animset, "rb");
    if (!file) Log_FatalF("could not open file %s", animset);
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fileSize < (long)sizeof(int32_t)) IERR;
    uint8_t *buffer = malloc(fileSize);
    if (!buffer) IERR;
    CHECK_LEN(fread(buffer, 1, fileSize, file), (size_t)fileSize);
    fclose(file);
    uint8_t *ptr = buffer;
    uint8_t *end = buffer + fileSize;
#define READ(var) do { if (ptr + sizeof(var) > end) IERR; memcpy(&(var), ptr, sizeof(var)); ptr += sizeof(var); } while(0)
#define READ_BYTES(dst, len) do { if (ptr + (len) > end) IERR; memcpy((dst), ptr, (len)); ptr += (len); } while(0)
    int32_t animationCount; READ(animationCount);
    if(animationCount < 1 || animationCount > 8192) IERR;
    uint8_t *p = ptr; size_t totalFrames = 0;
    for(int i = 0; i < animationCount; i++) {
        if(p + sizeof(int32_t) > end) IERR;
        int32_t nameLength; memcpy(&nameLength, p, sizeof(int32_t)); p += sizeof(int32_t);
        if(nameLength < 1 || nameLength > 31) IERR;
        p += nameLength;
        if(p + sizeof(int32_t) > end) IERR;
        int32_t dataCount; memcpy(&dataCount, p, sizeof(int32_t)); p += sizeof(int32_t);
        if(dataCount % 6 != 0) IERR;
        int32_t frameCount = dataCount / 6;
        if(frameCount < 1) IERR;
        totalFrames += frameCount;
        p += (size_t)frameCount * sizeof(Frame);
        if(p > end) IERR;
    }
    RayAnimation* animations = malloc(sizeof(RayAnimation) * animationCount);
    Frame* frameArena = malloc(sizeof(Frame) * totalFrames);
    if(!animations || !frameArena) IERR;
    Frame* arenaPtr = frameArena;
    for(int i = 0; i < animationCount; i++) {
        int32_t nameLength; READ(nameLength);
        RayAnimation* anim = &animations[i];
        READ_BYTES(anim->name, nameLength);
        anim->name[nameLength] = '\0';
        int32_t dataCount; READ(dataCount);
        int32_t frameCount = dataCount / 6;
        anim->frameCount = frameCount;
        anim->frames = arenaPtr;
        anim->animationOffset = (Vector2){0,0};
        anim->fps = 1; anim->looped = 0; anim->frameTime = 1.0f;
        READ_BYTES(arenaPtr, sizeof(Frame) * frameCount);
        for(int j = 0; j < frameCount; j++) if(arenaPtr[j].w < 0 || arenaPtr[j].h < 0) IERR;
        arenaPtr += frameCount;
    }
    free(buffer);
    return (RayAnimationHandler){ .texture = image, .animations = animations, .frameArena = frameArena, .animationCount = animationCount };
#undef READ
#undef READ_BYTES
}

int AnimationSet_FindAnimation(const RayAnimationHandler * restrict animations, const char* animationName) {
    for(int i = 0; i < animations->animationCount; i++) if(strcmp(animations->animations[i].name, animationName) == 0) return i;
    return -1;
}
void AnimationSet_FreeAnimations(RayAnimationHandler * restrict animations) {
    free(animations->frameArena); free(animations->animations);
    animations->frameArena = NULL; animations->animations = NULL;
}
void AnimationSet_FreeAll(RayAnimationHandler * restrict animations) {
    AnimationSet_FreeAnimations(animations);
    UnloadTexture(animations->texture);
}