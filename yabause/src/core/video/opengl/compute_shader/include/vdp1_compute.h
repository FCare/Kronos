#ifndef VDP1_COMPUTE_H
#define VDP1_COMPUTE_H

#include "vdp1_prog_compute.h"
#include "vdp1_prog_compute_upscale.h"
#include "vdp1.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NB_LINE_MAX_PER_DRAW 512

enum
{
  VDP1_MESH_STANDARD_BANDING = 0,
  VDP1_MESH_IMPROVED_BANDING,
  VDP1_MESH_STANDARD_NO_BANDING,
  VDP1_MESH_IMPROVED_NO_BANDING,
  WRITE,
  READ,
  CLEAR,
  CLEAR_MESH,
  DRAW_POLY_MSB_SHADOW_NO_MESH,
  DRAW_POLY_REPLACE_NO_MESH,
  DRAW_POLY_SHADOW_NO_MESH,
  DRAW_POLY_HALF_LUMINANCE_NO_MESH,
  DRAW_POLY_HALF_TRANSPARENT_NO_MESH,
  DRAW_POLY_GOURAUD_NO_MESH,
  DRAW_POLY_UNSUPPORTED_NO_MESH,
  DRAW_POLY_GOURAUD_HALF_LUMINANCE_NO_MESH,
  DRAW_POLY_GOURAUD_HALF_TRANSPARENT_NO_MESH,
  DRAW_QUAD_MSB_SHADOW_NO_MESH,
  DRAW_QUAD_REPLACE_NO_MESH,
  DRAW_QUAD_SHADOW_NO_MESH,
  DRAW_QUAD_HALF_LUMINANCE_NO_MESH,
  DRAW_QUAD_HALF_TRANSPARENT_NO_MESH,
  DRAW_QUAD_GOURAUD_NO_MESH,
  DRAW_QUAD_UNSUPPORTED_NO_MESH,
  DRAW_QUAD_GOURAUD_HALF_LUMINANCE_NO_MESH,
  DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH,
  DRAW_POLY_MSB_SHADOW_MESH,
  DRAW_POLY_REPLACE_MESH,
  DRAW_POLY_SHADOW_MESH,
  DRAW_POLY_HALF_LUMINANCE_MESH,
  DRAW_POLY_HALF_TRANSPARENT_MESH,
  DRAW_POLY_GOURAUD_MESH,
  DRAW_POLY_UNSUPPORTED_MESH,
  DRAW_POLY_GOURAUD_HALF_LUMINANCE_MESH,
  DRAW_POLY_GOURAUD_HALF_TRANSPARENT_MESH,
  DRAW_QUAD_MSB_SHADOW_MESH,
  DRAW_QUAD_REPLACE_MESH,
  DRAW_QUAD_SHADOW_MESH,
  DRAW_QUAD_HALF_LUMINANCE_MESH,
  DRAW_QUAD_HALF_TRANSPARENT_MESH,
  DRAW_QUAD_GOURAUD_MESH,
  DRAW_QUAD_UNSUPPORTED_MESH,
  DRAW_QUAD_GOURAUD_HALF_LUMINANCE_MESH,
  DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH,
  NB_PRG
};

typedef struct {
  int x, y;
} point;

extern void vdp1_compute_init(int width, int height, float ratio);
extern void vdp1_compute(vdp1cmd_struct* cmd, point tl, point br);
extern int get_vdp1_tex(int);
extern int get_vdp1_mesh(int);
extern int vdp1_add(vdp1cmd_struct* cmd, int clipcmd);
extern int vdp1_add_upscale(vdp1cmd_struct* cmd, int clipcmd);
extern void vdp1_clear(int id, float *col, int* limits);
extern u32* vdp1_get_directFB();
extern void vdp1_setup(void);
extern void vdp1_compute_reset(void);
extern void vdp1_update_banding(void);
extern void vdp1_update_mesh(void);
extern void vdp1_update_performance(void);
extern void startVdp1Render(void);
extern void startVdp1RenderUpscale(void);
extern void endVdp1Render(void);
extern void endVdp1RenderUpscale(void);

#ifdef __cplusplus
}
#endif

#endif //VDP1_COMPUTE_H
