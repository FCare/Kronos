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

#define NB_COARSE_RAST_X 16
#define NB_COARSE_RAST_Y 16

#define QUEUE_SIZE 512

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

#define COLINDEX(A) \
"int col"Stringify(A)" = (int("Stringify(A)".r*255.0) | (int("Stringify(A)".g*255.0)<<8));\n"

#define RECOLINDEX(A) \
"col"Stringify(A)" = (int("Stringify(A)".r*255.0) | (int("Stringify(A)".g*255.0)<<8));\n"

#define SHADOW(A) \
"if ((col"Stringify(A)" & 0x8000) != 0) { \n\
  int Rs = ((col"Stringify(A)" >> 00) & 0x1F)>>1;\n \
  int Gs = ((col"Stringify(A)" >> 05) & 0x1F)>>1;\n \
  int Bs = ((col"Stringify(A)" >> 10) & 0x1F)>>1;\n \
  int MSBs = (col"Stringify(A)" & 0x8000) >> 8;\n \
  "Stringify(A)".r = float(Rs | ((Gs & 0x7)<<5))/255.0;\n \
  "Stringify(A)".g = float((Gs>>3) | (Bs<<2) | MSBs)/255.0;\n \
} \n"

#define MSB_SHADOW(A) \
"int Rm = ((col"Stringify(A)" >> 00) & 0x1F);\n \
int Gm = ((col"Stringify(A)" >> 05) & 0x1F);\n \
int Bm = ((col"Stringify(A)" >> 10) & 0x1F);\n \
int MSBm = 0x80;\n \
"Stringify(A)".r = float(Rm | ((Gm & 0x7)<<5))/255.0;\n \
"Stringify(A)".g = float((Gm>>3) | (Bm<<2) | MSBm)/255.0;\n"

#define HALF_LUMINANCE(A) \
"int Rhl = ((col"Stringify(A)" >> 00) & 0x1F)>>1;\n \
int Ghl = ((col"Stringify(A)" >> 05) & 0x1F)>>1;\n \
int Bhl = ((col"Stringify(A)" >> 10) & 0x1F)>>1;\n \
int MSBhl = (col"Stringify(A)" & 0x8000) >> 8;\n \
"Stringify(A)".r = float(Rhl | ((Ghl & 0x7)<<5))/255.0;\n \
"Stringify(A)".g = float((Ghl>>3) | (Bhl<<2) | MSBhl)/255.0;\n"

#define HALF_TRANPARENT_MIX(A, B) \
"if ((col"Stringify(B)" & 0x8000) != 0) { \n\
  int Rht = int(clamp(((float((col"Stringify(A)" >> 00) & 0x1F)/31.0) + (float((col"Stringify(B)" >> 00) & 0x1F)/31.0))*0.5, 0.0, 1.0)*31.0);\n \
  int Ght = int(clamp(((float((col"Stringify(A)" >> 05) & 0x1F)/31.0) + (float((col"Stringify(B)" >> 05) & 0x1F)/31.0))*0.5, 0.0, 1.0)*31.0);\n \
  int Bht = int(clamp(((float((col"Stringify(A)" >> 10) & 0x1F)/31.0) + (float((col"Stringify(B)" >> 10) & 0x1F)/31.0))*0.5, 0.0, 1.0)*31.0);\n \
  int MSBht = (col"Stringify(A)" & 0x8000) >> 8;\n \
  "Stringify(A)".r = float(Rht | ((Ght & 0x7)<<5))/255.0;\n \
  "Stringify(A)".g = float((Ght>>3) | (Bht<<2) | MSBht)/255.0;\n \
}\n"

#define GOURAUD_PROCESS(A) "\
float Rg = float((col"Stringify(A)" >> 00) & 0x1F)/31.0;\n \
float Gg = float((col"Stringify(A)" >> 05) & 0x1F)/31.0;\n \
float Bg = float((col"Stringify(A)" >> 10) & 0x1F)/31.0;\n \
int MSBg = (col"Stringify(A)" & 0x8000) >> 8;\n \
Rg = clamp(Rg + mix(mix(pixcmd.G[0],pixcmd.G[4],gouraudcoord.x), mix(pixcmd.G[12],pixcmd.G[8],gouraudcoord.x), gouraudcoord.y), 0.0, 1.0);\n \
Gg = clamp(Gg+ mix(mix(pixcmd.G[1],pixcmd.G[5],gouraudcoord.x), mix(pixcmd.G[13],pixcmd.G[9],gouraudcoord.x), gouraudcoord.y), 0.0, 1.0);\n \
Bg = clamp(Bg + mix(mix(pixcmd.G[2],pixcmd.G[6],gouraudcoord.x), mix(pixcmd.G[14],pixcmd.G[10],gouraudcoord.x), gouraudcoord.y), 0.0, 1.0);\n \
"Stringify(A)".r = float((int(Rg*255.0)>>3) | (((int(Gg*255.0)>>3) & 0x7)<<5))/255.0;\n \
"Stringify(A)".g = float(((int(Gg*255.0)>>3)>>3) | ((int(Bg*255.0)>>3)<<2) | MSBg)/255.0;\n \
""\n"

#define GOURAUD_PROCESS_EXTENDED(A) "\
float Rg = float((col"Stringify(A)" >> 00) & 0x1F)/31.0;\n \
float Gg = float((col"Stringify(A)" >> 05) & 0x1F)/31.0;\n \
float Bg = float((col"Stringify(A)" >> 10) & 0x1F)/31.0;\n \
int MSBg = (col"Stringify(A)" & 0x8000) >> 8;\n \
Rg = clamp(Rg + mix(mix(pixcmd.G[0],pixcmd.G[4],gouraudcoord.x), mix(pixcmd.G[12],pixcmd.G[8],gouraudcoord.x), gouraudcoord.y), 0.0, 1.0);\n \
Gg = clamp(Gg+ mix(mix(pixcmd.G[1],pixcmd.G[5],gouraudcoord.x), mix(pixcmd.G[13],pixcmd.G[9],gouraudcoord.x), gouraudcoord.y), 0.0, 1.0);\n \
Bg = clamp(Bg + mix(mix(pixcmd.G[2],pixcmd.G[6],gouraudcoord.x), mix(pixcmd.G[14],pixcmd.G[10],gouraudcoord.x), gouraudcoord.y), 0.0, 1.0);\n \
"Stringify(A)".r = float((int(Rg*255.0)>>3) | (((int(Gg*255.0)>>3) & 0x7)<<5))/255.0;\n \
"Stringify(A)".g = float(((int(Gg*255.0)>>3)>>3) | ((int(Bg*255.0)>>3)<<2) | MSBg)/255.0;\n \
"Stringify(A)".b = float((int(Rg*255.0)&0x7) | (int(Gg*255.0)&0x7)<< 4  )/255.0;\n \
"Stringify(A)".a = float((int(Bg*255.0)&0x7))/255.0;\n \
""\n"

static const char vdp1_start_f_base[] =
SHADER_VERSION_COMPUTE
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"

"struct cmdparameter_struct{ \n"
"  uint CMDCTRL;\n"
"  uint CMDLINK;\n"
"  uint CMDPMOD;\n"
"  uint CMDCOLR;\n"
"  uint CMDSRCA;\n"
"  uint CMDSIZE;\n"
"  int CMDXA;\n"
"  int CMDYA;\n"
"  int CMDXB;\n"
"  int CMDYB;\n"
"  int CMDXC;\n"
"  int CMDYC;\n"
"  int CMDXD;\n"
"  int CMDYD;\n"
"  uint CMDGRDA;\n"
"  int COLOR[4];\n"
"  float G[16];\n"
"  uint priority;\n"
"  uint w;\n"
"  uint h;\n"
"  uint flip;\n"
"  uint type;\n"
"  uint SPCTL;\n"
"  uint nbStep;\n"
"  float uAstepx;\n"
"  float uAstepy;\n"
"  float uBstepx;\n"
"  float uBstepy;\n"
"  int pad[6];\n"
"};\n"

"layout(local_size_x = %d, local_size_y = %d) in;\n"
"layout(rgba8, binding = 0) uniform image2D outSurface;\n"
"layout(rgba8, binding = 1) writeonly uniform image2D meshSurface;\n"
"layout(std430, binding = 3) readonly buffer VDP1RAM { uint Vdp1Ram[]; };\n"
"layout(std430, binding = 6) readonly buffer CMD_LIST { \n"
"  cmdparameter_struct cmd[];\n"
"};\n"
"layout(location = 7) uniform vec2 upscale;\n"
"layout(location = 8) uniform ivec2 sysClip;\n"
"layout(location = 9) uniform ivec4 usrClip;\n"
"layout(location = 10) uniform ivec2 tl;\n"
"shared uint endIndex[256];\n"

//===================================================================
"vec3 antiAliasedPoint( vec2 P,  vec2 P0, vec2 P1 )\n"
// dist_Point_to_Segment(): get the distance of a point to a segment
//     Input:  a Point P and a Segment S (P0, P1) (in any dimension)
//     Return: the x,y distance from P to S
"{\n"
"  vec2 v = P1 - P0;\n"
"  vec2 w = P - P0;\n"
"  float c1 = dot(w,v);\n"
"  float c2 = dot(v,v);\n"
"  float b = (c1+0.5) / (c2+1);\n"
"  vec2 Pb = P0 + b * v;\n"
"  return vec3(Pb,b);\n"
"}\n"

"vec3 aliasedPoint( vec2 P,  vec2 P0, vec2 P1 )\n"
// dist_Point_to_Segment(): get the distance of a point to a segment
//     Input:  a Point P and a Segment S (P0, P1) (in any dimension)
//     Return: the x,y distance from P to S
"{\n"
"  if (all(equal(P0,P1))) return vec3(P0, 0.0);\n"
"  float r = 0.0;\n"
"  if (abs(P1.y-P0.y) > abs(P1.x-P0.x)) {\n"
"    if(P1.y != P0.y) r = (P.y-P0.y)/(P1.y-P0.y);\n"
"    else r = (P.x-P0.x)/(P1.x-P0.x);\n"
"  } else {\n"
"    if(P1.x != P0.x) r = (P.x-P0.x)/(P1.x-P0.x);\n"
"    else r = (P.y-P0.y)/(P1.y-P0.y);\n"
"  }"
"  if (r >= 1.0) return vec3(P1, 1.0);\n"
"  else if (r <= 0.0) return vec3(P0, 0.0);\n"
"  else return vec3(P0 + ((P1-P0) * r), r);\n"
"}\n"

"uint isOnALine( vec2 P, vec2 V0, vec2 V1, out vec2 uv){\n"
//A pixel shall be considered as part of an aliased line if all coordinates of the pixel center to the line is shorter than 0.5, which is the horizontal or vertical half-size of the pixel
//This represent the behavior of aliasing as displayed in vdp1 spec.
"  vec2 A = V0+vec2(0.5);\n" //Get the center of the first point
"  vec2 B = V1+vec2(0.5);\n" //Get the center of the last point
"  vec3 d = aliasedPoint(P+vec2(0.5), A, B);\n" //Get the projection of the point P to the line segment
"  if (all(lessThanEqual(abs(floor(d.xy)-floor(P+vec2(0.5))), vec2(0.5*upscale)))) {\n" //If the line is passing through the same pixel as the point P
"    float ux= d.z;\n" //u is the relative distance from first point to projected position
"    float uy= 0.5;\n" //v is the ratio between the current line and the total number of lines
"    uv = vec2(ux,uy);\n"
"    return 1u;\n"
"  }\n"
"  return 0u;\n"
"}\n"

"uint isOnAQuadLine( vec2 P, vec2 V0, vec2 V1, vec2 V2, vec2 V3, vec2 sA, vec2 sB, uint step, out vec2 uv){\n"
"  uint ret = 0u;\n"
"  vec2 A = V0;\n"
"  vec2 B = V1;\n"
"  for (uint i=0; i<=step; i++) {\n"
//A pixel shall be considered as part of an anti-aliased line if the distance of the pixel center to the line is shorter than (sqrt(0.5), which is the diagonal of the pixel
//This represent the behavior of antialiasing as displayed in vdp1 spec.
"    if ((floor(P.x/upscale.x) == 0.0) && (A.x == 0)) {\n"
//There is an issue with polygon on the line x=0 with upscale
//Workaround here
"      uv.x = 0.0;\n"
"      if (any(notEqual(upscale, vec2(1.0)))) uv.y = length(P - V0)/(length(V3 - V0) + length(floor(upscale/2.0)*2.0));\n" //uy is the ratio between P postion and the length of the line at this ux postion.
"      else uv.y = (float(i)+0.5)/float(step+1);\n"
"      return 1u;\n"
"    }\n"
"    vec3 d = antiAliasedPoint(P+vec2(0.5), A+upscale/2.0, B+upscale/2.0);\n" //Get the projection of the point P to the line segment
"    if(((distance(d.xy, P+vec2(0.5))) < (length(upscale/2.0)+0.25)) && (d.z>=0.0) && (d.z<=1.0) ){\n" //Test the distance between the projection on line and the center of the pixel
"      vec2 PTop = V0 + d.z*(V1 - V0);\n"
"      vec2 PDown = V3 + d.z*(V2 - V3);\n"
"      uv.x = d.z;\n" //u is the relative distance from first point to projected position
"      if (any(notEqual(upscale, vec2(1.0)))) uv.y = length(P - PTop)/(length(PDown - PTop) + length(floor(upscale/2.0)*2.0));\n" //uy is the ratio between P postion and the length of the line at this ux postion.
"      else uv.y = (float(i)+0.5)/float(step+1);\n"
"      return 1u;\n"
"    }\n"
"    A += sA;\n"
"    B += sB;\n"
"  }\n"
"  return ret;\n"
"}\n"

"uint isOnAQuad(vec2 P, vec2 V0, vec2 V1, out vec2 uv) {\n"
"  if (any(lessThan(P, min(V0,V1))) || any(greaterThanEqual(P, max(V0,V1)))) return 0u;\n"
" uv = (P + vec2(0.5) - V0)/(V1 - V0);\n"
" return 1u;\n"
// "  //B--------C\n"
// "  //|\n"
// "  //|---M\n"
// "  //A\n"
// "  vec2 AB = floor(vec2 (0.0, V1.y-V0.y) / upscale);\n"
// "  vec2 CB = floor(vec2 (V1.x - V0.x, 0.0) / upscale);\n"
// "  vec2 MB = floor((P - vec2 (V0.x, V0.y)) /upscale);\n"
// "  vec2 size = vec2(dot(CB, CB), dot(AB, AB));\n"
// "  float ux = dot(CB, MB);\n"
// "  float uy = dot(AB, MB) + 1.0;\n"
// "  if (((ux >= 0.0) && (ux<=size.x)) && ((uy >= 0.0) && (uy<=size.y))) {\n"
// "      uv = vec2(ux, uy)/size;\n"
// "      return 1u;\n"
// "  }\n"
// "  return 0u;\n"
"}\n"

"uint pixIsInside (vec2 Pin, out vec2 uv, out vec4 pixOut){\n"
"  vec2 Quad[4];\n"
"//Bounding box test\n"
"  Quad[0] = vec2(cmd[0].CMDXA,cmd[0].CMDYA)*upscale;\n"
"  Quad[1] = vec2(cmd[0].CMDXB,cmd[0].CMDYB)*upscale;\n"
"  Quad[2] = vec2(cmd[0].CMDXC,cmd[0].CMDYC)*upscale;\n"
"  Quad[3] = vec2(cmd[0].CMDXD,cmd[0].CMDYD)*upscale;\n"
"  if ((cmd[0].type == "Stringify(DISTORTED)") || (cmd[0].type == "Stringify(POLYGON)")) {\n"
"    return isOnAQuadLine(Pin, Quad[0], Quad[1], Quad[2], Quad[3], vec2(cmd[0].uAstepx, cmd[0].uAstepy)*upscale, vec2(cmd[0].uBstepx, cmd[0].uBstepy)*upscale, uint(float(cmd[0].nbStep)), uv);\n"
"  } else {\n"
"    if ((cmd[0].type == "Stringify(QUAD)")  || (cmd[0].type == "Stringify(QUAD_POLY)")) {\n"
"     return isOnAQuad(Pin, Quad[0], Quad[2], uv);\n"
"    } else if (cmd[0].type == "Stringify(POLYLINE)") {\n"
"      if (isOnALine(Pin, Quad[0], Quad[1], uv) != 0u) return 1u;\n"
"      if (isOnALine(Pin, Quad[1], Quad[2], uv) != 0u) return 1u;\n"
"      if (isOnALine(Pin, Quad[2], Quad[3], uv) != 0u) return 1u;\n"
"      if (isOnALine(Pin, Quad[3], Quad[0], uv) != 0u) return 1u;\n"
"      return 0u;\n"
"    } else if (cmd[0].type == "Stringify(LINE)") {\n"
"      if (isOnALine(Pin, Quad[0], Quad[1], uv) != 0u) return 1u;\n"
"    }\n"
"  }\n"
"  return 0u;\n"
"}\n"

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

"vec4 VDP1COLOR(uint CMDCOLR) {\n"
"  return vec4(float((CMDCOLR>>0)&0xFFu)/255.0,float((CMDCOLR>>8)&0xFFu)/255.0,0.0,0.0);\n"
"}\n"

"vec4 ReadSpriteColor(cmdparameter_struct pixcmd, vec2 uv, vec2 texel, out bool discarded){\n"
"  vec4 color = vec4(0.0);\n"
"  uint y = uint(floor((pixcmd.h)*uv.y));\n"
"  uint x = uint(uv.x*(pixcmd.w));\n"
"  uint pos = y*pixcmd.w+x;\n"

"  uint charAddr = ((pixcmd.CMDSRCA * 8)& 0x7FFFFu) + pos;\n"
"  uint dot;\n"
"  bool SPD = ((pixcmd.CMDPMOD & 0x40u) != 0);\n"
"  bool END = ((pixcmd.CMDPMOD & 0x80u) != 0);\n"
"  if ((!END) && (x > endIndex[y])) {discarded = true; return vec4(0.0);}\n"
"  discarded = false;\n"
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
"      if ((dot == 0x0Fu) && (!END)) {\n"
"        discarded = true;\n"
"      }\n"
"      else if ((dot == 0) && (!SPD)) {\n"
"        discarded = true;\n"
"      }\n"
"      else color = VDP1COLOR(dot | colorBank);\n"
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
"       if ((dot == 0x0Fu) && (!END)) {\n"
"        discarded = true;\n"
"      }\n"
"      else if ((dot == 0) && (!SPD)) {\n"
"        discarded = true;\n"
"      }\n"
"       else {\n"
"         temp = Vdp1RamReadWord((dot * 2 + colorLut));\n"
"         color = VDP1COLOR(temp);\n"
"       }\n"
"       break;\n"
"    }\n"
"    case 2:\n"
"    {\n"
"      // 8 bpp(64 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFFC0u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if ((dot == 0xFFu) && (!END)) {\n"
"        discarded = true;\n"
"      }\n"
"      else if ((dot == 0) && (!SPD)) {\n"
"        discarded = true;\n"
"      }\n"
"      else {\n"
"        color = VDP1COLOR((dot & 0x3Fu) | colorBank);\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 3:\n"
"    {\n"
"      // 8 bpp(128 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFF80u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if ((dot == 0xFFu) && (!END)) {\n"
"        discarded = true;\n"
"      }\n"
"      else if ((dot == 0) && (!SPD)) {\n"
"        discarded = true;\n"
"      }\n"
"      else {\n"
"        color = VDP1COLOR((dot & 0x7Fu) | colorBank);\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 4:\n"
"    {\n"
"      // 8 bpp(256 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFF00u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if ((dot == 0xFFu) && (!END)) {\n"
"        discarded = true;\n"
"      }\n"
"      else if ((dot == 0) && (!SPD)) {\n"
"        discarded = true;\n"
"      }\n"
"      else {\n"
"          color = VDP1COLOR(dot | colorBank);\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 5:\n"
"    {\n"
"      // 16 bpp Bank mode\n"
"      uint temp;\n"
"      charAddr += pos;\n"
"      temp = Vdp1RamReadWord(charAddr);\n"
"      if ((temp == 0x7FFFu) && (!END)) {\n"
"        discarded = true;\n"
"      }\n"
"      else if (((temp & 0x8000u) == 0) && (!SPD)) {\n"
"        discarded = true;\n"
"      }\n"
"      else {\n"
"        color = VDP1COLOR(temp);\n"
"      }\n"
"      break;\n"
"    }\n"
"    default:\n"
"      break;\n"
"  }\n"
"  return color;\n"
"}\n"

"vec4 extractPolygonColor(cmdparameter_struct pixcmd){\n"
"  uint color = pixcmd.COLOR[0];\n"
"  return VDP1COLOR(color);\n"
"};\n"

"uint getEndIndex(cmdparameter_struct pixcmd, uint y)\n"
"{\n"
"  uint color = 0;\n"
"  uint nbEnd = 0;\n"
"  ivec2 texSize = ivec2(((pixcmd.CMDSIZE >> 8) & 0x3F)<<3,pixcmd.CMDSIZE & 0xFF );\n"
"  for (uint x=0; x<texSize.x; x++) {\n"
"   uint pos = y*texSize.x+x;\n"
"   uint charAddr = ((pixcmd.CMDSRCA * 8)& 0x7FFFFu) + pos;\n"
"   uint dot;\n"
"   switch ((pixcmd.CMDPMOD >> 3) & 0x7u)\n"
"   {\n"
"    case 0:\n"
"    {\n"
"      // 4 bpp Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFFF0u;\n"
"      charAddr = pixcmd.CMDSRCA * 8 + pos/2;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if ((x & 0x1u) == 0u) dot = (dot>>4)&0xFu;\n"
"      else dot = (dot)&0xFu;\n"
"      if (dot == 0x0Fu) {\n"
"       nbEnd += 1;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 1:\n"
"    {\n"
"      // 4 bpp LUT mode\n"
"       charAddr = pixcmd.CMDSRCA * 8 + pos/2;\n"
"       dot = Vdp1RamReadByte(charAddr);\n"
"       if ((x & 0x1u) == 0u) dot = (dot>>4)&0xFu;\n"
"       else dot = (dot)&0xFu;\n"
"       if (dot == 0x0Fu) {\n"
"        nbEnd += 1;\n"
"       }\n"
"       break;\n"
"    }\n"
"    case 2:\n"
"    {\n"
"      // 8 bpp(64 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFFC0u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if (dot == 0xFFu) {\n"
"       nbEnd += 1;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 3:\n"
"    {\n"
"      // 8 bpp(128 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFF80u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if (dot == 0xFFu) {\n"
"       nbEnd += 1;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 4:\n"
"    {\n"
"      // 8 bpp(256 color) Bank mode\n"
"      uint colorBank = pixcmd.CMDCOLR & 0xFF00u;\n"
"      dot = Vdp1RamReadByte(charAddr);\n"
"      if (dot == 0xFFu) {\n"
"       nbEnd += 1;\n"
"      }\n"
"      break;\n"
"    }\n"
"    case 5:\n"
"    {\n"
"      // 16 bpp Bank mode\n"
"      uint temp;\n"
"      charAddr += pos;\n"
"      temp = Vdp1RamReadWord(charAddr);\n"
"      if (temp == 0x7FFFu) {\n"
"       nbEnd += 1;\n"
"      }\n"
"      break;\n"
"    }\n"
"    default:\n"
"      break;\n"
"   }\n"
"   if (nbEnd > 2) return x;"
"  }\n"
" return 1024;\n"
"}\n"

"void main()\n"
"{\n"
" if ((cmd[0].CMDPMOD & 0x80u) == 0) {\n"
"  uint index = gl_LocalInvocationID.y*gl_WorkGroupSize.x+gl_LocalInvocationID.x;\n"
"  ivec2 texSize = ivec2(((cmd[0].CMDSIZE >> 8) & 0x3F)<<3,cmd[0].CMDSIZE & 0xFF );\n"
"  while (index <= texSize.y) {\n"
"   endIndex[index] = getEndIndex(cmd[0], index);\n"
"   index += gl_WorkGroupSize.x*gl_WorkGroupSize.y;\n"
"  }\n"
"  memoryBarrierShared();"
"  barrier();\n"
" }\n"
"  vec4 finalColor = vec4(0.0);\n"
"  vec4 meshColor = vec4(0.0);\n"
"  vec4 newColor = vec4(0.0);\n"
"  vec4 outColor = vec4(0.0);\n"
"  vec4 pixFB = vec4(0.0);\n"
"  vec3 tag = vec3(0.0);\n"
"  cmdparameter_struct pixcmd;\n"
"  bool discarded = false;\n"
"  vec2 texcoord = vec2(0);\n"
"  vec2 gouraudcoord = vec2(0.0);\n"
"  ivec2 size = imageSize(outSurface);\n"
"  ivec2 pos = ivec2(gl_GlobalInvocationID.xy)+tl;\n"
"  if (pos.x >= size.x || pos.y >= size.y ) return;\n"
"  vec2 texel = vec2(float(pos.x),float(-pos.y));\n"
"  texel.y = -texel.y;\n"

"  uint zone = 0;\n"
"  vec2 OriginTexel = texel;\n"
"  vec2 uv = vec2(0.0);\n"
"  newColor = vec4(0.0);\n"
"  outColor = vec4(0.0);\n"
"  texel = OriginTexel;\n"
"  if (pixIsInside(texel, uv, pixFB) == 0) return;\n"
"  pixcmd = cmd[0];\n"
"  if (any(greaterThan(pos,sysClip*upscale))) { \n"
"    return;\n"
"  }"
"  if (((pixcmd.CMDPMOD >> 9) & 0x3u) == 2u) {\n"
"//Draw inside\n"
"    if (any(lessThan(pos,usrClip.xy*upscale)) || any(greaterThan(pos,usrClip.zw*upscale))) return;\n"
"  }\n"
"  if (((pixcmd.CMDPMOD >> 9) & 0x3u) == 3u) {\n"
"//Draw outside\n"
"    if (all(greaterThanEqual(pos,usrClip.xy*upscale)) && all(lessThanEqual(pos,usrClip.zw*upscale))) return;\n"
"  }\n"
"  texcoord = uv;\n"
"  gouraudcoord = texcoord;\n"
"  if ((pixcmd.flip & 0x1u) == 0x1u) texcoord.x = 1.0 - texcoord.x;\n" //invert horizontally
"  if ((pixcmd.flip & 0x2u) == 0x2u) texcoord.y = 1.0 - texcoord.y - 1.0f/float(pixcmd.h);\n" //invert vertically
"  if (pixcmd.type <= "Stringify(LINE)") {\n"
"    newColor = extractPolygonColor(pixcmd);\n"
"  } else if (pixcmd.type <= "Stringify(QUAD)") {\n"
"    newColor = ReadSpriteColor(pixcmd, texcoord, texel, discarded);\n"
"  }\n"
"  if (discarded) return;\n"
"  texel = OriginTexel;\n"
     COLINDEX(newColor);

static char vdp1_start_f[ sizeof(vdp1_start_f_base) + 64 ];

static const char vdp1_banding_f[] =
"    if ((pixcmd.CMDPMOD & 0x8000u) == 0x8000u) {\n"
"       //MSB shadow\n"
"      finalColor = imageLoad(outSurface, ivec2(texel));\n"
       COLINDEX(finalColor)
       MSB_SHADOW(finalColor)
"      outColor = finalColor;\n"
"    } else {\n"
"      switch (pixcmd.CMDPMOD & 0x7u){\n"
"        case 0u: {\n"
"           // replace_mode\n"
"          outColor = newColor;\n"
"          }; break;\n"
"        case 1u: {\n"
"           //shadow_mode,\n"
"           finalColor = imageLoad(outSurface, ivec2(texel));\n"
           COLINDEX(finalColor)
           SHADOW(finalColor)
"          outColor = finalColor;\n"
"          }; break;\n"
"        case 2u: {\n"
"           //half_luminance_mode,\n"
           HALF_LUMINANCE(newColor)
"          outColor = newColor;\n"
"          }; break;\n"
"        case 3u: {\n"
"           //half_trans_mode,\n"
"          finalColor = imageLoad(outSurface, ivec2(texel));\n"
           COLINDEX(finalColor)
           HALF_TRANPARENT_MIX(newColor, finalColor)
"          outColor = newColor;\n"
"          }; break;\n"
"        case 4u: {\n"
"           //gouraud_mode,\n"
           GOURAUD_PROCESS(newColor)
"          outColor = newColor;\n"
"          }; break;\n"
"        case 6u: {\n"
"           //gouraud_half_luminance_mode,\n"
           GOURAUD_PROCESS(newColor)
           RECOLINDEX(newColor)
           HALF_LUMINANCE(newColor)
"          outColor = newColor;\n"
"          }; break;\n"
"        case 7u: {\n"
"           //gouraud_half_trans_mode,\n"
           GOURAUD_PROCESS(newColor)
           RECOLINDEX(newColor)
"          finalColor = imageLoad(outSurface, ivec2(texel));\n"
           COLINDEX(finalColor)
           HALF_TRANPARENT_MIX(newColor, finalColor)
"          outColor = newColor;\n"
"          }; break;\n"
"        default:\n"
"          return;\n"
"          break;\n"
"      }\n"
"    }\n";

static const char vdp1_no_banding_f[] =
"    if ((pixcmd.CMDPMOD & 0x8000u) == 0x8000u) {\n"
"     //MSB shadow\n"
"      finalColor = imageLoad(outSurface, ivec2(texel));\n"
       COLINDEX(finalColor)
       MSB_SHADOW(finalColor)
"      outColor = finalColor;\n"
"    } else {\n"
"      switch (pixcmd.CMDPMOD & 0x7u){\n"
"        case 0u: {\n"
"           // replace_mode\n"
"          outColor = newColor;\n"
"          }; break;\n"
"        case 1u: {\n"
"           //shadow_mode,\n"
"          finalColor = imageLoad(outSurface, ivec2(texel));\n"
           COLINDEX(finalColor)
           SHADOW(finalColor)
"          outColor = finalColor;\n"
"          }; break;\n"
"        case 2u: {\n"
"           //half_luminance_mode,\n"
           HALF_LUMINANCE(newColor)
"          outColor = newColor;\n"
"          }; break;\n"
"        case 3u: {\n"
"           //half_trans_mode,\n"
"          finalColor = imageLoad(outSurface, ivec2(texel));\n"
           COLINDEX(finalColor)
           HALF_TRANPARENT_MIX(newColor, finalColor)
"          outColor = newColor;\n"
"          }; break;\n"
"        case 4u: {\n"
"           //gouraud_mode,\n"
           GOURAUD_PROCESS_EXTENDED(newColor)
"          outColor = newColor;\n"
"          }; break;\n"
"        case 6u: {\n"
"           //gouraud_half_luminance_mode,\n"
           GOURAUD_PROCESS_EXTENDED(newColor)
           RECOLINDEX(newColor)
           //MSB bits in .ba has to be divided by two also...
           HALF_LUMINANCE(newColor)
"          outColor = newColor;\n"
"          }; break;\n"
"        case 7u: {\n"
"           //gouraud_half_trans_mode,\n"
           GOURAUD_PROCESS_EXTENDED(newColor)
           RECOLINDEX(newColor)
           //MSB bits in .ba has to be divided by two also...
"          finalColor = imageLoad(outSurface, ivec2(texel));\n"
           COLINDEX(finalColor)
           HALF_TRANPARENT_MIX(newColor, finalColor)
"          outColor = newColor;\n"
"          }; break;\n"
"        default:\n"
"          discarded = true;\n"
"          break;\n"
"      }\n"
"    }\n";

static const char vdp1_standard_mesh_f[] =
"//Normal mesh\n"
"  tag = vec3(0.0);\n"
"  if ((pixcmd.CMDPMOD & 0x100u)==0x100u){\n"//IS_MESH
"    if( (int(texel.y) & 0x01) == 0 ){ \n"
"      if( (int(texel.x) & 0x01) == 0 ){ \n"
"        return;\n"
"      }\n"
"    }else{ \n"
"      if( (int(texel.x) & 0x01) == 1 ){ \n"
"        return;\n"
"      } \n"
"    } \n"
"  }\n";

static const char vdp1_improved_mesh_f[] =
"//Improved mesh\n"
"  if ((pixcmd.CMDPMOD & 0x100u)==0x100u){\n"//IS_MESH
"    tag.rg = outColor.rg;\n"
"    tag.b = 1.0;\n"
"    outColor = finalColor;\n"
"  } else {\n"
"    tag = vec3(0.0);\n"
"  }\n";

static const char vdp1_continue_mesh_f[] =
"      meshColor.rgb = tag;\n"
"      finalColor = outColor;\n";

static const char vdp1_continue_no_mesh_f[] =
"      finalColor = outColor;\n";

static const char vdp1_end_f[] =
"  imageStore(outSurface,ivec2(int(pos.x), int(pos.y)),finalColor);\n"
"}\n";

static const char vdp1_end_mesh_f[] =
"  imageStore(outSurface,ivec2(int(pos.x), int(pos.y)),finalColor);\n"
"  imageStore(meshSurface,ivec2(int(pos.x), int(pos.y)),meshColor);\n"
"}\n";

#ifdef __cplusplus
}
#endif

#endif //VDP1_PROG_COMPUTE_H
