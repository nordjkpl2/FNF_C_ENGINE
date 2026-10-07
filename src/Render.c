#include "Render.h"
#include "Game.h"

static Vector2 zero = {0, 0};
static Camera2D* targetCamera = NULL;

// DO NOT CHANGE THE ZOOM WHILE RENDERING THE CAMERA
void Render_SetCamera(Camera2D* camera) {
    targetCamera = camera; 
    
    targetCamera->zoom *= RayGame_ZoomFactor();
    targetCamera->offset = (Vector2) {RayGame_WindowWidth() / 2, RayGame_WindowHeight() / 2};

    BeginMode2D(*camera);  
}

void Render_StopCamera() {
    if(targetCamera != NULL) {
        targetCamera->zoom /= RayGame_ZoomFactor();
        targetCamera->offset = zero;
        targetCamera = NULL;
    }
    EndMode2D();
}

void Render_DefaultCamera(Camera2D* cam) {
    cam->zoom = 1;
    cam->target = (Vector2) {1280 / 2, 720 / 2};
}

void Render_DrawGraphicObject(RayGraphicObject* obj) {
    Vector2 scrollOffset = zero;
    if(targetCamera != NULL) {
        scrollOffset.x = (1 - obj->scrollFactor.x) * targetCamera->target.x;
        scrollOffset.y = (1 - obj->scrollFactor.y) * targetCamera->target.y;
    }  

    if(obj->objType == RGT_RECTANGLE) {
        Rectangle rect = {.x = scrollOffset.x + obj->position.x, .y = scrollOffset.y + obj->position.y, .width = obj->rectSize.x * obj->scaleX, .height = obj->rectSize.y  * obj->scaleY};
        DrawRectanglePro(rect, zero, obj->rotation, obj->color);
    }
    else if(obj->objType == RGT_CIRCLE)
        DrawCircleV((Vector2) {obj->position.x + scrollOffset.x, obj->position.y + scrollOffset.y}, obj->radius * obj->scaleX * obj->scaleY /* not sure about where to use the scale stuff here!!!!!! */, obj->color);
    else if(obj->objType == RGT_IMAGE) {
        DrawTexturePro(obj->image, 
            (Rectangle) {
                0, 0,
                obj->image.width, obj->image.height
            }, 
            (Rectangle) {
                obj->position.x + scrollOffset.x, obj->position.y + scrollOffset.y, 
                obj->image.width * obj->scaleX, obj->image.height * obj->scaleY
            }, zero, obj->rotation, obj->color
        ); 
    }
}

void Render_DrawAnimatedObject(RayAnimatedObject* obj) {
    if(!obj->currentAnim.isValid)
        return;

    Vector2 scrollOffset = zero;
    if(targetCamera != NULL) {
        scrollOffset.x = (1 - obj->scrollFactor.x) * targetCamera->target.x;
        scrollOffset.y = (1 - obj->scrollFactor.y) * targetCamera->target.y;
    } 
    
    RayAnimation* currentAnimation = obj->currentAnim.animationIndex + obj->animationSet.animations;
    Frame* currentFrame = obj->currentAnim.currentFrame + currentAnimation->frames;
    
    Rectangle source = {.x = currentFrame->x, .y = currentFrame->y, .width = currentFrame->w, .height = currentFrame->h};
    Rectangle dest   = {.x = scrollOffset.x + (-currentAnimation->animationOffset.x - currentFrame->fx) * obj->scaleX + obj->position.x, .y = scrollOffset.y + (-currentAnimation->animationOffset.y - currentFrame->fy) * obj->scaleY + obj->position.y, .width = currentFrame->w * obj->scaleX, .height = currentFrame->h * obj->scaleY};

    DrawTexturePro(obj->animationSet.texture, source, dest, zero, obj->rotation, obj->color);
}

Vector2 GraphicObject_Sizes(RayGraphicObject* obj) {
    Vector2 basic = zero;
    
    if(obj->objType == RGT_RECTANGLE)
        basic = obj->rectSize;
    else if(obj->objType == RGT_CIRCLE)
        basic.x = basic.y = obj->radius * 2;
    else if(obj->objType == RGT_IMAGE) {
        basic.x = obj->image.width;
        basic.y = obj->image.height;
    }
    
    return (Vector2) { basic.x * obj->scaleX, basic.y * obj->scaleY };
}

void AnimatedObject_UpdateFrame(RayAnimatedObject* obj) {
    if(!obj->currentAnim.isValid)
        return; 

    RayAnimation* currentAnimation = obj->currentAnim.animationIndex + obj->animationSet.animations;
    obj->currentAnim.time += RayGame_DeltaTime();

    float frameTime = 1.0f / (float) currentAnimation->fps; 
    while(obj->currentAnim.time >= frameTime) {
        if(obj->currentAnim.currentFrame == currentAnimation->frameCount - 1) {
            if(currentAnimation->looped) 
                obj->currentAnim.currentFrame = 0;
        }
        else 
            obj->currentAnim.currentFrame++;

        obj->currentAnim.time -= frameTime;
    }
}

char AnimatedObject_FinishedAnimation(RayAnimatedObject* obj) {
    if(!obj->currentAnim.isValid)
        return 0;
        
    RayAnimation* currentAnimation = obj->currentAnim.animationIndex + obj->animationSet.animations;
    return obj->currentAnim.currentFrame == currentAnimation->frameCount - 1;
}

Vector2 AnimatedObject_Sizes(RayAnimatedObject* obj) {
    if(!obj->currentAnim.isValid)
        return zero;
    
    RayAnimation* currentAnimation = obj->currentAnim.animationIndex + obj->animationSet.animations;
    Frame* currentFrame = obj->currentAnim.currentFrame + currentAnimation->frames;

    return (Vector2) {
        .x = currentFrame->w * obj->scaleX,
        .y = currentFrame->h * obj->scaleY
    };
}

void AnimatedObject_SetAnimation(RayAnimatedObject* obj, int animation) {
    obj->currentAnim = (RayAnimationState) {
        .isValid = animation >= 0 && animation < obj->animationSet.animationCount,
        .animationIndex = animation,
        .currentFrame = 0,
        .time = 0
    };
}

RayGraphicObject* Render_NewRayGraphicObjects(int count) { 
    RayGraphicObject* arr = calloc(count, sizeof(RayGraphicObject));
    return arr;
}

Texture2D Render_LoadTexture(const char* path) { 
    Image img = LoadImage(path); 
    
    Texture2D txt = LoadTextureFromImage(img); 
    SetTextureFilter(txt, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(txt, TEXTURE_WRAP_CLAMP); 
    UnloadImage(img);

    return txt;
}

void Render_DefaultRGT(RayGraphicObject* obj) {
    obj->position = zero; 
    obj->rotation = 0;
    obj->scaleX = 1;
    obj->scaleY = 1;
    obj->objType = RGT_RECTANGLE;
    obj->scrollFactor = (Vector2) {1, 1};
    obj->color = WHITE;
}

void Render_DefaultAnimated(RayAnimatedObject* obj) {
    obj->position = zero;
    obj->scaleX = 1;
    obj->scaleY = 1;
    obj->rotation = 0; 
    obj->color = WHITE;
    obj->scrollFactor = (Vector2) {1, 1};
    obj->currentAnim.isValid = 0;
}