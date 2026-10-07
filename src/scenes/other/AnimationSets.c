#include "scenes/AllScenes.h" 
#include <limits.h>

#define IERR {puts("\ninvalid data");exit(1);}
#define CHECK_LEN(readOperation, expectedSize) if(readOperation != expectedSize) IERR

RayAnimationHandler AnimationSet_LoadAnimations(const char* animset, Texture2D image) {  
    FILE *fileptr = fopen(animset, "rb");

    if(fileptr == NULL) {
        printf("could not open file %s\n", animset);
        exit(1);
    } 

    int animationCount;
    CHECK_LEN(fread(&animationCount, sizeof(int), 1, fileptr), 1) 
    if(animationCount < 1 || animationCount > SIZE_MAX / sizeof(RayAnimation) /* overflow */) IERR


    RayAnimation* animations = malloc(sizeof(RayAnimation) * animationCount);

    for(int i = 0; i < animationCount; i++) {
        int nameLength;
        CHECK_LEN(fread(&nameLength, sizeof(int), 1, fileptr), 1)
        if(nameLength < 1 || nameLength > 31) IERR
 
        RayAnimation* anim = animations + i; 
        CHECK_LEN(fread(anim->name, sizeof(char), nameLength, fileptr), nameLength)
        anim->name[nameLength] = 0;
 
        int dataCount;
        CHECK_LEN(fread(&dataCount, sizeof(int), 1, fileptr), 1)

        int frameCount = dataCount / 6;
        if(frameCount < 1 || frameCount > SIZE_MAX / sizeof(Frame) /* overflow */) IERR

        anim->animationOffset = (Vector2) {0, 0};
        anim->fps = 1;
        anim->looped = 0;
        anim->frameCount = frameCount;
        anim->frames = malloc(sizeof(Frame) * frameCount);  

        for(int j = 0; j < frameCount; j++) {
            Frame* frame = anim->frames + j;
            CHECK_LEN(fread(frame, sizeof(Frame), 1, fileptr), 1);
            if(frame->w < 0 || frame->h < 0) IERR  
        }
    }

    fclose(fileptr);

    return (RayAnimationHandler) {
        .texture = image,
        .animations = animations,
        .animationCount = animationCount
    };
}

float AnimationSet_AnimationLength(RayAnimationHandler* animations, int animIndex) {
    RayAnimation* animation = animations->animations + animIndex;
    return (1.0f / animation->fps) * animation->frameCount;
}

void AnimationSet_SetAnimationData(RayAnimationHandler* animations, int animationIndex, int fps, char looped) {
    RayAnimation* animation = animations->animations + animationIndex;
    animation->fps = fps;
    animation->looped = looped;
}

void AnimationSet_SetAnimationOffset(RayAnimationHandler* animations, int animationIndex, Vector2 offset) {
    RayAnimation* animation = animations->animations + animationIndex;
    animation->animationOffset = offset;
}

int AnimationSet_FindAnimation(RayAnimationHandler* animations, const char* animationName) {
    for(int i = 0; i < animations->animationCount; i++) {
        RayAnimation* anim = animations->animations + i;
        if(strcmp(anim->name, animationName) == 0)
            return i;
    }
    return -1;
}

void AnimationSet_FreeAnimations(RayAnimationHandler* animations) {
    for(int i = 0; i < animations->animationCount; i++) {
        RayAnimation *anim = animations->animations + i;
        free(anim->frames);
    }
    free(animations->animations); 
}

void AnimationSet_FreeAll(RayAnimationHandler* animations) {
    AnimationSet_FreeAnimations(animations);
    UnloadTexture(animations->texture);
}