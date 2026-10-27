#ifndef LUNA_CAMERA_DRAW_H
#define LUNA_CAMERA_DRAW_H

#ifdef LUNA_CAMERA_ANIMATIONS
/* Project live library geometry rather than scaling a low-resolution capture.
 * SceneBegin/AfterBackground/End scope this to the library foreground, so
 * menus, backgrounds and footer prompts keep their normal coordinates. */
extern float lunaCameraDrawScale;
static inline float lunaCameraDrawX(const GSGLOBAL *gs, float x) {
  return lunaCameraDrawScale == 1.0f ? x :
      gs->Width * 0.5f + (x - gs->Width * 0.5f) * lunaCameraDrawScale;
}
static inline float lunaCameraDrawY(const GSGLOBAL *gs, float y) {
  return lunaCameraDrawScale == 1.0f ? y :
      gs->Height * 0.5f + (y - gs->Height * 0.5f) * lunaCameraDrawScale;
}

/* Only destination coordinates change; UVs, depth, colors and clocks do not.
 * Define wrappers before the aliases so gsKit's own 2D-to-3D macros expand. */
static inline void lunaCameraSprite(GSGLOBAL *g, float x1, float y1,
    float x2, float y2, int z, uint64_t c) {
  gsKit_prim_sprite(g,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),z,c);
}
static inline void lunaCameraLine(GSGLOBAL *g, float x1, float y1,
    float x2, float y2, int z, uint64_t c) {
  gsKit_prim_line(g,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),z,c);
}
static inline void lunaCameraTriangle(GSGLOBAL *g, float x1, float y1,
    float x2, float y2, float x3, float y3, int z, uint64_t c) {
  gsKit_prim_triangle(g,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),
      lunaCameraDrawX(g,x3),lunaCameraDrawY(g,y3),z,c);
}
static inline void lunaCameraTriangleGouraud(GSGLOBAL *g, float x1, float y1,
    float x2, float y2, float x3, float y3, int z, uint64_t c1, uint64_t c2, uint64_t c3) {
  gsKit_prim_triangle_gouraud(g,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),
      lunaCameraDrawX(g,x3),lunaCameraDrawY(g,y3),z,c1,c2,c3);
}
static inline void lunaCameraQuadGouraud(GSGLOBAL *g, float x1, float y1,
    float x2, float y2, float x3, float y3, float x4, float y4, int z,
    uint64_t c1, uint64_t c2, uint64_t c3, uint64_t c4) {
  gsKit_prim_quad_gouraud(g,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),
      lunaCameraDrawX(g,x3),lunaCameraDrawY(g,y3),
      lunaCameraDrawX(g,x4),lunaCameraDrawY(g,y4),z,c1,c2,c3,c4);
}
static inline void lunaCameraSpriteTexture(GSGLOBAL *g, GSTEXTURE *t,
    float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2,
    int z, uint64_t c) {
  gsKit_prim_sprite_texture(g,t,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),u1,v1,
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),u2,v2,z,c);
}
static inline void lunaCameraQuadTexture(GSGLOBAL *g, GSTEXTURE *t,
    float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2,
    float x3, float y3, float u3, float v3, float x4, float y4, float u4, float v4,
    int z, uint64_t c) {
  gsKit_prim_quad_texture(g,t,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),u1,v1,
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),u2,v2,
      lunaCameraDrawX(g,x3),lunaCameraDrawY(g,y3),u3,v3,
      lunaCameraDrawX(g,x4),lunaCameraDrawY(g,y4),u4,v4,z,c);
}
static inline void lunaCameraQuadTextureGouraud(GSGLOBAL *g, GSTEXTURE *t,
    float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2,
    float x3, float y3, float u3, float v3, float x4, float y4, float u4, float v4,
    int z, uint64_t c1, uint64_t c2, uint64_t c3, uint64_t c4) {
  gsKit_prim_quad_goraud_texture(g,t,lunaCameraDrawX(g,x1),lunaCameraDrawY(g,y1),u1,v1,
      lunaCameraDrawX(g,x2),lunaCameraDrawY(g,y2),u2,v2,
      lunaCameraDrawX(g,x3),lunaCameraDrawY(g,y3),u3,v3,
      lunaCameraDrawX(g,x4),lunaCameraDrawY(g,y4),u4,v4,z,c1,c2,c3,c4);
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
