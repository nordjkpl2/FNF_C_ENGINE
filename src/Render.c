/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#include "Render.h"
#include "Game.h"
/* //* Definições de Animação e Renderização, como padrão atualização etc, OUTD day18/10/2026*/

static const Vector2 VEC_ZERO = {0,0};
static Camera2D* s_target = NULL;
static float s_savedZoom = 1.0f; // nunca faz /=

// --- 1. CAMERA (cold path, pode ficar no .c) ---
void Render_SetCamera(Camera2D* camera) {
    s_target = camera;
    s_savedZoom = camera->zoom;
    camera->zoom *= RayGame_ZoomFactor();
    camera->offset = (Vector2){RayGame_CanvasWidth() * 0.5f, RayGame_CanvasHeight() * 0.5f};
    BeginMode2D(*camera);
}

void Render_StopCamera(void) {
    if (__builtin_expect(s_target != NULL, 0)) {
        s_target->zoom = s_savedZoom; // restaura sem divisão
        s_target->offset = VEC_ZERO;
        s_target = NULL;
    }
    EndMode2D();
}

void Render_DefaultCamera(Camera2D* cam) {
    cam->zoom = 1.0f;
    cam->target = (Vector2){640.0f, 360.0f}; // 1280/2 já pré-calculado
}

Texture2D Render_LoadTexture(const char* path) {
    Texture2D txt = LoadTexture(path); // 1 cópia só, sem Image intermediária
    SetTextureFilter(txt, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(txt, TEXTURE_WRAP_CLAMP);
    return txt;
}

// --- 2. HOT PATH: tem que ser inline ---
static inline Vector2 Render_GetScrollOffset(const Vector2 scrollFactor) {
    if (__builtin_expect(s_target == NULL, 0)) return VEC_ZERO;
    // branchless: (1 - f) * target
    return (Vector2){
        (1.0f - scrollFactor.x) * s_target->target.x,
        (1.0f - scrollFactor.y) * s_target->target.y
    };
}

void Render_DrawGraphicObject(const RayGraphicObject* restrict obj) {
    Vector2 off = Render_GetScrollOffset(obj->scrollFactor);
    
    switch (obj->objType) {
        case RGT_RECTANGLE: {
            Rectangle r = {off.x + obj->position.x, off.y + obj->position.y, obj->rectSize.x * obj->scaleX, obj->rectSize.y * obj->scaleY};
            DrawRectanglePro(r, VEC_ZERO, obj->rotation, obj->color);
        } break;
        case RGT_CIRCLE: {
            Vector2 p = {obj->position.x + off.x, obj->position.y + off.y};
            DrawCircleV(p, obj->radius * obj->scaleX, obj->color); // scaleY não escala raio, era bug seu
        } break;
        case RGT_IMAGE: {
            Rectangle src = {0, 0, (float)obj->image.width, (float)obj->image.height};
            Rectangle dst = {obj->position.x + off.x, obj->position.y + off.y, (float)obj->image.width * obj->scaleX, (float)obj->image.height * obj->scaleY};
            DrawTexturePro(obj->image, src, dst, VEC_ZERO, obj->rotation, obj->color);
        } break;
    }
}

void Render_DrawAnimatedObject(const RayAnimatedObject* restrict obj) {
    if (__builtin_expect(!obj->currentAnim.isValid, 0)) return;

    const RayAnimation* anim = &obj->animationSet.animations[obj->currentAnim.animationIndex];
    const Frame* f = &anim->frames[obj->currentAnim.currentFrame];
    Vector2 off = Render_GetScrollOffset(obj->scrollFactor);
    
    Rectangle src = {(float)f->x, (float)f->y, (float)f->w, (float)f->h};
    Rectangle dst = {
        off.x + obj->position.x + (-anim->animationOffset.x - f->fx) * obj->scaleX,
        off.y + obj->position.y + (-anim->animationOffset.y - f->fy) * obj->scaleY,
        (float)f->w * obj->scaleX, (float)f->h * obj->scaleY
    };
    DrawTexturePro(obj->animationSet.texture, src, dst, VEC_ZERO, obj->rotation, obj->color);
}

Vector2 GraphicObject_Sizes(const RayGraphicObject* restrict obj) {
    Vector2 s;
    switch(obj->objType) {
        case RGT_RECTANGLE: s = obj->rectSize; break;
        case RGT_CIRCLE: s.x = s.y = obj->radius * 2.0f; break;
        default: s = (Vector2){(float)obj->image.width, (float)obj->image.height}; break;
    }
    s.x *= obj->scaleX; s.y *= obj->scaleY;
    return s;
}

Vector2 AnimatedObject_Sizes(const RayAnimatedObject* restrict obj) {
    if (__builtin_expect(!obj->currentAnim.isValid, 0)) return VEC_ZERO;
    const Frame* f = &obj->animationSet.animations[obj->currentAnim.animationIndex].frames[obj->currentAnim.currentFrame];
    return (Vector2){f->w * obj->scaleX, f->h * obj->scaleY};
}

// --- 3. ANIMAÇÃO ---
void AnimatedObject_UpdateFrame(RayAnimatedObject* restrict obj) {
    if (__builtin_expect(!obj->currentAnim.isValid, 0)) return;
    RayAnimation* anim = &obj->animationSet.animations[obj->currentAnim.animationIndex];
    obj->currentAnim.time += RayGame_DeltaTime();

    float frameTime = anim->frameTime; // PRÉ-CALCULA no load: frameTime = 1.0f / fps
    while (__builtin_expect(obj->currentAnim.time >= frameTime, 0)) {
        obj->currentAnim.time -= frameTime;
        if (__builtin_expect(obj->currentAnim.currentFrame == anim->frameCount - 1, 0)) {
            if (anim->looped) obj->currentAnim.currentFrame = 0;
            else break; // não-looped trava no último
        } else {
            obj->currentAnim.currentFrame++;
        }
    }
}

char AnimatedObject_FinishedAnimation(const RayAnimatedObject* restrict obj) {
    if (__builtin_expect(!obj->currentAnim.isValid, 0)) return 0;
    const RayAnimation* anim = &obj->animationSet.animations[obj->currentAnim.animationIndex];
    return obj->currentAnim.currentFrame == anim->frameCount - 1;
}

void AnimatedObject_SetAnimation(RayAnimatedObject* restrict obj, int idx) {
    obj->currentAnim = (RayAnimationState){
        .isValid = (unsigned)idx < (unsigned)obj->animationSet.animationCount,
        .animationIndex = idx, .currentFrame = 0, .time = 0
    };
}