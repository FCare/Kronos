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
#include "error.h"
#include "vdp1_compute.h"
#include "perfetto_trace.h"

#define YGLDEBUG

extern int GlHeight;
extern int GlWidth;

extern int DrawVDP2Screen(Vdp2 *varVdp2Regs, int id);

extern void YglUpdateVdp2Reg();
extern SpriteMode setupBlend(Vdp2 *varVdp2Regs, int layer);
extern int setupColorMode(Vdp2 *varVdp2Regs, int layer);
extern int setupShadow(Vdp2 *varVdp2Regs, int layer);
extern int setupBlur(Vdp2 *varVdp2Regs, int layer);
extern int YglDrawBackScreen();

//////////////////////////////////////////////////////////////////////////////
int VIDCSEraseWriteVdp1(int id) {

  float col[4] = {0.0};
  u16 color;
  int priority;
  u32 alpha = 0;
  int status = 0;
  if (_Ygl->vdp1_pbo[0] == 0) return 0;

  _Ygl->vdp1_stencil_mode = 0;

  color = Vdp1Regs->EWDR;

  int shift = ((Vdp1Regs->TVMR & 0x1) == 1)?4:3;
  int limits[4] = {0};
  limits[0] = ((Vdp1Regs->EWLR>>9)&0x3F)<<shift;
  limits[1] = ((Vdp1Regs->EWLR)&0x1FF); //TODO: manage double interlace

  limits[2] = (((Vdp1Regs->EWRR>>9)&0x7F)<<shift) - 1;
  limits[3] = ((Vdp1Regs->EWRR)&0x1FF); //TODO: manage double interlace

  //Prohibited value - Example Quake first screens
  if ((limits[2] == -1)||(limits[3] == 0)) return 0;

  if ((limits[0]>=limits[2])||(limits[1]>limits[3])) {
    return 0; //No erase write when invalid area - Should be done only for one dot but no idea of which dot it shall be
  }


//Can be usefull for next steps to evaluate effective possible pixels which can be deleted during VBLANK
//see p49 of vdp1 doc. A raster is the number of maxLinecount
/*
  int nbPixels = (x3-x1+1)*(y3-y1+1) x 8;
  int nbRaster =
*/
  col[0] = (color & 0xFF) / 255.0f;
  col[1] = ((color >> 8) & 0xFF) / 255.0f;

  if (color != 0x0) {
    if (((Vdp1Regs->TVMR & 0x1) == 1) && (col[0] != col[1])){
      YuiMsg("Unsupported clear process\n\tin 8 bits upper part of EWDR is for even coordinates and lower part for odd coordinates\n");
    }
  }

  vdp1_clear(id, col, limits);

  //Get back to drawframe
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
  return ((limits[2]-limits[0])*(limits[3]-limits[1]))>>(Vdp1Regs->TVMR & 0x1);
}

void VIDCSFinsihDraw(void) {
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSRenderVDP1(void) {
  TRACE_RENDER("VIDCSRenderVDP1");
  FRAMELOG("VIDCSRenderVDP1: drawframe =%d %d\n", _Ygl->drawframe, yabsys.LineCount);
  // vdp1_compute();
}

void VIDCSFrameChangeVdp1(){
  u32 current_drawframe = 0;
  if (_Ygl->shallVdp1Erase[_Ygl->readframe] != 0) {
    FRAMELOG("FB %d is erased now\n", _Ygl->readframe);
    _Ygl->shallVdp1Erase[_Ygl->readframe] = 0;
    VIDCSEraseWriteVdp1(_Ygl->readframe);
    clearVDP1Framebuffer(_Ygl->readframe);
  }
  VIDCSRenderVDP1();
  current_drawframe = _Ygl->drawframe;
  _Ygl->drawframe = _Ygl->readframe;
  _Ygl->readframe = current_drawframe;
  _Ygl->vdp1fb_read_buf[_Ygl->drawframe] = NULL;

  FRAMELOG("VIDCSFrameChangeVdp1: swap drawframe =%d readframe = %d (%d)\n", _Ygl->drawframe, _Ygl->readframe, yabsys.LineCount);
}

extern int WinS[enBGMAX+1];
extern int WinS_mode[enBGMAX+1];

static int warning = 0;


GLuint GetCSVDP1fb(int id) {
  if (id == 0) return get_vdp1_tex(_Ygl->readframe);
  else return get_vdp1_mesh(_Ygl->readframe);
}

void finishCSRender() {
  for (int i=0; i<SPRITE; i++)
    YglReset(_Ygl->vdp2levels[i]);
  glViewport(_Ygl->originx, _Ygl->originy, GlWidth, GlHeight);
  glScissor(0, 0, _Ygl->width, _Ygl->height);
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

  if (VIDCore->startVdp1Render) VIDCore->startVdp1Render();
}

void VIDCSRender(Vdp2 *varVdp2Regs) {
   TRACE_RENDER("VIDCSRender");
   double scale = 1.0;
   GLuint cprg=0;
   GLuint srcTexture;
   GLuint VDP1fb[2];
   int nbPass = 0;
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
   int winS_draw = 0;
   int winS_mode_draw= 0;
   int win0_draw = 0;
   int win0_mode_draw = 0;
   int win1_draw = 0;
   int win1_mode_draw= 0;
   int win_op_draw = 0;
   int drawScreen[enBGMAX];
   SpriteMode mode;
   GLenum DrawBuffers[8]= {GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2,GL_COLOR_ATTACHMENT3,GL_COLOR_ATTACHMENT4,GL_COLOR_ATTACHMENT5,GL_COLOR_ATTACHMENT6,GL_COLOR_ATTACHMENT7};
   RATIOMODE modeScreen = _Ygl->stretch;

   int width = (_Ygl->width*2.0)/(_Ygl->vdp2wdensity);
   int height = _Ygl->height;

   double dar = (double)GlWidth/(double)GlHeight;
   double par = 4.0/3.0;

   float Intw = (float)GlWidth/(float)width;
   float Inth = (float)GlHeight/(float)height;

    if (_Ygl->interlace != DOUBLE_INTERLACE) {
      height = height*2.0;
      Inth /= 2.0;
    }
    float Int  = 1.0;

   if (modeScreen == ORIGINAL_RATIO) {
     if (yabsys.CurSH2FreqType == CLKTYPE_26MHZ) {
       if (yabsys.IsPal)
        //4:3 corresponds to 720*576
        par = (((float)width/720.0) * 4.0) / (((float)height/576.0) * 3.0) ;
       else
        //4:3 corresponds to 720*480
        par = (((float)width/720.0) * 4.0) / (((float)height/480.0) * 3.0) ;
     } else {
       if (yabsys.IsPal)
        //4:3 corresponds to 768*576
        par = (((float)width/768.0) * 4.0) / (((float)height/576.0) * 3.0) ;
       else
        //4:3 corresponds to 768*480
        par = (((float)width/768.0) * 4.0) / (((float)height/480.0) * 3.0) ;
     }
   }

   if ((floor(Intw) == 0)&&((modeScreen == INTEGER_RATIO)||(modeScreen == INTEGER_RATIO_FULL))) {
     if (warning == 0) YuiMsg("Window width is too small - Do not use integer scaling or reduce scaling\n");
     warning = 1;
     modeScreen = ORIGINAL_RATIO;
     Intw = 1.0;
     width = _Ygl->width*2.0/_Ygl->vdp2wdensity;
   }
   if ((floor(Inth) == 0)&&((modeScreen == INTEGER_RATIO)||(modeScreen == INTEGER_RATIO_FULL))) {
     if (warning == 0) YuiMsg("Window height is too small - Do not use integer scaling or reduce scaling\n");
     warning = 1;
     modeScreen = ORIGINAL_RATIO;
     Inth = 1.0;
     width = _Ygl->width*2.0/_Ygl->vdp2wdensity;
   }
   if (modeScreen != INTEGER_RATIO)  Int = (Inth<Intw)?Inth:Intw;
   glDepthMask(GL_FALSE);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_BLEND);

   glBindVertexArray(_Ygl->vao);
#ifndef __LIBRETRO__
   switch(modeScreen) {
     case STRETCH_RATIO:
       w = GlWidth;
       h = GlHeight;
       x = 0;
       y = 0;
       break;
    case ORIGINAL_RATIO:
       w = (dar>par)?(double)GlHeight*par:GlWidth;
       h = (dar>par)?(double)GlHeight:(double)GlWidth/par;
       x = (GlWidth-w)/2;
       y = (GlHeight-h)/2;
       break;
     case INTEGER_RATIO:
     case INTEGER_RATIO_FULL:
      Int = floor(Int);
       w = Int * width;
       h = Int * height;
       x = (GlWidth-w)/2;
       y = (GlHeight-h)/2;
       break;
     default:
        break;
    }
    scale = MAX(w/_Ygl->rwidth, h/_Ygl->rheight);
#else
  //Libretro is taking care to the resize
  w = width;
  h = height;
  x = y = 0;
#endif
   glViewport(0, 0, GlWidth, GlHeight);

   glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
   glClearBufferfv(GL_COLOR, 0, col);

   VIDCore->setupFrame();

    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->original_fbo);
    glDrawBuffers(NB_RENDER_LAYER, &DrawBuffers[0]);
    //glClearBufferfv(GL_COLOR, 0, col);
#ifdef DEBUG_BLIT
    //glClearBufferfv(GL_COLOR, 1, col);
    //glClearBufferfv(GL_COLOR, 2, col);
    //glClearBufferfv(GL_COLOR, 3, col);
    //glClearBufferfv(GL_COLOR, 4, col);
#endif

   glDepthMask(GL_FALSE);
   glViewport(0, 0, _Ygl->width, _Ygl->height);
   glGetIntegerv( GL_VIEWPORT, _Ygl->m_viewport );
   glScissor(0, 0, _Ygl->width, _Ygl->height);
   glEnable(GL_SCISSOR_TEST);

   //glClearBufferfv(GL_COLOR, 0, colopaque);
   //glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);
   if (((varVdp2Regs->TVMD & 0x8000)==0) || (YglTM_vdp2 == NULL)) {
     finishCSRender();
     return;
   }
   glBindTexture(GL_TEXTURE_2D, YglTM_vdp2->textureID);
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

   YglUpdateVdp2Reg();
   if (_Ygl->needWinUpdate) {
     YglSetWindow(0);
     YglSetWindow(1);
     _Ygl->needWinUpdate = 0;
   }
   cprg = -1;

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, YglTM_vdp2->textureID);

  int min = 8;
  int oldPrio = 0;

  int nbPrio = 0;
  int minPrio = -1;
  int allPrio = 0;

  for (int i = 0; i < SPRITE; i++) {
    if ((i == RBG0) || (i == RBG1)) {
      glViewport(0, 0, _Ygl->width, _Ygl->height);
      glScissor(0, 0, _Ygl->width, _Ygl->height);
      glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->rbg_compute_fbo);
      if ( i == RBG0)
        glDrawBuffers(1, &DrawBuffers[0]);
      else
        glDrawBuffers(1, &DrawBuffers[1]);
    } else {
      glViewport(0, 0, _Ygl->rwidth, _Ygl->rheight);
      glScissor(0, 0, _Ygl->rwidth, _Ygl->rheight);
      glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->screen_fbo);
      glDrawBuffers(1, &DrawBuffers[i]);
    }
    drawScreen[i] = DrawVDP2Screen(varVdp2Regs, i);
    if ((Vdp2External.disptoggle & (1<<i)) == 0) {
      drawScreen[i] = 0;
    }
  }

  const int vdp2screens[] = {RBG0, RBG1, NBG0, NBG1, NBG2, NBG3};

  int prioscreens[6] = {0};
  int modescreens[7] = {0};
  int useLineColorOffset[6] = {0};
  int isRGB[7] = {0};
  int isBlur[7] = {0};
  int isPerline[8] = {0};
  int isShadow[7] = {0};
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
      if ((vdp2screens[j] == RBG0) ||(vdp2screens[j] == RBG1)) {
        if (vdp2screens[j] == RBG0)
        prioscreens[id] = _Ygl->rbg_compute_fbotex[0];
        else
        prioscreens[id] = _Ygl->rbg_compute_fbotex[1];
      } else {
        prioscreens[id] = _Ygl->screen_fbotex[vdp2screens[j]];
      }
      if (vdp2screens[j] == RBG0) useLineColorOffset[id] = _Ygl->useLineColorOffset[0];
      if (vdp2screens[j] == RBG1) useLineColorOffset[id] = _Ygl->useLineColorOffset[1];
      modescreens[id] =  setupBlend(varVdp2Regs, vdp2screens[j]);
      isRGB[id] = setupColorMode(varVdp2Regs, vdp2screens[j]);
      isBlur[id] = setupBlur(varVdp2Regs, vdp2screens[j]);
      isPerline[id] = vdp2screens[j];
      isShadow[id] = setupShadow(varVdp2Regs, vdp2screens[j]);
      lncl_draw[id] = lncl[vdp2screens[j]];
      winS_draw |= (WinS[vdp2screens[j]]<<id);
      winS_mode_draw |= (WinS_mode[vdp2screens[j]]<<id);
      win0_draw |= (_Ygl->Win0[vdp2screens[j]]<<id);
      win0_mode_draw |= (_Ygl->Win0_mode[vdp2screens[j]]<<id);
      win1_draw |= (_Ygl->Win1[vdp2screens[j]]<<id);
      win1_mode_draw |= (_Ygl->Win1_mode[vdp2screens[j]]<<id);
      win_op_draw |= (_Ygl->Win_op[vdp2screens[j]]<<id);
      id++;
    }
  }
  isBlur[6] = setupBlur(varVdp2Regs, SPRITE);
  lncl_draw[6] = lncl[6];
  isPerline[6] = 6;
  isPerline[7] = 7;

  for (int i = 6; i < 8; i++) {
    //Update dedicated sprite window and Color calculation window
    winS_draw |= WinS[i]<<i;
    winS_mode_draw |= WinS_mode[i]<<i;
    win0_draw |= _Ygl->Win0[i]<<i;
    win0_mode_draw |= _Ygl->Win0_mode[i]<<i;
    win1_draw |= _Ygl->Win1[i]<<i;
    win1_mode_draw |= _Ygl->Win1_mode[i]<<i;
    win_op_draw |= _Ygl->Win_op[i]<<i;
  }

  isShadow[6] = setupShadow(varVdp2Regs, SPRITE); //Use sprite index for background suuport

  glViewport(0, 0, _Ygl->width, _Ygl->height);
  glGetIntegerv( GL_VIEWPORT, _Ygl->m_viewport );
  glScissor(0, 0, _Ygl->width, _Ygl->height);

  modescreens[6] =  setupBlend(varVdp2Regs, 6);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->back_fbo);
  glDrawBuffers(1, &DrawBuffers[0]);
  //glClearBufferfv(GL_COLOR, 0, col);
  if ((Vdp2Regs->TVMD & 0x8100) == 0) {
    float black[4] = {0.0};
    glClearBufferfv(GL_COLOR, 0, black);
  } else {
    if ((varVdp2Regs->BKTAU & 0x8000) != 0) {
      YglDrawBackScreen();
    }else{
      glClearBufferfv(GL_COLOR, 0, _Ygl->clear);
    }
  }

  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->original_fbo);

  glDrawBuffers(NB_RENDER_LAYER, &DrawBuffers[0]);
  glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);

  YglBlitTexture( prioscreens, modescreens, isRGB, isBlur, isPerline, isShadow, lncl_draw, GetCSVDP1fb, winS_draw, winS_mode_draw, win0_draw, win0_mode_draw, win1_draw, win1_mode_draw, win_op_draw, useLineColorOffset, varVdp2Regs);
  srcTexture = _Ygl->original_fbotex[0];

   int scali = (int)(scale);
#ifndef __LIBRETRO__
   glViewport(x, y, w, h);
   glScissor(x, y, w-scali, h-scali);
#endif
   glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
   YglBlitFramebuffer(srcTexture, _Ygl->width, _Ygl->height, w, h);

  finishCSRender();
  return;
}
