#ifndef ROBOCOMPOSITOR_COMPOSE_H
#define ROBOCOMPOSITOR_COMPOSE_H

#include "types.h"

/*
 * CPU composition core. No syscalls. Linked by RoboCompositor and
 * the host compositor tests. A future GPU backend replaces the
 * compose/present pair without changing this layer model.
 */

typedef struct RoboComposeLayer {
    int used;
    uint32_t windowId;
    uint64_t ownerPid;
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t stride;
    uint32_t pixfmt;
    uint32_t *pixels;
    uint8_t opacity;
    uint8_t visible;
} RoboComposeLayer;

typedef struct RoboComposeScene {
    RoboComposeLayer layers[RoboComposeMaxLayers];
    uint32_t zorder[RoboComposeMaxLayers];
    unsigned nlayers;
    RoboDamageRect damage;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixfmt;
    uint32_t *back;
    uint32_t *front;
    uint32_t bgArgb;
    uint32_t frame;
    unsigned lastBlits;
} RoboComposeScene;

int DamageIsEmpty(const RoboDamageRect *d);
void DamageClear(RoboDamageRect *d);
void DamageAdd(RoboDamageRect *d, int32_t x, int32_t y,
                     uint32_t w, uint32_t h);
void DamageMerge(RoboDamageRect *d, const RoboDamageRect *o);
int DamageContains(const RoboDamageRect *d, int32_t x, int32_t y);
void DamageClip(RoboDamageRect *d, uint32_t dw, uint32_t dh);

uint32_t ComposePack(uint32_t pixfmt, uint32_t argb);
uint32_t ComposeUnpack(uint32_t pixfmt, uint32_t p);
uint32_t ComposeBlend(uint32_t pixfmt, uint32_t dst, uint32_t src,
                            uint32_t opacity);

void ComposeSceneReset(RoboComposeScene *s);
int ComposeInit(RoboComposeScene *s, uint32_t *back, uint32_t *front,
                      uint32_t w, uint32_t h, uint32_t stride, uint32_t pixfmt);
int ComposeInsert(RoboComposeScene *s, uint32_t id, uint64_t pid,
                        int32_t x, int32_t y, uint32_t w, uint32_t h,
                        uint32_t stride, uint32_t *pixels, uint32_t opacity);
int ComposeRemove(RoboComposeScene *s, uint32_t id);
int ComposeRaise(RoboComposeScene *s, uint32_t id);
int ComposeLower(RoboComposeScene *s, uint32_t id);
int ComposeSetZ(RoboComposeScene *s, uint32_t id, uint32_t z);
RoboComposeLayer *ComposeFind(RoboComposeScene *s, uint32_t id);
int ComposeFrame(RoboComposeScene *s);
uint32_t ComposeSample(const RoboComposeScene *s, int32_t x, int32_t y);

#endif
