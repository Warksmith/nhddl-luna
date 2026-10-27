#ifndef LUNA_CAMERA_DRAW_H
#define LUNA_CAMERA_DRAW_H

#ifdef LUNA_CAMERA_ANIMATIONS
/* Project live library geometry rather than scaling a low-resolution capture.
 * SceneBegin/AfterBackground/End scope this to the library foreground, so
 * menus, backgrounds and footer prompts keep their normal coordinates. */
extern float lunaCameraDrawScale;
extern float lunaCameraDrawOpacity;
static inline uint64_t lunaCameraDrawColor(uint64_t color) {
  if (lunaCameraDrawOpacity == 1.0f) return color;
  uint64_t alpha = (uint64_t)(((color >> 24) & 255) * lunaCameraDrawOpacity);
  return (color & ~((uint64_t)255 << 24)) | (alpha << 24);
}
static inline float lunaCameraDrawX(const GSGLOBAL *gs, float x) {
  return lunaCameraDrawScale == 1.0f ? x :
      gs->Width * 0.5f + (x - gs->Width * 0.5f) * lunaCameraDrawScale;
}
static inline float lunaCameraDrawY(const GSGLOBAL *gs, float y) {
  return lunaCameraDrawScale == 1.0f ? y :
      gs->Height * 0.5f + (y - gs->Height * 0.5f) * lunaCameraDrawScale;
}

typedef struct {
  uint64_t blend;
  GSTEST test;
  int enabled, pabe, fixed, active;
} LunaCameraTextureState;

static inline LunaCameraTextureState lunaCameraTextureBegin(GSGLOBAL *g) {
  LunaCameraTextureState state = {.active=lunaCameraDrawOpacity<1};
  if (!state.active) return state;
  state.blend=g->PrimAlpha; state.enabled=g->PrimAlphaEnable;
  state.pabe=g->PABE; state.test=*g->Test;
  state.fixed=state.enabled && ((state.blend>>4)&3)==2;
  g->PrimAlphaEnable=GS_SETTING_ON;
  if (state.fixed) {
    // Keep opaque cover texels at alpha 0x80 for their existing alpha test.
    uint64_t fix=(uint64_t)(((state.blend>>32)&255)*lunaCameraDrawOpacity);
    gsKit_set_primalpha(g,(state.blend & ~((uint64_t)255<<32)) | (fix<<32),0);
  } else {
    gsKit_set_primalpha(g,state.enabled ? state.blend : GS_SETREG_ALPHA(0,1,0,1,0),0);
    if (g->Test->ATST==2 && g->Test->AREF==0x80) {
      g->Test->ATST=6; g->Test->AREF=0;
      gsKit_set_test(g,GS_ATEST_ON);
    }
  }
  return state;
}

static inline uint64_t lunaCameraTextureColor(uint64_t color, const LunaCameraTextureState *state) {
  return state->active && state->fixed ? color : lunaCameraDrawColor(color);
}

static inline void lunaCameraTextureEnd(GSGLOBAL *g, const LunaCameraTextureState *state) {
  if (!state->active) return;
  g->PrimAlphaEnable=state->enabled;
  gsKit_set_primalpha(g,state->blend,state->pabe);
  *g->Test=state->test;
  gsKit_set_test(g,state->test.ATE ? GS_ATEST_ON : GS_ATEST_OFF);
}

/* Projection and optional scene opacity leave UVs, depth and clocks unchanged.
 * Define wrappers before the aliases so gsKit's own 2D-to-3D macros expand. */
static inline void lunaCameraSprite(GSGLOBAL *g, float x1, float y1,
    float x2, float y2, int z, uint64_t c) {
  gsKit_prim_sprite(g,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),z,lunaCameraDrawColor(c));
}
static inline void lunaCameraLine(GSGLOBAL *g, float x1, float y1,
    float x2, float y2, int z, uint64_t c) {
  gsKit_prim_line(g,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),z,lunaCameraDrawColor(c));
}
static inline void lunaCameraTriangle(GSGLOBAL *g, float x1, float y1,
    float x2, float y2, float x3, float y3, int z, uint64_t c) {
  gsKit_prim_triangle(g,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),
      lunaCameraDrawX(g,x3),lunaCameraDrawY(g,y3),z,lunaCameraDrawColor(c));
}
static inline void lunaCameraTriangleGouraud(GSGLOBAL *g, float x1, float y1,
    float x2, float y2, float x3, float y3, int z, uint64_t c1, uint64_t c2, uint64_t c3) {
  gsKit_prim_triangle_gouraud(g,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),
      lunaCameraDrawX(g,x3),lunaCameraDrawY(g,y3),z,
      lunaCameraDrawColor(c1),lunaCameraDrawColor(c2),lunaCameraDrawColor(c3));
}
static inline void lunaCameraQuadGouraud(GSGLOBAL *g, float x1, float y1,
    float x2, float y2, float x3, float y3, float x4, float y4, int z,
    uint64_t c1, uint64_t c2, uint64_t c3, uint64_t c4) {
  gsKit_prim_quad_gouraud(g,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),
      lunaCameraDrawX(g,x3),lunaCameraDrawY(g,y3),
      lunaCameraDrawX(g,x4),lunaCameraDrawY(g,y4),z,
      lunaCameraDrawColor(c1),lunaCameraDrawColor(c2),lunaCameraDrawColor(c3),lunaCameraDrawColor(c4));
}
static inline void lunaCameraSpriteTexture(GSGLOBAL *g, GSTEXTURE *t,
    float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2,
    int z, uint64_t c) {
  LunaCameraTextureState state=lunaCameraTextureBegin(g);
  gsKit_prim_sprite_texture(g,t,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),u1,v1,
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),u2,v2,z,lunaCameraTextureColor(c,&state));
  lunaCameraTextureEnd(g,&state);
}
static inline void lunaCameraQuadTexture(GSGLOBAL *g, GSTEXTURE *t,
    float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2,
    float x3, float y3, float u3, float v3, float x4, float y4, float u4, float v4,
    int z, uint64_t c) {
  LunaCameraTextureState state=lunaCameraTextureBegin(g);
  gsKit_prim_quad_texture(g,t,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),u1,v1,
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),u2,v2,
      lunaCameraDrawX(g,x3),lunaCameraDrawY(g,y3),u3,v3,
      lunaCameraDrawX(g,x4),lunaCameraDrawY(g,y4),u4,v4,z,lunaCameraTextureColor(c,&state));
  lunaCameraTextureEnd(g,&state);
}
static inline void lunaCameraQuadTextureGouraud(GSGLOBAL *g, GSTEXTURE *t,
    float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2,
    float x3, float y3, float u3, float v3, float x4, float y4, float u4, float v4,
    int z, uint64_t c1, uint64_t c2, uint64_t c3, uint64_t c4) {
  LunaCameraTextureState state=lunaCameraTextureBegin(g);
  gsKit_prim_quad_goraud_texture(g,t,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),u1,v1,
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),u2,v2,
      lunaCameraDrawX(g,x3),lunaCameraDrawY(g,y3),u3,v3,
      lunaCameraDrawX(g,x4),lunaCameraDrawY(g,y4),u4,v4,z,
      lunaCameraTextureColor(c1,&state),lunaCameraTextureColor(c2,&state),
      lunaCameraTextureColor(c3,&state),lunaCameraTextureColor(c4,&state));
  lunaCameraTextureEnd(g,&state);
}

#undef gsKit_prim_sprite
#undef gsKit_prim_line
#undef gsKit_prim_triangle
#undef gsKit_prim_triangle_gouraud
#undef gsKit_prim_quad_gouraud
#undef gsKit_prim_sprite_texture
#undef gsKit_prim_quad_texture
#undef gsKit_prim_quad_goraud_texture
#define gsKit_prim_sprite lunaCameraSprite
#define gsKit_prim_line lunaCameraLine
#define gsKit_prim_triangle lunaCameraTriangle
#define gsKit_prim_triangle_gouraud lunaCameraTriangleGouraud
#define gsKit_prim_quad_gouraud lunaCameraQuadGouraud
#define gsKit_prim_sprite_texture lunaCameraSpriteTexture
#define gsKit_prim_quad_texture lunaCameraQuadTexture
#define gsKit_prim_quad_goraud_texture lunaCameraQuadTextureGouraud
#endif
#endif
