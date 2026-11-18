#include "field/field_flash_darkness.h"

#include "global.h"

#include "constants/field/field_effect_renderer.h"

#include "field/field_effect_helpers.h"
#include "field/field_effect_manager.h"

#include "camera.h"
#include "field_effects.h"
#include "field_system.h"
#include "gf_3d_render.h"
#include "map_object.h"

// The darkness of a cave that needs Flash: a model with a lit hole in the middle, kept a third of the way from the
// player to the camera. Using Flash scales it up from 1x to 4x, widening the hole.

#define FLASH_DARKNESS_MODEL_FILE 0x6A

fx32 VEC_Distance(const VecFx32 *a, const VecFx32 *b);

struct FlashDarkness {
    FieldEffectManager *manager;
    NNSG3dRenderObj renderObj;
    NNSG3dResMdl *model;
    NNSG3dResFileHeader *resource;
    FieldEffect *effect;
    float scale;
};

typedef struct FlashDarknessEffectArgs {
    FieldEffectManager *manager;
    FlashDarkness *darkness;
    LocalMapObject *mapObject;
    fx32 distance;
    VecFx32 direction;
} FlashDarknessEffectArgs;

typedef struct FlashDarknessEffect {
    int param;
    FlashDarknessEffectArgs args;
} FlashDarknessEffect;

static void FlashDarkness_LoadModel(FlashDarkness *darkness);
static void FlashDarkness_FreeModel(FlashDarkness *darkness);
static BOOL FlashDarknessEffect_Init(FieldEffect *effect, void *state);
static void FlashDarknessEffect_Destroy(FieldEffect *effect, void *state);
static void FlashDarknessEffect_Update(FieldEffect *effect, void *state);
static void FlashDarknessEffect_Draw(FieldEffect *effect, void *state);
#ifdef SDK_PORT
static void FlashDarkness_PortDraw(FlashDarknessEffect *work, const VecFx32 *position, const MtxFx33 *rotation, const VecFx32 *scale);
#endif

static const FieldEffectBasicTemplate sFlashDarknessTemplate = {
    sizeof(FlashDarknessEffect),
    FlashDarknessEffect_Init,
    FlashDarknessEffect_Destroy,
    FlashDarknessEffect_Update,
    FlashDarknessEffect_Draw,
};

FlashDarkness *FlashDarkness_New(FieldEffectManager *manager) {
    FlashDarkness *darkness = FieldEffectManager_HeapAllocAndFill(manager, sizeof(FlashDarkness), FALSE, 0);
    darkness->manager = manager;
    darkness->effect = NULL;
    darkness->scale = 1.0f;
    FlashDarkness_LoadModel(darkness);
    return darkness;
}

void FlashDarkness_Free(FlashDarkness *darkness) {
    FlashDarkness_FreeModel(darkness);
    FieldEffectManager_HeapFree(darkness);
}

static void FlashDarkness_LoadModel(FlashDarkness *darkness) {
    FieldEffectManager_LoadModel(darkness->manager, FLASH_DARKNESS_MODEL_FILE, FALSE, &darkness->renderObj, &darkness->model, &darkness->resource);
}

static void FlashDarkness_FreeModel(FlashDarkness *darkness) {
    FieldEffectManager_HeapFree(darkness->resource);
}

// Creates the darkness around the map object (the player), or returns the one already shown.
FieldEffect *FlashDarkness_Show(LocalMapObject *mapObject) {
    FlashDarknessEffectArgs args;
    VecFx32 position;
    VecFx32 objectPos;
    VecFx32 cameraPos;
    FieldEffectManager *manager = FieldEffectManager_GetFromMapObject(mapObject);
    FieldSystem *fieldSystem = FieldEffectManager_GetFieldSystem(manager);

    args.manager = manager;
    args.darkness = FieldEffectManager_GetRendererData(manager, FIELD_EFFECT_RENDERER_FLASH_DARKNESS);
    if (args.darkness->effect != NULL) {
        return args.darkness->effect;
    }
    args.mapObject = mapObject;
    MapObject_CopyPositionVector(mapObject, &objectPos);
    cameraPos = Camera_GetLookAtCamPos(fieldSystem->camera);
    VEC_Subtract(&cameraPos, &objectPos, &args.direction);
    args.distance = VEC_Distance(&cameraPos, &objectPos);
    args.distance /= 3;
    VEC_Normalize(&args.direction, &args.direction);
    MapObject_CopyPositionVector(mapObject, &position);
    args.darkness->effect = FieldEffectManager_CreateEffect(manager, &sFlashDarknessTemplate, &position, 0, &args, MapObject_GetPriorityPlusValue(mapObject, 2));
    return args.darkness->effect;
}

void FlashDarkness_SetScale(FieldEffect *effect, float scale) {
    FlashDarknessEffect *work = FieldEffect_GetState(effect);
    work->args.darkness->scale = scale;
}

static BOOL FlashDarknessEffect_Init(FieldEffect *effect, void *state) {
    FlashDarknessEffect *work = state;
    FlashDarknessEffectArgs *args = FieldEffect_GetUserData(effect);
    work->args = *args;
    work->param = FieldEffect_GetParam(effect);
    return TRUE;
}

static void FlashDarknessEffect_Destroy(FieldEffect *effect, void *state) {
}

static void FlashDarknessEffect_Update(FieldEffect *effect, void *state) {
    FlashDarknessEffect *work = state;
    VecFx32 objectPos;
    VecFx32 position;
    MapObject_CopyPositionVector(work->args.mapObject, &objectPos);
    VEC_MultAdd(work->args.distance, &work->args.direction, &objectPos, &position);
    FieldEffect_SetPosition(effect, &position);
}

static void FlashDarknessEffect_Draw(FieldEffect *effect, void *state) {
    FlashDarknessEffect *work = state;
    static const MtxFx33 sIdentity = {
        FX32_ONE, 0, 0, 0, FX32_ONE, 0, 0, 0, FX32_ONE
    };
    VecFx32 position;
    VecFx32 scale;
    MtxFx33 rotation;

    rotation = sIdentity;
    scale.x = (fx32)(FX32_ONE * work->args.darkness->scale);
    scale.y = (fx32)(FX32_ONE * work->args.darkness->scale);
    scale.z = (fx32)(FX32_ONE * work->args.darkness->scale);
    FieldEffect_GetPosition(effect, &position);
#ifdef SDK_PORT
    FlashDarkness_PortDraw(work, &position, &rotation, &scale);
#else
    GF3dRender_DrawModel(&work->args.darkness->renderObj, &position, &rotation, &scale);
#endif
}

#ifdef SDK_PORT
// The port: the DS blacks out everything outside the darkness with fog (WeatherTask_Flash), which the port's renderer
// can't do as the DS does (weather_manager.c's FLASH_FOG). Instead the model is drawn as it is, and around it, in its
// plane, an opaque black frame reaching far past the screen's edges (a widescreen's too). Both are moved towards the
// camera, scaled down as much, so that they cover the same part of the screen with nothing of the map in front.
static void FlashDarkness_PortDraw(FlashDarknessEffect *work, const VecFx32 *position, const MtxFx33 *rotation, const VecFx32 *scale) {
    FieldSystem *fieldSystem = FieldEffectManager_GetFieldSystem(work->args.manager);
    VecFx32 cameraPos = Camera_GetLookAtCamPos(fieldSystem->camera);
    VecFx32 toModel;
    VEC_Subtract(position, &cameraPos, &toModel);
    fx32 distance = VEC_Mag(&toModel);
    // A little past the near plane: closer to the camera than any of the map it sees
    fx32 wanted = FX_Mul(Camera_GetPerspectiveClippingPlaneNear(fieldSystem->camera), FX32_CONST(1.25));
    fx32 ratio = distance > wanted ? FX_Div(wanted, distance) : FX32_ONE;
    VecFx32 nearPos;
    VEC_MultAdd(ratio, &toModel, &cameraPos, &nearPos);
    VecFx32 nearScale = { FX_Mul(scale->x, ratio), FX_Mul(scale->y, ratio), FX_Mul(scale->z, ratio) };
    GF3dRender_DrawModel(&work->args.darkness->renderObj, &nearPos, rotation, &nearScale);

    // The frame: the model's box (x and y; it is flat), a tenth of its width inside it at the sides and the top (the
    // model's dark edges don't quite fill it there; its lit middle reaches its bottom edge), and 2048 units past it on
    // every side. The vertices are fx16 (under 8), so they are in units of 512, the base scale 512 times the model's
    const NNSG3dResMdlInfo *info = NNS_G3dGetMdlInfo(work->args.darkness->model);
    const fx32 width = FX_Mul((fx32)info->boxW, info->boxPosScale) / 512;
    const fx32 inset = width / 10;
    const fx32 left = FX_Mul((fx32)info->boxX, info->boxPosScale) / 512 + inset;
    const fx32 bottom = FX_Mul((fx32)info->boxY, info->boxPosScale) / 512;
    const fx32 right = left + width - 2 * inset;
    const fx32 top = bottom + FX_Mul((fx32)info->boxH, info->boxPosScale) / 512 - inset;
    const fx32 outer = FX32_CONST(4);
    const fx16 xs[4] = { (fx16)-outer, (fx16)left, (fx16)right, (fx16)outer };
    const fx16 ys[4] = { (fx16)-outer, (fx16)bottom, (fx16)top, (fx16)outer };
    VecFx32 frameScale = { nearScale.x * 512, nearScale.y * 512, nearScale.z * 512 };
    NNS_G3dGlbSetBaseTrans(&nearPos);
    NNS_G3dGlbSetBaseRot(rotation);
    NNS_G3dGlbSetBaseScale(&frameScale);
    NNS_G3dGlbFlushP();
    G3_TexImageParam(GX_TEXFMT_NONE, GX_TEXGEN_NONE, GX_TEXSIZE_S8, GX_TEXSIZE_T8, GX_TEXREPEAT_NONE, GX_TEXFLIP_NONE, GX_TEXPLTTCOLOR0_USE, 0);
    G3_PolygonAttr(GX_LIGHTMASK_NONE, GX_POLYGONMODE_MODULATE, GX_CULL_NONE, 0, 31, GX_POLYGON_ATTR_MISC_NONE);
    G3_Begin(GX_BEGIN_QUADS);
    G3_Color(GX_RGB(0, 0, 0));
    // The eight cells of a 3x3 grid around the middle one (the model)
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            if (row == 1 && column == 1) {
                continue;
            }
            G3_Vtx(xs[column], ys[row], 0);
            G3_Vtx(xs[column + 1], ys[row], 0);
            G3_Vtx(xs[column + 1], ys[row + 1], 0);
            G3_Vtx(xs[column], ys[row + 1], 0);
        }
    }
    G3_End();
}
#endif
