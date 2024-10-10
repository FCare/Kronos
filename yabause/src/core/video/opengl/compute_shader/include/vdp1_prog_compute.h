#ifndef VDP1_PROG_COMPUTE_H
#define VDP1_PROG_COMPUTE_H

#include "ygl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QuoteIdent(ident) #ident
#define Stringify(macro) QuoteIdent(macro)


// To do: In order to know if a pixel has to be considered for a command,
// each command has o be expressed a set of lines (dx,dy) on AD segment, (dx2,dy2) on BC Segment
// if a pixel is on a line, it has to be considered as part of command => it shall simulate the per line rasterizer of the real VDP1

#define POLYGON 0
#define QUAD_POLY 1
#define POLYLINE 2
#define LINE 3
#define DISTORTED 4
#define QUAD 5
#define SYSTEM_CLIPPING 6
#define USER_CLIPPING 7

//#define SHOW_QUAD

static const char vdp1_write_f_base[] =
SHADER_VERSION_COMPUTE
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"layout(local_size_x = %d, local_size_y = %d) in;\n"
"layout(rgba8, binding = 0) writeonly uniform image2D outSurface;\n"
"layout(rgba8, binding = 1) readonly uniform image2D fbSurface;\n"
"layout(location = 2) uniform vec2 upscale;\n"
"void main()\n"
"{\n"
"  ivec2 size = imageSize(outSurface);\n"
"  ivec2 texel = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);\n"
"  ivec2 coord = ivec2(int(texel.x * upscale.x),int(texel.y * upscale.y));\n"
"  texel.y = texel.y;\n"
"  if (any(greaterThanEqual(coord, ivec2(512, 256)))) return;"
"  vec4 pix = imageLoad(fbSurface, coord);\n"
"  if (pix.a != 0.0) imageStore(outSurface,texel,vec4(pix.r, pix.g, 0.0, 0.0));\n"
"}\n";

static char vdp1_write_f[ sizeof(vdp1_write_f_base) + 64 ];

static const char vdp1_read_f_base[] =
SHADER_VERSION_COMPUTE
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"layout(local_size_x = %d, local_size_y = %d) in;\n"
"layout(rgba8, binding = 0) readonly uniform image2D s_texture;  \n"
"layout(std430, binding = 1) writeonly buffer VDP1FB { uint Vdp1FB[]; };\n"
"layout(location = 2) uniform vec2 upscale;\n"
"void main()\n"
"{\n"
"  ivec2 size = imageSize(s_texture);\n"
"  ivec2 texel = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);\n"
"  int x = int(texel.x * upscale.x);\n"
"  int y = int(texel.y * upscale.y);\n"
"  if (x >= 512 || y >= 256 ) return;\n"
"  int idx = int(x) + int(y)*512;\n"
"  vec4 pix = imageLoad(s_texture, ivec2(vec2(texel.x,texel.y)));\n"
"  uint val = (uint(pix.r*255.0)<<24) | (uint(pix.g*255.0)<<16);\n"
"  Vdp1FB[idx] = val;\n"
"}\n";

static char vdp1_read_f[ sizeof(vdp1_read_f_base) + 64 ];

static const char vdp1_clear_f_base[] =
SHADER_VERSION_COMPUTE
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"layout(local_size_x = %d, local_size_y = %d) in;\n"
"layout(rgba8, binding = 0) writeonly uniform image2D outSurface;\n"
"layout(rgba8, binding = 1) writeonly uniform image2D outMesh;\n"
"layout(location = 2) uniform vec4 col;\n"
"layout(location = 3) uniform ivec4 limits;\n"
"void main()\n"
"{\n"
"  ivec2 size = imageSize(outSurface);\n"
"  ivec2 texel = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);\n"
"  if (texel.x >= size.x || texel.y >= size.y ) return;\n"
"  if (texel.x < limits.x) return;\n"
"  if (texel.y < limits.y) return;\n"
"  if (texel.x > limits.z) return;\n"
"  if (texel.y > limits.w) return;\n"
"  imageStore(outSurface,texel,col);\n"
"  imageStore(outMesh, texel, vec4(0.0));\n"
"}\n";

static char vdp1_clear_f[ sizeof(vdp1_clear_f_base) + 64 ];

static const char vdp1_draw_line_start_f_base[] =
SHADER_VERSION_COMPUTE
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"layout(local_size_x = %d, local_size_y = %d) in;\n"
"struct cmdparameter_struct{ \n"
"  uint CMDPMOD;\n"
"  uint CMDSRCA;\n"
"  uint CMDSIZE;\n"
"  int CMDXA;\n"
"  int CMDYA;\n"
"  int CMDXB;\n"
"  int CMDYB;\n"
"  uint CMDCOLR;\n"
"  uint CMDCTRL;\n"
"  float dl;\n"
"  float dr;\n"
"  float G[16];\n"
"  int flip;\n"
"  int pad[4];\n"
"};\n"
"layout(rgba8, binding = 0) uniform image2D outSurface;\n"
"layout(std430, binding = 3) readonly buffer VDP1RAM { uint Vdp1Ram[]; };\n"
"layout(std430, binding = 6) readonly buffer CMD_LIST {\n"
"  cmdparameter_struct cmd[];\n"
"};\n"
"layout(location = 7) uniform ivec2 upscale;\n"
"layout(location = 8) uniform ivec2 sysClip;\n"
"layout(location = 9) uniform ivec4 usrClip;\n"
"layout(location = 10) uniform int nbLines;\n"
"bool clip(ivec2 P, ivec4 limit) {\n"
"  if (any(lessThan(P, limit.xy))) return false;\n"
"  if (any(greaterThan(P, limit.zw))) return false;\n"
"  return true;\n"
"}\n"
"vec4 VDP1COLOR(uint color) {\n"
"  return vec4(float((color>>0)&0xFFu)/255.0,float((color>>8)&0xFFu)/255.0,0.0,0.0);\n"
"}\n";

static char vdp1_draw_line_start_f[ sizeof(vdp1_draw_line_start_f_base) + 64 ];

static const char vdp1_get_non_textured_f[] =
"uint getColor(cmdparameter_struct pixcmd, float dp, out bool valid){\n"
"  valid = true;\n"
"  return pixcmd.CMDCOLR;\n"
"}\n";

static const char vdp1_get_textured_f[] =
"uint Vdp1RamReadByte(uint addr) {\n"
"  addr &= 0x7FFFFu;\n"
"  uint read = Vdp1Ram[addr>>2];\n"
"  return (read>>(8*(addr&0x3u)))&0xFFu;\n"
"}\n"
"uint Vdp1RamReadWord(uint addr) {\n"
"  addr &= 0x7FFFFu;\n"
"  uint read = Vdp1Ram[addr>>2];\n"
"  if( (addr & 0x02u) != 0u ) { read >>= 16; } \n"
"  return (((read) >> 8 & 0xFFu) | ((read) & 0xFFu) << 8);\n"
"}\n"
"uint getColor(cmdparameter_struct pixcmd, float dp, out bool valid)\n"
"{\n"
"  uint color = 0;\n"
"  ivec2 texSize = ivec2(((pixcmd.CMDSIZE >> 8) & 0x3F)<<3,pixcmd.CMDSIZE & 0xFF );\n"
"  vec2 uv = vec2(dp, mix(pixcmd.dl, pixcmd.dr, dp));\n"
"  uint y = uint(floor(uv.y*(texSize.y)));\n"
"  uint x = uint(floor(uv.x*(texSize.x)));\n"
"  if ((pixcmd.flip & 0x1u) == 0x1u) x = (texSize.x-1) - x;\n"
"  if ((pixcmd.flip & 0x2u) == 0x2u) y = (texSize.y-1) - y;\n"
"  uint pos = y*texSize.x+x;\n"
"  uint charAddr = ((pixcmd.CMDSRCA * 8)& 0x7FFFFu) + pos;\n"
"  uint dot;\n"
"  bool SPD = ((pixcmd.CMDPMOD & 0x40u) != 0);\n"
"  bool END = ((pixcmd.CMDPMOD & 0x80u) != 0);\n"
"  valid = true;\n"
"  switch ((pixcmd.CMDPMOD >> 3) & 0x7u)\n"
"  {\n"
"    case 0:\n"
"    {\n"
"      // 4 bpp Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFFF0u;\n"
"      uint i;\n"
"      charAddr = pixcmd.CMDSRCA * 8 + pos/2;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"       if ((x & 0x1u) == 0u) dot = (dot>>4)&0xFu;\n"
"       else dot = (dot)&0xFu;\n"
// "      if ((dot == 0x0Fu) && (!END)) {\n"
// // "        discarded = true;\n"
// "      }\n" else
"      if ((dot == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"      else color = dot | colorBank;\n"
"      break;\n"
"    }\n"
"    case 1:\n"
"    {\n"
"      // 4 bpp LUT mode\n"
"       uint temp;\n"
"       charAddr = pixcmd.CMDSRCA * 8 + pos/2;\n"
"       uint colorLut = pixcmd.CMDCOLR * 8;\n"
"       dot = Vdp1RamReadByte(charAddr);\n"
"       if ((x & 0x1u) == 0u) dot = (dot>>4)&0xFu;\n"
"       else dot = (dot)&0xFu;\n"
// "       if ((dot == 0x0Fu) && (!END)) {\n"
// "        discarded = true;\n"
// "      }\n" else
"      if ((dot == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"       else {\n"
"         temp = Vdp1RamReadWord((dot * 2 + colorLut));\n"
"         color = temp;\n"
"       }\n"
"       break;\n"
"    }\n"
"    case 2:\n"
"    {\n"
"      // 8 bpp(64 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFFC0u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
// "      if ((dot == 0xFFu) && (!END)) {\n"
// "        discarded = true;\n"
// "      }\n" else
"      if ((dot == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"      else {\n"
"        color = (dot & 0x3Fu) | colorBank;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 3:\n"
"    {\n"
"      // 8 bpp(128 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFF80u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
// "      if ((dot == 0xFFu) && (!END)) {\n"
// "        discarded = true;\n"
// "      }\n" else
"      if ((dot == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"      else {\n"
"        color = (dot & 0x7Fu) | colorBank;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 4:\n"
"    {\n"
"      // 8 bpp(256 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFF00u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
// "      if ((dot == 0xFFu) && (!END)) {\n"
// "        discarded = true;\n"
// "      }\n" else
"      if ((dot == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"      else {\n"
"          color = dot | colorBank;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 5:\n"
"    {\n"
"      // 16 bpp Bank mode\n"
"      uint temp;\n"
"      charAddr += pos;\n"
"      temp = Vdp1RamReadWord(charAddr);\n"
// "      if ((temp == 0x7FFFu) && (!END)) {\n"
// "        discarded = true;\n"
// "      }\n" else
"      if (((temp & 0x8000u) == 0) && (!SPD)) {\n"
"        valid = false;\n"
"      }\n"
"      else {\n"
"        color = temp;\n"
"      }\n"
"      break;\n"
"    }\n"
"    default:\n"
"      break;\n"
"  }\n"
" return color;\n"
"}\n";

static const char vdp1_get_pixel_msb_shadow_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, float dp, ivec2 P, out bool valid)\n""{\n"
"  uint testcolor = getColor(pixcmd, dp, valid);\n"
"  vec4 oldcol = imageLoad(outSurface, P);\n"
"  uint color = uint(oldcol.r*255.0) + (uint(oldcol.g*255.0)<<8);\n"
"  uint Rht = ((color >> 00) & 0x1F)>>1;\n"
"  uint Ght = ((color >> 05) & 0x1F)>>1;\n"
"  uint Bht = ((color >> 10) & 0x1F)>>1;\n"
"  uint MSBht = 0x8000;\n"
"  color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  return VDP1COLOR(color);\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_replace_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, float dp, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, dp, valid);\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_shadow_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, float dp, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, dp, valid);\n"
"  vec4 oldcol = imageLoad(outSurface, P);\n"
"  uint oldcolor = uint(oldcol.r*255.0) + (uint(oldcol.g*255.0)<<8);\n"
"  if (oldcolor & 0x8000) {\n"
"   uint Rht = ((oldcolor >> 00) & 0x1F)>>1;\n"
"   uint Ght = ((oldcolor >> 05) & 0x1F)>>1;\n"
"   uint Bht = ((oldcolor >> 10) & 0x1F)>>1;\n"
"   uint MSBht = oldcolor & 0x8000;\n"
"   color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  }\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_half_luminance_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, float dp, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, dp, valid);\n"
"  uint Rht = ((color >> 00) & 0x1F)>>1;\n"
"  uint Ght = ((color >> 05) & 0x1F)>>1;\n"
"  uint Bht = ((color >> 10) & 0x1F)>>1;\n"
"  uint MSBht = color & 0x8000;\n"
"  color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_half_transparent_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, float dp, ivec2 P, out bool valid)\n"
"{\n"
"  uint color = getColor(pixcmd, dp, valid);\n"
"  vec4 oldcol = imageLoad(outSurface, P);\n"
"  uint oldcolor = uint(oldcol.r*255.0) + (uint(oldcol.g*255.0)<<8);\n"
"  uint Rht = (((color >> 00) & 0x1F) + ((oldcolor >> 00) & 0x1F))>>1;\n"
"  uint Ght = (((color >> 05) & 0x1F) + ((oldcolor >> 05) & 0x1F))>>1;\n"
"  uint Bht = (((color >> 10) & 0x1F) + ((oldcolor >> 10) & 0x1F))>>1;\n"
"  uint MSBht = color & 0x8000;\n"
"  color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_gouraud_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, float dp, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, dp, valid);\n"
"  //Gouraud\n"
"  float Rg = float((color >> 00) & 0x1F)/31.0;\n"
"  float Gg = float((color >> 05) & 0x1F)/31.0;\n"
"  float Bg = float((color >> 10) & 0x1F)/31.0;\n"
"  int MSBg = int(color & 0x8000);\n"
"  Rg = clamp(Rg + mix(mix(pixcmd.G[0],pixcmd.G[12],pixcmd.dl), mix(pixcmd.G[4],pixcmd.G[8],pixcmd.dr), dp), 0.0, 1.0);\n"
"  Gg = clamp(Gg + mix(mix(pixcmd.G[1],pixcmd.G[13],pixcmd.dl), mix(pixcmd.G[5],pixcmd.G[9],pixcmd.dr), dp), 0.0, 1.0);\n"
"  Bg = clamp(Bg + mix(mix(pixcmd.G[2],pixcmd.G[14],pixcmd.dl), mix(pixcmd.G[6],pixcmd.G[10],pixcmd.dr), dp), 0.0, 1.0);\n"
"  color = MSBg | (int(Rg*31.0)&0x1F) | ((int(Gg*31.0)&0x1F)<<05) | ((int(Bg*31.0)&0x1F)<<10);\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_gouraud_half_luminance_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, float dp, ivec2 P, out bool valid)\n""{\n"
"  uint color = getColor(pixcmd, dp, valid);\n"
"  //Gouraud\n"
"  float Rg = float((color >> 00) & 0x1F)/31.0;\n"
"  float Gg = float((color >> 05) & 0x1F)/31.0;\n"
"  float Bg = float((color >> 10) & 0x1F)/31.0;\n"
"  int MSBg = int(color & 0x8000);\n"
"  Rg = clamp(Rg + mix(mix(pixcmd.G[0],pixcmd.G[12],pixcmd.dl), mix(pixcmd.G[4],pixcmd.G[8],pixcmd.dr), dp), 0.0, 1.0);\n"
"  Gg = clamp(Gg + mix(mix(pixcmd.G[1],pixcmd.G[13],pixcmd.dl), mix(pixcmd.G[5],pixcmd.G[9],pixcmd.dr), dp), 0.0, 1.0);\n"
"  Bg = clamp(Bg + mix(mix(pixcmd.G[2],pixcmd.G[14],pixcmd.dl), mix(pixcmd.G[6],pixcmd.G[10],pixcmd.dr), dp), 0.0, 1.0);\n"
"  color = MSBg | (int(Rg*31.0)&0x1F) | ((int(Gg*31.0)&0x1F)<<05) | ((int(Bg*31.0)&0x1F)<<10);\n"
"  uint Rht = ((color >> 00) & 0x1F)>>1;\n"
"  uint Ght = ((color >> 05) & 0x1F)>>1;\n"
"  uint Bht = ((color >> 10) & 0x1F)>>1;\n"
"  uint MSBht = color & 0x8000;\n"
"  color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  return VDP1COLOR(color);\n"
"}\n";

static const char vdp1_get_pixel_gouraud_half_transparent_f[] =
"vec4 getColoredPixel(cmdparameter_struct pixcmd, float dp, ivec2 P, out bool valid)\n"
"{\n"
"  uint color = getColor(pixcmd, dp, valid);\n"
"  //Gouraud\n"
"  float Rg = float((color >> 00) & 0x1F)/31.0;\n"
"  float Gg = float((color >> 05) & 0x1F)/31.0;\n"
"  float Bg = float((color >> 10) & 0x1F)/31.0;\n"
"  int MSBg = int(color & 0x8000);\n"
"  Rg = clamp(Rg + mix(mix(pixcmd.G[0],pixcmd.G[12],pixcmd.dl), mix(pixcmd.G[4],pixcmd.G[8],pixcmd.dr), dp), 0.0, 1.0);\n"
"  Gg = clamp(Gg + mix(mix(pixcmd.G[1],pixcmd.G[13],pixcmd.dl), mix(pixcmd.G[5],pixcmd.G[9],pixcmd.dr), dp), 0.0, 1.0);\n"
"  Bg = clamp(Bg + mix(mix(pixcmd.G[2],pixcmd.G[14],pixcmd.dl), mix(pixcmd.G[6],pixcmd.G[10],pixcmd.dr), dp), 0.0, 1.0);\n"
"  color = MSBg | (int(Rg*31.0)&0x1F) | ((int(Gg*31.0)&0x1F)<<05) | ((int(Bg*31.0)&0x1F)<<10);\n"
"  vec4 oldcol = imageLoad(outSurface, P);\n"
"  uint oldcolor = uint(oldcol.r*255.0) + (uint(oldcol.g*255.0)<<8);\n"
"  uint Rht = (((color >> 00) & 0x1F) + ((oldcolor >> 00) & 0x1F))>>1;\n"
"  uint Ght = (((color >> 05) & 0x1F) + ((oldcolor >> 05) & 0x1F))>>1;\n"
"  uint Bht = (((color >> 10) & 0x1F) + ((oldcolor >> 10) & 0x1F))>>1;\n"
"  uint MSBht = color & 0x8000;\n"
"  color = MSBht | Rht | (Ght<<05) | (Bht<<10);\n"
"  return VDP1COLOR(color);\n"
"}\n";

static char vdp1_draw_mesh_f[] =
"vec4 getMeshedPixel(cmdparameter_struct pixcmd, float dp, ivec2 P, out bool valid)\n"
"{\n"
" valid = true;\n"
" if( (int(P.y) & 0x01) == 0 ){\n"
"  if( (int(P.x) & 0x01) == 0 ){\n"
"   valid = false;\n"
"  }\n"
" }else{\n"
"  if( (int(P.x) & 0x01) == 1 ){\n"
"    valid = false;\n"
"  }\n"
" }\n"
" if (valid) return getColoredPixel(pixcmd, dp, P, valid);\n"
" else return vec4(0.0);\n"
"}\n";

static char vdp1_draw_no_mesh_f[] =
"vec4 getMeshedPixel(cmdparameter_struct pixcmd, float dp, ivec2 P, out bool valid)\n"
"{\n"
" return getColoredPixel(pixcmd, dp, P, valid);\n"
"}\n";


static const char vdp1_draw_line_f[] =
"void main()\n"
"{\n"
" if (gl_GlobalInvocationID.x >= nbLines) return;"
" cmdparameter_struct pixcmd = cmd[gl_GlobalInvocationID.x];\n"
" ivec2 line = ivec2(pixcmd.CMDXB, pixcmd.CMDYB) - ivec2(pixcmd.CMDXA, pixcmd.CMDYA);\n"
" float orientation = (abs(line.x) >= abs(line.y))?1.0:0.0;\n"
" mat2 trans = mat2(orientation, 1.0-orientation, 1.0-orientation, orientation);\n"
" mat2 transrev = trans;\n"
" ivec2 P0 = ivec2(trans*vec2(pixcmd.CMDXA, pixcmd.CMDYA));\n"
" ivec2 P1 = ivec2(trans*vec2(pixcmd.CMDXB, pixcmd.CMDYB));\n"
" vec4 limit_f = vec4(trans*vec2(usrClip.xy), trans*vec2(min(usrClip.zw, sysClip)));\n"
" ivec4 limit = ivec4(limit_f);\n"
" ivec2 P = P0;\n"
" ivec2 vector = P1-P0;\n"
" ivec3 a = ivec3(sign(vector), 0);\n"
" if (a.x == 0) a.x = 1;\n"
" if (a.y == 0) a.y = 1;\n"
" ivec2 delta = a.xy*upscale;\n"
" P1.x = P1.x+delta.x;\n"
" int veclong = P1.x-P0.x;\n"
" ivec2 greedyOffset = int((orientation == 0.0)^^(a.x == a.y))*ivec2(a.x, -a.y);\n"
"	if (a.x != a.y) vector.x = -vector.x;\n"
" for (; P.x != P1.x; P.x += a.x) {\n"
"  //Draw pixels\n"
"  bool valid = clip(P,limit);\n"
"  if (valid) {\n"
"   ivec2 Pn = P;\n"
"   float dp = (float(Pn.x-P0.x)+0.5*float(a.x))/float(veclong);\n"
"   vec4 pixout = getMeshedPixel(pixcmd, dp, ivec2(transrev*vec2(Pn)), valid);\n"
"   if (valid) imageStore(outSurface,ivec2(transrev*vec2(Pn)),pixout);\n"
"  }\n"
"  a.z += vector.y;\n"
"  if (abs(a.z) >= abs(vector.x)) {\n"
"   a.z -= vector.x;\n"
"   P.y += a.y;\n"
//Greedy saturn effect\n"
"   ivec2 Pn = P + greedyOffset;\n"
"   valid = clip(Pn,limit);\n"
"   if ((valid)&&(Pn.x != P1.x)) {\n"
"    float dp = (float(Pn.x-P0.x)+0.5*float(a.x))/float(veclong);\n"
"    vec4 pixout = getMeshedPixel(pixcmd, dp, Pn, valid);\n"
"    if (valid) imageStore(outSurface,ivec2(transrev*vec2(Pn)),pixout);\n"
"   }\n"
"  }\n"
" }\n"
"}\n";

static const char vdp1_clear_mesh_f_base[] =
SHADER_VERSION_COMPUTE
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"layout(local_size_x = %d, local_size_y = %d) in;\n"
"layout(rgba8, binding = 0) writeonly uniform image2D outMesh0;\n"
"layout(rgba8, binding = 1) writeonly uniform image2D outMesh1;\n"
"void main()\n"
"{\n"
"  ivec2 size = imageSize(outMesh0);\n"
"  ivec2 texel = ivec2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y);\n"
"  if (texel.x >= size.x || texel.y >= size.y ) return;\n"
"  imageStore(outMesh0, texel, vec4(0.0));\n"
"  imageStore(outMesh1, texel, vec4(0.0));\n"
"}\n";
static char vdp1_clear_mesh_f[ sizeof(vdp1_clear_mesh_f_base) + 64 ];

#ifdef __cplusplus
}
#endif

#endif //VDP1_PROG_COMPUTE_H
