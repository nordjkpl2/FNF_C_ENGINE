/*
 * Friday Night Funkin' C Engine
 * (c) 2026 nordjkpl
 *
 * Render.c - Optimized Rendering & Animation
 */

#include "Render.h"
#include "Game.h"

#include <math.h>
#include <stdbool.h>

/* =========================================================
   CONSTANTS / CAMERA STATE
   ========================================================= */

#define RENDER_EXPECT(x, value) __builtin_expect(!!(x), (value))

static const Vector2 VEC_ZERO = {0.0f, 0.0f};

static Camera2D *s_target = NULL;
static float s_savedZoom = 1.0f;
static Vector2 s_savedOffset = {0.0f, 0.0f};

/* =========================================================
   CAMERA
   ========================================================= */

void Render_SetCamera(Camera2D *camera)
{
    if (camera == NULL)
        return;

    /* Evita deixar um modo 2D anterior aberto. */
    if (s_target != NULL)
        Render_StopCamera();

    s_target = camera;

    s_savedZoom = camera->zoom;
    s_savedOffset = camera->offset;

    camera->zoom = s_savedZoom * RayGame_ZoomFactor();
    camera->offset = (Vector2){
        RayGame_CanvasWidth() * 0.5f,
        RayGame_CanvasHeight() * 0.5f
    };

    BeginMode2D(*camera);
}

void Render_StopCamera(void)
{
    Camera2D *camera = s_target;

    if (RENDER_EXPECT(camera != NULL, 0))
    {
        camera->zoom = s_savedZoom;
        camera->offset = s_savedOffset;
        s_target = NULL;

        EndMode2D();
    }
}

void Render_DefaultCamera(Camera2D *cam)
{
    if (cam == NULL)
        return;

    cam->zoom = 1.0f;
    cam->target = (Vector2){640.0f, 360.0f};
    cam->offset = VEC_ZERO;
    cam->rotation = 0.0f;
}

/* =========================================================
   TEXTURES
   ========================================================= */

Texture2D Render_LoadTexture(const char *path)
{
    Texture2D texture = LoadTexture(path);

    if (texture.id != 0)
    {
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
    }

    return texture;
}

/* =========================================================
   SCROLL OFFSET - HOT PATH
   ========================================================= */

static inline Vector2 Render_GetScrollOffset(const Vector2 scrollFactor)
{
    const Camera2D *camera = s_target;

    if (RENDER_EXPECT(camera == NULL, 0))
        return VEC_ZERO;

    const Vector2 target = camera->target;

    return (Vector2){
        (1.0f - scrollFactor.x) * target.x,
        (1.0f - scrollFactor.y) * target.y
    };
}

/* =========================================================
   GRAPHIC OBJECT RENDERING
   ========================================================= */

void Render_DrawGraphicObject(const RayGraphicObject *restrict obj)
{
    const Vector2 offset = Render_GetScrollOffset(obj->scrollFactor);
    const float x = obj->position.x + offset.x;
    const float y = obj->position.y + offset.y;

    switch (obj->objType)
    {
        case RGT_RECTANGLE:
        {
            const Rectangle rect = {
                x,
                y,
                obj->rectSize.x * obj->scaleX,
                obj->rectSize.y * obj->scaleY
            };

            DrawRectanglePro(rect, VEC_ZERO, obj->rotation, obj->color);
        }
        break;

        case RGT_CIRCLE:
        {
            DrawCircleV(
                (Vector2){x, y},
                obj->radius * obj->scaleX,
                obj->color
            );
        }
        break;

        case RGT_IMAGE:
        {
            const Texture2D texture = obj->image;

            const Rectangle source = {
                0.0f,
                0.0f,
                (float)texture.width,
                (float)texture.height
            };

            const Rectangle destination = {
                x,
                y,
                (float)texture.width * obj->scaleX,
                (float)texture.height * obj->scaleY
            };

            DrawTexturePro(
                texture,
                source,
                destination,
                VEC_ZERO,
                obj->rotation,
                obj->color
            );
        }
        break;

        default:
            break;
    }
}

/* =========================================================
   ANIMATED OBJECT RENDERING
   ========================================================= */

void Render_DrawAnimatedObject(const RayAnimatedObject *restrict obj)
{
    if (RENDER_EXPECT(!obj->currentAnim.isValid, 0))
        return;

    const RayAnimation *anim =
        &obj->animationSet.animations[obj->currentAnim.animationIndex];

    if (anim->frames == NULL || anim->frameCount <= 0 ||
        obj->currentAnim.currentFrame >= (unsigned)anim->frameCount)
        return;

    const Frame *frame = &anim->frames[obj->currentAnim.currentFrame];
    const Vector2 offset = Render_GetScrollOffset(obj->scrollFactor);

    const float scaleX = obj->scaleX;
    const float scaleY = obj->scaleY;

    const Rectangle source = {
        (float)frame->x,
        (float)frame->y,
        (float)frame->w,
        (float)frame->h
    };

    const Rectangle destination = {
        obj->position.x + offset.x +
            (-anim->animationOffset.x - frame->fx) * scaleX,
        obj->position.y + offset.y +
            (-anim->animationOffset.y - frame->fy) * scaleY,
        (float)frame->w * scaleX,
        (float)frame->h * scaleY
    };

    DrawTexturePro(
        obj->animationSet.texture,
        source,
        destination,
        VEC_ZERO,
        obj->rotation,
        obj->color
    );
}

/* =========================================================
   OBJECT SIZES
   ========================================================= */

Vector2 GraphicObject_Sizes(const RayGraphicObject *restrict obj)
{
    Vector2 size;

    switch (obj->objType)
    {
        case RGT_RECTANGLE:
            size = obj->rectSize;
            break;

        case RGT_CIRCLE:
            size.x = size.y = obj->radius * 2.0f;
            break;

        case RGT_IMAGE:
        default:
            size.x = (float)obj->image.width;
            size.y = (float)obj->image.height;
            break;
    }

    size.x *= obj->scaleX;
    size.y *= obj->scaleY;

    return size;
}

Vector2 AnimatedObject_Sizes(const RayAnimatedObject *restrict obj)
{
    if (RENDER_EXPECT(!obj->currentAnim.isValid, 0))
        return VEC_ZERO;

    const RayAnimation *anim =
        &obj->animationSet.animations[obj->currentAnim.animationIndex];

    if (anim->frames == NULL || anim->frameCount <= 0 ||
        obj->currentAnim.currentFrame >= (unsigned)anim->frameCount)
        return VEC_ZERO;

    const Frame *frame = &anim->frames[obj->currentAnim.currentFrame];

    return (Vector2){
        (float)frame->w * obj->scaleX,
        (float)frame->h * obj->scaleY
    };
}

/* =========================================================
   ANIMATION UPDATE
   ========================================================= */

void AnimatedObject_UpdateFrame(RayAnimatedObject *restrict obj)
{
    if (RENDER_EXPECT(!obj->currentAnim.isValid, 0))
        return;

    RayAnimation *anim =
        &obj->animationSet.animations[obj->currentAnim.animationIndex];

    const unsigned frameCount = (anim->frameCount > 0) ?
                               (unsigned)anim->frameCount : 0u;
    const float frameTime = anim->frameTime;
    const float deltaTime = RayGame_DeltaTime();

    if (frameCount == 0u || frameTime <= 0.0f || deltaTime <= 0.0f ||
        !isfinite(frameTime) || !isfinite(deltaTime) || anim->frames == NULL)
        return;

    unsigned current = obj->currentAnim.currentFrame;

    if (current >= frameCount)
    {
        obj->currentAnim.currentFrame = 0;
        obj->currentAnim.time = 0.0f;
        return;
    }

    /* Animação não-loopada já terminou: mantém o último frame. */
    if (!anim->looped && current == frameCount - 1u)
    {
        obj->currentAnim.time = 0.0f;
        return;
    }

    const float elapsed = obj->currentAnim.time + deltaTime;

    if (!isfinite(elapsed))
    {
        obj->currentAnim.time = 0.0f;
        return;
    }

    if (elapsed < frameTime)
    {
        obj->currentAnim.time = elapsed;
        return;
    }

    /* Avança vários frames sem um while por frame perdido. */
    const float steps = floorf(elapsed / frameTime);
    const float remainder = fmodf(elapsed, frameTime);

    if (anim->looped)
    {
        const unsigned advance =
            (unsigned)fmodf(steps, (float)frameCount);

        obj->currentAnim.currentFrame =
            (current >= frameCount - advance)
                ? current - (frameCount - advance)
                : current + advance;
        obj->currentAnim.time = remainder;
    }
    else
    {
        const unsigned remaining = (frameCount - 1u) - current;

        if (steps >= (float)remaining)
        {
            obj->currentAnim.currentFrame = frameCount - 1u;
            obj->currentAnim.time = 0.0f;
        }
        else
        {
            obj->currentAnim.currentFrame = current + (unsigned)steps;
            obj->currentAnim.time = remainder;
        }
    }
}

/* =========================================================
   ANIMATION STATE
   ========================================================= */

char AnimatedObject_FinishedAnimation(const RayAnimatedObject *restrict obj)
{
    if (RENDER_EXPECT(!obj->currentAnim.isValid, 0))
        return 0;

    const RayAnimation *anim =
        &obj->animationSet.animations[obj->currentAnim.animationIndex];

    if (!(anim->frameCount > 0))
        return 0;

    return obj->currentAnim.currentFrame >=
           (unsigned)(anim->frameCount - 1);
}

void AnimatedObject_SetAnimation(RayAnimatedObject *restrict obj, int idx)
{
    const bool valid =
        idx >= 0 &&
        idx < obj->animationSet.animationCount &&
        obj->animationSet.animations[idx].frameCount > 0 &&
        obj->animationSet.animations[idx].frames != NULL;

    obj->currentAnim = (RayAnimationState){
        .isValid = valid,
        .animationIndex = valid ? idx : 0,
        .currentFrame = 0,
        .time = 0.0f
    };
}
