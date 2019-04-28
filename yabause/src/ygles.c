/*  Copyright 2005-2006 Guillaume Duhamel
    Copyright 2005-2006 Theo Berkau
    Copyright 2011-2015 Shinya Miyamoto(devmiyax)

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/


#include <stdlib.h>
#include <math.h>
#include "ygl.h"
#include "yui.h"
#include "vidshared.h"
#include "debug.h"
#include "frameprofile.h"
#include "error.h"


//#define __USE_OPENGL_DEBUG__

#define YGLDEBUG
//#define YGLDEBUG printf
//#define YGLDEBUG LOG
//#define YGLDEBUG yprintf
//#define YGLLOG

extern u8 * Vdp1FrameBuffer[];
static int rebuild_frame_buffer = 0;
int opengl_mode = 1;

extern int WaitVdp2Async(int sync);
extern int YglDrawBackScreen();

static int YglCalcTextureQ( float   *pnts,float *q);

static void waitVdp1End(int id);
static void executeTMVDP1(int in, int out);
static void releaseVDP1FB(int i);

extern vdp2rotationparameter_struct  Vdp1ParaA;

u32 * YglGetColorRamPointer();

int YglGenFrameBuffer();

#define PI 3.1415926535897932384626433832795f

#ifdef VDP1_TEXTURE_ASYNC
extern int waitVdp1Textures( int sync);
#endif

#define ATLAS_BIAS (0.025f)

#if (defined(__ANDROID__) || defined(IOS)) && !defined(__LIBRETRO__)
PFNGLPATCHPARAMETERIPROC glPatchParameteri = NULL;
PFNGLMEMORYBARRIERPROC glMemoryBarrier = NULL;
#endif

#if defined(__USE_OPENGL_DEBUG__)
static void MessageCallback( GLenum source,
                      GLenum type,
                      GLuint id,
                      GLenum severity,
                      GLsizei length,
                      const GLchar* message,
                      const void* userParam )
{
#ifndef __WIN32__
  printf("GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
           ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
            type, severity, message );
#endif
}
#endif

void YglScalef(YglMatrix *result, GLfloat sx, GLfloat sy, GLfloat sz)
{
    result->m[0][0] *= sx;
    result->m[0][1] *= sx;
    result->m[0][2] *= sx;
    result->m[0][3] *= sx;

    result->m[1][0] *= sy;
    result->m[1][1] *= sy;
    result->m[1][2] *= sy;
    result->m[1][3] *= sy;

    result->m[2][0] *= sz;
    result->m[2][1] *= sz;
    result->m[2][2] *= sz;
    result->m[2][3] *= sz;
}

void YglTranslatef(YglMatrix *result, GLfloat tx, GLfloat ty, GLfloat tz)
{
    result->m[0][3] += (result->m[0][0] * tx + result->m[0][1] * ty + result->m[0][2] * tz);
    result->m[1][3] += (result->m[1][0] * tx + result->m[1][1] * ty + result->m[1][2] * tz);
    result->m[2][3] += (result->m[2][0] * tx + result->m[2][1] * ty + result->m[2][2] * tz);
    result->m[3][3] += (result->m[3][0] * tx + result->m[3][1] * ty + result->m[3][2] * tz);
}

void YglRotatef(YglMatrix *result, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
   GLfloat sinAngle, cosAngle;
   GLfloat mag = sqrtf(x * x + y * y + z * z);

   sinAngle = sinf ( angle * PI / 180.0f );
   cosAngle = cosf ( angle * PI / 180.0f );
   if ( mag > 0.0f )
   {
      GLfloat xx, yy, zz, xy, yz, zx, xs, ys, zs;
      GLfloat oneMinusCos;
      YglMatrix rotMat;

      x /= mag;
      y /= mag;
      z /= mag;

      xx = x * x;
      yy = y * y;
      zz = z * z;
      xy = x * y;
      yz = y * z;
      zx = z * x;
      xs = x * sinAngle;
      ys = y * sinAngle;
      zs = z * sinAngle;
      oneMinusCos = 1.0f - cosAngle;

      rotMat.m[0][0] = (oneMinusCos * xx) + cosAngle;
      rotMat.m[0][1] = (oneMinusCos * xy) - zs;
      rotMat.m[0][2] = (oneMinusCos * zx) + ys;
      rotMat.m[0][3] = 0.0F;

      rotMat.m[1][0] = (oneMinusCos * xy) + zs;
      rotMat.m[1][1] = (oneMinusCos * yy) + cosAngle;
      rotMat.m[1][2] = (oneMinusCos * yz) - xs;
      rotMat.m[1][3] = 0.0F;

      rotMat.m[2][0] = (oneMinusCos * zx) - ys;
      rotMat.m[2][1] = (oneMinusCos * yz) + xs;
      rotMat.m[2][2] = (oneMinusCos * zz) + cosAngle;
      rotMat.m[2][3] = 0.0F;

      rotMat.m[3][0] = 0.0F;
      rotMat.m[3][1] = 0.0F;
      rotMat.m[3][2] = 0.0F;
      rotMat.m[3][3] = 1.0F;

      YglMatrixMultiply( result, &rotMat, result );
   }
}

void YglFrustum(YglMatrix *result, float left, float right, float bottom, float top, float nearZ, float farZ)
{
    float       deltaX = right - left;
    float       deltaY = top - bottom;
    float       deltaZ = farZ - nearZ;
    YglMatrix    frust;

    if ( (nearZ <= 0.0f) || (farZ <= 0.0f) ||
         (deltaX <= 0.0f) || (deltaY <= 0.0f) || (deltaZ <= 0.0f) )
         return;

    frust.m[0][0] = 2.0f * nearZ / deltaX;
    frust.m[0][1] = frust.m[0][2] = frust.m[0][3] = 0.0f;

    frust.m[1][1] = 2.0f * nearZ / deltaY;
    frust.m[1][0] = frust.m[1][2] = frust.m[1][3] = 0.0f;

    frust.m[2][0] = (right + left) / deltaX;
    frust.m[2][1] = (top + bottom) / deltaY;
    frust.m[2][2] = -(nearZ + farZ) / deltaZ;
    frust.m[2][3] = -1.0f;

    frust.m[3][2] = -2.0f * nearZ * farZ / deltaZ;
    frust.m[3][0] = frust.m[3][1] = frust.m[3][3] = 0.0f;

    YglMatrixMultiply(result, &frust, result);
}


void YglPerspective(YglMatrix *result, float fovy, float aspect, float nearZ, float farZ)
{
   GLfloat frustumW, frustumH;

   frustumH = tanf( fovy / 360.0f * PI ) * nearZ;
   frustumW = frustumH * aspect;

   YglFrustum( result, -frustumW, frustumW, -frustumH, frustumH, nearZ, farZ );
}

void YglOrtho(YglMatrix *result, float left, float right, float bottom, float top, float nearZ, float farZ)
{
    float       deltaX = right - left;
    float       deltaY = top - bottom;
    float       deltaZ = farZ - nearZ;
    YglMatrix    ortho;

    if ( (deltaX == 0.0f) || (deltaY == 0.0f) || (deltaZ == 0.0f) )
        return;

    YglLoadIdentity(&ortho);
    ortho.m[0][0] = 2.0f / deltaX;
    ortho.m[0][3] = -(right + left) / deltaX;
    ortho.m[1][1] = 2.0f / deltaY;
    ortho.m[1][3] = -(top + bottom) / deltaY;
    ortho.m[2][2] = -2.0f / deltaZ;
    ortho.m[2][3] = -(nearZ + farZ) / deltaZ;

    YglMatrixMultiply(result, &ortho, result);
}

void YglTransform(YglMatrix *mtx, float * inXyz, float * outXyz )
{
    outXyz[0] = inXyz[0] * mtx->m[0][0] + inXyz[0] * mtx->m[0][1]  + inXyz[0] * mtx->m[0][2] + mtx->m[0][3];
    outXyz[1] = inXyz[1] * mtx->m[1][0] + inXyz[1] * mtx->m[1][1]  + inXyz[1] * mtx->m[1][2] + mtx->m[1][3];
    outXyz[2] = inXyz[2] * mtx->m[2][0] + inXyz[2] * mtx->m[2][1]  + inXyz[2] * mtx->m[2][2] + mtx->m[2][3];
}

void YglMatrixMultiply(YglMatrix *result, YglMatrix *srcA, YglMatrix *srcB)
{
    YglMatrix    tmp;
    int         i;

    for (i=0; i<4; i++)
    {
        tmp.m[i][0] =   (srcA->m[i][0] * srcB->m[0][0]) +
                        (srcA->m[i][1] * srcB->m[1][0]) +
                        (srcA->m[i][2] * srcB->m[2][0]) +
                        (srcA->m[i][3] * srcB->m[3][0]) ;

        tmp.m[i][1] =   (srcA->m[i][0] * srcB->m[0][1]) +
                        (srcA->m[i][1] * srcB->m[1][1]) +
                        (srcA->m[i][2] * srcB->m[2][1]) +
                        (srcA->m[i][3] * srcB->m[3][1]) ;

        tmp.m[i][2] =   (srcA->m[i][0] * srcB->m[0][2]) +
                        (srcA->m[i][1] * srcB->m[1][2]) +
                        (srcA->m[i][2] * srcB->m[2][2]) +
                        (srcA->m[i][3] * srcB->m[3][2]) ;

        tmp.m[i][3] =   (srcA->m[i][0] * srcB->m[0][3]) +
                        (srcA->m[i][1] * srcB->m[1][3]) +
                        (srcA->m[i][2] * srcB->m[2][3]) +
                        (srcA->m[i][3] * srcB->m[3][3]) ;
    }
    memcpy(result, &tmp, sizeof(YglMatrix));
}


void YglLoadIdentity(YglMatrix *result)
{
    memset(result, 0x0, sizeof(YglMatrix));
    result->m[0][0] = 1.0f;
    result->m[1][1] = 1.0f;
    result->m[2][2] = 1.0f;
    result->m[3][3] = 1.0f;
}


YglTextureManager * YglTM_vdp2 = NULL;
YglTextureManager * YglTM_vdp1[2] = { NULL, NULL };
Ygl * _Ygl;

typedef struct
{
   float s, t, r, q;
} texturecoordinate_struct;


extern int GlHeight;
extern int GlWidth;
extern int vdp1cor;
extern int vdp1cog;
extern int vdp1cob;

extern int maxWidth;
extern int maxHeight;

#define STD_Q2 (1.0f)
#define EPS (1e-10)
#define EQ(a,b) (abs((a)-(b)) < EPS)
#define IS_ZERO(a) ( (a) < EPS && (a) > -EPS)

// AXB = |A||B|sin
static INLINE float cross2d( float veca[2], float vecb[2] )
{
   return (veca[0]*vecb[1])-(vecb[0]*veca[1]);
}

/*-----------------------------------------
    b1+--+ a1
     /  / \
    /  /   \
  a2+-+-----+b2
      ans

  get intersection point for opssite edge.
--------------------------------------------*/
int FASTCALL YglIntersectionOppsiteEdge(float * a1, float * a2, float * b1, float * b2, float * out )
{
  float veca[2];
  float vecb[2];
  float vecc[2];
  float d1;
  float d2;

  veca[0]=a2[0]-a1[0];
  veca[1]=a2[1]-a1[1];
  vecb[0]=b1[0]-a1[0];
  vecb[1]=b1[1]-a1[1];
  vecc[0]=b2[0]-a1[0];
  vecc[1]=b2[1]-a1[1];
  d1 = cross2d(vecb,vecc);
  if( IS_ZERO(d1) ) return -1;
  d2 = cross2d(vecb,veca);

  out[0] = a1[0]+vecc[0]*d2/d1;
  out[1] = a1[1]+vecc[1]*d2/d1;

  return 0;
}





int YglCalcTextureQ(
   float   *pnts,
   float *q
)
{
   float p1[2],p2[2],p3[2],p4[2],o[2];
   float   q1, q3, q4, qw;
   float   dx, w;
   float   ww;
#if 0
   // fast calculation for triangle
   if (( pnts[2*0+0] == pnts[2*1+0] ) && ( pnts[2*0+1] == pnts[2*1+1] )) {
      q[0] = 1.0f;
      q[1] = 1.0f;
      q[2] = 1.0f;
      q[3] = 1.0f;
      return 0;

   } else if (( pnts[2*1+0] == pnts[2*2+0] ) && ( pnts[2*1+1] == pnts[2*2+1] ))  {
      q[0] = 1.0f;
      q[1] = 1.0f;
      q[2] = 1.0f;
      q[3] = 1.0f;
      return 0;
   } else if (( pnts[2*2+0] == pnts[2*3+0] ) && ( pnts[2*2+1] == pnts[2*3+1] ))  {
      q[0] = 1.0f;
      q[1] = 1.0f;
      q[2] = 1.0f;
      q[3] = 1.0f;
      return 0;
   } else if (( pnts[2*3+0] == pnts[2*0+0] ) && ( pnts[2*3+1] == pnts[2*0+1] )) {
      q[0] = 1.0f;
      q[1] = 1.0f;
      q[2] = 1.0f;
      q[3] = 1.0f;
      return 0;
   }
#endif
   p1[0]=pnts[0];
   p1[1]=pnts[1];
   p2[0]=pnts[2];
   p2[1]=pnts[3];
   p3[0]=pnts[4];
   p3[1]=pnts[5];
   p4[0]=pnts[6];
   p4[1]=pnts[7];

   // calcurate Q1
   if( YglIntersectionOppsiteEdge( p3, p1, p2, p4,  o ) == 0 )
   {
      dx = o[0]-p1[0];
      if( !IS_ZERO(dx) )
      {
         w = p3[0]-p2[0];
         if( !IS_ZERO(w) )
          q1 = fabs(dx/w);
         else
          q1 = 0.0f;
      }else{
         w = p3[1] - p2[1];
         if ( !IS_ZERO(w) )
         {
            ww = ( o[1] - p1[1] );
            if ( !IS_ZERO(ww) )
               q1 = fabs(ww / w);
            else
               q1 = 0.0f;
         } else {
            q1 = 0.0f;
         }
      }
   }else{
      q1 = 1.0f;
   }

   /* q2 = 1.0f; */

   // calcurate Q3
   if( YglIntersectionOppsiteEdge( p1, p3, p2,p4,  o ) == 0 )
   {
      dx = o[0]-p3[0];
      if( !IS_ZERO(dx) )
      {
         w = p1[0]-p2[0];
         if( !IS_ZERO(w) )
          q3 = fabs(dx/w);
         else
          q3 = 0.0f;
      }else{
         w = p1[1] - p2[1];
         if ( !IS_ZERO(w) )
         {
            ww = ( o[1] - p3[1] );
            if ( !IS_ZERO(ww) )
               q3 = fabs(ww / w);
            else
               q3 = 0.0f;
         } else {
            q3 = 0.0f;
         }
      }
   }else{
      q3 = 1.0f;
   }


   // calcurate Q4
   if( YglIntersectionOppsiteEdge( p3, p1, p4, p2,  o ) == 0 )
   {
      dx = o[0]-p1[0];
      if( !IS_ZERO(dx) )
      {
         w = p3[0]-p4[0];
         if( !IS_ZERO(w) )
          qw = fabs(dx/w);
         else
          qw = 0.0f;
      }else{
         w = p3[1] - p4[1];
         if ( !IS_ZERO(w) )
         {
            ww = ( o[1] - p1[1] );
            if ( !IS_ZERO(ww) )
               qw = fabs(ww / w);
            else
               qw = 0.0f;
         } else {
            qw = 0.0f;
         }
      }
      if ( !IS_ZERO(qw) )
      {
         w   = qw / q1;
      }
      else
      {
         w   = 0.0f;
      }
      if ( IS_ZERO(w) ) {
         q4 = 1.0f;
      } else {
         q4 = 1.0f / w;
      }
   }else{
      q4 = 1.0f;
   }

   qw = q1;
   if ( qw < 1.0f )   /* q2 = 1.0f */
      qw = 1.0f;
   if ( qw < q3 )
      qw = q3;
   if ( qw < q4 )
      qw = q4;

   if ( 1.0f != qw )
   {
      qw      = 1.0f / qw;

      q[0]   = q1 * qw;
      q[1]   = 1.0f * qw;
      q[2]   = q3 * qw;
      q[3]   = q4 * qw;
   }
   else
   {
      q[0]   = q1;
      q[1]   = 1.0f;
      q[2]   = q3;
      q[3]   = q4;
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

YglTextureManager * YglTMInit(unsigned int w, unsigned int h) {

  GLuint error;
  YglTextureManager * tm;
  tm = (YglTextureManager *)malloc(sizeof(YglTextureManager));
  memset(tm, 0, sizeof(YglTextureManager));
  tm->width = w;
  tm->height = h;
  tm->mtx =  YabThreadCreateMutex();

  tm->currentX = 0;
  tm->currentY = 0;
  tm->yMax = 0;

  glGenBuffers(1, &tm->pixelBufferID);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, tm->pixelBufferID);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, tm->width * tm->height * 4, NULL, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  glGenTextures(1, &tm->textureID);
  glBindTexture(GL_TEXTURE_2D, tm->textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tm->width, tm->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindTexture(GL_TEXTURE_2D, tm->textureID);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, tm->pixelBufferID);
  tm->texture = (unsigned int *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, tm->width * tm->height * 4, GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  YglGetColorRamPointer();

  return tm;
}

//////////////////////////////////////////////////////////////////////////////

void YglTMDeInit(YglTextureManager * tm) {
  glDeleteTextures(1, &tm->textureID);
  glDeleteBuffers(1, &tm->pixelBufferID);
  free(tm);
}

//////////////////////////////////////////////////////////////////////////////

void YglTMReset(YglTextureManager * tm  ) {
  YabThreadLock(tm->mtx);
  tm->currentX = 0;
  tm->currentY = 0;
  tm->yMax = 0;
  YabThreadUnLock(tm->mtx);
}

#if 0
void YglTMReserve(YglTextureManager * tm, unsigned int w, unsigned int h){

  if (tm->width < w){
    YGLDEBUG("can't allocate texture: %dx%d\n", w, h);
    YglTMRealloc(tm, w, tm->height);
  }
  if ((tm->height - tm->currentY) < h) {
    YGLDEBUG("can't allocate texture: %dx%d\n", w, h);
    YglTMRealloc(tm, tm->width, tm->height + (h * 2));
    return;
  }
}
#endif
void YglTmPush(YglTextureManager * tm){
#ifdef VDP1_TEXTURE_ASYNC
  if ((tm == YglTM_vdp1[0]) || (tm == YglTM_vdp1[1]))
    waitVdp1Textures(1);
  else WaitVdp2Async(1);
#endif
  YabThreadLock(tm->mtx);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tm->textureID);
  if (tm->texture != NULL ) {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, tm->pixelBufferID);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tm->width, tm->yMax, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    tm->texture = NULL;
  }
  YabThreadUnLock(tm->mtx);
  YglTMReset(tm);
  YglCacheReset(tm);
}

void YglTmPull(YglTextureManager * tm, u32 flg){
  if (tm == YglTM_vdp1[0])
    waitVdp1End(0);
  if (tm == YglTM_vdp1[1])
    waitVdp1End(1);
  YabThreadLock(tm->mtx);
  if (tm->texture == NULL) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tm->textureID);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, tm->pixelBufferID);
    tm->texture = (int*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, tm->width * tm->height * 4, GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_WRITE_BIT | flg | GL_MAP_UNSYNCHRONIZED_BIT  );
    if (tm->texture == NULL){
      abort();
    }
  }
  YabThreadUnLock(tm->mtx);
}


void YglTMCheck()
{
  YglTextureManager * tm = YglTM_vdp1[_Ygl->drawframe];
  if ((tm->width > 3072) || (tm->height > 3072)) {
    executeTMVDP1(_Ygl->drawframe,_Ygl->drawframe);
  }
}

static void YglTMRealloc(YglTextureManager * tm, unsigned int width, unsigned int height ){
  GLuint new_textureID;
  GLuint new_pixelBufferID;
  unsigned int * new_texture;
  GLuint error;
  int dh;

#ifdef VDP1_TEXTURE_ASYNC
  if ((tm == YglTM_vdp1[0]) || (tm == YglTM_vdp1[1]))
    waitVdp1Textures(1);
  else WaitVdp2Async(1);
#endif

  if (tm->texture != NULL) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tm->textureID);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, tm->pixelBufferID);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    tm->texture = NULL;
  }

  glGenTextures(1, &new_textureID);
  glBindTexture(GL_TEXTURE_2D, new_textureID);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


  glGenBuffers(1, &new_pixelBufferID);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, new_pixelBufferID);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, width * height * 4, NULL, GL_DYNAMIC_DRAW);

  dh = tm->height;
  if (dh > height) dh = height;

  glBindBuffer(GL_COPY_READ_BUFFER, tm->pixelBufferID);
  glBindBuffer(GL_COPY_WRITE_BUFFER, new_pixelBufferID);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, tm->width * dh * 4);

  glBindBuffer(GL_COPY_READ_BUFFER, 0);
  glBindBuffer(GL_COPY_WRITE_BUFFER, 0);


  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, new_pixelBufferID);
  new_texture = (unsigned int *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, width * height * 4, GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );

  // Free textures
  glDeleteTextures(1, &tm->textureID);
  glDeleteBuffers(1, &tm->pixelBufferID);

  // user new texture
    tm->width = width;
  tm->height = height;
  tm->texture = new_texture;
  tm->textureID = new_textureID;
  tm->pixelBufferID = new_pixelBufferID;
  return;

}

//////////////////////////////////////////////////////////////////////////////
static void YglTMAllocate_in(YglTextureManager * tm, YglTexture * output, unsigned int w, unsigned int h, unsigned int * x, unsigned int * y) {
  if( tm->width < w ){
    YGLDEBUG("can't allocate texture: %dx%d\n", w, h);
    YglTMRealloc( tm, w, tm->height);
    YglTMAllocate_in(tm, output, w, h, x, y);
    return;
  }
  if ((tm->height - tm->currentY) < h) {
    YGLDEBUG("can't allocate texture: %dx%d\n", w, h);
    YglTMRealloc( tm, tm->width, tm->height+512);
    YglTMAllocate_in(tm, output, w, h, x, y);
    return;
  }

  if ((tm->width - tm->currentX) >= w) {
    *x = tm->currentX;
    *y = tm->currentY;
    output->w = tm->width - w;
    output->textdata = tm->texture + tm->currentY * tm->width + tm->currentX;
    tm->currentX += w;

    if ((tm->currentY + h) > tm->yMax){
      tm->yMax = tm->currentY + h;
    }
   } else {
     tm->currentX = 0;
     tm->currentY = tm->yMax;
     YglTMAllocate_in(tm, output, w, h, x, y);
   }

}

void getCurrentOpenGLContext() {
  YabThreadLock(_Ygl->mutex);
  YuiUseOGLOnThisThread();
}

void releaseCurrentOpenGLContext() {
  YuiRevokeOGLOnThisThread();
  YabThreadUnLock(_Ygl->mutex);
}

void YglTMAllocate(YglTextureManager * tm, YglTexture * output, unsigned int w, unsigned int h, unsigned int * x, unsigned int * y) {
  YabThreadLock(tm->mtx);
  YglTMAllocate_in(tm, output, w, h, x, y);
  YabThreadUnLock(tm->mtx);
}

u32* getVdp1DrawingFBMemWrite(int id) {
  //Ici le read doit etre different du write. Il faut faire un pack dans le cas du read... et un glReadPixel
  u32* fbptr = NULL;
  GLuint error;
  executeTMVDP1(id, id);
  YglGenFrameBuffer();
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->vdp1AccessFB);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->vdp1AccessTex[id], 0);
  glViewport(0,0,_Ygl->rwidth,_Ygl->rheight);
  YglBlitVDP1(_Ygl->vdp1FrameBuff[id*2], 512.0, 256.0, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);

  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1AccessTex[id]);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->vdp1_pbo[id]);
  fbptr = (u32 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 0x40000*2, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  return fbptr;
}

u32* getVdp1DrawingFBMemRead(int id) {
  //Ici le read doit etre different du write. Il faut faire un pack dans le cas du read... et un glReadPixel
  u32* fbptr = NULL;
  GLuint error;
  executeTMVDP1(id, id);
  YglGenFrameBuffer();
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->vdp1AccessFB);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->vdp1AccessTex[id], 0);
  glViewport(0,0,_Ygl->rwidth,_Ygl->rheight);
  YglBlitVDP1(_Ygl->vdp1FrameBuff[id*2], _Ygl->rwidth, _Ygl->rheight, 0);
  //glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);

  //glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1AccessTex[id]);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, _Ygl->vdp1_pbo[id]);
  glReadPixels(0, 0, 512, 256, GL_RGBA, GL_UNSIGNED_BYTE, 0);
  fbptr = (u32 *)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, 0x40000*2, GL_MAP_READ_BIT );
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
  return fbptr;
}

void releaseVDP1DrawingFBMemRead(int id) {
  if (_Ygl->vdp1fb_buf_read[id] == NULL) return;
  glBindBuffer(GL_PIXEL_PACK_BUFFER, _Ygl->vdp1_pbo[id]);
  glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  _Ygl->vdp1fb_buf_read[id] = NULL;
}

u32 COLOR16TO24(u16 temp) {
  if ((temp>>15)&0x1 == 1)
    return (((u32)temp & 0x1F) << 3 | ((u32)temp & 0x3E0) << 6 | ((u32)temp & 0x7C00) << 9);
  else
    return (temp & 0x7FFF);
}

u16 COLOR24TO16(u32 temp) {
  if (((temp >> 30)&0x1) == 0)
    return 0x8000 | ((u32)(temp >> 3)& 0x1F) | ((u32)(temp >> 6)& 0x3E0) | ((u32)(temp >> 9)& 0x7C00);
  else
    return (temp & 0x7FFF);
}

void VIDOGLVdp1WriteFrameBuffer(u32 type, u32 addr, u32 val ) {
  u8 priority = Vdp2Regs->PRISA &0x7;
  int rgb = !((val>>15)&0x1);
  u16 full = 0;
  if (_Ygl->vdp1fb_buf[_Ygl->drawframe] == NULL) {
    releaseVDP1DrawingFBMemRead(_Ygl->drawframe);
    _Ygl->vdp1fb_buf[_Ygl->drawframe] =  getVdp1DrawingFBMemWrite(_Ygl->drawframe);
  }
  switch (type)
  {
  case 0:
    T1WriteByte((u8*)_Ygl->vdp1fb_exactbuf[_Ygl->drawframe], addr, val);
    full = T1ReadWord((u8*)_Ygl->vdp1fb_exactbuf[_Ygl->drawframe],addr&(~0x1));
    rgb = !((full>>15)&0x1);
    T1WriteLong(_Ygl->vdp1fb_buf[_Ygl->drawframe], (addr&(~0x1))*2, VDP1COLOR(rgb, 0, priority, 0, COLOR16TO24(full&0xFFFF)));
    break;
  case 1:
    T1WriteWord((u8*)_Ygl->vdp1fb_exactbuf[_Ygl->drawframe], addr, val);
    T1WriteLong((u8*)_Ygl->vdp1fb_buf[_Ygl->drawframe], addr*2, VDP1COLOR(rgb, 0, priority, 0, COLOR16TO24(val&0xFFFF)));
    break;
  case 2:
    T1WriteLong((u8*)_Ygl->vdp1fb_exactbuf[_Ygl->drawframe], addr, val);
    T1WriteLong((u8*)_Ygl->vdp1fb_buf[_Ygl->drawframe], addr*2+4, VDP1COLOR(rgb, 0, priority, 0, COLOR16TO24(val&0xFFFF)));
    rgb = !(((val>>16)>>15)&0x1);
    T1WriteLong((u8*)_Ygl->vdp1fb_buf[_Ygl->drawframe], addr*2, VDP1COLOR(rgb, 0, priority, 0, COLOR16TO24((val>>16)&0xFFFF)));
    break;
  default:
    break;
  }
  if (val != 0) {
    _Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = 1;
  }
}

void VIDOGLVdp1ReadFrameBuffer(u32 type, u32 addr, void * out) {
    if (_Ygl->vdp1fb_buf_read[_Ygl->drawframe] == NULL) {
      if(_Ygl->vdp1fb_buf[_Ygl->drawframe] != NULL) {
        releaseVDP1FB(_Ygl->drawframe);
      }
      _Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = 0;
      _Ygl->vdp1fb_buf_read[_Ygl->drawframe] =  getVdp1DrawingFBMemRead(_Ygl->drawframe);
    }
    switch (type)
    {
    case 0:
      *(u8*)out = 0x0;
      break;
    case 1:
      *(u16*)out = COLOR24TO16(T1ReadLong((u8*)_Ygl->vdp1fb_buf_read[_Ygl->drawframe], addr*2));
      break;
    case 2:
      *(u32*)out = (COLOR24TO16(T1ReadLong((u8*)_Ygl->vdp1fb_buf_read[_Ygl->drawframe], addr*2))<<16)|(COLOR24TO16(T1ReadLong((u8*)_Ygl->vdp1fb_buf_read[_Ygl->drawframe], addr*2+4)));
      break;
    default:
      break;
    }
}

//////////////////////////////////////////////////////////////////////////////
int YglGenFrameBuffer() {
  int status;
  GLuint error;
  float col[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  if (rebuild_frame_buffer == 0){
    return 0;
  }

  if (_Ygl->upfbo != 0){
    glDeleteFramebuffers(1, &_Ygl->upfbo);
    _Ygl->upfbo = 0;
    glDeleteTextures(1, &_Ygl->upfbotex);
    _Ygl->upfbotex = 0;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  if (_Ygl->vdp1FrameBuff[0] != 0) {
    glDeleteTextures(4,_Ygl->vdp1FrameBuff);
    _Ygl->vdp1FrameBuff[0] = 0;
  }
  glGenTextures(4, _Ygl->vdp1FrameBuff);

  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, maxWidth, maxHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, maxWidth, maxHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[2]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, maxWidth, maxHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[3]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, maxWidth, maxHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  _Ygl->pFrameBuffer = NULL;

  if (_Ygl->vdp1_pbo[0] == 0) {
    GLuint error;
    glGenTextures(2, _Ygl->vdp1AccessTex);
    glGenBuffers(2, _Ygl->vdp1_pbo);
    YGLDEBUG("glGenBuffers %d %d\n",_Ygl->vdp1_pbo[0], _Ygl->vdp1_pbo[1]);

    glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1AccessTex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, _Ygl->vdp1_pbo[0]);
    glBufferData(GL_PIXEL_PACK_BUFFER, 0x40000*2, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1AccessTex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, _Ygl->vdp1_pbo[1]);
    glBufferData(GL_PIXEL_PACK_BUFFER, 0x40000*2, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

   glGenFramebuffers(1, &_Ygl->vdp1AccessFB);
  }

  if (_Ygl->rboid_depth != 0) glDeleteRenderbuffers(1, &_Ygl->rboid_depth);
  glGenRenderbuffers(1, &_Ygl->rboid_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, _Ygl->rboid_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, maxWidth, maxHeight);

  if (_Ygl->vdp1fbo != 0)
    glDeleteFramebuffers(1, &_Ygl->vdp1fbo);

  glGenFramebuffers(1, &_Ygl->vdp1fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->vdp1fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[0], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[1], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[2], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[3], 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _Ygl->rboid_depth);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    YGLDEBUG("YglGenFrameBuffer:Framebuffer line %d status = %08X\n", __LINE__, status);
    abort();
  }
  glClearBufferfv(GL_COLOR, 0, col);
  glClearBufferfv(GL_COLOR, 1, col);
  glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);

  YglGenerateOriginalBuffer();
  YglGenerateBackBuffer();
  YglGenerateWindowBuffer();
  YglGenerateWindowCCBuffer();
  YglGenerateScreenBuffer();

  YGLDEBUG("YglGenFrameBuffer OK\n");
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
  glBindTexture(GL_TEXTURE_2D, 0);
  rebuild_frame_buffer = 0;
  return 0;
}

//////////////////////////////////////////////////////////////////////////////
int YglGenerateWindowBuffer(){

  int status;
  GLuint error;

  YGLDEBUG("YglGenerateWindowBuffer: %d,%d\n", _Ygl->width, _Ygl->height);

  if (_Ygl->window_fbotex[0] != 0) {
    glDeleteTextures(SPRITE,&_Ygl->window_fbotex[0]);
  }
  glGenTextures(SPRITE, &_Ygl->window_fbotex[0]);


  for (int i=0; i<SPRITE; i++) {
    glBindTexture(GL_TEXTURE_2D, _Ygl->window_fbotex[i]);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->rwidth, _Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  if (_Ygl->window_fbo != 0){
    glDeleteFramebuffers(1, &_Ygl->window_fbo);
  }

  glGenFramebuffers(1, &_Ygl->window_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->window_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->window_fbotex[0], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _Ygl->window_fbotex[1], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, _Ygl->window_fbotex[2], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, _Ygl->window_fbotex[3], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, _Ygl->window_fbotex[4], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT5, GL_TEXTURE_2D, _Ygl->window_fbotex[5], 0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    YGLDEBUG("YglGenerateOriginalBuffer:Framebuffer status = %08X\n", status);
    abort();
  }

  _Ygl->window_tex[0] = _Ygl->window_tex[1] = 0;
  return 0;
}

//////////////////////////////////////////////////////////////////////////////
int YglGenerateWindowCCBuffer(){

  int status;
  GLuint error;

  YGLDEBUG("YglGenerateWindowCCBuffer: %d,%d\n", _Ygl->width, _Ygl->height);

  if (_Ygl->window_cc_fbotex != 0) {
    glDeleteTextures(1,&_Ygl->window_cc_fbotex);
  }
  glGenTextures(1, &_Ygl->window_cc_fbotex);

  glBindTexture(GL_TEXTURE_2D, _Ygl->window_cc_fbotex);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->rwidth, _Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  if (_Ygl->window_cc_fbo != 0){
    glDeleteFramebuffers(1, &_Ygl->window_cc_fbo);
  }

  glGenFramebuffers(1, &_Ygl->window_cc_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->window_cc_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->window_cc_fbotex, 0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    YGLDEBUG("YglGenerateOriginalBuffer:Framebuffer status = %08X\n", status);
    abort();
  }

  return 0;
}

//////////////////////////////////////////////////////////////////////////////
int YglGenerateScreenBuffer(){

  int status;
  GLuint error;
  float col[4] = {0.0f,0.0f,0.0f,0.0f};

  YGLDEBUG("YglGenerateScreenBuffer: %d,%d\n", _Ygl->rwidth, _Ygl->rheight);

  if (_Ygl->screen_fbotex[0] != 0) {
    glDeleteTextures(SPRITE,&_Ygl->screen_fbotex[0]);
  }
  glGenTextures(SPRITE, &_Ygl->screen_fbotex[0]);


  for (int i=0; i<SPRITE; i++) {
    glBindTexture(GL_TEXTURE_2D, _Ygl->screen_fbotex[i]);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->rwidth, _Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  if (_Ygl->screen_depth != 0) glDeleteRenderbuffers(1, &_Ygl->screen_depth);
  glGenRenderbuffers(1, &_Ygl->screen_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, _Ygl->screen_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, _Ygl->rwidth, _Ygl->rheight);

  if (_Ygl->screen_fbo != 0){
    glDeleteFramebuffers(1, &_Ygl->screen_fbo);
  }

  glGenFramebuffers(1, &_Ygl->screen_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->screen_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->screen_fbotex[0], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _Ygl->screen_fbotex[1], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, _Ygl->screen_fbotex[2], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, _Ygl->screen_fbotex[3], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, _Ygl->screen_fbotex[4], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT5, GL_TEXTURE_2D, _Ygl->screen_fbotex[5], 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _Ygl->screen_depth);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    YGLDEBUG("YglGenerateOriginalBuffer:Framebuffer status = %08X\n", status);
    abort();
  }
  return 0;
}

//////////////////////////////////////////////////////////////////////////////
int YglGenerateBackBuffer(){

  int status;
  GLuint error;
  float col[4] = {0.0f,0.0f,0.0f,0.0f};

  YGLDEBUG("YglGenerateBackBuffer: %d,%d\n", _Ygl->width, _Ygl->height);

  if (_Ygl->back_fbotex[0] != 0) {
    glDeleteTextures(2,&_Ygl->back_fbotex[0]);
  }
  glGenTextures(2, &_Ygl->back_fbotex[0]);


  for (int i=0; i<2; i++) {
    glBindTexture(GL_TEXTURE_2D, _Ygl->back_fbotex[i]);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->rwidth, _Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  if (_Ygl->back_fbo != 0){
    glDeleteFramebuffers(1, &_Ygl->back_fbo);
  }

  glGenFramebuffers(1, &_Ygl->back_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->back_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->back_fbotex[0], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _Ygl->back_fbotex[1], 0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    YGLDEBUG("YglGenerateOriginalBuffer:Framebuffer status = %08X\n", status);
    abort();
  }
  return 0;
}

//////////////////////////////////////////////////////////////////////////////
int YglGenerateOriginalBuffer(){

  int status;
  GLuint error;
  float col[4] = {0.0f,0.0f,0.0f,0.0f};

  YGLDEBUG("YglGenerateOriginalBuffer: %d,%d\n", _Ygl->width, _Ygl->height);

  if (_Ygl->original_fbotex[0] != 0) {
    glDeleteTextures(NB_RENDER_LAYER,&_Ygl->original_fbotex[0]);
  }
  glGenTextures(NB_RENDER_LAYER, &_Ygl->original_fbotex[0]);


  for (int i=0; i<NB_RENDER_LAYER; i++) {
    glBindTexture(GL_TEXTURE_2D, _Ygl->original_fbotex[i]);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, maxWidth, maxHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  if (_Ygl->original_depth != 0) glDeleteRenderbuffers(1, &_Ygl->original_depth);
  glGenRenderbuffers(1, &_Ygl->original_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, _Ygl->original_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, maxWidth, maxHeight);

  if (_Ygl->original_fbo != 0){
    glDeleteFramebuffers(1, &_Ygl->original_fbo);
  }

  glGenFramebuffers(1, &_Ygl->original_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->original_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->original_fbotex[0], 0);
#ifdef DEBUG_BLIT
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _Ygl->original_fbotex[1], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, _Ygl->original_fbotex[2], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, _Ygl->original_fbotex[3], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, _Ygl->original_fbotex[4], 0);
#endif
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _Ygl->original_depth);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    YGLDEBUG("YglGenerateOriginalBuffer:Framebuffer status = %08X\n", status);
    abort();
  }
  glClearBufferfv(GL_COLOR, 0, col);
  glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);
  return 0;
}

//////////////////////////////////////////////////////////////////////////////
int YglScreenInit(int r, int g, int b, int d) {
  return 0;
}

void deinitLevels(YglLevel * levels, int size) {
  int i, j;
  for (i = 0; i < (size); i++)
  {
    for (j = 0; j < levels[i].prgcount; j++)
    {
      if (levels[i].prg[j].quads)
        free(levels[i].prg[j].quads);
      if (levels[i].prg[j].textcoords)
        free(levels[i].prg[j].textcoords);
      if (levels[i].prg[j].vertexAttribute)
        free(levels[i].prg[j].vertexAttribute);
    }
    free(levels[i].prg);
  }
  free(levels);
}

void initLevels(YglLevel** levels, int size) {
  int i, j;

  if ((*levels = (YglLevel *)malloc(sizeof(YglLevel) * (size))) == NULL){
    return;
  }

  memset(*levels,0,sizeof(YglLevel) * size );
  for(i = 0;i < size ;i++) {
    YglLevel* level = *levels;
    level[i].prgcurrent = 0;
    level[i].uclipcurrent = 0;
    level[i].prgcount = 1;
    level[i].prg = (YglProgram*)malloc(sizeof(YglProgram)*level[i].prgcount);
    memset(  level[i].prg,0,sizeof(YglProgram)*level[i].prgcount);
    if (level[i].prg == NULL){
      return;
    }
    for(j = 0;j < level[i].prgcount; j++) {
      level[i].prg[j].prg=0;
      level[i].prg[j].currentQuad = 0;
      level[i].prg[j].maxQuad = 12 * 2000;
      if ((level[i].prg[j].quads = (float *)malloc(level[i].prg[j].maxQuad * sizeof(float))) == NULL){ return; }
      if ((level[i].prg[j].textcoords = (float *)malloc(level[i].prg[j].maxQuad * sizeof(float) * 2)) == NULL){ return; }
      if ((level[i].prg[j].vertexAttribute = (float *)malloc(level[i].prg[j].maxQuad * sizeof(float) * 2)) == NULL){ return; }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////
int YglInit(int width, int height, unsigned int depth) {
  unsigned int i,j;
  int maj, min;
  void * dataPointer=NULL;
  float col[4] = {0.0f,0.0f,0.0f,0.0f};
  YGLLOG("YglInit(%d,%d,%d);",width,height,depth );

  glGetIntegerv(GL_MAJOR_VERSION, &maj);
  glGetIntegerv(GL_MINOR_VERSION, &min);

#ifndef __LIBRETRO__
  if (maj*10+min < 42) {
   YabSetError(YAB_ERR_CANNOTINIT, _("OpenGL context"));
   YuiMsg("Using OpenGL %d.%d\n", maj, min);
   return -1;
  }
#endif

  if ((_Ygl = (Ygl *)malloc(sizeof(Ygl))) == NULL) {
    return -1;
  }

  memset(_Ygl,0,sizeof(Ygl));

  _Ygl->depth = depth;
  _Ygl->rwidth = 320;
  _Ygl->rheight = 240;
  _Ygl->density = 1;
  _Ygl->resolution_mode = 1;

  initLevels(&_Ygl->vdp2levels, SPRITE);
  initLevels(&_Ygl->vdp1levels, 2);

  if( _Ygl->mutex == NULL){
    _Ygl->mutex = YabThreadCreateMutex();
  }

  if (_Ygl->crammutex == NULL) {
    _Ygl->crammutex = YabThreadCreateMutex();
  }


#if defined(_USEGLEW_) && !defined(__LIBRETRO__)
  glewExperimental=GL_TRUE;
  if (glewInit() != 0) {
    printf("Glew can not init\n");
    YabSetError(YAB_ERR_CANNOTINIT, _("Glew"));
    exit(-1);
  }
#endif

  glGenBuffers(1, &_Ygl->quads_buf);
  glGenBuffers(1, &_Ygl->textcoords_buf);
  glGenBuffers(1, &_Ygl->vertexAttribute_buf);

  glGenVertexArrays(1, &_Ygl->vao);
  glBindVertexArray(_Ygl->vao);
  glGenBuffers(1, &_Ygl->vertices_buf);
  glGenBuffers(1, &_Ygl->texcord_buf);
  glGenBuffers(1, &_Ygl->win0v_buf);
  glGenBuffers(1, &_Ygl->win1v_buf);
  glGenBuffers(1, &_Ygl->vertexPosition_buf);
  glGenBuffers(1, &_Ygl->textureCoordFlip_buf);
  glGenBuffers(1, &_Ygl->textureCoord_buf);
  glGenBuffers(1, &_Ygl->vb_buf);
  glGenBuffers(1, &_Ygl->tb_buf);
  //glEnableVertexAttribArray(_Ygl->vao);

#if defined(__USE_OPENGL_DEBUG__)
  // During init, enable debug output
  glEnable              ( GL_DEBUG_OUTPUT );
  glDebugMessageCallback( (GLDEBUGPROC) MessageCallback, 0 );
#endif

#if defined(__ANDROID__) && !defined(__LIBRETRO__)
  glPatchParameteri = (PFNGLPATCHPARAMETERIPROC)eglGetProcAddress("glPatchParameteri");
  glMemoryBarrier = (PFNGLPATCHPARAMETERIPROC)eglGetProcAddress("glMemoryBarrier");
#endif

  _Ygl->default_fbo = YuiGetFB();
  _Ygl->drawframe = 0;
  _Ygl->readframe = 1;

#if !defined(__LIBRETRO__)
  // This line is causing a black screen on the libretro port
  glGetIntegerv(GL_FRAMEBUFFER_BINDING,&_Ygl->default_fbo);
#endif

  glClearBufferfv(GL_COLOR, 0, col);
  glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);

  YglLoadIdentity(&_Ygl->mtxModelView);
  YglOrtho(&_Ygl->mtxModelView, 0.0f, 320.0f, 224.0f, 0.0f, 10.0f, 0.0f);

  glDisable(GL_BLEND);

  glDisable(GL_DEPTH_TEST);
  glDepthFunc(GL_GEQUAL);

  glCullFace(GL_FRONT_AND_BACK);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DITHER);

  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  YglTM_vdp1[0] = YglTMInit(2048, 2048);
  YglTM_vdp1[1] = YglTMInit(2048, 2048);
  YglTM_vdp2 = YglTMInit(2048, 2048);

  _Ygl->vdp1fb_exactbuf[0] = (u8*)malloc(512*704*2);
  _Ygl->vdp1fb_exactbuf[1] = (u8*)malloc(512*704*2);

  _Ygl->vdp2buf = (u8*)malloc(512 * NB_VDP2_REG);

  _Ygl->smallfbo = 0;
  _Ygl->smallfbotex = 0;
  _Ygl->tmpfbo = 0;
  _Ygl->tmpfbotex = 0;
  _Ygl->upfbo = 0;
  _Ygl->upfbotex = 0;
  _Ygl->upfbo = 0;
  _Ygl->upfbotex = 0;

  if (YglProgramInit() != 0) {
    YGLDEBUG("Fail to YglProgramInit\n");
    abort();
  }

  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo );
  glBindTexture(GL_TEXTURE_2D, 0);
  _Ygl->st = 0;
  _Ygl->aamode = AA_NONE;
  _Ygl->scanline = 0;
  _Ygl->stretch = 0;

  return 0;
}

//////////////////////////////////////////////////////////////////////////////
void YglDeInit(void) {
   unsigned int i,j;

   if (YglTM_vdp1[0] != NULL) YglTMDeInit(YglTM_vdp1[0]);
   if (YglTM_vdp1[1] != NULL) YglTMDeInit(YglTM_vdp1[1]);
   if (YglTM_vdp2 != NULL)    YglTMDeInit(YglTM_vdp2);



   if (_Ygl)
   {
      if(_Ygl->mutex) YabThreadFreeMutex(_Ygl->mutex );

      if (_Ygl->vdp2levels)
      deinitLevels(_Ygl->vdp2levels, SPRITE);
      if (_Ygl->vdp1levels)
      deinitLevels(_Ygl->vdp1levels, 2);

      free(_Ygl);
   }

}


//////////////////////////////////////////////////////////////////////////////

YglProgram * YglGetProgram( YglSprite * input, int prg, YglTextureManager *tm, int prio)
{
   YglLevel   *level;
   YglProgram *program;
   float checkval;

   if (input->priority > 8) {
      VDP1LOG("sprite with priority %d\n", input->priority);
      return NULL;
   }

   if(tm == YglTM_vdp1[_Ygl->drawframe]){
     level = &_Ygl->vdp1levels[_Ygl->drawframe];
   } else {
     level = &_Ygl->vdp2levels[input->idScreen];
   }
   level->blendmode |= (input->blendmode&0x03);
   if( input->uclipmode != level->uclipcurrent ||
     (input->uclipmode !=0 &&
    (level->ux1 != Vdp1Regs->userclipX1 || level->uy1 != Vdp1Regs->userclipY1 ||
    level->ux2 != Vdp1Regs->userclipX2 || level->uy2 != Vdp1Regs->userclipY2) )
     )
   {
      if( input->uclipmode == 0x02 || input->uclipmode == 0x03 )
      {
         YglProgramChange(level,PG_VDP1_STARTUSERCLIP);
         program = &level->prg[level->prgcurrent];
         program->uClipMode = input->uclipmode;
         program->ux1=Vdp1Regs->userclipX1;
         program->uy1=Vdp1Regs->userclipY1;
         program->ux2=Vdp1Regs->userclipX2;
         program->uy2=Vdp1Regs->userclipY2;
         level->ux1=Vdp1Regs->userclipX1;
         level->uy1=Vdp1Regs->userclipY1;
         level->ux2=Vdp1Regs->userclipX2;
         level->uy2=Vdp1Regs->userclipY2;
      }else{
         YglProgramChange(level,PG_VDP1_ENDUSERCLIP);
         program = &level->prg[level->prgcurrent];
         program->uClipMode = input->uclipmode;
      }
      level->uclipcurrent = input->uclipmode;

   }

   checkval = (float)(input->cor) / 255.0f;
   if (checkval != level->prg[level->prgcurrent].color_offset_val[0])
   {
     YglProgramChange(level, prg);
     level->prg[level->prgcurrent].id = input->idScreen;
     level->prg[level->prgcurrent].blendmode = input->blendmode;

   } else if( level->prg[level->prgcurrent].prgid != prg ) {
      YglProgramChange(level,prg);
    level->prg[level->prgcurrent].id = input->idScreen;
    level->prg[level->prgcurrent].blendmode = input->blendmode;
   }
   else if (level->prg[level->prgcurrent].blendmode != input->blendmode){
     YglProgramChange(level, prg);
     level->prg[level->prgcurrent].id = input->idScreen;
     level->prg[level->prgcurrent].blendmode = input->blendmode;
   }
   else if (input->idScreen != level->prg[level->prgcurrent].id ){
     YglProgramChange(level, prg);
     level->prg[level->prgcurrent].id = input->idScreen;
     level->prg[level->prgcurrent].blendmode = input->blendmode;
   }
// for polygon debug
  //else if (prg == PG_VDP1_GOURAUDSHADING ){
  //   YglProgramChange(level, prg);
  //}
   program = &level->prg[level->prgcurrent];

   if ((program->currentQuad + YGL_MAX_NEED_BUFFER) >= program->maxQuad) {
     program->maxQuad += YGL_MAX_NEED_BUFFER*32;
    program->quads = (float *)realloc(program->quads, program->maxQuad * sizeof(float));
      program->textcoords = (float *) realloc(program->textcoords, program->maxQuad * sizeof(float) * 2);
      program->vertexAttribute = (float *) realloc(program->vertexAttribute, program->maxQuad * sizeof(float)*2);
    YglCacheReset(tm);
   }

   return program;
}



//////////////////////////////////////////////////////////////////////////////

static int YglCheckTriangle( const float * point ){
  if ((point[2 * 0 + 0] == point[2 * 1 + 0]) && (point[2 * 0 + 1] == point[2 * 1 + 1])) {
    return 1;
  }
  else if ((point[2 * 1 + 0] == point[2 * 2 + 0]) && (point[2 * 1 + 1] == point[2 * 2 + 1]))  {
    return 1;
  }
  else if ((point[2 * 2 + 0] == point[2 * 3 + 0]) && (point[2 * 2 + 1] == point[2 * 3 + 1]))  {
    return 1;
  }
  else if ((point[2 * 3 + 0] == point[2 * 0 + 0]) && (point[2 * 3 + 1] == point[2 * 0 + 1])) {
    return 1;
  }
  return 0;
}

static int YglTriangleGrowShading_in(YglSprite * input, YglTexture * output, float * colors, YglCache * c, int cash_flg, YglTextureManager *tm);
static int YglQuadGrowShading_in(YglSprite * input, YglTexture * output, float * colors, YglCache * c, int cash_flg, YglTextureManager *tm);
static int YglQuadGrowShading_tesselation_in(YglSprite * input, YglTexture * output, float * colors, YglCache * c, int cash_flg, YglTextureManager *tm);

void YglCacheQuadGrowShading(YglSprite * input, float * colors, YglCache * cache, YglTextureManager *tm){
    if (_Ygl->polygonmode == GPU_TESSERATION) {
      YglQuadGrowShading_tesselation_in(input, NULL, colors, cache, 0, tm);
    }
    else if (_Ygl->polygonmode == CPU_TESSERATION) {
      YglTriangleGrowShading_in(input, NULL, colors, cache, 0, tm);
    }
    else if (_Ygl->polygonmode == PERSPECTIVE_CORRECTION) {
      if (YglCheckTriangle(input->vertices)){
        YglTriangleGrowShading_in(input, NULL, colors, cache, 0, tm);
      }
      else{
        YglQuadGrowShading_in(input, NULL, colors, cache, 0, tm);
      }
    }
}

int YglQuadGrowShading(YglSprite * input, YglTexture * output, float * colors, YglCache * c, YglTextureManager *tm){
  if (_Ygl->polygonmode == GPU_TESSERATION) {
    return YglQuadGrowShading_tesselation_in(input, output, colors, c, 1, tm);
  }
  else if (_Ygl->polygonmode == CPU_TESSERATION) {
    return YglTriangleGrowShading_in(input, output, colors, c, 1, tm);
  }
  else if (_Ygl->polygonmode == PERSPECTIVE_CORRECTION) {
    if (YglCheckTriangle(input->vertices)){
       return YglTriangleGrowShading_in(input, output, colors, c, 1, tm);
    }
    return YglQuadGrowShading_in(input, output, colors, c, 1,tm);
  }
}

int YglTriangleGrowShading_in(YglSprite * input, YglTexture * output, float * colors, YglCache * c, int cash_flg, YglTextureManager *tm ) {
  unsigned int x, y;
  YglProgram *program;
  int prg = PG_VDP1_GOURAUDSHADING;
  float * pos;
  int u, v;
  float *colv;
  texturecoordinate_struct texv[6];
  texturecoordinate_struct * tpos;

//Ajouter un blend mode MSB_SHADOW et faire le rendu en deux passe de programme

  // Select Program
  switch (input->blendmode) {
    case VDP1_COLOR_CL_GROW_HALF_TRANSPARENT:
      prg = PG_VDP1_GOURAUDSHADING_HALFTRANS;
      break;
    case VDP1_COLOR_CL_HALF_LUMINANCE:
      prg = PG_VDP1_HALF_LUMINANCE;
      break;
    case VDP1_COLOR_CL_MESH:
      prg = PG_VDP1_MESH;
      break;
    case VDP1_COLOR_CL_SHADOW:
      prg = PG_VDP1_SHADOW;
      break;
    case VDP1_COLOR_CL_MSB_SHADOW:
      prg = PG_VDP1_MSB_SHADOW;
      break;
    default:
      prg = PG_VDP1_GOURAUDSHADING;
  }

  program = YglGetProgram(input, prg, tm, input->priority);
  if (program == NULL || program->quads == NULL) return -1;

  program->color_offset_val[0] = (float)(input->cor) / 255.0f;
  program->color_offset_val[1] = (float)(input->cog) / 255.0f;
  program->color_offset_val[2] = (float)(input->cob) / 255.0f;
  program->color_offset_val[3] = 0;


  pos = program->quads + program->currentQuad;
  colv = (program->vertexAttribute + (program->currentQuad * 2));
  tpos = (texturecoordinate_struct *)(program->textcoords + (program->currentQuad * 2));

  if (output != NULL){
    YglTMAllocate(tm, output, input->w, input->h, &x, &y);
  }
  else{
    x = c->x;
    y = c->y;
  }

  texv[0].r = texv[1].r = texv[2].r = texv[3].r = texv[4].r = texv[5].r = 0; // these can stay at 0
  texv[0].q = texv[1].q = texv[2].q = texv[3].q = texv[4].q = texv[5].q = 1.0f; // these can stay at 0

  if (input->flip & 0x1) {
    texv[0].s = texv[3].s = texv[5].s = (float)((x + input->w) - ATLAS_BIAS);
    texv[1].s = texv[2].s = texv[4].s = (float)((x)+ATLAS_BIAS);
  }
  else {
    texv[0].s = texv[3].s = texv[5].s = (float)((x)+ATLAS_BIAS);
    texv[1].s = texv[2].s = texv[4].s = (float)((x + input->w) - ATLAS_BIAS);
  }
  if (input->flip & 0x2) {
    texv[0].t = texv[1].t = texv[3].t = (float)((y + input->h) - ATLAS_BIAS);
    texv[2].t = texv[4].t = texv[5].t = (float)((y)+ATLAS_BIAS);
  }
  else {
    texv[0].t = texv[1].t = texv[3].t = (float)((y)+ATLAS_BIAS);
    texv[2].t = texv[4].t = texv[5].t = (float)((y + input->h) - ATLAS_BIAS);
  }

  if (c != NULL && cash_flg == 1)
  {
    switch (input->flip) {
    case 0:
      c->x = texv[0].s; //  *(program->textcoords + ((program->currentQuad + 12 - 12) * 2));
      c->y = texv[0].t; // *(program->textcoords + ((program->currentQuad + 12 - 12) * 2) + 1);
      break;
    case 1:
      c->x = texv[1].s; // *(program->textcoords + ((program->currentQuad + 12 - 10) * 2));
      c->y = texv[0].t; // (program->textcoords + ((program->currentQuad + 12 - 10) * 2) + 1);
      break;
    case 2:
      c->x = texv[0].s; //*(program->textcoords + ((program->currentQuad + 12 - 2) * 2));
      c->y = texv[2].t; // *(program->textcoords + ((program->currentQuad + 12 - 2) * 2) + 1);
      break;
    case 3:
      c->x = texv[1].s; //  *(program->textcoords + ((program->currentQuad + 12 - 4) * 2));
      c->y = texv[2].t; //*(program->textcoords + ((program->currentQuad + 12 - 4) * 2) + 1);
      break;
    }
  }

  {
  int tess_count = YGL_TESS_COUNT;
  float s_step = (float)(texv[2].s-texv[0].s)/(float)tess_count;
  float t_step = (float)(texv[2].t-texv[0].t)/(float)tess_count;

  float vec_ad_x = input->vertices[6] - input->vertices[0];
  float vec_ad_y = input->vertices[7] - input->vertices[1];
  float vec_ad_xs = vec_ad_x / tess_count;
  float vec_ad_ys = vec_ad_y / tess_count;

  float vec_bc_x = input->vertices[4] - input->vertices[2];
  float vec_bc_y = input->vertices[5] - input->vertices[3];
  float vec_bc_xs = vec_bc_x / tess_count;
  float vec_bc_ys = vec_bc_y / tess_count;

  for (v = 0; v < tess_count ; v++){

    // Top Line for current row
    float ax = input->vertices[0] + vec_ad_xs * v;
    float ay = input->vertices[1] + vec_ad_ys * v;
    float bx = input->vertices[2] + vec_bc_xs * v;
    float by = input->vertices[3] + vec_bc_ys * v;
    float ab_step_x = (bx - ax) / tess_count;
    float ab_step_y = (by - ay) / tess_count;

    // botton Line for current row
    float cx = input->vertices[2] + vec_bc_xs * (v + 1);
    float cy = input->vertices[3] + vec_bc_ys * (v + 1);
    float dx = input->vertices[0] + vec_ad_xs * (v + 1);
    float dy = input->vertices[1] + vec_ad_ys * (v + 1);

    float dc_step_x = (cx - dx) / tess_count;
    float dc_step_y = (cy - dy) / tess_count;

    for (u = 0; u < tess_count ; u++){

      float * cpos = &pos[12*(u + tess_count*v) ];
      texturecoordinate_struct * ctpos = &tpos[6 * (u + tess_count*v)];
      float * vtxa = &colv[24 * (u + tess_count*v)];

      /*
        A+--+B
         |  |
        D+--+C
      */
      float dax = ax + ab_step_x * u;
      float day = ay + ab_step_y * u;
      float dbx = dax + ab_step_x;
      float dby = day + ab_step_y;
      float ddx = dx + dc_step_x * u;
      float ddy = dy + dc_step_y * u;
      float dcx = ddx + dc_step_x;
      float dcy = ddy + dc_step_y;

      cpos[0] = dax;
      cpos[1] = day;
      cpos[2] = dbx;
      cpos[3] = dby;
      cpos[4] = dcx;
      cpos[5] = dcy;

      cpos[6] = dax;
      cpos[7] = day;
      cpos[8] = dcx;
      cpos[9] = dcy;
      cpos[10] = ddx;
      cpos[11] = ddy;

      ctpos[0].s = texv[0].s + s_step * u;
      ctpos[0].t = texv[0].t + t_step * v;
      ctpos[1].s = ctpos[0].s + s_step;
      ctpos[1].t = ctpos[0].t;
      ctpos[2].s = ctpos[0].s + s_step;
      ctpos[2].t = ctpos[0].t + t_step;

      ctpos[3].s = ctpos[0].s;
      ctpos[3].t = ctpos[0].t;
      ctpos[4].s = ctpos[2].s;
      ctpos[4].t = ctpos[2].t;
      ctpos[5].s = ctpos[0].s;
      ctpos[5].t = ctpos[0].t + t_step;
      ctpos[0].r = ctpos[1].r = ctpos[2].r = ctpos[3].r = ctpos[4].r = ctpos[5].r = 0; // these can stay at 0
      ctpos[0].q = ctpos[1].q = ctpos[2].q = ctpos[3].q = ctpos[4].q = ctpos[5].q = 1.0f; // these can stay at 0

      // ToDo: color interpolation
      if (colors == NULL) {
        memset(vtxa, 0, sizeof(float) * 24);
      }
      else {

        int uindex = u;
        int vindex = v;
        vtxa[0] = (colors[0] * (tess_count - uindex) + colors[4] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[12] * (tess_count - u) + colors[8] * uindex) / (float)tess_count * vindex;
        vtxa[1] = (colors[1] * (tess_count - uindex) + colors[5] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[13] * (tess_count - u) + colors[9] * uindex) / (float)tess_count * vindex;
        vtxa[2] = (colors[2] * (tess_count - uindex) + colors[6] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[14] * (tess_count - u) + colors[10] * uindex) / (float)tess_count * vindex;
        vtxa[3] = (colors[3] * (tess_count - uindex) + colors[7] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[15] * (tess_count - u) + colors[11] * uindex) / (float)tess_count * vindex;
        vtxa[0] /= (float)tess_count;
        vtxa[1] /= (float)tess_count;
        vtxa[2] /= (float)tess_count;
        vtxa[3] /= (float)tess_count;

        uindex = u + 1;
        vindex = v;
        vtxa[4] = (colors[0] * (tess_count - uindex) + colors[4] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[12] * (tess_count - u) + colors[8] * uindex) / (float)tess_count * vindex;
        vtxa[5] = (colors[1] * (tess_count - uindex) + colors[5] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[13] * (tess_count - u) + colors[9] * uindex) / (float)tess_count * vindex;
        vtxa[6] = (colors[2] * (tess_count - uindex) + colors[6] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[14] * (tess_count - u) + colors[10] * uindex) / (float)tess_count * vindex;
        vtxa[7] = (colors[3] * (tess_count - uindex) + colors[7] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[15] * (tess_count - u) + colors[11] * uindex) / (float)tess_count * vindex;
        vtxa[4] /= (float)tess_count;
        vtxa[5] /= (float)tess_count;
        vtxa[6] /= (float)tess_count;
        vtxa[7] /= (float)tess_count;

        uindex = u + 1;
        vindex = v + 1;
        vtxa[8] = (colors[0] * (tess_count - uindex) + colors[4] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[12] * (tess_count - u) + colors[8] * uindex) / (float)tess_count * vindex;
        vtxa[9] = (colors[1] * (tess_count - uindex) + colors[5] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[13] * (tess_count - u) + colors[9] * uindex) / (float)tess_count * vindex;
        vtxa[10] = (colors[2] * (tess_count - uindex) + colors[6] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[14] * (tess_count - u) + colors[10] * uindex) / (float)tess_count * vindex;
        vtxa[11] = (colors[3] * (tess_count - uindex) + colors[7] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[15] * (tess_count - u) + colors[11] * uindex) / (float)tess_count * vindex;
        vtxa[8] /= (float)tess_count;
        vtxa[9] /= (float)tess_count;
        vtxa[10] /= (float)tess_count;
        vtxa[11] /= (float)tess_count;

        vtxa[12] = vtxa[0];
        vtxa[13] = vtxa[1];
        vtxa[14] = vtxa[2];
        vtxa[15] = vtxa[3];

        vtxa[16] = vtxa[8];
        vtxa[17] = vtxa[9];
        vtxa[18] = vtxa[10];
        vtxa[19] = vtxa[11];

        uindex = u;
        vindex = v + 1;
        vtxa[20] = (colors[0] * (tess_count - uindex) + colors[4] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[12] * (tess_count - u) + colors[8] * uindex) / (float)tess_count * vindex;
        vtxa[21] = (colors[1] * (tess_count - uindex) + colors[5] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[13] * (tess_count - u) + colors[9] * uindex) / (float)tess_count * vindex;
        vtxa[22] = (colors[2] * (tess_count - uindex) + colors[6] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[14] * (tess_count - u) + colors[10] * uindex) / (float)tess_count * vindex;
        vtxa[23] = (colors[3] * (tess_count - uindex) + colors[7] * uindex) / (float)tess_count * (tess_count - vindex) + (colors[15] * (tess_count - u) + colors[11] * uindex) / (float)tess_count * vindex;
        vtxa[20] /= (float)tess_count;
        vtxa[21] /= (float)tess_count;
        vtxa[22] /= (float)tess_count;
        vtxa[23] /= (float)tess_count;
      }

    }
  }
  program->currentQuad = program->currentQuad + (12*tess_count*tess_count);
  }
  return 0;
}

int YglQuadGrowShading_in(YglSprite * input, YglTexture * output, float * colors, YglCache * c, int cash_flg, YglTextureManager *tm) {
   unsigned int x, y;
   YglProgram *program;
   texturecoordinate_struct *tmp;
   float * vtxa;
   float q[4];
   int prg = PG_VDP1_GOURAUDSHADING;
   float * pos;


  switch (input->blendmode) {
    case VDP1_COLOR_CL_GROW_HALF_TRANSPARENT:
      prg = PG_VDP1_GOURAUDSHADING_HALFTRANS;
      break;
    case VDP1_COLOR_CL_HALF_LUMINANCE:
      prg = PG_VDP1_HALF_LUMINANCE;
      break;
    case VDP1_COLOR_CL_MESH:
      prg = PG_VDP1_MESH;
      break;
    case VDP1_COLOR_CL_SHADOW:
      prg = PG_VDP1_SHADOW;
      break;
    case VDP1_COLOR_CL_MSB_SHADOW:
      prg = PG_VDP1_MSB_SHADOW;
      break;
    default:
      prg = PG_VDP1_GOURAUDSHADING;
  }

   program = YglGetProgram(input,prg,tm,input->priority);
   if( program == NULL ) return -1;
   //YGLLOG( "program->quads = %X,%X,%d/%d\n",program->quads,program->vertexBuffer,program->currentQuad,program->maxQuad );
   if( program->quads == NULL ) {
       int a=0;
   }

   program->color_offset_val[0] = (float)(input->cor)/255.0f;
   program->color_offset_val[1] = (float)(input->cog)/255.0f;
   program->color_offset_val[2] = (float)(input->cob)/255.0f;
   program->color_offset_val[3] = 0;

   if (output != NULL){
     YglTMAllocate(tm, output, input->w, input->h, &x, &y);
   }
   else{
     x = c->x;
     y = c->y;
   }

   // Vertex
   pos = program->quads + program->currentQuad;

   pos[0] = input->vertices[0];
   pos[1] = input->vertices[1];
   pos[2] = input->vertices[2];
   pos[3] = input->vertices[3];
   pos[4] = input->vertices[4];
   pos[5] = input->vertices[5];
   pos[6] = input->vertices[0];
   pos[7] = input->vertices[1];
   pos[8] = input->vertices[4];
   pos[9] = input->vertices[5];
   pos[10] = input->vertices[6];
   pos[11] = input->vertices[7];


   // Color
   vtxa = (program->vertexAttribute + (program->currentQuad * 2));
   if( colors == NULL ) {
      memset(vtxa,0,sizeof(float)*24);
   } else {
     vtxa[0] = colors[0];
     vtxa[1] = colors[1];
     vtxa[2] = colors[2];
     vtxa[3] = colors[3];

     vtxa[4] = colors[4];
     vtxa[5] = colors[5];
     vtxa[6] = colors[6];
     vtxa[7] = colors[7];

     vtxa[8] = colors[8];
     vtxa[9] = colors[9];
     vtxa[10] = colors[10];
     vtxa[11] = colors[11];

     vtxa[12] = colors[0];
     vtxa[13] = colors[1];
     vtxa[14] = colors[2];
     vtxa[15] = colors[3];

     vtxa[16] = colors[8];
     vtxa[17] = colors[9];
     vtxa[18] = colors[10];
     vtxa[19] = colors[11];

     vtxa[20] = colors[12];
     vtxa[21] = colors[13];
     vtxa[22] = colors[14];
     vtxa[23] = colors[15];
   }

   // texture
   tmp = (texturecoordinate_struct *)(program->textcoords + (program->currentQuad * 2));

   program->currentQuad += 12;

   tmp[0].r = tmp[1].r = tmp[2].r = tmp[3].r = tmp[4].r = tmp[5].r = 0; // these can stay at 0
   if (input->flip & 0x1) {
     tmp[0].s = tmp[3].s = tmp[5].s = (float)((x + input->w) - ATLAS_BIAS) ;
     tmp[1].s = tmp[2].s = tmp[4].s = (float)((x)+ATLAS_BIAS) ;
   } else {
     tmp[0].s = tmp[3].s = tmp[5].s = (float)((x)+ATLAS_BIAS) ;
     tmp[1].s = tmp[2].s = tmp[4].s = (float)((x + input->w) - ATLAS_BIAS);
   }
   if (input->flip & 0x2) {
     tmp[0].t = tmp[1].t = tmp[3].t = (float)((y + input->h) - ATLAS_BIAS);
     tmp[2].t = tmp[4].t = tmp[5].t = (float)((y)+ATLAS_BIAS);
   } else {
     tmp[0].t = tmp[1].t = tmp[3].t = (float)((y)+ATLAS_BIAS);
     tmp[2].t = tmp[4].t = tmp[5].t = (float)((y + input->h) - ATLAS_BIAS);
   }

   if (c != NULL && cash_flg == 1)
   {
      switch(input->flip) {
        case 0:
          c->x = *(program->textcoords + ((program->currentQuad - 12) * 2));   // upper left coordinates(0)
          c->y = *(program->textcoords + ((program->currentQuad - 12) * 2)+1); // upper left coordinates(0)
          break;
        case 1:
          c->x = *(program->textcoords + ((program->currentQuad - 10) * 2));   // upper left coordinates(0)
          c->y = *(program->textcoords + ((program->currentQuad - 10) * 2)+1); // upper left coordinates(0)
          break;
       case 2:
          c->x = *(program->textcoords + ((program->currentQuad - 2) * 2));   // upper left coordinates(0)
          c->y = *(program->textcoords + ((program->currentQuad - 2) * 2)+1); // upper left coordinates(0)
          break;
       case 3:
          c->x = *(program->textcoords + ((program->currentQuad - 4) * 2));   // upper left coordinates(0)
          c->y = *(program->textcoords + ((program->currentQuad - 4) * 2)+1); // upper left coordinates(0)
          break;
      }
   }

   if( input->dst == 1 )
   {
      YglCalcTextureQ(input->vertices,q);

      tmp[0].s *= q[0];
      tmp[0].t *= q[0];
      tmp[1].s *= q[1];
      tmp[1].t *= q[1];
      tmp[2].s *= q[2];
      tmp[2].t *= q[2];
      tmp[3].s *= q[0];
      tmp[3].t *= q[0];
      tmp[4].s *= q[2];
      tmp[4].t *= q[2];
      tmp[5].s *= q[3];
      tmp[5].t *= q[3];

      tmp[0].q = q[0];
      tmp[1].q = q[1];
      tmp[2].q = q[2];
      tmp[3].q = q[0];
      tmp[4].q = q[2];
      tmp[5].q = q[3];
   }else{
      tmp[0].q = 1.0f;
      tmp[1].q = 1.0f;
      tmp[2].q = 1.0f;
      tmp[3].q = 1.0f;
      tmp[4].q = 1.0f;
      tmp[5].q = 1.0f;
   }

   return 0;
}

int YglQuadGrowShading_tesselation_in(YglSprite * input, YglTexture * output, float * colors, YglCache * c, int cash_flg, YglTextureManager *tm) {
  unsigned int x, y;
  YglProgram *program;
  texturecoordinate_struct *tmp;
  float * vtxa;
  int prg;
  float * pos;

  switch (input->blendmode) {
    case VDP1_COLOR_CL_GROW_HALF_TRANSPARENT:
      prg = PG_VDP1_GOURAUDSHADING_HALFTRANS_TESS;
    break;
    case VDP1_COLOR_CL_MESH:
      prg = PG_VDP1_MESH_TESS;
    break;
    case VDP1_COLOR_CL_SHADOW:
      prg = PG_VDP1_SHADOW_TESS;
    break;
    case VDP1_COLOR_CL_MSB_SHADOW:
      prg = PG_VDP1_MSB_SHADOW_TESS;
      break;
    default:
      prg = PG_VDP1_GOURAUDSHADING_TESS;
  }

  program = YglGetProgram(input, prg, tm,input->priority);
  if (program == NULL) return -1;
  //YGLLOG( "program->quads = %X,%X,%d/%d\n",program->quads,program->vertexBuffer,program->currentQuad,program->maxQuad );
  if (program->quads == NULL) {
    int a = 0;
  }

  program->color_offset_val[0] = (float)(input->cor) / 255.0f;
  program->color_offset_val[1] = (float)(input->cog) / 255.0f;
  program->color_offset_val[2] = (float)(input->cob) / 255.0f;
  program->color_offset_val[3] = 0.0;

  if (output != NULL){
    YglTMAllocate(tm, output, input->w, input->h, &x, &y);
  }
  else{
    x = c->x;
    y = c->y;
  }

  // Vertex
  pos = program->quads + program->currentQuad;

  pos[0] = input->vertices[0];
  pos[1] = input->vertices[1];
  pos[2] = input->vertices[2];
  pos[3] = input->vertices[3];
  pos[4] = input->vertices[4];
  pos[5] = input->vertices[5];
  pos[6] = input->vertices[6];
  pos[7] = input->vertices[7];


  // Color
  vtxa = (program->vertexAttribute + (program->currentQuad * 2));
  if (colors == NULL) {
    memset(vtxa, 0, sizeof(float) * 24);
  }
  else {
    vtxa[0] = colors[0];
    vtxa[1] = colors[1];
    vtxa[2] = colors[2];
    vtxa[3] = colors[3];

    vtxa[4] = colors[4];
    vtxa[5] = colors[5];
    vtxa[6] = colors[6];
    vtxa[7] = colors[7];

    vtxa[8] = colors[8];
    vtxa[9] = colors[9];
    vtxa[10] = colors[10];
    vtxa[11] = colors[11];

    vtxa[12] = colors[12];
    vtxa[13] = colors[13];
    vtxa[14] = colors[14];
    vtxa[15] = colors[15];
  }

  // texture
  tmp = (texturecoordinate_struct *)(program->textcoords + (program->currentQuad * 2));

  program->currentQuad += 8;

  tmp[0].r = tmp[1].r = tmp[2].r = tmp[3].r = 0.0f; // these can stay at 0.0
  tmp[0].q = tmp[1].q = tmp[2].q = tmp[3].q = 1.0f; // these can stay at 1.0

  if ( input->flip & 0x1) {
    tmp[0].s = tmp[3].s = (float)((x + input->w) - ATLAS_BIAS);
    tmp[1].s = tmp[2].s = (float)((x)+ATLAS_BIAS);
  }
  else {
    tmp[0].s = tmp[3].s = (float)((x)+ATLAS_BIAS);
    tmp[1].s = tmp[2].s = (float)((x + input->w) - ATLAS_BIAS);
  }
  if( input->flip & 0x2) {
    tmp[0].t = tmp[1].t = (float)((y + input->h) - ATLAS_BIAS);
    tmp[2].t = tmp[3].t = (float)((y)+ATLAS_BIAS);
  }
  else {
    tmp[0].t = tmp[1].t = (float)((y)+ATLAS_BIAS);
    tmp[2].t = tmp[3].t = (float)((y + input->h) - ATLAS_BIAS);
  }

  if (c != NULL && cash_flg == 1)
  {
    switch (input->flip) {
    case 0:
      c->x = tmp[0].s;
      c->y = tmp[0].t;
      break;
    case 1:
      c->x = tmp[1].s;
      c->y = tmp[0].t;
      break;
    case 2:
      c->x = tmp[0].s;
      c->y = tmp[2].t;
      break;
    case 3:
      c->x = tmp[1].s;
      c->y = tmp[2].t;
      break;
    }
  }


  return 0;
}


static void YglQuadOffset_in(vdp2draw_struct * input, YglTexture * output, YglCache * c, int cx, int cy, float sx, float sy, int cash_flg, YglTextureManager *tm);

void YglQuadOffset(vdp2draw_struct * input, YglTexture * output, YglCache * c, int cx, int cy, float sx, float sy, YglTextureManager *tm) {
  YglQuadOffset_in(input, output, c, cx, cy, sx, sy, 1, tm);
}

void YglCachedQuadOffset(vdp2draw_struct * input, YglCache * cache, int cx, int cy, float sx, float sy, YglTextureManager *tm) {
  YglQuadOffset_in(input, NULL, cache, cx, cy, sx, sy, 0, tm);
}

void YglQuadOffset_in(vdp2draw_struct * input, YglTexture * output, YglCache * c, int cx, int cy, float sx, float sy, int cash_flg, YglTextureManager *tm) {
  unsigned int x, y;
  YglProgram *program;
  texturecoordinate_struct *tmp;
  int prg = PG_VDP2_NORMAL;
  float * pos;
  //float * vtxa;

  int vHeight;

  if (input->colornumber >= 3) {
    prg = PG_VDP2_NORMAL;
    if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
      prg = PG_VDP2_MOSAIC;
    }
    if ((input->blendmode & VDP2_CC_BLUR) != 0) {
      prg = PG_VDP2_BLUR;
    }
  }
  else {

    prg = PG_VDP2_NORMAL_CRAM;

    if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
      prg = PG_VDP2_MOSAIC_CRAM;
    }
    if ((input->blendmode & VDP2_CC_BLUR) != 0) {
      prg = PG_VDP2_BLUR_CRAM;
    }
  }

  program = YglGetProgram((YglSprite*)input, prg,tm,input->priority);
  if (program == NULL) return;

  program->colornumber = input->colornumber;
  program->lineTexture = input->lineTexture;

  program->mosaic[0] = input->mosaicxmask;
  program->mosaic[1] = input->mosaicymask;

  program->color_offset_val[0] = (float)(input->cor) / 255.0f;
  program->color_offset_val[1] = (float)(input->cog) / 255.0f;
  program->color_offset_val[2] = (float)(input->cob) / 255.0f;
  program->color_offset_val[3] = 0;
  //info->cor

  vHeight = input->vertices[5] - input->vertices[1];

  pos = program->quads + program->currentQuad;
  pos[0] = (input->vertices[0] - cx) * sx;
  pos[1] = input->vertices[1] * sy;
  pos[2] = (input->vertices[2] - cx) * sx;
  pos[3] = input->vertices[3] * sy;
  pos[4] = (input->vertices[4] - cx) * sx;
  pos[5] = input->vertices[5] * sy;
  pos[6] = (input->vertices[0] - cx) * sx;
  pos[7] = (input->vertices[1]) * sy;
  pos[8] = (input->vertices[4] - cx)*sx;
  pos[9] = input->vertices[5] * sy;
  pos[10] = (input->vertices[6] - cx) * sx;
  pos[11] = input->vertices[7] * sy;

  // vtxa = (program->vertexAttribute + (program->currentQuad * 2));
  // memset(vtxa,0,sizeof(float)*24);

  tmp = (texturecoordinate_struct *)(program->textcoords + (program->currentQuad * 2));

  program->currentQuad += 12;
  if (output != NULL){
    YglTMAllocate(tm, output, input->cellw, input->cellh, &x, &y);
  }
  else{
    x = c->x;
    y = c->y;
  }

  tmp[0].r = tmp[1].r = tmp[2].r = tmp[3].r = tmp[4].r = tmp[5].r = 0; // these can stay at 0

  /*
  0 +---+ 1
  |   |
  +---+ 2
  3 +---+
  |   |
  5 +---+ 4
  */

  if (input->flipfunction & 0x1) {
    tmp[0].s = tmp[3].s = tmp[5].s = (float)(x + input->cellw) - ATLAS_BIAS;
    tmp[1].s = tmp[2].s = tmp[4].s = (float)(x)+ATLAS_BIAS;
  }
  else {
    tmp[0].s = tmp[3].s = tmp[5].s = (float)(x)+ATLAS_BIAS;
    tmp[1].s = tmp[2].s = tmp[4].s = (float)(x + input->cellw) - ATLAS_BIAS;
  }
  if (input->flipfunction & 0x2) {
    tmp[0].t = tmp[1].t = tmp[3].t = (float)(y + input->cellh - cy) - ATLAS_BIAS;
    tmp[2].t = tmp[4].t = tmp[5].t = (float)(y + input->cellh - (cy + vHeight)) + ATLAS_BIAS;
  }
  else {
    tmp[0].t = tmp[1].t = tmp[3].t = (float)(y + cy) + ATLAS_BIAS;
    tmp[2].t = tmp[4].t = tmp[5].t = (float)(y + (cy + vHeight)) - ATLAS_BIAS;
  }

  if (c != NULL && cash_flg == 1)
  {
    c->x = x;
    c->y = y;
  }

  tmp[0].q = 1.0f;
  tmp[1].q = 1.0f;
  tmp[2].q = 1.0f;
  tmp[3].q = 1.0f;
  tmp[4].q = 1.0f;
  tmp[5].q = 1.0f;
}


static int YglQuad_in(vdp2draw_struct * input, YglTexture * output, YglCache * c, int cash_flg, YglTextureManager *tm);

float * YglQuad(vdp2draw_struct * input, YglTexture * output, YglCache * c, YglTextureManager *tm){
  YglQuad_in(input, output, c, 1, tm);
  return 0;
}

void YglCachedQuad(vdp2draw_struct * input, YglCache * cache, YglTextureManager *tm){
  YglQuad_in(input, NULL, cache, 0, tm);
}

int YglQuad_in(vdp2draw_struct * input, YglTexture * output, YglCache * c, int cash_flg, YglTextureManager *tm) {
  unsigned int x, y;
  YglProgram *program;
  texturecoordinate_struct *tmp;
  int prg;
  float * pos;
  //float * vtxa;

  if (input->colornumber >= 3) {
      prg = PG_VDP2_NORMAL;
      if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
        prg = PG_VDP2_MOSAIC;
      }
      if ((input->blendmode & VDP2_CC_BLUR) != 0) {
        prg = PG_VDP2_BLUR;
      }
  } else {
      prg = PG_VDP2_NORMAL_CRAM;

      if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
        prg = PG_VDP2_MOSAIC_CRAM;
      }
      if (((input->blendmode & VDP2_CC_BLUR) != 0)) {
        prg = PG_VDP2_BLUR_CRAM;
      }
  }

  program = YglGetProgram((YglSprite*)input, prg,tm,input->priority);
  if (program == NULL) return -1;

  program->colornumber = input->colornumber;
  program->lineTexture = input->lineTexture;
  program->blendmode = input->blendmode;

  program->mosaic[0] = input->mosaicxmask;
  program->mosaic[1] = input->mosaicymask;

  program->color_offset_val[0] = (float)(input->cor) / 255.0f;
  program->color_offset_val[1] = (float)(input->cog) / 255.0f;
  program->color_offset_val[2] = (float)(input->cob) / 255.0f;
  program->color_offset_val[3] = 0;
  //info->cor

  pos = program->quads + program->currentQuad;
  pos[0] = input->vertices[0];
  pos[1] = input->vertices[1];
  pos[2] = input->vertices[2];
  pos[3] = input->vertices[3];
  pos[4] = input->vertices[4];
  pos[5] = input->vertices[5];
  pos[6] = input->vertices[0];
  pos[7] = input->vertices[1];
  pos[8] = input->vertices[4];
  pos[9] = input->vertices[5];
  pos[10] = input->vertices[6];
  pos[11] = input->vertices[7];

  // vtxa = (program->vertexAttribute + (program->currentQuad * 2));
  // memset(vtxa,0,sizeof(float)*24);

  tmp = (texturecoordinate_struct *)(program->textcoords + (program->currentQuad * 2));

  program->currentQuad += 12;

  if (output != NULL){
    YglTMAllocate(tm, output, input->cellw, input->cellh, &x, &y);
  }
  else{
    x = c->x;
    y = c->y;
  }



  tmp[0].r = tmp[1].r = tmp[2].r = tmp[3].r = tmp[4].r = tmp[5].r = 0; // these can stay at 0

  /*
  0 +---+ 1
  |   |
  +---+ 2
  3 +---+
  |   |
  5 +---+ 4
  */

  if (input->flipfunction & 0x1) {
    tmp[0].s = tmp[3].s = tmp[5].s = (float)(x + input->cellw) - ATLAS_BIAS;
    tmp[1].s = tmp[2].s = tmp[4].s = (float)(x)+ATLAS_BIAS;
  }
  else {
    tmp[0].s = tmp[3].s = tmp[5].s = (float)(x)+ATLAS_BIAS;
    tmp[1].s = tmp[2].s = tmp[4].s = (float)(x + input->cellw) - ATLAS_BIAS;
  }
  if (input->flipfunction & 0x2) {
    tmp[0].t = tmp[1].t = tmp[3].t = (float)(y + input->cellh) - ATLAS_BIAS;
    tmp[2].t = tmp[4].t = tmp[5].t = (float)(y)+ATLAS_BIAS;
  }
  else {
    tmp[0].t = tmp[1].t = tmp[3].t = (float)(y)+ATLAS_BIAS;
    tmp[2].t = tmp[4].t = tmp[5].t = (float)(y + input->cellh) - ATLAS_BIAS;
  }

  if (c != NULL && cash_flg == 1)
  {
    switch (input->flipfunction) {
    case 0:
      c->x = *(program->textcoords + ((program->currentQuad - 12) * 2));   // upper left coordinates(0)
      c->y = *(program->textcoords + ((program->currentQuad - 12) * 2) + 1); // upper left coordinates(0)
      break;
    case 1:
      c->x = *(program->textcoords + ((program->currentQuad - 10) * 2));   // upper left coordinates(0)
      c->y = *(program->textcoords + ((program->currentQuad - 10) * 2) + 1); // upper left coordinates(0)
      break;
    case 2:
      c->x = *(program->textcoords + ((program->currentQuad - 2) * 2));   // upper left coordinates(0)
      c->y = *(program->textcoords + ((program->currentQuad - 2) * 2) + 1); // upper left coordinates(0)
      break;
    case 3:
      c->x = *(program->textcoords + ((program->currentQuad - 4) * 2));   // upper left coordinates(0)
      c->y = *(program->textcoords + ((program->currentQuad - 4) * 2) + 1); // upper left coordinates(0)
      break;
    }
  }

  tmp[0].q = 1.0f;
  tmp[1].q = 1.0f;
  tmp[2].q = 1.0f;
  tmp[3].q = 1.0f;
  tmp[4].q = 1.0f;
  tmp[5].q = 1.0f;

  return 0;
}


int YglQuadRbg0(vdp2draw_struct * input, YglTexture * output, YglCache * c, YglCache * line, YglTextureManager *tm) {
  unsigned int x, y;
  YglProgram *program;
  texturecoordinate_struct *tmp;
  int prg;
  float * pos;

  if(input->colornumber >= 3 ) {
    prg = PG_VDP2_NORMAL;
    if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
      prg = PG_VDP2_MOSAIC;
    }
    if ((input->blendmode & VDP2_CC_BLUR) != 0) {
      prg = PG_VDP2_BLUR;
    }
  }
  else {

    if (line->x != -1 && VDP2_CC_NONE != input->blendmode) {
      prg = PG_VDP2_RBG_CRAM_LINE;
    }
    else if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
      prg = PG_VDP2_MOSAIC_CRAM;
    }
    else if ((input->blendmode & VDP2_CC_BLUR) != 0) {
      prg = PG_VDP2_BLUR_CRAM;
    }
    else {
        prg = PG_VDP2_NORMAL_CRAM;
    }
/*
    if (line->x != -1 && VDP2_CC_NONE != input->blendmode ) {
      prg = PG_VDP2_RBG_CRAM_LINE;
    }
    else {
      prg = PG_VDP2_NORMAL_CRAM;
    }
*/
  }

  program = YglGetProgram((YglSprite*)input, prg,tm,input->priority);
  if (program == NULL) return -1;

  program->colornumber = input->colornumber;
  program->blendmode = input->blendmode;
  program->lineTexture = input->lineTexture;

  program->mosaic[0] = input->mosaicxmask;
  program->mosaic[1] = input->mosaicymask;

  program->color_offset_val[0] = (float)(input->cor) / 255.0f;
  program->color_offset_val[1] = (float)(input->cog) / 255.0f;
  program->color_offset_val[2] = (float)(input->cob) / 255.0f;
  program->color_offset_val[3] = 0;
  //info->cor
  pos = program->quads + program->currentQuad;
  pos[0] = input->vertices[0];
  pos[1] = input->vertices[1];
  pos[2] = input->vertices[2];
  pos[3] = input->vertices[3];
  pos[4] = input->vertices[4];
  pos[5] = input->vertices[5];
  pos[6] = input->vertices[0];
  pos[7] = input->vertices[1];
  pos[8] = input->vertices[4];
  pos[9] = input->vertices[5];
  pos[10] = input->vertices[6];
  pos[11] = input->vertices[7];

  // vtxa = (program->vertexAttribute + (program->currentQuad * 2));
  // memset(vtxa,0,sizeof(float)*24);

  tmp = (texturecoordinate_struct *)(program->textcoords + (program->currentQuad * 2));
  program->currentQuad += 12;
  x = c->x;
  y = c->y;



  /*
  0 +---+ 1
    |   |
    +---+ 2
  3 +---+
    |   |
  5 +---+ 4
            */

  tmp[0].s = tmp[3].s = tmp[5].s = (float)(x)+ATLAS_BIAS;
  tmp[1].s = tmp[2].s = tmp[4].s = (float)(x + input->cellw) - ATLAS_BIAS;
  tmp[0].t = tmp[1].t = tmp[3].t = (float)(y)+ATLAS_BIAS;
  tmp[2].t = tmp[4].t = tmp[5].t = (float)(y + input->cellh) - ATLAS_BIAS;

  if (line == NULL) {
    tmp[0].r = tmp[1].r = tmp[2].r = tmp[3].r = tmp[4].r = tmp[5].r = 0;
    tmp[0].q = tmp[1].q = tmp[2].q = tmp[3].q = tmp[4].q = tmp[5].q = 0;
  }
  else {
    tmp[0].r = (float)(line->x) + ATLAS_BIAS;
    tmp[0].q = (float)(line->y) + ATLAS_BIAS;

    tmp[1].r = (float)(line->x) + ATLAS_BIAS;
    tmp[1].q = (float)(line->y+1) - ATLAS_BIAS;

    tmp[2].r = (float)(line->x + input->cellh) - ATLAS_BIAS;
    tmp[2].q = (float)(line->y+1) - ATLAS_BIAS;

    tmp[3].r = (float)(line->x) + ATLAS_BIAS;
    tmp[3].q = (float)(line->y) + ATLAS_BIAS;

    tmp[4].r = (float)(line->x +input->cellh ) - ATLAS_BIAS;
    tmp[4].q = (float)(line->y + 1 ) - ATLAS_BIAS;

    tmp[5].r = (float)(line->x + input->cellh) - ATLAS_BIAS;
    tmp[5].q = (float)(line->y) + ATLAS_BIAS;
  }
  return 0;
}

//////////////////////////////////////////////////////////////////////////////
void YglEraseWriteVDP1(void) {

  float col[4];
  float colclear[4] = {0.0f};
  u16 color;
  int priority;
  u32 alpha = 0;
  int status = 0;
  GLenum DrawBuffers[4]= {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};
  _Ygl->vdp1On[_Ygl->readframe] = 0;
  if (_Ygl->vdp1FrameBuff[0] == 0) return;

  memset(_Ygl->vdp1fb_exactbuf[_Ygl->readframe], 0x0, 512*704*2);

  releaseVDP1DrawingFBMemRead(_Ygl->readframe);

  if(_Ygl->vdp1fb_buf[_Ygl->readframe] != NULL) {
    releaseVDP1FB(_Ygl->readframe);
  }
  _Ygl->vdp1IsNotEmpty[_Ygl->readframe] = 0;
  releaseVDP1DrawingFBMemRead(_Ygl->readframe);

  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->vdp1fbo);
  glDrawBuffers(2, &DrawBuffers[_Ygl->readframe*2]);

  _Ygl->vdp1_stencil_mode = 0;

  color = Vdp1Regs->EWDR;
  priority = 0;

  if ((color & 0x8000) && (Vdp2Regs->SPCTL & 0x20)) {
    alpha = 0;
  }
  else{
    int rgb = ((color&0x8000) == 0);
    int shadow, normalshadow, colorcalc;
    Vdp1ProcessSpritePixel(Vdp2Regs->SPCTL & 0xF, &color, &shadow, &normalshadow, &priority, &colorcalc);
    alpha = VDP1COLOR(rgb, colorcalc, priority, 0, 0);
    alpha >>= 24;
  }
  col[0] = (color & 0x1F) / 31.0f;
  col[1] = ((color >> 5) & 0x1F) / 31.0f;
  col[2] = ((color >> 10) & 0x1F) / 31.0f;
  col[3] = alpha / 255.0f;

  glClearBufferfv(GL_COLOR, 0, col);
  glClearBufferfv(GL_COLOR, 1, colclear);
  glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);
  FRAMELOG("YglEraseWriteVDP1xx: clear %d\n", _Ygl->readframe);
  //Get back to drawframe
  glDrawBuffers(2, &DrawBuffers[_Ygl->drawframe*2]);

  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);

}

static void waitVdp1End(int id) {
  int end = 0;
  if (_Ygl->syncVdp1[id] != 0) {
    while (end == 0) {
      int ret;
      ret = glClientWaitSync(_Ygl->syncVdp1[id], 0, 20000000);
      if ((ret == GL_CONDITION_SATISFIED) || (ret == GL_ALREADY_SIGNALED)) end = 1;
    }
    glDeleteSync(_Ygl->syncVdp1[id]);
    _Ygl->syncVdp1[id] = 0;
  }
}

static void executeTMVDP1(int in, int out) {
  if (_Ygl->needVdp1Render != 0){
    YglTmPush(YglTM_vdp1[in]);
    //YuiUseOGLOnThisThread();
    YglRenderVDP1();
    //YuiRevokeOGLOnThisThread();
    _Ygl->syncVdp1[in] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    YglReset(_Ygl->vdp1levels[out]);
    YglTmPull(YglTM_vdp1[out], 0);
    _Ygl->needVdp1Render = 0;
  }
}

//////////////////////////////////////////////////////////////////////////////
void YglFrameChangeVDP1(){
  u32 current_drawframe = 0;
  executeTMVDP1(_Ygl->drawframe, _Ygl->readframe);
  current_drawframe = _Ygl->drawframe;
  _Ygl->drawframe = _Ygl->readframe;
  _Ygl->readframe = current_drawframe;

  FRAMELOG("YglFrameChangeVDP1: swap drawframe =%d readframe = %d\n", _Ygl->drawframe, _Ygl->readframe);
}
//////////////////////////////////////////////////////////////////////////////
static int renderVDP1Level( YglLevel * level, int j, int* cprg, YglMatrix *mat, Vdp2 *varVdp2Regs) {
    int ret = 0;
    if( level->prg[j].prgid != *cprg ) {
      *cprg = level->prg[j].prgid;
      if (*cprg == 0) return 0; //prgid 0 has no meaning
//printf("USe prg %d\n", *cprg);
      glUseProgram(level->prg[j].prg);
    }

    if(level->prg[j].setupUniform) {
      level->prg[j].setupUniform((void*)&level->prg[j], YglTM_vdp1[_Ygl->drawframe], varVdp2Regs, SPRITE);
    }
    if( level->prg[j].currentQuad != 0 ) {
      ret = 1;
      glUniformMatrix4fv(level->prg[j].mtxModelView, 1, GL_FALSE, (GLfloat*)&mat->m[0][0]);
      glBindBuffer(GL_ARRAY_BUFFER, _Ygl->quads_buf);
      glBufferData(GL_ARRAY_BUFFER, level->prg[j].currentQuad * sizeof(float), level->prg[j].quads, GL_STREAM_DRAW);
      glVertexAttribPointer(level->prg[j].vertexp, 2, GL_FLOAT, GL_FALSE, 0, 0);
      glEnableVertexAttribArray(level->prg[j].vertexp);
      glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textcoords_buf);
      glBufferData(GL_ARRAY_BUFFER, level->prg[j].currentQuad * sizeof(float) * 2, level->prg[j].textcoords, GL_STREAM_DRAW);
      glVertexAttribPointer(level->prg[j].texcoordp,4,GL_FLOAT,GL_FALSE,0,0);
      glEnableVertexAttribArray(level->prg[j].texcoordp);
      if( level->prg[j].vaid != 0 ) {
        glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexAttribute_buf);
        glBufferData(GL_ARRAY_BUFFER, level->prg[j].currentQuad * sizeof(float) * 2, level->prg[j].vertexAttribute, GL_STREAM_DRAW);
        glVertexAttribPointer(level->prg[j].vaid,4, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(level->prg[j].vaid);
      }
      if ( level->prg[j].prgid >= PG_VDP1_GOURAUDSHADING_TESS ) {
        if (glPatchParameteri) glPatchParameteri(GL_PATCH_VERTICES, 4);
        glDrawArrays(GL_PATCHES, 0, level->prg[j].currentQuad / 2);
      }else{
        glDrawArrays(GL_TRIANGLES, 0, level->prg[j].currentQuad / 2);
      }
    }
    if( level->prg[j].cleanupUniform ){
      level->prg[j].cleanupUniform((void*)&level->prg[j], YglTM_vdp1[_Ygl->drawframe]);
    }
    return ret;
}

void YglRenderVDP1(void) {
  YglLevel * level;
  GLuint cprg=0;
  int i,j;
  int status;
  int drawAttr = -1;
  Vdp2 *varVdp2Regs = &Vdp2Lines[Vdp1External.plot_trigger_line];
  GLenum DrawBuffers[4]= {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};
  //YabThreadLock(_Ygl->mutex);
  YglMatrix m, *mat;
  mat = &_Ygl->mtxModelView;

  FrameProfileAdd("YglRenderVDP1 start");

  glBindVertexArray(_Ygl->vao);

  FRAMELOG("YglRenderVDP1: drawframe =%d", _Ygl->drawframe);

  if (_Ygl->pFrameBuffer != NULL) {
    _Ygl->pFrameBuffer = NULL;
    glBindTexture(GL_TEXTURE_2D, _Ygl->smallfbotex);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, _Ygl->vdp1pixelBufferID);
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  }
  releaseVDP1DrawingFBMemRead(_Ygl->readframe);

  YGLLOG("YglRenderVDP1 %d, PTMR = %d\n", _Ygl->drawframe, Vdp1Regs->PTMR);

  level = &(_Ygl->vdp1levels[_Ygl->drawframe]);
    if( level == NULL ) {
        //YabThreadUnLock(_Ygl->mutex);
        return;
    }
  cprg = -1;

  YglGenFrameBuffer();

  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->vdp1fbo);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glCullFace(GL_FRONT_AND_BACK);
  glDisable(GL_CULL_FACE);

  if (Vdp1Regs->TVMR & 0x02) {
    YglMatrix rotate, scale;
    int x = (_Ygl->rwidth - Vdp1Regs->systemclipX2)/2 * (_Ygl->width/_Ygl->rwidth);
    int y = ( Vdp1Regs->systemclipY2 - _Ygl->rheight)/2 * (_Ygl->height/_Ygl->rheight);
    YglLoadIdentity(&rotate);
    rotate.m[0][0] = Vdp1ParaA.deltaX;
    rotate.m[0][1] = Vdp1ParaA.deltaY;
    rotate.m[1][0] = Vdp1ParaA.deltaXst;
    rotate.m[1][1] = Vdp1ParaA.deltaYst;
    YglTranslatef(&rotate, -Vdp1ParaA.Xst, -Vdp1ParaA.Yst, 0.0f);
    YglMatrixMultiply(&m, mat, &rotate);
    YglLoadIdentity(&scale);
    scale.m[0][0] = 1.0;
    scale.m[1][1] = 1.0 / (1.0 + Vdp1ParaA.deltaY);
    scale.m[0][3] = 0.0;
    scale.m[1][3] = 1.0 - scale.m[1][1];
    YglMatrixMultiply(&m, &scale, &m);
    mat = &m;
  }
  glViewport(0, 0, maxWidth, maxHeight);
  glScissor(0, 0, maxWidth, maxHeight);

  glEnable(GL_SCISSOR_TEST);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, YglTM_vdp1[_Ygl->drawframe]->textureID);

  if (_Ygl->vdp1_stencil_mode != 0) {
    glEnable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
    if( _Ygl->vdp1_stencil_mode == 1 )
    {
       glStencilFunc(GL_EQUAL,0x1,0xFF);
    }else if( _Ygl->vdp1_stencil_mode == 2 )
    {
       glStencilFunc(GL_EQUAL,0x0,0xFF);
    }else{
       glStencilFunc(GL_ALWAYS,0,0xFF);
    }
  }
  else
    glDisable(GL_STENCIL_TEST);

  for( j=0;j<(level->prgcurrent+1); j++ ) {
    if ((level->prg[j].prgid == PG_VDP1_MSB_SHADOW) || (level->prg[j].prgid == PG_VDP1_MSB_SHADOW_TESS)) {
      if (drawAttr != 1) {
        glDrawBuffers(1, &DrawBuffers[_Ygl->drawframe*2+1]);
        drawAttr = 1;
      }
    } else {
      if (drawAttr != 0) {
        glDrawBuffers(2, &DrawBuffers[_Ygl->drawframe*2]);
        drawAttr = 0;
      }
    }
    _Ygl->vdp1On[_Ygl->drawframe] |= renderVDP1Level(level, j, &cprg, mat, varVdp2Regs);
  }
  for( j=0;j<(level->prgcurrent+1); j++ ) {
    level->prg[j].currentQuad = 0;
  }

  level->prgcurrent = 0;

  glDisable(GL_STENCIL_TEST);
  glDisable(GL_SCISSOR_TEST);
  //YabThreadUnLock(_Ygl->mutex);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
  //glEnable(GL_DEPTH_TEST);
  FrameProfileAdd("YglRenderVDP1 end");
}

//////////////////////////////////////////////////////////////////////////////
void YglDmyRenderVDP1(void) {
}

//////////////////////////////////////////////////////////////////////////////
static int useRotWin = 0;
void YglSetVdp2Window(Vdp2 *varVdp2Regs)
{
  float col[4] = {0.0f,0.0f,0.0f,0.0f};
  GLenum DrawBuffers[SPRITE]= {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3,GL_COLOR_ATTACHMENT4,GL_COLOR_ATTACHMENT5};
  float const vertexPosition[] = {
    _Ygl->rwidth, 0.0f,
    0.0f, 0.0f,
    _Ygl->rwidth, _Ygl->rheight,
    0.0f, _Ygl->rheight };

  int Win0[enBGMAX];
  int Win0_mode[enBGMAX];
  int Win1[enBGMAX];
  int Win1_mode[enBGMAX];
  int Win_op[enBGMAX];
  int needUpdate = 0;

  if (((varVdp2Regs->WCTLD & 0xA)!=0x0) != useRotWin) {
    useRotWin = ((varVdp2Regs->WCTLD & 0xA)!=0x0);
    needUpdate |= 1;
  }


  Win0[NBG0] = (varVdp2Regs->WCTLA >> 1) & 0x01;
  Win1[NBG0] = (varVdp2Regs->WCTLA >> 3) & 0x01;
  Win0[NBG1] = (varVdp2Regs->WCTLA >> 9) & 0x01;
  Win1[NBG1] = (varVdp2Regs->WCTLA >> 11) & 0x01;

  Win0[NBG2] = (varVdp2Regs->WCTLB >> 1) & 0x01;
  Win1[NBG2] = (varVdp2Regs->WCTLB >> 3) & 0x01;
  Win0[NBG3] = (varVdp2Regs->WCTLB >> 9) & 0x01;
  Win1[NBG3] = (varVdp2Regs->WCTLB >> 11) & 0x01;

  Win0[RBG0] = (varVdp2Regs->WCTLC >> 1) & 0x01;
  Win1[RBG0] = (varVdp2Regs->WCTLC >> 3) & 0x01;
  Win0[SPRITE] = (varVdp2Regs->WCTLC >> 9) & 0x01;
  Win1[SPRITE] = (varVdp2Regs->WCTLC >> 11) & 0x01;

  Win0_mode[NBG0] = (varVdp2Regs->WCTLA) & 0x01;
  Win1_mode[NBG0] = (varVdp2Regs->WCTLA >> 2) & 0x01;
  Win0_mode[NBG1] = (varVdp2Regs->WCTLA >> 8) & 0x01;
  Win1_mode[NBG1] = (varVdp2Regs->WCTLA >> 10) & 0x01;

  Win0_mode[NBG2] = (varVdp2Regs->WCTLB) & 0x01;
  Win1_mode[NBG2] = (varVdp2Regs->WCTLB >> 2) & 0x01;
  Win0_mode[NBG3] = (varVdp2Regs->WCTLB >> 8) & 0x01;
  Win1_mode[NBG3] = (varVdp2Regs->WCTLB >> 10) & 0x01;

  Win0_mode[RBG0] = (varVdp2Regs->WCTLC) & 0x01;
  Win1_mode[RBG0] = (varVdp2Regs->WCTLC >> 2) & 0x01;
  Win0_mode[SPRITE] = (varVdp2Regs->WCTLC >> 8) & 0x01;
  Win1_mode[SPRITE] = (varVdp2Regs->WCTLC >> 10) & 0x01;

  Win_op[NBG0] = (varVdp2Regs->WCTLA >> 7) & 0x01;
  Win_op[NBG1] = (varVdp2Regs->WCTLA >> 15) & 0x01;
  Win_op[NBG2] = (varVdp2Regs->WCTLB >> 7) & 0x01;
  Win_op[NBG3] = (varVdp2Regs->WCTLB >> 15) & 0x01;
  Win_op[RBG0] = (varVdp2Regs->WCTLC >> 7) & 0x01;
  Win_op[SPRITE] = (varVdp2Regs->WCTLC >> 15) & 0x01;

  Win0[RBG1] = Win0[NBG0];
  Win0_mode[RBG1] = Win0_mode[NBG0];
  Win1[RBG1] = Win1[NBG0];
  Win1_mode[RBG1] = Win1_mode[NBG0];
  Win_op[RBG1] = Win_op[NBG0];

  for (int i=0; i<enBGMAX; i++) {
    if (Win0[i] != _Ygl->Win0[i]) needUpdate |= 1;
    if (Win1[i] != _Ygl->Win1[i]) needUpdate |= 1;
    if (Win0_mode[i] != _Ygl->Win0_mode[i]) needUpdate |= 1;
    if (Win1_mode[i] != _Ygl->Win1_mode[i]) needUpdate |= 1;
    if (Win_op[i] != _Ygl->Win_op[i]) needUpdate |= 1;
  }

  //needUpdate |= 1;
  needUpdate |= Vdp2GenerateWindowInfo(varVdp2Regs);

   if (needUpdate) {

     glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->window_fbo);
     memcpy(&_Ygl->Win0[0], &Win0[0], enBGMAX*sizeof(int));
     memcpy(&_Ygl->Win1[0], &Win1[0], enBGMAX*sizeof(int));
     memcpy(&_Ygl->Win0_mode[0], &Win0_mode[0], enBGMAX*sizeof(int));
     memcpy(&_Ygl->Win1_mode[0], &Win1_mode[0], enBGMAX*sizeof(int));
     memcpy(&_Ygl->Win_op[0], &Win_op[0], enBGMAX*sizeof(int));
     for (int i = 0; i< SPRITE; i++) {
       _Ygl->use_win[i] = 0;
       if(needUpdate && (_Ygl->Win0[i] || _Ygl->Win1[i] || useRotWin))
       {
         if(_Ygl->Win0[i] || _Ygl->Win1[i])
         {
           _Ygl->use_win[i] = 1;
           glDrawBuffers(1, &DrawBuffers[i]);
           glClearBufferfv(GL_COLOR, 0, col);
           Ygl_uniformWindow(&_Ygl->windowpg);
           glUniformMatrix4fv( _Ygl->windowpg.mtxModelView, 1, GL_FALSE, (GLfloat*) &_Ygl->mtxModelView.m[0][0] );

           //Draw color///
           glActiveTexture(GL_TEXTURE0);
           glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[0]);
           glActiveTexture(GL_TEXTURE1);
           glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[1]);
           glUniform1i(_Ygl->windowpg.var1, _Ygl->Win0[i]);
           glUniform1i(_Ygl->windowpg.var2, _Ygl->Win0_mode[i]);
           glUniform1i(_Ygl->windowpg.var3, _Ygl->Win1[i]);
           glUniform1i(_Ygl->windowpg.var4, _Ygl->Win1_mode[i]);
           glUniform1i(_Ygl->windowpg.var5, _Ygl->Win_op[i]);
           glBindBuffer(GL_ARRAY_BUFFER, _Ygl->win0v_buf);
           glBufferData(GL_ARRAY_BUFFER, 4 * 2 *sizeof(float), vertexPosition, GL_STREAM_DRAW);
           glVertexAttribPointer(_Ygl->windowpg.vertexp, 2, GL_FLOAT, GL_FALSE, 0, 0 );
           glEnableVertexAttribArray(_Ygl->windowpg.vertexp);
           glDrawArrays(GL_TRIANGLE_STRIP,0,4);
         }
       }
     }
  }
  return;
}

void YglSetCCWindow(Vdp2 *varVdp2Regs)
{
  float col[4] = {0.0f,0.0f,0.0f,0.0f};
  float const vertexPosition[] = {
    _Ygl->rwidth, 0.0f,
    0.0f, 0.0f,
    _Ygl->rwidth, _Ygl->rheight,
    0.0f, _Ygl->rheight };

  int Win0;
  int Win0_mode;
  int Win1;
  int Win1_mode;

  int Win_op;

  //Manque la sprite window

  Win0 = (varVdp2Regs->WCTLD >> 9) & 0x01;
  Win1 = (varVdp2Regs->WCTLD >> 11) & 0x01;

  Win0_mode = ((varVdp2Regs->WCTLD >> 8) & 0x01 == 0);
  Win1_mode = ((varVdp2Regs->WCTLD >> 10) & 0x01 == 0);

  Win_op = (varVdp2Regs->WCTLD >> 15) & 0x01;

   GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
   glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->window_cc_fbo);
   _Ygl->use_cc_win = 0;
   if(Win0 || Win1)
   {
      Vdp2GenerateWindowInfo(varVdp2Regs);
      _Ygl->use_cc_win = 1;
      glDrawBuffers(1, &DrawBuffers[0]);
      glClearBufferfv(GL_COLOR, 0, col);
      Ygl_uniformWindow(&_Ygl->windowpg);
      glUniformMatrix4fv( _Ygl->windowpg.mtxModelView, 1, GL_FALSE, (GLfloat*) &_Ygl->mtxModelView.m[0][0] );

      //Draw color///
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[0]);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[1]);
      glUniform1i(_Ygl->windowpg.var1, Win0);
      glUniform1i(_Ygl->windowpg.var2, Win0_mode);
      glUniform1i(_Ygl->windowpg.var3, Win1);
      glUniform1i(_Ygl->windowpg.var4, Win1_mode);
      glUniform1i(_Ygl->windowpg.var5, Win_op);
      glBindBuffer(GL_ARRAY_BUFFER, _Ygl->win0v_buf);
      glBufferData(GL_ARRAY_BUFFER, 4 * 2 *sizeof(float), vertexPosition, GL_STREAM_DRAW);
      glVertexAttribPointer(_Ygl->windowpg.vertexp, 2, GL_FLOAT, GL_FALSE, 0, 0 );
      glEnableVertexAttribArray(_Ygl->windowpg.vertexp);
      glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    }
  return;
}



static void updateColorOffset(Vdp2 *varVdp2Regs) {
  if (varVdp2Regs->CLOFEN & 0x40)
  {
    // color offset enable
    if (varVdp2Regs->CLOFSL & 0x40)
    {
      // color offset B
      vdp1cor = varVdp2Regs->COBR & 0xFF;
      if (varVdp2Regs->COBR & 0x100)
        vdp1cor |= 0xFFFFFF00;
      vdp1cog = varVdp2Regs->COBG & 0xFF;
      if (varVdp2Regs->COBG & 0x100)
        vdp1cog |= 0xFFFFFF00;

      vdp1cob = varVdp2Regs->COBB & 0xFF;
      if (varVdp2Regs->COBB & 0x100)
        vdp1cob |= 0xFFFFFF00;
    }
    else
    {
      // color offset A
      vdp1cor = varVdp2Regs->COAR & 0xFF;
      if (varVdp2Regs->COAR & 0x100)
        vdp1cor |= 0xFFFFFF00;

      vdp1cog = varVdp2Regs->COAG & 0xFF;
      if (varVdp2Regs->COAG & 0x100)
        vdp1cog |= 0xFFFFFF00;

      vdp1cob = varVdp2Regs->COAB & 0xFF;
      if (varVdp2Regs->COAB & 0x100)
        vdp1cob |= 0xFFFFFF00;
    }
  }
  else // color offset disable
    vdp1cor = vdp1cog = vdp1cob = 0;
}

u8 * YglGetVDP2RegPointer(){
  int error;
  if (_Ygl->vdp2reg_tex == 0){
    glGenTextures(1, &_Ygl->vdp2reg_tex);

    glGenBuffers(1, &_Ygl->vdp2reg_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->vdp2reg_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 512 * NB_VDP2_REG, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, _Ygl->vdp2reg_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, NB_VDP2_REG * 512, 1, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp2reg_tex);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->vdp2reg_pbo);
  _Ygl->vdp2reg_buf = (u8 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 512 * NB_VDP2_REG, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  return _Ygl->vdp2reg_buf;
}

void YglSetVDP2Reg(u32 * pbuf, int start, int size){

  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp2reg_tex);
  //if (_Ygl->lincolor_buf == pbuf) {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->vdp2reg_pbo);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glTexSubImage2D(GL_TEXTURE_2D, 0, NB_VDP2_REG * start, 0, NB_VDP2_REG * size, 1, GL_RED, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    _Ygl->vdp2reg_buf = NULL;
  //}
  glBindTexture(GL_TEXTURE_2D, 0 );
  return;
}

void YglUpdateVdp2Reg() {
  int needupdate = 0;
  int step = ((Vdp2Lines[0].TVMD >> 6) & 0x3 == 3)?2:1;
  for (int i = 0; i<_Ygl->rheight; i++) {
    Vdp2 *varVdp2Regs = &Vdp2Lines[i/step];
    u8 bufline[NB_VDP2_REG] = {0};
    updateColorOffset(varVdp2Regs);

    bufline[S0CCRT] = (0x1F - ((varVdp2Regs->CCRSA >> 0) & 0x1F));
    bufline[S1CCRT] = (0x1F - ((varVdp2Regs->CCRSA >> 8) & 0x1F));
    bufline[S2CCRT] = (0x1F - ((varVdp2Regs->CCRSB >> 0) & 0x1F));
    bufline[S3CCRT] = (0x1F - ((varVdp2Regs->CCRSB >> 8) & 0x1F));
    bufline[S4CCRT] = (0x1F - ((varVdp2Regs->CCRSC >> 0) & 0x1F));
    bufline[S5CCRT] = (0x1F - ((varVdp2Regs->CCRSC >> 8) & 0x1F));
    bufline[S6CCRT] = (0x1F - ((varVdp2Regs->CCRSD >> 0) & 0x1F));
    bufline[S7CCRT] = (0x1F - ((varVdp2Regs->CCRSD >> 8) & 0x1F));
    bufline[S0PRI] = ((varVdp2Regs->PRISA >> 0) & 0x7);
    bufline[S1PRI] = ((varVdp2Regs->PRISA >> 8) & 0x7);
    bufline[S2PRI] = ((varVdp2Regs->PRISB >> 0) & 0x7);
    bufline[S3PRI] = ((varVdp2Regs->PRISB >> 8) & 0x7);
    bufline[S4PRI] = ((varVdp2Regs->PRISC >> 0) & 0x7);
    bufline[S5PRI] = ((varVdp2Regs->PRISC >> 8) & 0x7);
    bufline[S6PRI] = ((varVdp2Regs->PRISD >> 0) & 0x7);
    bufline[S7PRI] = ((varVdp2Regs->PRISD >> 8) & 0x7);
    bufline[SPCC] = ((varVdp2Regs->SPCTL >> 8) & 0x07);
    bufline[VDP1COR] = vdp1cor & 0xFF;
    bufline[VDP1COG] = vdp1cog & 0xFF;
    bufline[VDP1COB] = vdp1cob & 0xFF;
    bufline[VDP1CORS] = (vdp1cor >> 8) & 0xFF;
    bufline[VDP1COGS] = (vdp1cog >> 8) & 0xFF;
    bufline[VDP1COBS] = (vdp1cob >> 8) & 0xFF;
    bufline[CRAOFB] = ((varVdp2Regs->CRAOFB>>4) & 0x7);

    if (memcmp(bufline, &_Ygl->vdp2buf[i*NB_VDP2_REG], NB_VDP2_REG) != 0){
      needupdate = 1;
      memcpy(&_Ygl->vdp2buf[i*NB_VDP2_REG], bufline, NB_VDP2_REG);
    }
  }
  if (needupdate) {
      u8 * pbuf = YglGetVDP2RegPointer();
      memcpy(pbuf, _Ygl->vdp2buf, _Ygl->rheight*NB_VDP2_REG);
      YglSetVDP2Reg(pbuf, 0, _Ygl->rheight);
      needupdate = 0;
  }
}

SpriteMode getSpriteRenderMode(Vdp2* varVdp2Regs) {
  SpriteMode ret = NONE;
  if (varVdp2Regs->CCCTL & (1<<6)) {
    if (((varVdp2Regs->CCCTL>>8)&0x1) == 0x1) {
      ret = AS_IS;
    } else {
      if (((varVdp2Regs->CCCTL >> 9) & 0x01) == 0x01 ) {
        ret = DST_ALPHA;
      } else {
        ret = SRC_ALPHA;
      }
    }
  }

  return ret;
}

void YglSetClearColor(float r, float g, float b){
  _Ygl->clear[0] = r;
  _Ygl->clear[1] = g;
  _Ygl->clear[2] = b;
  _Ygl->clear[3] = (float)(0xF8|NONE)/255.0f;
}

static void releaseVDP1FB(int i) {
  GLenum DrawBuffers[4]= {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};
  if (_Ygl->vdp1FrameBuff[i*2] != 0) {
    if (_Ygl->vdp1fb_buf[i] != NULL) {
      glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->vdp1fbo);
      glDrawBuffers(2, &DrawBuffers[i*2]);
      glViewport(0, 0, _Ygl->width, _Ygl->height);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1AccessTex[i]);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->vdp1_pbo[i]);
      glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 256, GL_RGBA, GL_UNSIGNED_BYTE, 0);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
      _Ygl->vdp1fb_buf[i] = NULL;
      _Ygl->vdp1IsNotEmpty[i] = 0;
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  }
}

void YglUpdateVDP1FB(void) {
   GLenum DrawBuffers[4]= {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3};
  waitVdp1End(_Ygl->readframe);
  if (_Ygl->vdp1IsNotEmpty[_Ygl->readframe] != 0) {
    _Ygl->vdp1On[_Ygl->readframe] = 1;
    YglGenFrameBuffer();
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->vdp1fbo);
    glDrawBuffers(2, &DrawBuffers[_Ygl->readframe*2]);

    releaseVDP1FB(_Ygl->readframe);
    releaseVDP1DrawingFBMemRead(_Ygl->readframe);
    YglBlitVDP1(_Ygl->vdp1AccessTex[_Ygl->readframe], (float)_Ygl->rwidth, (float)_Ygl->rheight, 0);
    // clean up
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
  }
  releaseVDP1DrawingFBMemRead(_Ygl->readframe);
}

void YglCheckFBSwitch(int sync) {
  GLenum ret = GL_WAIT_FAILED;
  if (_Ygl->sync == 0) return;
  ret = glClientWaitSync(_Ygl->sync, 0, 0);
  if (sync != 0) {
    int end = 0;
    while (end == 0) {
     ret = glClientWaitSync(_Ygl->sync, 0, 20000000);
     if ((ret == GL_CONDITION_SATISFIED) || (ret == GL_ALREADY_SIGNALED)) end = 1;
    }
  }
  if ((ret == GL_CONDITION_SATISFIED) || (ret == GL_ALREADY_SIGNALED)) {
    glDeleteSync(_Ygl->sync);
    _Ygl->sync = 0;
    YuiSwapBuffers();
  }
}

static int DrawVDP2Screen(Vdp2 *varVdp2Regs, int id) {
  YglLevel * level;
  int cprg = -1;

  int ret = 0;

  level = &_Ygl->vdp2levels[id];

  if (level->prgcurrent == 0) return 0;

  if (_Ygl->use_win[id] == 1) {
    glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);

    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);

    YglBlitSimple(_Ygl->window_fbotex[id], 0);

    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);

    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, YglTM_vdp2->textureID);


  for (int j = 0; j < (level->prgcurrent + 1); j++)
  {
    if (level->prg[j].currentQuad != 0) {

      ret = 1;

      if (level->prg[j].prgid != cprg)
      {
        cprg = level->prg[j].prgid;
        glUseProgram(level->prg[j].prg);
      }
      if (level->prg[j].setupUniform)
      {
        level->prg[j].setupUniform((void*)&level->prg[j], YglTM_vdp2, varVdp2Regs, id);
      }

      glUniformMatrix4fv(level->prg[j].mtxModelView, 1, GL_FALSE, (GLfloat*)&_Ygl->mtxModelView.m[0][0]);
      glBindBuffer(GL_ARRAY_BUFFER, _Ygl->quads_buf);
      glBufferData(GL_ARRAY_BUFFER, level->prg[j].currentQuad * sizeof(float), level->prg[j].quads, GL_STREAM_DRAW);
      glVertexAttribPointer(level->prg[j].vertexp, 2, GL_FLOAT, GL_FALSE, 0, 0);
      glEnableVertexAttribArray(level->prg[j].vertexp);

      glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textcoords_buf);
      glBufferData(GL_ARRAY_BUFFER, level->prg[j].currentQuad * sizeof(float) * 2, level->prg[j].textcoords, GL_STREAM_DRAW);
      glVertexAttribPointer(level->prg[j].texcoordp, 4, GL_FLOAT, GL_FALSE, 0, 0);
      glEnableVertexAttribArray(level->prg[j].texcoordp);

      if (level->prg[j].vaid != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexAttribute_buf);
        glBufferData(GL_ARRAY_BUFFER, level->prg[j].currentQuad * sizeof(float) * 2, level->prg[j].vertexAttribute, GL_STREAM_DRAW);
        glVertexAttribPointer(level->prg[j].vaid, 4, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(level->prg[j].vaid);
      }

      glDrawArrays(GL_TRIANGLES, 0, level->prg[j].currentQuad / 2);

      if (level->prg[j].cleanupUniform)
      {
        level->prg[j].matrix = (GLfloat*)&_Ygl->mtxModelView.m[0][0];
        level->prg[j].cleanupUniform((void*)&level->prg[j], YglTM_vdp2);
      }
    }
    level->prg[j].currentQuad = 0;
  }
  glDisable(GL_STENCIL_TEST);
  return ret;
}

int setupColorMode(Vdp2 *varVdp2Regs, int layer) {
  //Return 1 if color format is RGB / 0 otherwise
  switch (layer) {
    case NBG0:
    case RBG1:
       return ((varVdp2Regs->CHCTLA >> 4)&0x7) > 2;
    break;
    case NBG1:
       return ((varVdp2Regs->CHCTLA >> 12)&0x4) > 2;
    break;
    case NBG2:
    case NBG3:
       //Always in palette mode
       return 0;
    break;
    case RBG0:
       return ((varVdp2Regs->CHCTLB >> 12)&0x7) > 2;
    default:
       return 0;
  }
  return 0;
}

SpriteMode setupBlend(Vdp2 *varVdp2Regs, int layer) {
  SpriteMode ret = NONE;
  const int enableBit[7] = {0, 1, 2, 3, 4, 0, 6};
  if (varVdp2Regs->CCCTL & (1<<enableBit[layer])) {
    if (((varVdp2Regs->CCCTL>>8)&0x1) == 0x1) {
      ret = AS_IS;
      YGLDEBUG("Layer %d as_is\n", layer);
    } else {
      //Add as calculation rate
      if (((varVdp2Regs->CCCTL >> 9) & 0x01) == 0x01 ) {
        YGLDEBUG("Layer %d src_alpha\n", layer);
        ret = DST_ALPHA;
      } else {
        YGLDEBUG("Layer %d dst_alpha\n", layer);
        ret = SRC_ALPHA;
      }
    }
    if (layer < enBGMAX) {
      if ((varVdp2Regs->SFCCMD >> (enableBit[layer]*2) & 0x3) == 3) {
        ret |= CC_ON_MSB;
      }
    }
  }
  return ret;
}

void YglRender(Vdp2 *varVdp2Regs) {
   GLuint cprg=0;
   GLuint srcTexture;
   int nbPass = 0;
   YglMatrix mtx;
   YglMatrix dmtx;
   unsigned int i,j;
   double w = 0;
   double h = 0;
   double x = 0;
   double y = 0;
   float col[4] = {0.0f,0.0f,0.0f,0.0f};
   float colopaque[4] = {0.0f,0.0f,0.0f,1.0f};
   int img[6] = {0};
   int lncl[7] = {0};
   int lncl_draw[7] = {0};
   int drawScreen[enBGMAX];
   SpriteMode mode;
   GLenum DrawBuffers[8]= {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3,GL_COLOR_ATTACHMENT4,GL_COLOR_ATTACHMENT5,GL_COLOR_ATTACHMENT6,GL_COLOR_ATTACHMENT7};

   glDepthMask(GL_FALSE);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_BLEND);

#if 0
   if ( (varVdp2Regs->CCCTL & 0x400) != 0 ) {
     printf("Extended Color calculation!\n");
   }
   printf("Ram mode %d\n", Vdp2Internal.ColorMode);
#endif
   glBindVertexArray(_Ygl->vao);

   if (_Ygl->stretch == 0) {
     double dar = (double)GlWidth/(double)GlHeight;
     double par = 4.0/3.0;

     if (yabsys.isRotated) par = 1.0/par;

     w = (dar>par)?(double)GlHeight*par:GlWidth;
     h = (dar>par)?(double)GlHeight:(double)GlWidth/par;
     x = (GlWidth-w)/2;
     y = (GlHeight-h)/2;
   } else {
     w = GlWidth;
     h = GlHeight;
     x = 0;
     y = 0;
   }

   glViewport(0, 0, GlWidth, GlHeight);

   FrameProfileAdd("YglRender start");
   glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
   glClearBufferfv(GL_COLOR, 0, col);

   YglGenFrameBuffer();

   glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->original_fbo);
   glDrawBuffers(NB_RENDER_LAYER, &DrawBuffers[0]);
   glClearBufferfv(GL_COLOR, 0, col);
#ifdef DEBUG_BLIT
   glClearBufferfv(GL_COLOR, 1, col);
   glClearBufferfv(GL_COLOR, 2, col);
   glClearBufferfv(GL_COLOR, 3, col);
   glClearBufferfv(GL_COLOR, 4, col);
#endif
   if ((Vdp2Regs->TVMD & 0x8000) == 0) goto render_finish;

   _Ygl->targetfbo = _Ygl->original_fbo;
   glDepthMask(GL_FALSE);

   glViewport(0, 0, _Ygl->rwidth, _Ygl->rheight);

   glGetIntegerv( GL_VIEWPORT, _Ygl->m_viewport );

   glScissor(0, 0, _Ygl->rwidth, _Ygl->rheight);
   glEnable(GL_SCISSOR_TEST);

   //glClearBufferfv(GL_COLOR, 0, colopaque);
   //glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);

   if (YglTM_vdp2 == NULL) goto render_finish;
   glBindTexture(GL_TEXTURE_2D, YglTM_vdp2->textureID);
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

   YglUpdateVdp2Reg();
   YglSetVdp2Window(varVdp2Regs);
   YglSetCCWindow(varVdp2Regs);
   cprg = -1;

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, YglTM_vdp2->textureID);

  int min = 8;
  int oldPrio = 0;

  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->screen_fbo);
  glDrawBuffers(enBGMAX, &DrawBuffers[0]);
  glClearBufferfv(GL_COLOR, 0, col);
  glClearBufferfv(GL_COLOR, 1, col);
  glClearBufferfv(GL_COLOR, 2, col);
  glClearBufferfv(GL_COLOR, 3, col);
  glClearBufferfv(GL_COLOR, 4, col);
  glClearBufferfv(GL_COLOR, 5, col);
  glClearBufferfv(GL_COLOR, 6, col);
  int nbPrio = 0;
  int minPrio = -1;
  int allPrio = 0;

  for (int i = 0; i < SPRITE; i++) {
    glDrawBuffers(1, &DrawBuffers[i]);
    glClearBufferfv(GL_COLOR, 0, col);
    drawScreen[i] = DrawVDP2Screen(varVdp2Regs, i);
  }

  const int vdp2screens[] = {RBG0, RBG1, NBG0, NBG1, NBG2, NBG3};

  int prioscreens[6] = {0};
  int modescreens[7];
  int isRGB[6];
  glDisable(GL_BLEND);
  int id = 0;

  lncl[0] = (varVdp2Regs->LNCLEN >> 0)&0x1; //NBG0
  lncl[1] = (varVdp2Regs->LNCLEN >> 1)&0x1; //NBG1
  lncl[2] = (varVdp2Regs->LNCLEN >> 2)&0x1; //NBG2
  lncl[3] = (varVdp2Regs->LNCLEN >> 3)&0x1; //NBG3
  lncl[4] = (varVdp2Regs->LNCLEN >> 4)&0x1; //RBG0
  lncl[5] = (varVdp2Regs->LNCLEN >> 0)&0x1; //RBG1
  lncl[6] = (varVdp2Regs->LNCLEN >> 5)&0x1; //SPRITE

  for (int j=0; j<6; j++) {
    if (drawScreen[vdp2screens[j]] != 0) {
      prioscreens[id] = _Ygl->screen_fbotex[vdp2screens[j]];
      modescreens[id] =  setupBlend(varVdp2Regs, vdp2screens[j]);
      isRGB[id] = setupColorMode(varVdp2Regs, vdp2screens[j]);
      lncl_draw[id] = lncl[vdp2screens[j]];
      id++;
    }
  }
  lncl_draw[6] = lncl[6];

  glViewport(0, 0, maxWidth, maxHeight);
  glGetIntegerv( GL_VIEWPORT, _Ygl->m_viewport );
  glScissor(0, 0, maxWidth, maxHeight);

  modescreens[6] =  setupBlend(varVdp2Regs, 6);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->back_fbo);
  glDrawBuffers(1, &DrawBuffers[0]);
  glClearBufferfv(GL_COLOR, 0, col);
  if ((varVdp2Regs->BKTAU & 0x8000) != 0) {
    YglDrawBackScreen();
  }else{
    glClearBufferfv(GL_COLOR, 0, _Ygl->clear);
  }


  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->original_fbo);
  glDrawBuffers(NB_RENDER_LAYER, &DrawBuffers[0]);
  glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);

  YglBlitTexture( _Ygl->bg, prioscreens, modescreens, isRGB, lncl_draw, varVdp2Regs);


    //if((img[0] == 0) && (img[1] == 0) && (img[2] == 0)) { // Break doom...
      //if (Vdp1External.disptoggle & 0x01) YglRenderFrameBuffer(0, 8, varVdp2Regs);
      srcTexture = _Ygl->original_fbotex[0];
    //} else {
     // if (nbPass > 1) {
     //   YglBlitImage(img, varVdp2Regs);
     //   srcTexture = _Ygl->original_fbotex;
     // } else {
    //    srcTexture = _Ygl->original_fbotex;
     // }
    //}

   glViewport(x, y, w, h);
   glScissor(x, y, w, h);
   glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
   YglBlitFramebuffer(srcTexture, maxWidth, maxHeight, w, h);

render_finish:

  for (int i=0; i<SPRITE; i++)
    YglReset(_Ygl->vdp2levels[i]);
  glViewport(_Ygl->originx, _Ygl->originy, GlWidth, GlHeight);
  glUseProgram(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  OSDDisplayMessages(NULL,0,0);

  _Ygl->sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
  glFlush();
  FrameProfileAdd("YglRender end");
  return;
}

//////////////////////////////////////////////////////////////////////////////

void YglReset(YglLevel level) {
  unsigned int i,j;
  level.blendmode  = 0;
  level.prgcurrent = 0;
  level.uclipcurrent = 0;
  level.ux1 = 0;
  level.uy1 = 0;
  level.ux2 = 0;
  level.uy2 = 0;
  for( j=0; j< level.prgcount; j++ )
  {
    level.prg[j].currentQuad = 0;
  }
}

//////////////////////////////////////////////////////////////////////////////

void YglShowTexture(void) {
   _Ygl->st = !_Ygl->st;
}

u32 * YglGetColorRamPointer() {
  int error;
  if (_Ygl->cram_tex == 0) {
    glGenTextures(1, &_Ygl->cram_tex);
#if 0
    glGenBuffers(1, &_Ygl->cram_tex_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->cram_tex_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 2048 * 4, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
#endif
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2048, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    _Ygl->colupd_min_addr = 0xFFFFFFFF ;
    _Ygl->colupd_max_addr = 0x00000000;



  }

  if (_Ygl->cram_tex_buf == NULL) {
#if 0
    glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->cram_tex_pbo);
    _Ygl->cram_tex_buf = (u32 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 2048 * 4, GL_MAP_WRITE_BIT /*| GL_MAP_INVALIDATE_BUFFER_BIT*/);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
#endif
    _Ygl->cram_tex_buf = malloc(2048 * 4);
    memset(_Ygl->cram_tex_buf, 0, 2048 * 4);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,2048, 1,GL_RGBA, GL_UNSIGNED_BYTE,_Ygl->cram_tex_buf);
  }

  return _Ygl->cram_tex_buf;
}

void YglOnUpdateColorRamWord(u32 addr) {

  u32 * buf;
  if (_Ygl == NULL) return;

  //YabThreadLock(_Ygl->crammutex);
  Vdp2ColorRamUpdated = 1;

  if (_Ygl->colupd_min_addr > addr)
    _Ygl->colupd_min_addr = addr;
  if (_Ygl->colupd_max_addr < addr)
    _Ygl->colupd_max_addr = addr;

  buf = _Ygl->cram_tex_buf;
  if (buf == NULL) {
    //YabThreadUnLock(_Ygl->crammutex);
    return;
  }

  switch (Vdp2Internal.ColorMode)
  {
  case 0:
  case 1:
  {
    u16 tmp;
    u8 alpha = 0;
    tmp = T2ReadWord(Vdp2ColorRam, addr);
    if (tmp & 0x8000) alpha = 0xF8;
    buf[(addr >> 1) & 0x7FF] = SAT2YAB1(alpha, tmp);
    break;
  }
  case 2:
  {
    u32 tmp1 = T2ReadWord(Vdp2ColorRam, (addr&0xFFC));
    u32 tmp2 = T2ReadWord(Vdp2ColorRam, (addr&0xFFC)+2);
    u8 alpha = 0;
    if (tmp1 & 0x8000) alpha = 0xF8;
    buf[(addr >> 2) & 0x7FF] = SAT2YAB2(alpha, tmp1, tmp2);
    break;
  }
  default:
    break;
  }
  //YabThreadUnLock(_Ygl->crammutex);
}


void YglUpdateColorRam() {
  u32 * buf;
  int index_shft  = 1;
  u32 start_addr,size;
  //YabThreadLock(_Ygl->crammutex);
  if (Vdp2ColorRamUpdated) {
    Vdp2ColorRamUpdated = 0;
    if (_Ygl->colupd_min_addr > _Ygl->colupd_max_addr) {
      //YabThreadUnLock(_Ygl->crammutex);
      return; // !? not initilized?
    }

    buf = YglGetColorRamPointer();
    if (Vdp2Internal.ColorMode == 2) {
      index_shft = 2;
    }
    glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    _Ygl->colupd_min_addr &= 0xFFF;
    _Ygl->colupd_max_addr &= 0xFFF;
    start_addr = (_Ygl->colupd_min_addr >> index_shft);
    size = ((_Ygl->colupd_max_addr - _Ygl->colupd_min_addr) >> index_shft) + 1;
#if 0
    glTexSubImage2D(GL_TEXTURE_2D,
      0,
      0, 0,
      2048, 1,
      GL_RGBA, GL_UNSIGNED_BYTE,
      buf);
#else
    glTexSubImage2D(GL_TEXTURE_2D,
      0,
      start_addr, 0,
      size, 1,
      GL_RGBA, GL_UNSIGNED_BYTE,
      &buf[start_addr] );
#endif
    _Ygl->colupd_min_addr = 0xFFFFFFFF;
    _Ygl->colupd_max_addr = 0x00000000;
  }
  //YabThreadUnLock(_Ygl->crammutex);
  return;

}



u32 * YglGetLineColorPointer(){
  int error;
  if (_Ygl->lincolor_tex == 0){
    glGenTextures(1, &_Ygl->lincolor_tex);

    glGenBuffers(1, &_Ygl->linecolor_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->linecolor_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 512 * 4, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, _Ygl->lincolor_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  glBindTexture(GL_TEXTURE_2D, _Ygl->lincolor_tex);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->linecolor_pbo);
  _Ygl->lincolor_buf = (u32 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 512 * 4, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  return _Ygl->lincolor_buf;
}

void YglSetLineColor(u32 * pbuf, int size){

  glBindTexture(GL_TEXTURE_2D, _Ygl->lincolor_tex);
  //if (_Ygl->lincolor_buf == pbuf) {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->linecolor_pbo);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    _Ygl->lincolor_buf = NULL;
  //}
  glBindTexture(GL_TEXTURE_2D, 0 );
  return;
}

//////////////////////////////////////////////////////////////////////////////
void YglGetWindowPointer(int id) {
  int status;
  GLuint error;

  YGLDEBUG("YglGetWindowPointer: %d,%d", _Ygl->width, _Ygl->height);


  if (_Ygl->window_tex[0] == 0) {
    glGenTextures(2, &_Ygl->window_tex[0]);

    glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  if( _Ygl->win[id] == NULL ){
    glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[id]);
    _Ygl->win[id] = malloc(512 * 4);
  }
}

void YglSetWindow(int id) {
  glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[id]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 1, GL_RGBA, GL_UNSIGNED_BYTE, _Ygl->win[id] );

  glBindTexture(GL_TEXTURE_2D, 0);
  return;
}

//////////////////////////////////////////////////////////////////////////////
u32* YglGetBackColorPointer() {
  int status;
  GLuint error;

  YGLDEBUG("YglGetBackColorPointer: %d,%d", _Ygl->width, _Ygl->height);


  if (_Ygl->back_tex == 0) {
    glGenTextures(1, &_Ygl->back_tex);

    glGenBuffers(1, &_Ygl->back_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->back_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 512 * 4, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, _Ygl->back_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glBindTexture(GL_TEXTURE_2D, _Ygl->back_tex);
#if 0
    if( _Ygl->backcolor_buf == NULL ){
        _Ygl->backcolor_buf = malloc(512 * 4);
    }
#else
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->back_pbo);
  if( _Ygl->backcolor_buf != NULL ){
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  }
  _Ygl->backcolor_buf = (u32 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 512 * 4, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
#endif

  return _Ygl->backcolor_buf;
}

void YglSetBackColor(int size) {

  glBindTexture(GL_TEXTURE_2D, _Ygl->back_tex);
#if 0
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, _Ygl->backcolor_buf);
#else
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->back_pbo);
  glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  _Ygl->backcolor_buf = NULL;
#endif
  glBindTexture(GL_TEXTURE_2D, 0);
  return;
}
void setupMaxSize() {
  int oldWidth = maxWidth;
  int oldHeight = maxHeight;
  maxWidth = _Ygl->width;
  maxHeight = _Ygl->height;

  if ((_Ygl->rwidth != 0) && (maxWidth > GlWidth)) maxWidth = _Ygl->rwidth * (GlWidth/_Ygl->rwidth + 1);
  if ((_Ygl->rheight != 0) && (maxHeight > GlHeight)) maxHeight = _Ygl->rheight * (GlHeight/_Ygl->rheight + 1);

  if (oldWidth != maxWidth) rebuild_frame_buffer = 1;
  if (oldHeight != maxHeight) rebuild_frame_buffer = 1;
}
//////////////////////////////////////////////////////////////////////////////

void YglChangeResolution(int w, int h) {
  YglLoadIdentity(&_Ygl->mtxModelView);
  YglOrtho(&_Ygl->mtxModelView, 0.0f, (float)w, (float)h, 0.0f, 10.0f, 0.0f);
#ifndef __LIBRETRO__
  if (( h > 256) &&  (_Ygl->resolution_mode >= 4)) _Ygl->resolution_mode = _Ygl->resolution_mode>>1; //Do not use 4x rendering when original res is already 2x
#endif
  releaseVDP1FB(0);
  releaseVDP1FB(1);
  releaseVDP1DrawingFBMemRead(0);
  releaseVDP1DrawingFBMemRead(1);
       YGLDEBUG("YglChangeResolution %d,%d\n",w,h);
       if (_Ygl->smallfbo != 0) {
         glDeleteFramebuffers(1, &_Ygl->smallfbo);
         _Ygl->smallfbo = 0;
         glDeleteTextures(1, &_Ygl->smallfbotex);
         _Ygl->smallfbotex = 0;
         glDeleteBuffers(1, &_Ygl->vdp1pixelBufferID);
         _Ygl->vdp1pixelBufferID = 0;
         _Ygl->pFrameBuffer = NULL;
       }
       if (_Ygl->vdp1_pbo[0] != 0) {
         glDeleteBuffers(2, _Ygl->vdp1_pbo);
         _Ygl->vdp1_pbo[0] = 0;
         _Ygl->vdp1_pbo[1] = 0;
         glDeleteTextures(2,_Ygl->vdp1AccessTex);
       }
     if (_Ygl->tmpfbo != 0){
       glDeleteFramebuffers(1, &_Ygl->tmpfbo);
       _Ygl->tmpfbo = 0;
       glDeleteTextures(1, &_Ygl->tmpfbotex);
       _Ygl->tmpfbotex = 0;
     }

     if (_Ygl->upfbo != 0){
       glDeleteFramebuffers(1, &_Ygl->upfbo);
       _Ygl->upfbo = 0;
       glDeleteTextures(1, &_Ygl->upfbotex);
       _Ygl->upfbotex = 0;
     }

  _Ygl->width = w * _Ygl->resolution_mode;
  _Ygl->height = h * _Ygl->resolution_mode;

  setupMaxSize();

  rebuild_frame_buffer = 1;

  _Ygl->rwidth = w;
  _Ygl->rheight = h;
}

void YglSetDensity(int d) {
  _Ygl->density = d;
}

void VIDOGLSync(){
}

///////////////////////////////////////////////////////////////////////////////
// Per line operation
u32 * YglGetPerlineBuf(YglPerLineInfo * perline, int linecount, int depth ){
  int error;
  if (perline->lincolor_tex == 0){
    glGenTextures(1, &perline->lincolor_tex);

    glGenBuffers(1, &perline->linecolor_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, perline->linecolor_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 512 * 4 * depth, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, perline->lincolor_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, depth, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  }

  glBindTexture(GL_TEXTURE_2D, perline->lincolor_tex);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, perline->linecolor_pbo);
  perline->lincolor_buf = (u32 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, linecount * 4 * depth, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  return perline->lincolor_buf;
}

void YglSetPerlineBuf(YglPerLineInfo * perline, u32 * pbuf, int linecount, int depth){

  glBindTexture(GL_TEXTURE_2D, perline->lincolor_tex);
  //if (_Ygl->lincolor_buf == pbuf) {
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, perline->linecolor_pbo);
  glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, linecount, depth, GL_RGBA, GL_UNSIGNED_BYTE, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  perline->lincolor_buf = NULL;
  //}
  glBindTexture(GL_TEXTURE_2D, 0);
  return;
}
