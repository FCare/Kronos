/*  Copyright 2003-2006 Guillaume Duhamel
    Copyright 2004 Lawrence Sebald
    Copyright 2004-2007 Theo Berkau

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

/*! \file vidcs.c
    \brief OpenGL video renderer
*/
#if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)

#include <math.h>
#define EPSILON (1e-10 )


#include "vidcs.h"
#include "vidshared.h"
#include "debug.h"
#include "vdp2.h"
#include "yabause.h"
#include "ygl.h"
#include "yui.h"
#include "vdp1_compute.h"

#define Y_MAX(a, b) ((a) > (b) ? (a) : (b))
#define Y_MIN(a, b) ((a) < (b) ? (a) : (b))

#define LOG_AREA

#define LOG_CMD

static int renderer_started = 0;
static Vdp2 baseVdp2Regs;
static int drawcell_run = 0;

static int isEnabled(int id, Vdp2* varVdp2Regs);
static void VIDCSVdp2DrawScreens(void);
static void Vdp2SetResolution(u16 TVMD);

extern GLuint GetCSVDP1fb(int id);

static Vdp2 baseVdp2Regs;

static int vdp1_interlace = 0;

int GlWidth = 320;
int GlHeight = 224;

int vdp1cor = 0;
int vdp1cog = 0;
int vdp1cob = 0;

static int vdp2busy = 0;
static int screenDirty = 0;

vdp2rotationparameter_struct  Vdp1ParaA;


static void Vdp2DrawRBG0();

static u32 Vdp2ColorRamGetLineColor(u32 colorindex, int alpha);
static int Vdp2PatternAddrPos(Vdp2Ctrl *ctrl, int planex, int x, int planey, int y);
static void Vdp2DrawPatternPos(Vdp2Ctrl *ctrl, int x, int y, int cx, int cy, int lines);
static INLINE void ReadVdp2ColorOffset(Vdp2 * regs, vdp2draw_struct *info, int mask);
static INLINE u16 Vdp2ColorRamGetColorRaw(u32 colorindex);
static void FASTCALL Vdp2DrawRotation(RBGDrawInfo * rbg);
static void Vdp2DrawRotation_in_sync(RBGDrawInfo * rbg);


static int FASTCALL Vdp2CheckWindowRange(Vdp2Ctrl *ctrl, int x, int y, int w, int h);
static void requestDrawCell(Vdp2Ctrl * ctrl);
static void requestDrawCellQuad(Vdp2Ctrl *ctrl);
static INLINE u32 Vdp2RotationFetchPixel(vdp2draw_struct *info, int x, int y, int cellw);
static u32 Vdp2ColorRamGetLineColorOffset(u32 colorindex, int alpha, int offset);
static void FASTCALL Vdp2DrawBitmapCoordinateInc(Vdp2Ctrl *ctrl);
static void FASTCALL Vdp2DrawBitmapLineScroll(Vdp2Ctrl *ctrl, int width, int height);
static void Vdp2DrawMapPerLine(Vdp2Ctrl *ctrl);
static void Vdp2DrawMapTest(Vdp2Ctrl *ctrl, int delayed);
static int Vdp2CheckCharAccessPenalty(int char_access, int ptn_access);
static int sameVDP2Reg(int id, Vdp2 *a, Vdp2 *b);

static void Vdp2GenLineinfo(vdp2draw_struct *info);
static void Vdp2DrawBackScreen(Vdp2 *varVdp2Regs);
static void Vdp2DrawLineColorScreen(Vdp2 *varVdp2Regs);
static void Vdp2DrawRBG1();
static void Vdp1SetTextureRatio(int vdp2widthratio, int vdp2heightratio);

static void finishRbgQueue();

static pixel_t *VIDCSGetVdp2ScreenExtract(u32 screen, int * w, int * h);

static vdp2Lineinfo lineNBG0[512];
static vdp2Lineinfo lineNBG1[512];

#define WA_INSIDE (0)
#define WA_OUTSIDE (1)

extern void YglGenReset();

int VIDCSInit(void);
void VIDCSDeInit(void);
void VIDCSResize(int, int, unsigned int, unsigned int, int);
void VIDCSGetScale(float *, float *, int *, int *);
int VIDCSIsFullscreen(void);
int VIDCSVdp1Reset(void);

extern vdp2rotationparameter_struct  Vdp1ParaA;

void VIDCSVdp1Draw();
void VIDCSVdp1NormalSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1ScaledSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1DistortedSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1PolygonDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1PolylineDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1LineDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1UserClipping(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1SystemClipping(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1NormalSpriteDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1ScaledSpriteDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1DistortedSpriteDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1PolygonDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1PolylineDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1LineDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1UserClippingUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1SystemClippingUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
void VIDCSVdp1DrawFB(void);
void VIDCSReadColorOffset(void);
void VIDCSVdp1LocalCoordinate(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
extern void VIDCSRender(Vdp2 *varVdp2Regs);
extern void VIDCSRenderVDP1(void);
extern void VIDCSFinsihDraw(void);

 int VIDCSVdp2Reset(void);
 void VIDCSVdp2Draw(void);
extern void VIDCSGetGlSize(int *width, int *height);
extern void VIDCSSetSettingValueMode(int type, int value);
extern void VIDCSSync();
extern void VIDCSVdp2DispOff(void);
extern int VIDCSGenFrameBuffer();

static void VIDCSSetupVdp1Scale(int scale);

static void VIDCSStartVdp1Render(void);
static void VIDCSStartVdp1RenderUpscale(void);
static void VIDCSEndVdp1Render(void);
static void VIDCSEndVdp1RenderUpscale(void);


VideoInterface_struct VIDCS = {
VIDCORE_CS,
"Compute Shader Video Interface",
VIDCSInit,
VIDCSDeInit,
VIDCSResize,
VIDCSGetScale,
VIDCSIsFullscreen,
VIDCSVdp1Reset,
VIDCSVdp1Draw,
VIDCSVdp1NormalSpriteDraw,
VIDCSVdp1ScaledSpriteDraw,
VIDCSVdp1DistortedSpriteDraw,
VIDCSVdp1PolygonDraw,
VIDCSVdp1PolylineDraw,
VIDCSVdp1LineDraw,
VIDCSVdp1UserClipping,
VIDCSVdp1SystemClipping,
VIDCSVdp1LocalCoordinate,
VIDCSEraseWriteVdp1,
VIDCSFrameChangeVdp1,
NULL,
VIDCSVdp2Reset,
VIDCSVdp2Draw,
VIDCSGetGlSize,
VIDCSSetSettingValueMode,
VIDCSSync,
VIDCSVdp2DispOff,
VIDCSRender,
VIDCSRenderVDP1,
VIDCSGenFrameBuffer,
VIDCSFinsihDraw,
VIDCSVdp1DrawFB,
VIDCSGetVdp2ScreenExtract,
VIDCSSetupVdp1Scale,
VIDCSStartVdp1Render,
VIDCSEndVdp1Render
};


#define LOG_ASYN

static void FASTCALL Vdp2DrawCell_in_sync(Vdp2Ctrl *ctrl);

#define NB_MSG 256

YabMutex * Vdp2CtrlLock = NULL;

YabEventQueue *VDP2CtrlStack = NULL;
YabEventQueue *RBGStack = NULL;

Vdp2Ctrl* popCtrl() {
  return (Vdp2Ctrl *)YabWaitEventQueue(VDP2CtrlStack);
}

void pushCtrl(Vdp2Ctrl* val) {
  YabAddEventQueue(VDP2CtrlStack,val);
}

RBGDrawInfo* popRBG() {
  return (RBGDrawInfo *)YabWaitEventQueue(RBGStack);
}

void pushRBG(RBGDrawInfo* val) {
  YabAddEventQueue(RBGStack,val);
}

static void VIDCSSetupVdp1Scale(int scale) {
  if (scale == 1) {
    VIDCS.Vdp1NormalSpriteDraw = VIDCSVdp1NormalSpriteDraw;
    VIDCS.Vdp1ScaledSpriteDraw = VIDCSVdp1ScaledSpriteDraw;
    VIDCS.Vdp1DistortedSpriteDraw = VIDCSVdp1DistortedSpriteDraw;
    VIDCS.Vdp1PolygonDraw = VIDCSVdp1PolygonDraw;
    VIDCS.Vdp1PolylineDraw = VIDCSVdp1PolylineDraw;
    VIDCS.Vdp1LineDraw = VIDCSVdp1LineDraw;
    VIDCS.Vdp1UserClipping = VIDCSVdp1UserClipping;
    VIDCS.Vdp1SystemClipping = VIDCSVdp1SystemClipping;
    VIDCS.endVdp1Render = VIDCSEndVdp1Render;
    VIDCS.startVdp1Render = VIDCSStartVdp1Render;
  } else {
    VIDCS.Vdp1NormalSpriteDraw = VIDCSVdp1NormalSpriteDrawUpscale;
    VIDCS.Vdp1ScaledSpriteDraw = VIDCSVdp1ScaledSpriteDrawUpscale;
    VIDCS.Vdp1DistortedSpriteDraw = VIDCSVdp1DistortedSpriteDrawUpscale;
    VIDCS.Vdp1PolygonDraw = VIDCSVdp1PolygonDrawUpscale;
    VIDCS.Vdp1PolylineDraw = VIDCSVdp1PolylineDrawUpscale;
    VIDCS.Vdp1LineDraw = VIDCSVdp1LineDrawUpscale;
    VIDCS.Vdp1UserClipping = VIDCSVdp1UserClippingUpscale;
    VIDCS.Vdp1SystemClipping = VIDCSVdp1SystemClippingUpscale;
    VIDCS.endVdp1Render = VIDCSEndVdp1RenderUpscale;
    VIDCS.startVdp1Render = VIDCSStartVdp1RenderUpscale;
  }
}

static void VIDCSStartVdp1Render(void) {
 startVdp1Render();
}
static void VIDCSStartVdp1RenderUpscale(void) {
  startVdp1RenderUpscale();
}

static void VIDCSEndVdp1Render(void) {
  endVdp1Render();
}
static void VIDCSEndVdp1RenderUpscale(void) {
  endVdp1RenderUpscale();
}

#define CELL_SINGLE 0x1
#define CELL_QUAD   0x2

static void Vdp2DrawPatternPos(Vdp2Ctrl *ctrl, int x, int y, int cx, int cy, int lines)
{
  u64 cacheaddr = (ctrl->info.paladdr << 20) | ctrl->info.charaddr | ctrl->info.transparencyenable |
    ((ctrl->info.patternpixelwh >> 4) << 1) | (((u64)(ctrl->info.coloroffset >> 8) & 0x07) << 32) | (((u64)(ctrl->info.idScreen) & 0x07) << 39)
    | ((u32)(ctrl->info.alpha_per_line[y] >> 3) << 27);
  int priority = ctrl->info.priority;
  YglCache c;
  vdp2draw_struct tile = ctrl->info;
  int winmode = 0;

  tile.dst = 0;
  tile.colornumber = ctrl->info.colornumber;
  tile.mosaicxmask = ctrl->info.mosaicxmask;
  tile.mosaicymask = ctrl->info.mosaicymask;
  tile.idScreen = ctrl->info.idScreen;

  tile.cellw = tile.cellh = ctrl->info.patternpixelwh;
  tile.flipfunction = ctrl->info.flipfunction;

  if (ctrl->info.specialprimode == 1) {
    ctrl->info.priority = (ctrl->info.priority & 0xFFFFFFFE) | ctrl->info.specialfunction;
  }

  cacheaddr |= ((u64)(ctrl->info.priority) & 0x07) << 42;

  tile.priority = ctrl->info.priority;

  tile.vertices[0] = x;
  tile.vertices[1] = y;
  tile.vertices[2] = (x + tile.cellw);
  tile.vertices[3] = y;
  tile.vertices[4] = (x + tile.cellh);
  tile.vertices[5] = (y + lines /*(float)ctrl->info.lineinc*/);
  tile.vertices[6] = x;
  tile.vertices[7] = (y + lines/*(float)ctrl->info.lineinc*/ );

  // Screen culling
  //if (tile.vertices[0] >= _Ygl->rwidth || tile.vertices[1] >= _Ygl->rheight || tile.vertices[2] < 0 || tile.vertices[5] < 0)
  //{
  //	return;
  //}

  if ((_Ygl->Win0[ctrl->info.idScreen] != 0 || _Ygl->Win1[ctrl->info.idScreen] != 0) && ctrl->info.coordincx == 1.0f && ctrl->info.coordincy == 1.0f)
  {                                                 // coordinate inc is not supported yet.
    winmode = Vdp2CheckWindowRange(ctrl, x - cx, y - cy, tile.cellw, ctrl->info.lineinc);
    if (winmode == 0) // all outside, no need to draw
    {
      return;
    }
  }

  tile.cor = ctrl->info.cor;
  tile.cog = ctrl->info.cog;
  tile.cob = ctrl->info.cob;

  if (1 == YglIsCached(YglTM_vdp2, cacheaddr, &c))
  {
    YglCachedQuadOffset(&tile, &c, cx, cy, ctrl->info.coordincx, ctrl->info.coordincy, YglTM_vdp2);
    return;
  }

  YglQuadOffset(&tile, &ctrl->texture, &c, cx, cy, ctrl->info.coordincx, ctrl->info.coordincy, YglTM_vdp2);
  YglCacheAdd(YglTM_vdp2, cacheaddr, &c);
  switch (ctrl->info.patternwh)
  {
  case 1:
    requestDrawCell(ctrl);
    break;
  case 2:
    ctrl->texture.w += 8;
    requestDrawCellQuad(ctrl);
    break;
  }
  ctrl->info.priority = priority;
}


//////////////////////////////////////////////////////////////////////////////

static int Vdp2PatternAddrPos(Vdp2Ctrl *ctrl, int planex, int x, int planey, int y)
{

  u32 addr = ctrl->info.addr +
    (ctrl->info.pagewh*ctrl->info.pagewh*ctrl->info.planew*planey +
      ctrl->info.pagewh*ctrl->info.pagewh*planex +
      ctrl->info.pagewh*y +
      x)*ctrl->info.patterndatasize * 2;

  int ptnAddrBk = (((addr >> 16)& 0xF) >> ((ctrl->regs->VRSIZE >> 15)&0x1)) >> 1;
  if (ctrl->info.pname_bank[ptnAddrBk] == 0) return 0;

  switch (ctrl->info.patterndatasize)
  {
  case 1:
  {
    u16 tmp = Vdp2RamReadWord(NULL, Vdp2Ram, addr);

    ctrl->info.specialfunction = (ctrl->info.supplementdata >> 9) & 0x1;
    ctrl->info.specialcolorfunction = (ctrl->info.supplementdata >> 8) & 0x1;

    switch (ctrl->info.colornumber)
    {
    case 0: // in 16 colors
      ctrl->info.paladdr = ((tmp & 0xF000) >> 12) | ((ctrl->info.supplementdata & 0xE0) >> 1);
      break;
    default: // not in 16 colors
      ctrl->info.paladdr = (tmp & 0x7000) >> 8;
      break;
    }

    switch (ctrl->info.auxmode)
    {
    case 0:
      ctrl->info.flipfunction = (tmp & 0xC00) >> 10;

      switch (ctrl->info.patternwh)
      {
      case 1:
        ctrl->info.charaddr = (tmp & 0x3FF) | ((ctrl->info.supplementdata & 0x1F) << 10);
        break;
      case 2:
        ctrl->info.charaddr = ((tmp & 0x3FF) << 2) | (ctrl->info.supplementdata & 0x3) | ((ctrl->info.supplementdata & 0x1C) << 10);
        break;
      }
      break;
    case 1:
      ctrl->info.flipfunction = 0;

      switch (ctrl->info.patternwh)
      {
      case 1:
        ctrl->info.charaddr = (tmp & 0xFFF) | ((ctrl->info.supplementdata & 0x1C) << 10);
        break;
      case 2:
        ctrl->info.charaddr = ((tmp & 0xFFF) << 2) | (ctrl->info.supplementdata & 0x3) | ((ctrl->info.supplementdata & 0x10) << 10);
        break;
      }
      break;
    }

    break;
  }
  case 2: {
    u16 tmp1 = Vdp2RamReadWord(NULL, Vdp2Ram, addr);
    u16 tmp2 = Vdp2RamReadWord(NULL, Vdp2Ram, addr + 2);
    ctrl->info.charaddr = tmp2 & 0x7FFF;
    ctrl->info.flipfunction = (tmp1 & 0xC000) >> 14;
    switch (ctrl->info.colornumber) {
    case 0:
      ctrl->info.paladdr = (tmp1 & 0x7F);
      break;
    default:
      ctrl->info.paladdr = (tmp1 & 0x70);
      break;
    }
    ctrl->info.specialfunction = (tmp1 & 0x2000) >> 13;
    ctrl->info.specialcolorfunction = (tmp1 & 0x1000) >> 12;
    break;
  }
  }

  if (!(ctrl->regs->VRSIZE & 0x8000))
    ctrl->info.charaddr &= 0x3FFF;

  ctrl->info.charaddr *= 0x20; // thanks Runik

  return 1;
}

static void Vdp2DrawRotation_in_sync(RBGDrawInfo * rbg)
{

  if (rbg == NULL) return;

  vdp2draw_struct *info = &rbg->ctrl.info;
  YglTexture *texture = &rbg->ctrl.texture;

  float i, j;
  int k,l;
  int x, y;
  int cellw, cellh;
  int oldcellx = -1, oldcelly = -1;
  u32 color;
  int vres, hres, vstart;
  int h;
  int v;
  vdp2rotationparameter_struct *parameter;
  u32* colpoint = NULL;

  u32 addr;
  u8 alpha = 0x00;
  vres = rbg->vres * (rbg->ctrl.info.endLine - rbg->ctrl.info.startLine)/yabsys.VBlankLineCount;
  vstart = rbg->ctrl.info.startLine;
  hres = rbg->hres;

  cellw = rbg->ctrl.info.cellw;
  cellh = rbg->ctrl.info.cellh;

  x = 0;
  y = 0;
  if (rbg->rbg_type == 0)
  {
    rbg->paraA.dx = rbg->paraA.A * rbg->paraA.deltaX + rbg->paraA.B * rbg->paraA.deltaY;
    rbg->paraA.dy = rbg->paraA.D * rbg->paraA.deltaX + rbg->paraA.E * rbg->paraA.deltaY;
    rbg->paraA.Xp = rbg->paraA.A * (rbg->paraA.Px - rbg->paraA.Cx) +
      rbg->paraA.B * (rbg->paraA.Py - rbg->paraA.Cy) +
      rbg->paraA.C * (rbg->paraA.Pz - rbg->paraA.Cz) + rbg->paraA.Cx + rbg->paraA.Mx;
    rbg->paraA.Yp = rbg->paraA.D * (rbg->paraA.Px - rbg->paraA.Cx) +
      rbg->paraA.E * (rbg->paraA.Py - rbg->paraA.Cy) +
      rbg->paraA.F * (rbg->paraA.Pz - rbg->paraA.Cz) + rbg->paraA.Cy + rbg->paraA.My;
  }

  if (rbg->useb)
  {
    rbg->paraB.dx = rbg->paraB.A * rbg->paraB.deltaX + rbg->paraB.B * rbg->paraB.deltaY;
    rbg->paraB.dy = rbg->paraB.D * rbg->paraB.deltaX + rbg->paraB.E * rbg->paraB.deltaY;
    rbg->paraB.Xp = rbg->paraB.A * (rbg->paraB.Px - rbg->paraB.Cx) + rbg->paraB.B * (rbg->paraB.Py - rbg->paraB.Cy)
      + rbg->paraB.C * (rbg->paraB.Pz - rbg->paraB.Cz) + rbg->paraB.Cx + rbg->paraB.Mx;
    rbg->paraB.Yp = rbg->paraB.D * (rbg->paraB.Px - rbg->paraB.Cx) + rbg->paraB.E * (rbg->paraB.Py - rbg->paraB.Cy)
      + rbg->paraB.F * (rbg->paraB.Pz - rbg->paraB.Cz) + rbg->paraB.Cy + rbg->paraB.My;
  }

  rbg->paraA.over_pattern_name = rbg->ctrl.regs->OVPNRA;
  rbg->paraB.over_pattern_name = rbg->ctrl.regs->OVPNRB;


  rbg->ctrl.info.cellw = rbg->hres;
  rbg->ctrl.info.cellh = (rbg->vres * (info->endLine - info->startLine))/yabsys.VBlankLineCount;

  if (info->isbitmap) {
    rbg->ctrl.info.cellw = cellw;
    rbg->ctrl.info.cellh = cellh ;
  }

  YglQuadRbg0(rbg, NULL, &rbg->c, rbg->rbg_type, YglTM_vdp2, rbg->ctrl.regs);

 //Not optimal. Should be 0 if there is no offset used.
  _Ygl->useLineColorOffset[0] = ((rbg->ctrl.regs->KTCTL & 0x1010)!=0)?_Ygl->linecolorcoef_tex[0]:0;
  _Ygl->useLineColorOffset[1] = ((rbg->ctrl.regs->KTCTL & 0x1010)!=0)?_Ygl->linecolorcoef_tex[1]:0;
  return;
}

  /*------------------------------------------------------------------------------
   Rotate Screen drawing
   ------------------------------------------------------------------------------*/
  static void FASTCALL Vdp2DrawRotation(RBGDrawInfo * rbg)
  {
    vdp2draw_struct *info = &rbg->ctrl.info;
    YglTexture *texture = &rbg->ctrl.texture;

    int x, y;
    int oldcellx = -1, oldcelly = -1;
    int screenHeight = _Ygl->rheight;
    int screenWidth  = _Ygl->rwidth;

      rbg->vres = _Ygl->rheight;
      if (_Ygl->rwidth >= 640) rbg->hres = (_Ygl->rwidth >> 1); else rbg->hres = _Ygl->rwidth;

    rbg->hres *= _Ygl->widthRatio;
    rbg->vres *= _Ygl->heightRatio;

    RBGGenerator_init(_Ygl->width, _Ygl->height);
    info->vertices[0] = 0;
    info->vertices[1] = (screenHeight * info->startLine)/yabsys.VBlankLineCount;
    info->vertices[2] = screenWidth;
    info->vertices[3] = (screenHeight * info->startLine)/yabsys.VBlankLineCount;
    info->vertices[4] = screenWidth;
    info->vertices[5] = (screenHeight * info->endLine)/yabsys.VBlankLineCount;
    info->vertices[6] = 0;
    info->vertices[7] = (screenHeight * info->endLine)/yabsys.VBlankLineCount;
    info->flipfunction = 0;
    info->cor = 0x00;
    info->cog = 0x00;
    info->cob = 0x00;

    if (rbg->ctrl.regs->RPMD != 0) rbg->useb = 1;
    if (rbg->rbg_type == 0x04) rbg->useb = 1;

    if (!info->isbitmap)
    {
      oldcellx = -1;
      oldcelly = -1;
      rbg->pagesize = info->pagewh*info->pagewh;
      rbg->patternshift = (2 + info->patternwh);
    }
    else
    {
      oldcellx = 0;
      oldcelly = 0;
      rbg->pagesize = 0;
      rbg->patternshift = 0;
    }

      u64 cacheaddr = 0x90000000BAD;

      rbg->vdp2_sync_flg = -1;

      Vdp2DrawRotation_in_sync(rbg);
      pushRBG(rbg);
  }

#ifdef CELL_ASYNC

YabEventQueue *cellq = NULL;
YabEventQueue *cellq_end = NULL;

typedef struct {
  Vdp2Ctrl *ctrl;
} drawCellTask;

static int nbLoop = 0;
static int nbClear = 0;

void* Vdp2DrawCell_in_async(void *p)
{
   while(drawcell_run != 0){
     Vdp2Ctrl *ctrl = (Vdp2Ctrl *)YabWaitEventQueue(cellq);
     if (ctrl != NULL) {
       if (ctrl->order == CELL_SINGLE) {
         Vdp2DrawCell_in_sync(ctrl);
       } else {
         Vdp2DrawCell_in_sync(ctrl);
         ctrl->texture.textdata -= (ctrl->texture.w + 8) * 8 - 8;
         Vdp2DrawCell_in_sync(ctrl);

         ctrl->texture.textdata -= 8;
         ctrl->info.draw_line += 8;
         Vdp2DrawCell_in_sync(ctrl);
         ctrl->texture.textdata -= (ctrl->texture.w + 8) * 8 - 8;
         Vdp2DrawCell_in_sync(ctrl);

       }
       pushCtrl(ctrl);
     }
     YabWaitEventQueue(cellq_end);
   }
   return NULL;
}

static void FASTCALL Vdp2DrawCell(Vdp2Ctrl *ctrl) {

   if (drawcell_run == 0) {
     drawcell_run = 1;
     cellq = YabThreadCreateQueue(NB_MSG);
     cellq_end = YabThreadCreateQueue(NB_MSG);
     YabThreadStart(YAB_THREAD_VDP2_NBG0, Vdp2DrawCell_in_async, 0);
   }
   Vdp2Ctrl *ctrl_toExec = popCtrl();
   memcpy(ctrl_toExec, ctrl, sizeof(Vdp2Ctrl));
   YabAddEventQueue(cellq_end, NULL);
   YabAddEventQueue(cellq, ctrl_toExec);
   // YabThreadYield();
}

static void requestDrawCell(Vdp2Ctrl *ctrl) {
#ifdef CELL_ASYNC
   ctrl->order = CELL_SINGLE;
   Vdp2DrawCell(ctrl);
#else
   Vdp2DrawCell_in_sync(ctrl);
#endif
}

static void requestDrawCellQuad(Vdp2Ctrl *ctrl) {
#ifdef CELL_ASYNC
   ctrl->order = CELL_QUAD;
   Vdp2DrawCell(ctrl);
#else
   Vdp2DrawCell_in_sync(ctrl);
   ctrl->texture.textdata -= (ctrl->texture.w + 8) * 8 - 8;
   Vdp2DrawCell_in_sync(ctrl);
   ctrl->texture.textdata -= 8;
   Vdp2DrawCell_in_sync(ctrl);
   ctrl->texture.textdata -= (ctrl->texture.w + 8) * 8 - 8;
   Vdp2DrawCell_in_sync(ctrl);
#endif
}
#endif

//////////////////////////////////////////////////////////////////////////////

int VIDCSInit(void)
{
  if(renderer_started)
    return -1;

  if (YglInit(2048, 1024, 8) != 0)
    return -1;

  for (int i=0; i<SPRITE; i++)
    YglReset(_Ygl->vdp2levels[i]);

  memset(_Ygl->last_back_color, 0x0, 4*sizeof(float));
  _Ygl->vdp1ratio = 1.0;
  if (VIDCore->SetupVdp1Scale) VIDCore->SetupVdp1Scale((int)_Ygl->vdp1ratio);

  _Ygl->vdp1wdensity = 1.0;
  _Ygl->vdp1hdensity = 1.0;

  _Ygl->vdp2wdensity = 1.0;
  _Ygl->vdp2hdensity = 1.0;
  YglChangeResolution(320, 224);

  VDP2CtrlStack = YabThreadCreateQueue(NB_MSG);
  for (int i=0; i<NB_MSG; i++) pushCtrl((Vdp2Ctrl*)calloc(sizeof(Vdp2Ctrl),1));

  RBGStack = YabThreadCreateQueue(NB_MSG);
  for (int i=0; i<NB_MSG; i++) pushRBG((RBGDrawInfo*)calloc(sizeof(RBGDrawInfo),1));

  renderer_started = 1;
  return 0;
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSDeInit(void)
{
  if(!renderer_started)
    return;
#ifdef CELL_ASYNC
  if (drawcell_run == 1) {
    drawcell_run = 0;
    for (int i=0; i<4; i++) {
      YabAddEventQueue(cellq_end, NULL);
      YabAddEventQueue(cellq, NULL);
    }
    YabThreadWait(YAB_THREAD_VDP2_NBG0);
    YabThreadWait(YAB_THREAD_VDP2_NBG1);
    YabThreadWait(YAB_THREAD_VDP2_NBG2);
    YabThreadWait(YAB_THREAD_VDP2_NBG3);
  }
#endif
  YglGenReset();
  YglDeInit();

  renderer_started = 0;
}

int WaitVdp2Async(int sync) {
  int empty = 0;
  if (vdp2busy == 1) {
#ifdef CELL_ASYNC
    if (cellq_end != NULL) {
      empty = 1;
      while (((empty = YaGetQueueSize(cellq_end))!=0) && (sync == 1))
      {
        YabThreadYield();
      }
    }
#endif
    RBGGenerator_onFinish();
    if (empty == 0) vdp2busy = 0;
  }
  return empty;
}

void waitVdp2DrawScreensEnd(int sync) {
  YglCheckFBSwitch(0);
  if (vdp2busy == 1) {
    WaitVdp2Async(sync);
    YglTmPush(YglTM_vdp2);
    if (VIDCore != NULL) {
      VIDCSReadColorOffset();
      VIDCore->composeFB(&Vdp2Lines[0]);
    }
  }
}

void addCSCommands(vdp1cmd_struct* cmd, int type)
{
  //Test game: Sega rally : The aileron at the start
  int ADx = (cmd->CMDXD - cmd->CMDXA);
  int ADy = (cmd->CMDYD - cmd->CMDYA);
  int BCx = (cmd->CMDXC - cmd->CMDXB);
  int BCy = (cmd->CMDYC - cmd->CMDYB);

  int nbStepAD = sqrt(ADx*ADx + ADy*ADy);
  int nbStepBC = sqrt(BCx*BCx + BCy*BCy);

  int nbStep = MAX(nbStepAD, nbStepBC);

  cmd->type = type;

  cmd->nbStep = nbStep;
  if(cmd->nbStep  != 0) {
    // Ici faut voir encore les Ax doivent faire un de plus.
    cmd->uAstepx = (float)ADx/(float)nbStep;
    cmd->uAstepy = (float)ADy/(float)nbStep;
    cmd->uBstepx = (float)BCx/(float)nbStep;
    cmd->uBstepy = (float)BCy/(float)nbStep;
  } else {
    cmd->uAstepx = 0.0;
    cmd->uAstepy = 0.0;
    cmd->uBstepx = 0.0;
    cmd->uBstepy = 0.0;
  }
#ifdef DEBUG_VDP1_CMD
  YuiMsg("Add Distorted\n");
  YuiMsg("\t[%d,%d]\n", cmd->CMDXA, cmd->CMDYA);
  YuiMsg("\t[%d,%d]\n", cmd->CMDXB, cmd->CMDYB);
  YuiMsg("\t[%d,%d]\n", cmd->CMDXC, cmd->CMDYC);
  YuiMsg("\t[%d,%d]\n", cmd->CMDXD, cmd->CMDYD);
  YuiMsg("\n\n");
  YuiMsg("=> %d (%d %d %d %d => %d %d) %f %f %f %f\n", cmd->nbStep, ADx, ADy, BCx, BCy, nbStepAD, nbStepBC, cmd->uAstepx, cmd->uAstepy, cmd->uBstepx, cmd->uBstepy);
  YuiMsg("==============\n");
#endif
  vdp1_add_upscale(cmd,0);
}

//////////////////////////////////////////////////////////////////////////////
void VIDCSVdp1Draw()
{
  int i;
  int line = 0;
  Vdp2 *varVdp2Regs = &Vdp2Lines[yabsys.LineCount];
  _Ygl->vpd1_running = 1;

  _Ygl->msb_shadow_count_[_Ygl->drawframe] = 0;

  Vdp1DrawCommands(Vdp1Ram, Vdp1Regs);

  _Ygl->vpd1_running = 0;
}


//////////////////////////////////////////////////////////////////////////////
void VIDCSVdp1NormalSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  LOG_CMD("%d\n", __LINE__);

  if (((cmd->CMDPMOD >> 3) & 0x7u) == 5) {
    // hard/vdp2/hon/p09_20.htm#no9_21
    u32 *cclist = (u32 *)&(Vdp2Lines[0].CCRSA);
    cclist[0] &= 0x1Fu;
  }
  cmd->type = QUAD;

  vdp1_add(cmd,0);

  LOG_CMD("%d\n", __LINE__);
}

void VIDCSVdp1ScaledSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{

  if (((cmd->CMDPMOD >> 3) & 0x7u) == 5) {
    // hard/vdp2/hon/p09_20.htm#no9_21
    u32 *cclist = (u32 *)&(Vdp2Lines[0].CCRSA);
    cclist[0] &= 0x1Fu;
  }

  cmd->type = QUAD;
  vdp1_add(cmd,0);

  LOG_CMD("%d\n", __LINE__);
}

void VIDCSVdp1DistortedSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  LOG_CMD("%d\n", __LINE__);

  if (((cmd->CMDPMOD >> 3) & 0x7u) == 5) {
    // hard/vdp2/hon/p09_20.htm#no9_21
    u32 *cclist = (u32 *)&(Vdp2Lines[0].CCRSA);
    cclist[0] &= 0x1Fu;
  }

  cmd->type = DISTORTED;
  vdp1_add(cmd,0);

  return;
}

void VIDCSVdp1PolygonDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  // cmd->type = POLYGON;
  cmd->type = POLYGON;
  vdp1_add(cmd,0);
  return;
}

void VIDCSVdp1PolylineDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  LOG_CMD("%d\n", __LINE__);

  cmd->type = POLYLINE;

  vdp1_add(cmd,0);
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSVdp1LineDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  LOG_CMD("%d\n", __LINE__);

  cmd->type = LINE;

  vdp1_add(cmd,0);
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSVdp1UserClipping(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  if (  (cmd->CMDXC > regs->systemclipX2)
    && (cmd->CMDYC > regs->systemclipY2)
  ) {
    regs->localX = 0;
    regs->localY = 0;
  }

  cmd->type = USER_CLIPPING;
  regs->userclipX1 = cmd->CMDXA;
  regs->userclipY1 = cmd->CMDYA;
  regs->userclipX2 = cmd->CMDXC;
  regs->userclipY2 = cmd->CMDYC;
  vdp1_add(cmd,1);
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSVdp1SystemClipping(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  if (((cmd->CMDXC) == regs->systemclipX2) && (regs->systemclipY2 == (cmd->CMDYC))) return;
  cmd->type = SYSTEM_CLIPPING;
  regs->systemclipX2 = cmd->CMDXC;
  regs->systemclipY2 = cmd->CMDYC;
  vdp1_add(cmd,1);
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSVdp1NormalSpriteDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  LOG_CMD("%d\n", __LINE__);

  cmd->CMDXB = cmd->CMDXA + MAX(1,cmd->w);
  cmd->CMDYB = cmd->CMDYA;
  cmd->CMDXC = cmd->CMDXA + MAX(1,cmd->w);
  cmd->CMDYC = cmd->CMDYA + MAX(1,cmd->h);
  cmd->CMDXD = cmd->CMDXA;
  cmd->CMDYD = cmd->CMDYA + MAX(1,cmd->h);

  if (((cmd->CMDPMOD >> 3) & 0x7u) == 5) {
    // hard/vdp2/hon/p09_20.htm#no9_21
    u32 *cclist = (u32 *)&(Vdp2Lines[0].CCRSA);
    cclist[0] &= 0x1Fu;
  }
  cmd->type = QUAD;

  vdp1_add_upscale(cmd,0);

  LOG_CMD("%d\n", __LINE__);
}

void VIDCSVdp1ScaledSpriteDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{

  // Setup Zoom Point
  switch ((cmd->CMDCTRL & 0xF00) >> 8)
  {
    case 0x0: // Only two coordinates
      if ((s16)cmd->CMDXC > (s16)cmd->CMDXA){ cmd->CMDXB += 1; cmd->CMDXC += 1;} else { cmd->CMDXA += 1; cmd->CMDXD += 1;}
      if ((s16)cmd->CMDYC > (s16)cmd->CMDYA){ cmd->CMDYC += 1; cmd->CMDYD += 1;} else { cmd->CMDYA += 1; cmd->CMDYD += 1;}
      break;
    case 0x5: // Upper-left
    case 0x6: // Upper-Center
    case 0x7: // Upper-Right
    case 0x9: // Center-left
    case 0xA: // Center-center
    case 0xB: // Center-right
    case 0xD: // Lower-left
    case 0xE: // Lower-center
    case 0xF: // Lower-right
      cmd->CMDXB += 1;
      cmd->CMDXC += 1;
      cmd->CMDYC += 1;
      cmd->CMDYD += 1;
      break;
    default: break;
  }
  if (((cmd->CMDPMOD >> 3) & 0x7u) == 5) {
    // hard/vdp2/hon/p09_20.htm#no9_21
    u32 *cclist = (u32 *)&(Vdp2Lines[0].CCRSA);
    cclist[0] &= 0x1Fu;
  }

  cmd->type = QUAD;
  vdp1_add_upscale(cmd,0);

  LOG_CMD("%d\n", __LINE__);
}

void VIDCSVdp1DistortedSpriteDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  LOG_CMD("%d\n", __LINE__);

  if (((cmd->CMDPMOD >> 3) & 0x7u) == 5) {
    // hard/vdp2/hon/p09_20.htm#no9_21
    u32 *cclist = (u32 *)&(Vdp2Lines[0].CCRSA);
    cclist[0] &= 0x1Fu;
  }

  addCSCommands(cmd, DISTORTED);

  return;
}

void VIDCSVdp1PolygonDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  // cmd->type = POLYGON;

  addCSCommands(cmd,POLYGON);
  return;
}

void VIDCSVdp1PolylineDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  LOG_CMD("%d\n", __LINE__);

  cmd->type = POLYLINE;

  vdp1_add_upscale(cmd,0);
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSVdp1LineDrawUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  LOG_CMD("%d\n", __LINE__);

  cmd->type = LINE;

  vdp1_add_upscale(cmd,0);
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSVdp1UserClippingUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  if (  (cmd->CMDXC > regs->systemclipX2)
    && (cmd->CMDYC > regs->systemclipY2)
  ) {
    regs->localX = 0;
    regs->localY = 0;
  }

  cmd->type = USER_CLIPPING;
  regs->userclipX1 = cmd->CMDXA;
  regs->userclipY1 = cmd->CMDYA;
  regs->userclipX2 = cmd->CMDXC;
  regs->userclipY2 = cmd->CMDYC;
  vdp1_add_upscale(cmd,1);
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSVdp1SystemClippingUpscale(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  if (((cmd->CMDXC) == regs->systemclipX2) && (regs->systemclipY2 == (cmd->CMDYC))) return;
  cmd->type = SYSTEM_CLIPPING;
  regs->systemclipX2 = cmd->CMDXC;
  regs->systemclipY2 = cmd->CMDYC;
  vdp1_add_upscale(cmd,1);
}

void VIDCSVdp1DrawFB(void) {
  VIDCSRenderVDP1();
  vdp1_write();
}

static void Vdp2DrawNBG0(Vdp2* varVdp2Regs) {
  YglCache tmpc;
  u32 char_access = 0;
  u32 ptn_access = 0;
  Vdp2Ctrl ctrl;
  ctrl.regs = varVdp2Regs;
  ctrl.info.dst = 0;
  ctrl.info.idScreen = NBG0;
  ctrl.info.coordincx = 1.0f;
  ctrl.info.coordincy = 1.0f;

  ctrl.info.cor = 0;
  ctrl.info.cog = 0;
  ctrl.info.cob = 0;
  int i;
  ctrl.info.enable = 0;
  ctrl.info.startLine = 0;
  ctrl.info.endLine = (yabsys.VBlankLineCount < 270)?yabsys.VBlankLineCount:270;

  ctrl.info.cellh = 256;
  if (_Ygl->interlace == DOUBLE_INTERLACE) ctrl.info.cellh = ctrl.info.cellh << 1;
  ctrl.info.specialcolorfunction = 0;

    // NBG0 mode
  for (i=0; i<yabsys.VBlankLineCount; i++) {
    ctrl.info.display[i] = isEnabled(NBG0, &Vdp2Lines[i]);
    ctrl.info.enable |= ctrl.info.display[i];
    ctrl.info.alpha_per_line[i] = (~Vdp2Lines[i].CCRNA & 0x1F) << 3;
  }
    if (!ctrl.info.enable) {
      return;
    }

    for (int i=0; i < 4; i++) {
        ctrl.info.char_bank[i] = 0;
        ctrl.info.pname_bank[i] = 0;
        for (int j=0; j < 8; j++) {
          if (Vdp2External.AC_VRAM[i][j] == 0x04) {
            ctrl.info.char_bank[i] = 1;
            char_access |= 1<<j;
          }
          if (Vdp2External.AC_VRAM[i][j] == 0x00) {
            ctrl.info.pname_bank[i] = 1;
            ptn_access |= (1 << j);
          }
        }
      }

      //ToDo Need to determine if NBG0 shall be disabled due to VRAM access
      //if (char_access == 0) return;

    if (char_access == 0) {
      return;
    }
    if ((ctrl.info.isbitmap = ctrl.regs->CHCTLA & 0x2) != 0)
    {
      // Bitmap Mode
      ReadBitmapSize(&ctrl.info, ctrl.regs->CHCTLA >> 2, 0x3);

      ctrl.info.x = -((ctrl.regs->SCXIN0 & 0x7FF) % ctrl.info.cellw);
      ctrl.info.y = -((ctrl.regs->SCYIN0 & 0x7FF) % ctrl.info.cellh);

      ctrl.info.charaddr = (ctrl.regs->MPOFN & 0x7) * 0x20000;
      ctrl.info.paladdr = (ctrl.regs->BMPNA & 0x7) << 4;
      ctrl.info.flipfunction = 0;
      ctrl.info.specialcolorfunction = (ctrl.regs->BMPNA & 0x10) >> 4;
      ctrl.info.specialfunction = (ctrl.regs->BMPNA >> 5) & 0x01;

      //If RBG0 is used and the VRAM is used for it, check that NBGx is not using reserved area, otherwise, do not display
      int charAddrBk = (((ctrl.info.charaddr >> 16)& 0xF) >> ((ctrl.regs->VRSIZE >> 15)&0x1)) >> 1;
      int needUpdate = 0;
      for (int i=0; i<yabsys.VBlankLineCount; i++) {
        if ((Vdp2Lines[i].BGON & 0x10)!=0) {
          //RBG0 is enabled for this line. Check we can display the NBGx
          if(((Vdp2Lines[i].RAMCTL>>(charAddrBk<<1))&0x3) != 0x0){
            //VRAM on the dedicated bank is used by RBG0, it can not be used by NBGx
            needUpdate = 1;
            ctrl.info.display[i] = 0;
          }
        }
      }
      if (needUpdate != 0) {
        ctrl.info.enable = 0;
        for (int i=0; i<yabsys.VBlankLineCount; i++) ctrl.info.enable |= ctrl.info.display[i];
        if (!ctrl.info.enable) {
          return;
        }
      }
    }
    else
    {
      // Tile Mode
      if (ptn_access == 0) {
        return;
      }
      ctrl.info.mapwh = 2;

      ReadPlaneSize(&ctrl.info, ctrl.regs->PLSZ);

      ctrl.info.x = -((ctrl.regs->SCXIN0 & 0x7FF) % (512 * ctrl.info.planew));
      ctrl.info.y = -((ctrl.regs->SCYIN0 & 0x7FF) % (512 * ctrl.info.planeh));
      ReadPatternData(&ctrl.info, ctrl.regs->PNCN0, ctrl.regs->CHCTLA & 0x1);
    }

    if ((ctrl.regs->ZMXN0.all & 0x7FF00) == 0) {
      //invalid zoom value
      return;
    }
    else
      ctrl.info.coordincx = (float)65536 / (ctrl.regs->ZMXN0.all & 0x7FF00);

    switch (ctrl.regs->ZMCTL & 0x03)
    {
    case 0:
      ctrl.info.maxzoom = 1.0f;
      break;
    case 1:
      ctrl.info.maxzoom = 0.5f;
      //if( info.coordincx < 0.5f )  info.coordincx = 0.5f;
      break;
    case 2:
    case 3:
      ctrl.info.maxzoom = 0.25f;
      //if( info.coordincx < 0.25f )  info.coordincx = 0.25f;
      break;
    }

    if ((ctrl.regs->ZMYN0.all & 0x7FF00) == 0) {
      //Invalid zoom value
      return;
    }
    else
      ctrl.info.coordincy = (float)65536 / (ctrl.regs->ZMYN0.all & 0x7FF00);

    ctrl.info.PlaneAddr = (void FASTCALL(*)(void *, int, Vdp2*))&Vdp2NBG0PlaneAddr;


  ReadMosaicData(&ctrl.info, 0x1, ctrl.regs);

  ctrl.info.transparencyenable = !(ctrl.regs->BGON & 0x100);
  ctrl.info.specialprimode = ctrl.regs->SFPRMD & 0x3;
  ctrl.info.specialcolormode = ctrl.regs->SFCCMD & 0x3;

  if (ctrl.regs->SFSEL & 0x1)
    ctrl.info.specialcode = ctrl.regs->SFCODE >> 8;
  else
    ctrl.info.specialcode = ctrl.regs->SFCODE & 0xFF;

  ctrl.info.colornumber = (ctrl.regs->CHCTLA & 0x70) >> 4;

  int dest_alpha = ((ctrl.regs->CCCTL >> 9) & 0x01);

  ctrl.info.coloroffset = (ctrl.regs->CRAOFA & 0x7) << 8;
  ctrl.info.linecheck_mask = 0x01;
  ctrl.info.priority = ctrl.regs->PRINA & 0x7;

  if (ctrl.info.priority == 0){
    return;
  }

  ReadLineScrollData(&ctrl.info, ctrl.regs->SCRCTL & 0xFF, ctrl.regs->LSTA0.all);
  ctrl.info.lineinfo = lineNBG0;
  Vdp2GenLineinfo(&ctrl.info);

  if (ctrl.regs->SCRCTL & 1)
  {
    ctrl.info.isverticalscroll = 1;
    ctrl.info.verticalscrolltbl = (ctrl.regs->VCSTA.all & 0x7FFFE) << 1;
    if (ctrl.regs->SCRCTL & 0x100)
      ctrl.info.verticalscrollinc = 8;
    else
      ctrl.info.verticalscrollinc = 4;
  }
  else
    ctrl.info.isverticalscroll = 0;

// NBG0 draw
    if (ctrl.info.isbitmap)
    {
      if (ctrl.info.coordincx != 1.0f || ctrl.info.coordincy != 1.0f || VDPLINE_SZ(ctrl.info.islinescroll)) {
        ctrl.info.sh = (ctrl.regs->SCXIN0 & 0x7FF);
        ctrl.info.sv = (ctrl.regs->SCYIN0 & 0x7FF);
        ctrl.info.x = 0;
        ctrl.info.y = 0;
        ctrl.info.vertices[0] = 0;
        ctrl.info.vertices[1] = 0;
        ctrl.info.vertices[2] = _Ygl->rwidth;
        ctrl.info.vertices[3] = 0;
        ctrl.info.vertices[4] = _Ygl->rwidth;
        ctrl.info.vertices[5] = _Ygl->rheight;
        ctrl.info.vertices[6] = 0;
        ctrl.info.vertices[7] = _Ygl->rheight;
        vdp2draw_struct infotmp = ctrl.info;
        infotmp.cellw = _Ygl->rwidth;
        infotmp.cellh = _Ygl->rheight;
        // if (_Ygl->interlace == SINGLE_INTERLACE) infotmp.cellh = infotmp.cellh << 1;
        YglQuad(&infotmp, &ctrl.texture, &tmpc, YglTM_vdp2);
        Vdp2DrawBitmapCoordinateInc(&ctrl);
      }
      else {

        int xx, yy;
        int isCached = 0;

        if (ctrl.info.islinescroll) // Nights Movie
        {
          ctrl.info.sh = (ctrl.regs->SCXIN0 & 0x7FF);
          ctrl.info.sv = (ctrl.regs->SCYIN0 & 0x7FF);
          ctrl.info.x = 0;
          ctrl.info.y = 0;
          ctrl.info.vertices[0] = 0;
          ctrl.info.vertices[1] = 0;
          ctrl.info.vertices[2] = _Ygl->rwidth;
          ctrl.info.vertices[3] = 0;
          ctrl.info.vertices[4] = _Ygl->rwidth;
          ctrl.info.vertices[5] = _Ygl->rheight;
          ctrl.info.vertices[6] = 0;
          ctrl.info.vertices[7] = _Ygl->rheight;
          vdp2draw_struct infotmp = ctrl.info;
          infotmp.cellw = _Ygl->rwidth;
          infotmp.cellh = _Ygl->rheight;
          // if (_Ygl->interlace == SINGLE_INTERLACE) infotmp.cellh = infotmp.cellh << 1;
          YglQuad(&infotmp, &ctrl.texture, &tmpc, YglTM_vdp2);
          Vdp2DrawBitmapLineScroll(&ctrl, _Ygl->rwidth, _Ygl->rheight);

        }
        else {
          int cellh = ctrl.info.cellh;
          // if (_Ygl->interlace == SINGLE_INTERLACE) cellh = cellh << 1;
          yy = ctrl.info.y;
          while (yy + ctrl.info.y < _Ygl->rheight)
          {
            ctrl.info.draw_line = yy;
            xx = ctrl.info.x;
            while (xx + ctrl.info.x < _Ygl->rwidth)
            {
              ctrl.info.vertices[0] = xx;
              ctrl.info.vertices[1] = yy;
              ctrl.info.vertices[2] = (xx + ctrl.info.cellw);
              ctrl.info.vertices[3] = yy;
              ctrl.info.vertices[4] = (xx + ctrl.info.cellw);
              ctrl.info.vertices[5] = (yy + cellh);
              ctrl.info.vertices[6] = xx;
              ctrl.info.vertices[7] = (yy + cellh);
              if (isCached == 0)
              {
                YglQuad(&ctrl.info, &ctrl.texture, &tmpc, YglTM_vdp2);
                if (ctrl.info.islinescroll) {
                  Vdp2DrawBitmapLineScroll(&ctrl, ctrl.info.cellw, cellh);
                } else {
                  requestDrawCell(&ctrl);
                }
                isCached = 1;
              }
              else {
                YglCachedQuad(&ctrl.info, &tmpc, YglTM_vdp2);
              }
              xx += ctrl.info.cellw;
            }
            yy += cellh;
          }
        }
      }
    }
    else
    {
      if (ctrl.info.islinescroll) {
        int screenH = _Ygl->rheight;
        // if (_Ygl->interlace == DOUBLE_INTERLACE) screenH <<= 1;
        ctrl.info.sh = (ctrl.regs->SCXIN0 & 0x7FF);
        ctrl.info.sv = (ctrl.regs->SCYIN0 & 0x7FF);
        ctrl.info.x = 0;
        ctrl.info.y = 0;
        ctrl.info.vertices[0] = 0;
        ctrl.info.vertices[1] = 0;
        ctrl.info.vertices[2] = _Ygl->rwidth;
        ctrl.info.vertices[3] = 0;
        ctrl.info.vertices[4] = _Ygl->rwidth;
        ctrl.info.vertices[5] = screenH;
        ctrl.info.vertices[6] = 0;
        ctrl.info.vertices[7] = screenH;
        vdp2draw_struct infotmp = ctrl.info;
        infotmp.cellw = _Ygl->rwidth;
        infotmp.cellh = _Ygl->rheight;
        // if (_Ygl->interlace == SINGLE_INTERLACE) infotmp.cellh <<= 1;
        infotmp.flipfunction = 0;

        YglQuad(&infotmp, &ctrl.texture, &tmpc, YglTM_vdp2);
        Vdp2DrawMapPerLine(&ctrl);
      }
      else {
        int delayed = 0;
        // Setting miss of cycle patten need to plus 8 dot vertical
        // If pattern access is defined on T0 for NBG0 or NBG1, there is no limitation
        if (((ptn_access & 0x1)==0) && Vdp2CheckCharAccessPenalty(char_access, ptn_access) != 0) {
          delayed = 1;
        }
        ctrl.info.x = ctrl.regs->SCXIN0 & 0x7FF;
        ctrl.info.y = ctrl.regs->SCYIN0 & 0x7FF;
        Vdp2DrawMapTest(&ctrl, delayed);
      }
    }
#ifdef CELL_ASYNC
    YabThreadYield();
#endif
}

//////////////////////////////////////////////////////////////////////////////

static void Vdp2DrawNBG1(Vdp2* varVdp2Regs)
{
  YglCache tmpc;
  u32 char_access = 0;
  u32 ptn_access = 0;
  Vdp2Ctrl ctrl;
  ctrl.regs = varVdp2Regs;
  ctrl.info.dst = 0;
  ctrl.info.idScreen = NBG1;
  ctrl.info.cor = 0;
  ctrl.info.cog = 0;
  ctrl.info.cob = 0;
  ctrl.info.specialcolorfunction = 0;
  ctrl.info.enable = 0;
  ctrl.info.startLine = 0;
  ctrl.info.endLine = (yabsys.VBlankLineCount < 270)?yabsys.VBlankLineCount:270;

  for (int i=0; i<yabsys.VBlankLineCount; i++) {
    ctrl.info.display[i] = isEnabled(NBG1, &Vdp2Lines[i]);
    ctrl.info.enable |= ctrl.info.display[i];
    ctrl.info.alpha_per_line[i] = ((~Vdp2Lines[i].CCRNA & 0x1F00) >> 5);
  }
  if (!ctrl.info.enable) {
    return;
  }

  for (int i=0; i < 4; i++) {
      ctrl.info.char_bank[i] = 0;
      ctrl.info.pname_bank[i] = 0;
      for (int j=0; j < 8; j++) {
        if (Vdp2External.AC_VRAM[i][j] == 0x05) {
          ctrl.info.char_bank[i] = 1;
          char_access |= 1<<j;
        }
        if (Vdp2External.AC_VRAM[i][j] == 0x01) {
          ctrl.info.pname_bank[i] = 1;
          ptn_access |= (1 << j);
        }
      }
    }
  //ToDo Need to determine if NBG1 shall be disabled due to VRAM access
  //if (char_access == 0) return;

  ctrl.info.transparencyenable = !(ctrl.regs->BGON & 0x200);
  ctrl.info.specialprimode = (ctrl.regs->SFPRMD >> 2) & 0x3;

  ctrl.info.colornumber = (ctrl.regs->CHCTLA & 0x3000) >> 12;

  if (char_access == 0) {
    return;
  }

  if ((ctrl.info.isbitmap = ctrl.regs->CHCTLA & 0x200) != 0)
  {
    //If there is no access to character pattern data, do not display the layer
    ReadBitmapSize(&ctrl.info, ctrl.regs->CHCTLA >> 10, 0x3);

    ctrl.info.x = -((ctrl.regs->SCXIN1 & 0x7FF) % ctrl.info.cellw);
    ctrl.info.y = -((ctrl.regs->SCYIN1 & 0x7FF) % ctrl.info.cellh);
    ctrl.info.charaddr = ((ctrl.regs->MPOFN & 0x70) >> 4) * 0x20000;
    ctrl.info.paladdr = (ctrl.regs->BMPNA & 0x700) >> 4;
    ctrl.info.flipfunction = 0;
    ctrl.info.specialfunction = 0;
    ctrl.info.specialcolorfunction = (ctrl.regs->BMPNA & 0x1000) >> 4;

    //If RBG0 is used and the VRAM is used for it, check that NBGx is not using reserved area, otherwise, do not display
    int charAddrBk = (((ctrl.info.charaddr >> 16)& 0xF) >> ((ctrl.regs->VRSIZE >> 15)&0x1)) >> 1;
    int needUpdate = 0;
    for (int i=0; i<yabsys.VBlankLineCount; i++) {
      if ((Vdp2Lines[i].BGON & 0x10)!=0) {
        //RBG0 is enabled for this line. Check we can display the NBGx
        if(((Vdp2Lines[i].RAMCTL>>(charAddrBk<<1))&0x3) != 0x0){
          //VRAM on the dedicated bank is used by RBG0, it can not be used by NBGx
          needUpdate = 1;
          ctrl.info.display[i] = 0;
        }
      }
    }
    if (needUpdate != 0) {
      ctrl.info.enable = 0;
      for (int i=0; i<yabsys.VBlankLineCount; i++) ctrl.info.enable |= ctrl.info.display[i];
      if (!ctrl.info.enable) {
        return;
      }
    }
  }
  else
  {
    if (ptn_access == 0) {
      return;
    }
    ctrl.info.mapwh = 2;

    ReadPlaneSize(&ctrl.info, ctrl.regs->PLSZ >> 2);

    ctrl.info.x = -((ctrl.regs->SCXIN1 & 0x7FF) % (512 * ctrl.info.planew));
    ctrl.info.y = -((ctrl.regs->SCYIN1 & 0x7FF) % (512 * ctrl.info.planeh));

    ReadPatternData(&ctrl.info, ctrl.regs->PNCN1, ctrl.regs->CHCTLA & 0x100);
  }

  ctrl.info.specialcolormode = (ctrl.regs->SFCCMD >> 2) & 0x3;

  if (ctrl.regs->SFSEL & 0x2)
    ctrl.info.specialcode = ctrl.regs->SFCODE >> 8;
  else
    ctrl.info.specialcode = ctrl.regs->SFCODE & 0xFF;

  ReadMosaicData(&ctrl.info, 0x2, ctrl.regs);


  ctrl.info.coloroffset = (ctrl.regs->CRAOFA & 0x70) << 4;
  ctrl.info.linecheck_mask = 0x02;

  if ((ctrl.regs->ZMXN1.all & 0x7FF00) == 0){
    //invalid zoom value
    return;
  }
  else
    ctrl.info.coordincx = (float)65536 / (ctrl.regs->ZMXN1.all & 0x7FF00);

  switch ((ctrl.regs->ZMCTL >> 8) & 0x03)
  {
  case 0:
    ctrl.info.maxzoom = 1.0f;
    break;
  case 1:
    ctrl.info.maxzoom = 0.5f;
    //      if( ctrl.info.coordincx < 0.5f )  ctrl.info.coordincx = 0.5f;
    break;
  case 2:
  case 3:
    ctrl.info.maxzoom = 0.25f;
    //      if( ctrl.info.coordincx < 0.25f )  ctrl.info.coordincx = 0.25f;
    break;
  }
  if ((ctrl.regs->ZMYN1.all & 0x7FF00) == 0)
    ctrl.info.coordincy = 1.0f;
  else
    ctrl.info.coordincy = (float)65536 / (ctrl.regs->ZMYN1.all & 0x7FF00);


  ctrl.info.priority = (ctrl.regs->PRINA >> 8) & 0x7;

  ctrl.info.PlaneAddr = (void FASTCALL(*)(void *, int, Vdp2*))&Vdp2NBG1PlaneAddr;

  if ((ctrl.info.priority == 0) ||
    (ctrl.regs->BGON & 0x1 && (ctrl.regs->CHCTLA & 0x70) >> 4 == 4)) {
      // If NBG0 16M mode is enabled, don't draw
      return;
    }

  ReadLineScrollData(&ctrl.info, ctrl.regs->SCRCTL >> 8, ctrl.regs->LSTA1.all);
  ctrl.info.lineinfo = lineNBG1;
  Vdp2GenLineinfo(&ctrl.info);
  if (ctrl.regs->SCRCTL & 0x100)
  {
    ctrl.info.isverticalscroll = 1;
    if (ctrl.regs->SCRCTL & 0x1)
    {
      ctrl.info.verticalscrolltbl = 4 + ((ctrl.regs->VCSTA.all & 0x7FFFE) << 1);
      ctrl.info.verticalscrollinc = 8;
    }
    else
    {
      ctrl.info.verticalscrolltbl = (ctrl.regs->VCSTA.all & 0x7FFFE) << 1;
      ctrl.info.verticalscrollinc = 4;
    }
  }
  else
    ctrl.info.isverticalscroll = 0;

  if (ctrl.info.isbitmap)
  {

    if (ctrl.info.coordincx != 1.0f || ctrl.info.coordincy != 1.0f || VDPLINE_SZ(ctrl.info.islinescroll)) {
      ctrl.info.sh = (ctrl.regs->SCXIN1 & 0x7FF);
      ctrl.info.sv = (ctrl.regs->SCYIN1 & 0x7FF);
      ctrl.info.x = 0;
      ctrl.info.y = 0;
      ctrl.info.vertices[0] = 0;
      ctrl.info.vertices[1] = 0;
      ctrl.info.vertices[2] = _Ygl->rwidth;
      ctrl.info.vertices[3] = 0;
      ctrl.info.vertices[4] = _Ygl->rwidth;
      ctrl.info.vertices[5] = _Ygl->rheight;
      ctrl.info.vertices[6] = 0;
      ctrl.info.vertices[7] = _Ygl->rheight;
      vdp2draw_struct infotmp = ctrl.info;
      infotmp.cellw = _Ygl->rwidth;
      infotmp.cellh = _Ygl->rheight;
      // if (_Ygl->interlace == SINGLE_INTERLACE) infotmp.cellh = infotmp.cellh << 1;
      YglQuad(&infotmp, &ctrl.texture, &tmpc, YglTM_vdp2);
      Vdp2DrawBitmapCoordinateInc(&ctrl);
    }
    else {

      int xx, yy;
      int isCached = 0;

      if (ctrl.info.islinescroll) // Nights Movie
      {
        ctrl.info.sh = (ctrl.regs->SCXIN1 & 0x7FF);
        ctrl.info.sv = (ctrl.regs->SCYIN1 & 0x7FF);
        ctrl.info.x = 0;
        ctrl.info.y = 0;
        ctrl.info.vertices[0] = 0;
        ctrl.info.vertices[1] = 0;
        ctrl.info.vertices[2] = _Ygl->rwidth;
        ctrl.info.vertices[3] = 0;
        ctrl.info.vertices[4] = _Ygl->rwidth;
        ctrl.info.vertices[5] = _Ygl->rheight;
        ctrl.info.vertices[6] = 0;
        ctrl.info.vertices[7] = _Ygl->rheight;
        vdp2draw_struct infotmp = ctrl.info;
        infotmp.cellw = _Ygl->rwidth;
        infotmp.cellh = _Ygl->rheight;
        // if (_Ygl->interlace == SINGLE_INTERLACE) infotmp.cellh = infotmp.cellh << 1;
        YglQuad(&infotmp, &ctrl.texture, &tmpc, YglTM_vdp2);
        Vdp2DrawBitmapLineScroll(&ctrl, _Ygl->rwidth, _Ygl->rheight);
      }
      else {
        int cellh = ctrl.info.cellh;
        // if (_Ygl->interlace == SINGLE_INTERLACE) cellh = cellh << 1;
        yy = ctrl.info.y;
        while (yy + ctrl.info.y < _Ygl->rheight)
        {
          ctrl.info.draw_line = yy;
          xx = ctrl.info.x;
          while (xx + ctrl.info.x < _Ygl->rwidth)
          {
            ctrl.info.vertices[0] = xx;
            ctrl.info.vertices[1] = yy;
            ctrl.info.vertices[2] = (xx + ctrl.info.cellw);
            ctrl.info.vertices[3] = yy;
            ctrl.info.vertices[4] = (xx + ctrl.info.cellw);
            ctrl.info.vertices[5] = (yy + cellh);
            ctrl.info.vertices[6] = xx;
            ctrl.info.vertices[7] = (yy + cellh);
            if (isCached == 0)
            {
              YglQuad(&ctrl.info, &ctrl.texture, &tmpc, YglTM_vdp2);
              if (ctrl.info.islinescroll) {
                Vdp2DrawBitmapLineScroll(&ctrl, ctrl.info.cellw, cellh);
              }
              else {
                requestDrawCell(&ctrl);
              }
              isCached = 1;
            }
            else {
              YglCachedQuad(&ctrl.info, &tmpc, YglTM_vdp2);
            }
            xx += ctrl.info.cellw;
          }
          yy += cellh;
        }
      }
    }
  }
  else {
    if (ctrl.info.islinescroll) {
      if (char_access == 0) {
        return;
      }
      int screenH = _Ygl->rheight;
      // if (_Ygl->interlace == DOUBLE_INTERLACE) screenH <<= 1;
      ctrl.info.sh = (ctrl.regs->SCXIN1 & 0x7FF);
      ctrl.info.sv = (ctrl.regs->SCYIN1 & 0x7FF);
      ctrl.info.x = 0;
      ctrl.info.y = 0;
      ctrl.info.vertices[0] = 0;
      ctrl.info.vertices[1] = 0;
      ctrl.info.vertices[2] = _Ygl->rwidth;
      ctrl.info.vertices[3] = 0;
      ctrl.info.vertices[4] = _Ygl->rwidth;
      ctrl.info.vertices[5] = screenH;
      ctrl.info.vertices[6] = 0;
      ctrl.info.vertices[7] = screenH;
      vdp2draw_struct infotmp = ctrl.info;
      infotmp.cellw = _Ygl->rwidth;
      infotmp.cellh = _Ygl->rheight;
      // if (_Ygl->interlace == SINGLE_INTERLACE) infotmp.cellh <<= 1;
      infotmp.flipfunction = 0;

      YglQuad(&infotmp, &ctrl.texture, &tmpc, YglTM_vdp2);
      Vdp2DrawMapPerLine(&ctrl);
    }
    else {
      //Vdp2DrawMap(&ctrl.info, &ctrl.texture);
      int delayed = 0;
      // Setting miss of cycle patten need to plus 8 dot vertical
      // If pattern access is defined on T0 for NBG0 or NBG1, there is no limitation
      //If there is no access to pattern data, do not display the layer
      if (((ptn_access & 0x1)==0) && Vdp2CheckCharAccessPenalty(char_access, ptn_access) != 0) {
        delayed = 1;
        // YuiMsg("Penalty %x %x\n", char_access, ptn_access);
      }
      ctrl.info.x = ctrl.regs->SCXIN1 & 0x7FF;
      ctrl.info.y = ctrl.regs->SCYIN1 & 0x7FF;
      Vdp2DrawMapTest(&ctrl, delayed);
    }
  }
  #ifdef CELL_ASYNC
      YabThreadYield();
  #endif
}

//////////////////////////////////////////////////////////////////////////////

static void Vdp2DrawNBG2(Vdp2* varVdp2Regs)
{
  Vdp2Ctrl ctrl;
  ctrl.regs = varVdp2Regs;
  ctrl.info.dst = 0;
  ctrl.info.idScreen = NBG2;
  ctrl.info.cor = 0;
  ctrl.info.cog = 0;
  ctrl.info.cob = 0;
  ctrl.info.specialcolorfunction = 0;
  ctrl.info.enable = 0;
  ctrl.info.startLine = 0;
  ctrl.info.endLine = (yabsys.VBlankLineCount < 270)?yabsys.VBlankLineCount:270;

  for (int i=0; i<yabsys.VBlankLineCount; i++) {
    ctrl.info.display[i] = isEnabled(NBG2, &Vdp2Lines[i]);
    ctrl.info.enable |= ctrl.info.display[i];
    ctrl.info.alpha_per_line[i] = (~Vdp2Lines[i].CCRNB & 0x1F) << 3;
  }
  if (!ctrl.info.enable) {
    return;
  }

  ctrl.info.transparencyenable = !(ctrl.regs->BGON & 0x400);
  ctrl.info.specialprimode = (ctrl.regs->SFPRMD >> 4) & 0x3;

  ctrl.info.colornumber = (ctrl.regs->CHCTLB & 0x2) >> 1;
  ctrl.info.mapwh = 2;

  ReadPlaneSize(&ctrl.info, ctrl.regs->PLSZ >> 4);
  ctrl.info.x = -((ctrl.regs->SCXN2 & 0x7FF) % (512 * ctrl.info.planew));
  ctrl.info.y = -((ctrl.regs->SCYN2 & 0x7FF) % (512 * ctrl.info.planeh));
  ReadPatternData(&ctrl.info, ctrl.regs->PNCN2, ctrl.regs->CHCTLB & 0x1);

  ReadMosaicData(&ctrl.info, 0x4, ctrl.regs);

  ctrl.info.specialcolormode = (ctrl.regs->SFCCMD >> 4) & 0x3;

  if (ctrl.regs->SFSEL & 0x4)
    ctrl.info.specialcode = ctrl.regs->SFCODE >> 8;
  else
    ctrl.info.specialcode = ctrl.regs->SFCODE & 0xFF;


  ctrl.info.coloroffset = ctrl.regs->CRAOFA & 0x700;

  ctrl.info.linecheck_mask = 0x04;
  ctrl.info.coordincx = ctrl.info.coordincy = 1;
  ctrl.info.priority = ctrl.regs->PRINB & 0x7;
  ctrl.info.PlaneAddr = (void FASTCALL(*)(void *, int, Vdp2*))&Vdp2NBG2PlaneAddr;

  if ((ctrl.info.priority == 0) ||
    (ctrl.regs->BGON & 0x1 && (ctrl.regs->CHCTLA & 0x70) >> 4 >= 2)) {
      // If NBG0 2048/32786/16M mode is enabled, don't draw
      return;
    }

  ctrl.info.islinescroll = 0;
  ctrl.info.linescrolltbl = 0;
  ctrl.info.lineinc = 0;
  ctrl.info.isverticalscroll = 0;

  int delayed = 0;
  {
    int char_access = 0;
    int ptn_access = 0;

    for (int i = 0; i < 4; i++) {
      ctrl.info.char_bank[i] = 0;
      ctrl.info.pname_bank[i] = 0;
      for (int j = 0; j < 8; j++) {
        if (Vdp2External.AC_VRAM[i][j] == 0x06) {
          ctrl.info.char_bank[i] = 1;
          char_access |= (1 << j);
        }
        if (Vdp2External.AC_VRAM[i][j] == 0x02) {
          ctrl.info.pname_bank[i] = 1;
          ptn_access |= (1 << j);
        }
      }
    }
    if (char_access == 0) {
      return;
    }
    if (ptn_access == 0) {
      return;
    }
    // Setting miss of cycle patten need to plus 8 dot vertical
    if (Vdp2CheckCharAccessPenalty(char_access, ptn_access) != 0) {
      delayed = 1;
    }
  }


  ctrl.info.x = ctrl.regs->SCXN2 & 0x7FF;
  ctrl.info.y = ctrl.regs->SCYN2 & 0x7FF;
  Vdp2DrawMapTest(&ctrl, delayed);
#ifdef CELL_ASYNC
    YabThreadYield();
#endif
}

//////////////////////////////////////////////////////////////////////////////

static void Vdp2DrawNBG3(Vdp2* varVdp2Regs)
{
  Vdp2Ctrl ctrl;
  ctrl.regs = varVdp2Regs;
  ctrl.info.idScreen = NBG3;
  ctrl.info.dst = 0;
  ctrl.info.cor = 0;
  ctrl.info.cog = 0;
  ctrl.info.cob = 0;
  ctrl.info.specialcolorfunction = 0;
  ctrl.info.enable = 0;
  ctrl.info.startLine = 0;
  ctrl.info.endLine = (yabsys.VBlankLineCount < 270)?yabsys.VBlankLineCount:270;

  for (int i=0; i<yabsys.VBlankLineCount; i++) {
    ctrl.info.display[i] = isEnabled(NBG3, &Vdp2Lines[i]);
    ctrl.info.enable |= ctrl.info.display[i];
    ctrl.info.alpha_per_line[i] = (~Vdp2Lines[i].CCRNB & 0x1F00) >> 5;
  }
  if (!ctrl.info.enable) {
    return;
  }
  ctrl.info.transparencyenable = !(ctrl.regs->BGON & 0x800);
  ctrl.info.specialprimode = (ctrl.regs->SFPRMD >> 6) & 0x3;

  ctrl.info.colornumber = (ctrl.regs->CHCTLB & 0x20) >> 5;

  ctrl.info.mapwh = 2;

  ReadPlaneSize(&ctrl.info, ctrl.regs->PLSZ >> 6);
  ctrl.info.x = -((ctrl.regs->SCXN3 & 0x7FF) % (512 * ctrl.info.planew));
  ctrl.info.y = -((ctrl.regs->SCYN3 & 0x7FF) % (512 * ctrl.info.planeh));
  ReadPatternData(&ctrl.info, ctrl.regs->PNCN3, ctrl.regs->CHCTLB & 0x10);

  ReadMosaicData(&ctrl.info, 0x8, ctrl.regs);

  ctrl.info.specialcolormode = (ctrl.regs->SFCCMD >> 6) & 0x03;
  if (ctrl.regs->SFSEL & 0x8)
    ctrl.info.specialcode = ctrl.regs->SFCODE >> 8;
  else
    ctrl.info.specialcode = ctrl.regs->SFCODE & 0xFF;


  ctrl.info.coloroffset = (ctrl.regs->CRAOFA & 0x7000) >> 4;

  ctrl.info.linecheck_mask = 0x08;
  ctrl.info.coordincx = ctrl.info.coordincy = 1;

  ctrl.info.priority = (ctrl.regs->PRINB >> 8) & 0x7;
  ctrl.info.PlaneAddr = (void FASTCALL(*)(void *, int, Vdp2*))&Vdp2NBG3PlaneAddr;

  if ((ctrl.info.priority == 0) ||
    (ctrl.regs->BGON & 0x1 && (ctrl.regs->CHCTLA & 0x70) >> 4 == 4) || // If NBG0 16M mode is enabled, don't draw
    (ctrl.regs->BGON & 0x2 && (ctrl.regs->CHCTLA & 0x3000) >> 12 >= 2)) // If NBG1 2048/32786 is enabled, don't draw
    {
      return;
    }

  ctrl.info.islinescroll = 0;
  ctrl.info.linescrolltbl = 0;
  ctrl.info.lineinc = 0;
  ctrl.info.isverticalscroll = 0;


  int delayed = 0;
{
  int char_access = 0;
  int ptn_access = 0;
  for (int i = 0; i < 4; i++) {
    ctrl.info.char_bank[i] = 0;
    ctrl.info.pname_bank[i] = 0;
    for (int j = 0; j < 8; j++) {
      if (Vdp2External.AC_VRAM[i][j] == 0x07) {
        ctrl.info.char_bank[i] = 1;
        char_access |= (1 << j);
      }
      if (Vdp2External.AC_VRAM[i][j] == 0x03) {
        ctrl.info.pname_bank[i] = 1;
        ptn_access |= (1 << j);
      }
    }
  }
  if (char_access == 0) {
    return;
  }
  if (ptn_access == 0) {
    return;
  }
  // Setting miss of cycle patten need to plus 8 dot vertical
  if (Vdp2CheckCharAccessPenalty(char_access, ptn_access) != 0) {
    delayed = 1;
  }
}

  ctrl.info.x = ctrl.regs->SCXN3 & 0x7FF;
  ctrl.info.y = ctrl.regs->SCYN3 & 0x7FF;
  Vdp2DrawMapTest(&ctrl, delayed);
#ifdef CELL_ASYNC
    YabThreadYield();
#endif
}

//////////////////////////////////////////////////////////////////////////////

static void Vdp2DrawRBG0_part( RBGDrawInfo *rbg)
{
  vdp2draw_struct* info = &rbg->ctrl.info;

  info->dst = 0;
  info->idScreen = RBG0;
  info->cor = 0;
  info->cog = 0;
  info->cob = 0;
  info->specialcolorfunction = 0;
  info->enable = 0;
  info->RotWin = NULL;
  info->RotWinMode = 0;

  info->enable = ((rbg->ctrl.regs->BGON & 0x10)!=0);
  if (!info->enable) {
    pushRBG(rbg);
    return;
  }
  // //If no VRAM access is granted to RBG0, just abort.
  if ((rbg->ctrl.regs->RAMCTL & 0xFF) == 0) {
    LOG("No RAMCTL for RBG0\n");
    pushRBG(rbg);
    return;
  }

  for (int i=info->startLine; i<info->endLine; i++) {
    info->display[i] = info->enable;
    // Color calculation ratio
    rbg->alpha[i] = (~(Vdp2Lines[i].CCRR & 0x1F)) << 3;
    info->alpha_per_line[i] = rbg->alpha[i];
  }

  info->priority = rbg->ctrl.regs->PRIR & 0x7;

  LOG_AREA("RGB0 prio = %d\n", info->priority);

  if (info->priority == 0) {
    pushRBG(rbg);
    return;
  }

  info->transparencyenable = !(rbg->ctrl.regs->BGON & 0x1000);

  info->specialprimode = (rbg->ctrl.regs->SFPRMD >> 8) & 0x3;

  info->colornumber = (rbg->ctrl.regs->CHCTLB & 0x7000) >> 12;

  LOG_AREA("RGB0 colornumber = %d\n", info->colornumber);

  info->islinescroll = 0;
  info->linescrolltbl = 0;
  info->lineinc = 0;

  Vdp2ReadRotationTable(0, &rbg->paraA, rbg->ctrl.regs, Vdp2Ram);
  Vdp2ReadRotationTable(1, &rbg->paraB, rbg->ctrl.regs, Vdp2Ram);

  //rbg->paraA.PlaneAddr = (void FASTCALL(*)(void *, int, Vdp2*))&Vdp2ParameterAPlaneAddr;
  //rbg->paraB.PlaneAddr = (void FASTCALL(*)(void *, int, Vdp2*))&Vdp2ParameterBPlaneAddr;
  rbg->paraA.charaddr = (rbg->ctrl.regs->MPOFR & 0x7) * 0x20000;
  rbg->paraB.charaddr = (rbg->ctrl.regs->MPOFR & 0x70) * 0x2000;
  ReadPlaneSizeR(&rbg->paraA, rbg->ctrl.regs->PLSZ >> 8);
  ReadPlaneSizeR(&rbg->paraB, rbg->ctrl.regs->PLSZ >> 12);

if (rbg->ctrl.regs->RPMD == 0x03)
  {
    //printf("RPMD 0x3\n");
    // Enable Window0(RPW0E)?
    if (((rbg->ctrl.regs->WCTLD >> 1) & 0x01) == 0x01)
    {
      info->RotWin = _Ygl->win[0];
      // RPW0A( inside = 0, outside = 1 )
      info->RotWinMode = (rbg->ctrl.regs->WCTLD & 0x01);
      // Enable Window1(RPW1E)?
    }
    else if (((rbg->ctrl.regs->WCTLD >> 3) & 0x01) == 0x01)
    {
      info->RotWin = _Ygl->win[1];
      // RPW1A( inside = 0, outside = 1 )
      info->RotWinMode = ((rbg->ctrl.regs->WCTLD >> 2) & 0x01);
      // Bad Setting Both Window is disabled
    }
  }

  rbg->paraA.screenover = (rbg->ctrl.regs->PLSZ >> 10) & 0x03;
  rbg->paraB.screenover = (rbg->ctrl.regs->PLSZ >> 14) & 0x03;

  // Figure out which Rotation Parameter we're uqrt
  switch (rbg->ctrl.regs->RPMD & 0x3)
  {
  case 0:
    // Parameter A
    info->rotatenum = 0;
    info->PlaneAddr = (void FASTCALL(*)(void *, int, Vdp2*))&Vdp2ParameterAPlaneAddr;
    break;
  case 1:
    // Parameter B
    info->rotatenum = 1;
    info->PlaneAddr = (void FASTCALL(*)(void *, int, Vdp2*))&Vdp2ParameterBPlaneAddr;
    break;
  case 2:
    // Parameter A+B switched via coefficients
    // FIX ME(need to figure out which Parameter is being used)
  case 3:
  default:
    // Parameter A+B switched via rotation parameter window
    // FIX ME(need to figure out which Parameter is being used)
    info->rotatenum = 0;
    info->PlaneAddr = (void FASTCALL(*)(void *, int, Vdp2*))&Vdp2ParameterAPlaneAddr;
    break;
  }

  info->isbitmap = ((rbg->ctrl.regs->CHCTLB & 0x200) != 0);

  if (info->isbitmap)
  {

    // Bitmap Mode
    ReadBitmapSize(info, rbg->ctrl.regs->CHCTLB >> 10, 0x1);
    if (info->rotatenum == 0)
      // Parameter A
      info->charaddr = (rbg->ctrl.regs->MPOFR & 0x7) * 0x20000;
    else
      // Parameter B
      info->charaddr = (rbg->ctrl.regs->MPOFR & 0x70) * 0x2000;

    //If no VRAM access is granted to RBG0 character pattern table , just abort.
      int charAddrBk = (((info->charaddr >> 16)& 0xF) >> ((rbg->ctrl.regs->VRSIZE >> 15)&0x1)) >> 1;
      if ((((rbg->ctrl.regs->RAMCTL>>(charAddrBk<<1))&0x3) != 0x3) && ((rbg->ctrl.regs->RPMD & 0x3) < 0x2)) {
        pushRBG(rbg);
        return;
      }

    info->paladdr = (rbg->ctrl.regs->BMPNB & 0x7) << 4;
    info->flipfunction = 0;
    info->specialfunction = 0;
  }
  else
  {
    int i;
    // Tile Mode
    info->mapwh = 4;

    if (info->rotatenum == 0)
      // Parameter A
      ReadPlaneSize(info, rbg->ctrl.regs->PLSZ >> 8);
    else
      // Parameter B
      ReadPlaneSize(info, rbg->ctrl.regs->PLSZ >> 12);

    ReadPatternData(info, rbg->ctrl.regs->PNCR, rbg->ctrl.regs->CHCTLB & 0x100);

    rbg->paraA.ShiftPaneX = 8 + rbg->paraA.planew;
    rbg->paraA.ShiftPaneY = 8 + rbg->paraA.planeh;
    rbg->paraB.ShiftPaneX = 8 + rbg->paraB.planew;
    rbg->paraB.ShiftPaneY = 8 + rbg->paraB.planeh;

    rbg->paraA.MskH = (8 * 64 * rbg->paraA.planew) - 1;
    rbg->paraA.MskV = (8 * 64 * rbg->paraA.planeh) - 1;
    rbg->paraB.MskH = (8 * 64 * rbg->paraB.planew) - 1;
    rbg->paraB.MskV = (8 * 64 * rbg->paraB.planeh) - 1;

    rbg->paraA.MaxH = 8 * 64 * rbg->paraA.planew * 4;
    rbg->paraA.MaxV = 8 * 64 * rbg->paraA.planeh * 4;
    rbg->paraB.MaxH = 8 * 64 * rbg->paraB.planew * 4;
    rbg->paraB.MaxV = 8 * 64 * rbg->paraB.planeh * 4;

    if (rbg->paraA.screenover == OVERMODE_512)
    {
      rbg->paraA.MaxH = 512;
      rbg->paraA.MaxV = 512;
    }

    if (rbg->paraB.screenover == OVERMODE_512)
    {
      rbg->paraB.MaxH = 512;
      rbg->paraB.MaxV = 512;
    }

    for (i = 0; i < 16; i++)
    {
	  Vdp2ParameterAPlaneAddr(info, i, rbg->ctrl.regs);
      rbg->paraA.PlaneAddrv[i] = info->addr;
	  Vdp2ParameterBPlaneAddr(info, i, rbg->ctrl.regs);
      rbg->paraB.PlaneAddrv[i] = info->addr;
    }
  }

  ReadMosaicData(info, 0x10, rbg->ctrl.regs);

  info->specialcolormode = (rbg->ctrl.regs->SFCCMD >> 8) & 0x03;
  if (rbg->ctrl.regs->SFSEL & 0x10)
    info->specialcode = rbg->ctrl.regs->SFCODE >> 8;
  else
    info->specialcode = rbg->ctrl.regs->SFCODE & 0xFF;

  info->coloroffset = (rbg->ctrl.regs->CRAOFB & 0x7) << 8;

  info->linecheck_mask = 0x10;
  info->coordincx = info->coordincy = 1;

  Vdp2DrawRotation(rbg);
}


static void Vdp2DrawRBG0()
{
  int nbZone = 1;
  int lastLine = 0;
  int line;
  int max = (yabsys.VBlankLineCount >= 270)?270:yabsys.VBlankLineCount;
  RBGDrawInfo* rbg = NULL;
  for (line = 2; line<max; line++) {
    if (!sameVDP2Reg(RBG0, &Vdp2Lines[line-1], &Vdp2Lines[line])) {
      rbg = popRBG();
      rbg->rbg_type = 0x0;
      rbg->ctrl.info.startLine = lastLine;
      rbg->ctrl.info.endLine = line;
      rbg->ctrl.regs = &Vdp2Lines[rbg->ctrl.info.startLine];
      lastLine = line;
      LOG_AREA("RBG0 Draw from %d to %d %x\n", rbg->ctrl.info.startLine, rbg->ctrl.info.endLine, rbg->ctrl.regs->BGON);
      Vdp2DrawRBG0_part(rbg);
    }
  }
  rbg = popRBG();
  rbg->rbg_type = 0x0;
  rbg->ctrl.info.startLine = lastLine;
  rbg->ctrl.info.endLine = line;
  rbg->ctrl.regs = &Vdp2Lines[rbg->ctrl.info.startLine];
  LOG_AREA("RBG0 Draw from %d to %d %x\n", rbg->ctrl.info.startLine, rbg->ctrl.info.endLine, rbg->ctrl.regs->BGON);
  Vdp2DrawRBG0_part(rbg);

}

#define ceilf(a) ((a)+0.99999f)


//////////////////////////////////////////////////////////////////////////////

static int _VIDCSIsFullscreen;

void VIDCSResize(int originx, int originy, unsigned int w, unsigned int h, int on)
{
  if ((originx == _Ygl->originx)&&
     (originy == _Ygl->originy)&&
     (w == GlWidth)&&
     (h == GlHeight)&&
     (_VIDCSIsFullscreen == on)) return;

  _VIDCSIsFullscreen = on;

  GlWidth = w;
  GlHeight = h;

  YglChangeResolution(_Ygl->rwidth, _Ygl->rheight);

  _Ygl->originx = originx;
  _Ygl->originy = originy;

  glViewport(originx, originy, GlWidth, GlHeight);

}

void VIDCSGetScale(float *xRatio, float *yRatio, int *xUp, int *yUp) {
  double w = 0;
  double h = 0;
  int x,y = 0;
  double dar = (double)GlWidth/(double)GlHeight;
  double par = 4.0/3.0;

  int width = (_Ygl->interlace == SINGLE_INTERLACE)?_Ygl->width*2:_Ygl->width;
  int Intw = (int)(floor((float)GlWidth/(float)width));
  int Inth = (int)(floor((float)GlHeight/(256.0 * _Ygl->vdp1ratio)));
  if (_Ygl->interlace != NORMAL_INTERLACE) Inth >>= 1;
  int Int  = 1<<(_Ygl->interlace == NORMAL_INTERLACE);
  RATIOMODE modeScreen = _Ygl->stretch;
  #ifndef __LIBRETRO__
  if (yabsys.isRotated) par = 1.0/par;
  #endif
  if (Intw == 0) {
    modeScreen = 0;
    Intw = 1;
  }
  if (Inth == 0) {
    modeScreen = 0;
    Inth = 1;
  }

  switch(modeScreen) {
    case ORIGINAL_RATIO:
      w = (dar>par)?(double)GlHeight*par:GlWidth;
      h = (dar>par)?(double)GlHeight:(double)GlWidth/par;
      x = (GlWidth-w)/2;
      y = (GlHeight-h)/2;
      break;
    case STRETCH_RATIO:
      w = GlWidth;
      h = GlHeight;
      x = 0;
      y = 0;
      break;
    case INTEGER_RATIO:
    case INTEGER_RATIO_FULL:
      w = Int * _Ygl->width;
      h = Int * _Ygl->height;
      x = (GlWidth-w)/2;
      y = (GlHeight-h)/2;
      break;
    default:
       break;
   }
   if (modeScreen == INTEGER_RATIO_FULL)  Int = (Inth<Intw)?Inth:Intw;

  *xRatio = w / _Ygl->rwidth;
  *yRatio = h / _Ygl->rheight;
  *xUp = x;
  *yUp = y;
}
//////////////////////////////////////////////////////////////////////////////

int VIDCSIsFullscreen(void) {
  return _VIDCSIsFullscreen;
}

//////////////////////////////////////////////////////////////////////////////

int VIDCSVdp1Reset(void)
{
  return 0;
}

void VIDCSReadColorOffset(void) {
  u8 offset[enBGMAX+1] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x1, 0x40, 0x20};
  int line_shift = 0;
  if (_Ygl->rheight > 256) {
    line_shift = 1;
  }
  else {
    line_shift = 0;
  }

  u32 * linebuf = YglGetPerlineBuf();
  for (int line = 0; line < _Ygl->rheight; line++) {
    Vdp2 * lVdp2Regs = &Vdp2Lines[line >> line_shift];
    int b_cor = lVdp2Regs->COBR & 0xFF;
    int b_cog = lVdp2Regs->COBG & 0xFF;
    int b_cob = lVdp2Regs->COBB & 0xFF;
    int a_cor = lVdp2Regs->COAR & 0xFF;
    int a_cog = lVdp2Regs->COAG & 0xFF;
    int a_cob = lVdp2Regs->COAB & 0xFF;
    if (lVdp2Regs->COBR & 0x100)
      b_cor |= 0xFFFFFF00;
    if (lVdp2Regs->COBG & 0x100)
      b_cog |= 0xFFFFFF00;
    if (lVdp2Regs->COBB & 0x100)
      b_cob |= 0xFFFFFF00;
    if (lVdp2Regs->COAR & 0x100)
      a_cor |= 0xFFFFFF00;
    if (lVdp2Regs->COAG & 0x100)
      a_cog |= 0xFFFFFF00;
    if (lVdp2Regs->COAB & 0x100)
      a_cob |= 0xFFFFFF00;
    int colOffB =
       (((int)(128.0f + (b_cob / 2.0)) & 0xFF) << 16)
    | (((int)(128.0f + (b_cog / 2.0)) & 0xFF) << 8)
    | (((int)(128.0f + (b_cor / 2.0)) & 0xFF) << 0);
    int colOffA =
      (((int)(128.0f + (a_cob / 2.0)) & 0xFF) << 16)
    | (((int)(128.0f + (a_cog / 2.0)) & 0xFF) << 8)
    | (((int)(128.0f + (a_cor / 2.0)) & 0xFF) << 0);
    for(int id = 0; id<enBGMAX+1; id++){
      if (isEnabled(id,lVdp2Regs) == 0) {
        linebuf[line+512*id] = 0x0;
      } else {
        if (lVdp2Regs->CLOFEN & offset[id]) {
          // color offset enable
          if (lVdp2Regs->CLOFSL & offset[id])
          {
            // color offset B
            linebuf[line+512*id] = colOffB;
          }
          else
          {
            // color offset A
            linebuf[line+512*id] = colOffA;
          }
        }
        else {
          linebuf[line+512*id] = 0x00808080;
        }
      }
    }
  }
  YglSetPerlineBuf(linebuf);

}

//////////////////////////////////////////////////////////////////////////////

void VIDCSVdp1LocalCoordinate(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs)
{
  regs->localX = cmd->CMDXA;
  regs->localY = cmd->CMDYA;
}

//////////////////////////////////////////////////////////////////////////////

int VIDCSVdp2Reset(void)
{
  return 0;
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSVdp2Draw(void)
{
  YglCheckFBSwitch(1);
  //varVdp2Regs = Vdp2RestoreRegs(0, Vdp2Lines);
  //if (varVdp2Regs == NULL) varVdp2Regs = Vdp2Regs;
  Vdp2SetResolution(Vdp2Lines[0].TVMD);
  if (_Ygl->rwidth > YglTM_vdp2->width) {
    int new_width = _Ygl->rwidth;
    int new_height = YglTM_vdp2->height;
    YglTMDeInit(&YglTM_vdp2);
    YglTM_vdp2 = YglTMInit(new_width, new_height);
  }
  YglTmPull(YglTM_vdp2, 0);

  if (Vdp2Regs->TVMD & 0x8000) {
    VIDCSVdp2DrawScreens();
    screenDirty = 1;
    vdp2busy = 1;
  } else {
    if (screenDirty != 0)
      vdp2busy = 1;
    screenDirty = 0;
  }

  /* It would be better to reset manualchange in a Vdp1SwapFrameBuffer
  function that would be called here and during a manual change */
  //Vdp1External.manualchange = 0;
}

//////////////////////////////////////////////////////////////////////////////

#define VDP2_DRAW_LINE 0
extern u8 Vdp2Ram_Updated;
static void VIDCSVdp2DrawScreens(void)
{
  u64 before;
  u64 now;
  u32 difftime;
  char str[64];
LOG_ASYN("===================================\n");

  _Ygl->useLineColorOffset[0] = 0;
  _Ygl->useLineColorOffset[1] = 0;

  Vdp2GenerateWindowInfo(&Vdp2Lines[VDP2_DRAW_LINE]);

  if (Vdp1Regs->TVMR & 0x02) {
    Vdp2ReadRotationTable(0, &Vdp1ParaA, &Vdp2Lines[VDP2_DRAW_LINE], Vdp2Ram);
  }
  Vdp2DrawBackScreen(&Vdp2Lines[VDP2_DRAW_LINE]);
  Vdp2DrawLineColorScreen(&Vdp2Lines[VDP2_DRAW_LINE]);

  Vdp2DrawRBG0();
  Vdp2DrawNBG3(&Vdp2Lines[VDP2_DRAW_LINE]);
  Vdp2DrawNBG2(&Vdp2Lines[VDP2_DRAW_LINE]);
  Vdp2DrawNBG1(&Vdp2Lines[VDP2_DRAW_LINE]);
  Vdp2DrawNBG0(&Vdp2Lines[VDP2_DRAW_LINE]);
  Vdp2DrawRBG1();

LOG_ASYN("*********************************\n");

  Vdp2Ram_Updated = 0;
}

//////////////////////////////////////////////////////////////////////////////

static void Vdp2SetResolution(u16 TVMD)
{
  int width = 1, height = 1;
  int wratio = 1, hratio = 1;
  InterlaceMode old_simple_interlace = _Ygl->interlace;

  // Horizontal Resolution
  switch (TVMD & 0x7)
  {
  case 0:
    width = 320;
    wratio = 1;
    break;
  case 1:
    width = 352;
    wratio = 1;
    break;
  case 2:
    width = 640;
    wratio = 2;
    break;
  case 3:
    width = 704;
      wratio = 2;
    break;
  case 4:
    width = 320;
    wratio = 1;
    break;
  case 5:
    width = 352;
    wratio = 1;
    break;
  case 6:
    width = 640;
    wratio = 2;
    break;
  case 7:
    width = 704;
    wratio = 2;
    break;
  }

  // Vertical Resolution
  switch ((TVMD >> 4) & 0x3)
  {
  case 0:
    height = 224;
    break;
  case 1: height = 240;
    break;
  case 2:
    if (yabsys.IsPal) height = 256;
    else height = 224;
    break;
  default:
    if (yabsys.IsPal) height = 256;
    else height = 224;
    break;
  }

  hratio = 1;

  _Ygl->interlace = NORMAL_INTERLACE;
  // Check for interlace
  switch ((TVMD >> 6) & 0x3)
  {
  case 3: // Double-density Interlace
    hratio = 2;
    _Ygl->interlace = DOUBLE_INTERLACE;
    break;
  case 2: // Single-density Interlace
    _Ygl->interlace = SINGLE_INTERLACE;
    break;
  case 0: // Non-interlace
  default:
    break;
  }

  float oldvdp1wd = _Ygl->vdp1wdensity;
  float oldvdp1hd = _Ygl->vdp1hdensity;

  float oldvdp2wd = _Ygl->vdp2wdensity;
  float oldvdp2hd = _Ygl->vdp2hdensity;

  Vdp1SetTextureRatio(wratio, hratio);

  int change = 0;
  change |= (old_simple_interlace != _Ygl->interlace);
  change |= (oldvdp1wd != _Ygl->vdp1wdensity);
  change |= (oldvdp1hd != _Ygl->vdp1hdensity);
  change |= (oldvdp2wd != _Ygl->vdp2wdensity);
  change |= (oldvdp2hd != _Ygl->vdp2hdensity);

  change |= (width != _Ygl->rwidth);
  if (_Ygl->interlace == DOUBLE_INTERLACE) {
    change |= ((height*2) != _Ygl->rheight);
  } else {
    change |= (height != _Ygl->rheight);
  }
  if (change != 0)YglChangeResolution(width, height);
}

//////////////////////////////////////////////////////////////////////////////

void VIDCSGetGlSize(int *width, int *height)
{
  *width = GlWidth;
  *height = GlHeight;
}

void VIDCSVdp2DispOff()
{
}

void updateMeshMode(MESHMODE value) {
  if (_Ygl->meshmode != value){
    _Ygl->meshmode = value;
    vdp1_update_mesh();
  }
}
void updateBandingMode(BANDINGMODE value) {
  if (_Ygl->bandingmode != value){
    _Ygl->bandingmode = value;
    vdp1_update_banding();
  }
}
void VIDCSSetSettingValueMode(int type, int value) {

  switch (type) {
  case VDP_SETTING_FILTERMODE:
    _Ygl->aamode = (AAMODE)value;
    break;
  case VDP_SETTING_UPSCALMODE:
    _Ygl->upmode = (UPMODE)value;
    break;
  case VDP_SETTING_RESOLUTION_MODE:
    if (_Ygl->resolution_mode != (RESOLUTION_MODE)value) {
       _Ygl->resolution_mode = (RESOLUTION_MODE)value;
       YglChangeResolution(_Ygl->rwidth, _Ygl->rheight);
    }
    break;
  case VDP_SETTING_ASPECT_RATIO:
    _Ygl->stretch = (RATIOMODE)value;
  break;
  case VDP_SETTING_WIREFRAME:
    _Ygl->wireframe_mode = value;
  break;
  case VDP_SETTING_MESH_MODE:
    updateMeshMode((MESHMODE)value);
  break;
  case VDP_SETTING_BANDING_MODE:
    updateBandingMode((BANDINGMODE)value);
  break;
  default:
  return;
  }
}

//////////////////////////////////////////////////////////////////////////////
static void Vdp1SetTextureRatio(int vdp2widthratio, int vdp2heightratio)
{
  int vdp1w = 1;
  int vdp1h = 1;

  // may need some tweaking
  if (Vdp1Regs->TVMR & 0x1) VDP1_MASK = 0xFF;
  else VDP1_MASK = 0xFFFF;

  // Figure out which vdp1 screen mode to use
  switch (Vdp1Regs->TVMR & 7)
  {
  case 0:
  case 2:
  case 3:
    vdp1w = 1;
    break;
  case 1:
    vdp1w = 2;
    break;
  default:
    vdp1w = 1;
    vdp1h = 1;
    break;
  }

  // Is double-interlace enabled?
  if (Vdp1Regs->FBCR & 0x8) {
    vdp1h = 2;
    vdp1_interlace = (Vdp1Regs->FBCR & 0x4) ? 2 : 1;
  }
  else {
    vdp1_interlace = 0;
  }
  _Ygl->vdp1wdensity = vdp1w;
  _Ygl->vdp1hdensity = vdp1h;

  _Ygl->vdp2wdensity = vdp2widthratio;
  _Ygl->vdp2hdensity = vdp2heightratio;
}

//////////////////////////////////////////////////////////////////////////////
static u16 Vdp2ColorRamGetColorRaw(u32 colorindex) {
  switch (Vdp2Internal.ColorMode)
  {
  case 0:
  case 1:
  {
    colorindex <<= 1;
    return T2ReadWord(Vdp2ColorRam, colorindex & 0xFFF);
  }
  case 2:
  {
    colorindex <<= 2;
    colorindex &= 0xFFF;
    return T2ReadWord(Vdp2ColorRam, colorindex);
  }
  default: break;
  }
  return 0;
}

static u32 Vdp2ColorRamGetLineColorOffset(u32 colorindex, int alpha, int offset)
{
  int flag = 0xFFF;
  switch (Vdp2Internal.ColorMode)
  {
  case 0:
    flag &= 0x380;
  case 1:
  {
    u32 tmp;
    flag &= 0x780;
    //Line color offset from rotation table might be applicable here
    if (offset != 0) colorindex = (colorindex&flag) | (offset&0x7F);
    colorindex <<= 1;
    tmp = T2ReadWord(Vdp2ColorRam, colorindex & 0xFFF);
    return SAT2YAB1(alpha, tmp);
  }
  case 2:
  {
    u32 tmp1, tmp2;
    colorindex <<= 2;
    colorindex &= 0xFFF;
    tmp1 = T2ReadWord(Vdp2ColorRam, colorindex & 0xFFF);
    tmp2 = T2ReadWord(Vdp2ColorRam, (colorindex + 2) & 0xFFF);
    //Line color offset from rotation table are not applicable here
    return SAT2YAB2(alpha, tmp1, tmp2);
  }
  default: break;
  }
  return 0;
}

static u32 Vdp2ColorRamGetLineColor(u32 colorindex, int alpha) {
  return Vdp2ColorRamGetLineColorOffset(colorindex, alpha,0);
}
//////////////////////////////////////////////////////////////////////////////
// Window
static int useRotWin = 0;
int WinS[enBGMAX+1];
int WinS_mode[enBGMAX+1];

void Vdp2GenerateWindowInfo(Vdp2 *varVdp2Regs)
{
  int HShift;
  int v = 0;
  int p = 0;
  u32 LineWinAddr;
  int upWindow = 0;
  u32 val = 0;

  int Win0[enBGMAX+1];
  int Win0_mode[enBGMAX+1];
  int Win1[enBGMAX+1];
  int Win1_mode[enBGMAX+1];
  int Win_op[enBGMAX+1];

  if (((varVdp2Regs->WCTLD & 0xA)!=0x0) != useRotWin) {
    useRotWin = ((varVdp2Regs->WCTLD & 0xA)!=0x0);
    _Ygl->needWinUpdate |= 1;
  }

  Win0[NBG0] = (varVdp2Regs->WCTLA >> 1) & 0x01;
  Win1[NBG0] = (varVdp2Regs->WCTLA >> 3) & 0x01;
  WinS[NBG0] = (varVdp2Regs->WCTLA >> 5) & 0x01;
  Win0[NBG1] = (varVdp2Regs->WCTLA >> 9) & 0x01;
  Win1[NBG1] = (varVdp2Regs->WCTLA >> 11) & 0x01;
  WinS[NBG1] = (varVdp2Regs->WCTLA >> 13) & 0x01;

  Win0[NBG2] = (varVdp2Regs->WCTLB >> 1) & 0x01;
  Win1[NBG2] = (varVdp2Regs->WCTLB >> 3) & 0x01;
  WinS[NBG2] = (varVdp2Regs->WCTLB >> 5) & 0x01;
  Win0[NBG3] = (varVdp2Regs->WCTLB >> 9) & 0x01;
  Win1[NBG3] = (varVdp2Regs->WCTLB >> 11) & 0x01;
  WinS[NBG3] = (varVdp2Regs->WCTLB >> 13) & 0x01;

  Win0[RBG0] = (varVdp2Regs->WCTLC >> 1) & 0x01;
  Win1[RBG0] = (varVdp2Regs->WCTLC >> 3) & 0x01;
  WinS[RBG0] = (varVdp2Regs->WCTLC >> 5) & 0x01;
  Win0[SPRITE] = (varVdp2Regs->WCTLC >> 9) & 0x01;
  Win1[SPRITE] = (varVdp2Regs->WCTLC >> 11) & 0x01;
  WinS[SPRITE] = (varVdp2Regs->WCTLC >> 13) & 0x01;

  Win0_mode[NBG0] = (varVdp2Regs->WCTLA) & 0x01;
  Win1_mode[NBG0] = (varVdp2Regs->WCTLA >> 2) & 0x01;
  WinS_mode[NBG0] = (varVdp2Regs->WCTLA >> 4) & 0x01;
  Win0_mode[NBG1] = (varVdp2Regs->WCTLA >> 8) & 0x01;
  Win1_mode[NBG1] = (varVdp2Regs->WCTLA >> 10) & 0x01;
  WinS_mode[NBG1] = (varVdp2Regs->WCTLA >> 12) & 0x01;

  Win0_mode[NBG2] = (varVdp2Regs->WCTLB) & 0x01;
  Win1_mode[NBG2] = (varVdp2Regs->WCTLB >> 2) & 0x01;
  WinS_mode[NBG2] = (varVdp2Regs->WCTLB >> 4) & 0x01;
  Win0_mode[NBG3] = (varVdp2Regs->WCTLB >> 8) & 0x01;
  Win1_mode[NBG3] = (varVdp2Regs->WCTLB >> 10) & 0x01;
  WinS_mode[NBG3] = (varVdp2Regs->WCTLB >> 12) & 0x01;

  Win0_mode[RBG0] = (varVdp2Regs->WCTLC) & 0x01;
  Win1_mode[RBG0] = (varVdp2Regs->WCTLC >> 2) & 0x01;
  WinS_mode[RBG0] = (varVdp2Regs->WCTLC >> 4) & 0x01;
  Win0_mode[SPRITE] = (varVdp2Regs->WCTLC >> 8) & 0x01;
  Win1_mode[SPRITE] = (varVdp2Regs->WCTLC >> 10) & 0x01;
  WinS_mode[SPRITE] = (varVdp2Regs->WCTLC >> 12) & 0x01;

  Win_op[NBG0] = (varVdp2Regs->WCTLA >> 7) & 0x01;
  Win_op[NBG1] = (varVdp2Regs->WCTLA >> 15) & 0x01;
  Win_op[NBG2] = (varVdp2Regs->WCTLB >> 7) & 0x01;
  Win_op[NBG3] = (varVdp2Regs->WCTLB >> 15) & 0x01;
  Win_op[RBG0] = (varVdp2Regs->WCTLC >> 7) & 0x01;
  Win_op[SPRITE] = (varVdp2Regs->WCTLC >> 15) & 0x01;

  Win0_mode[SPRITE+1] = (varVdp2Regs->WCTLD >> 8) & 0x01;
  Win0[SPRITE+1] = (varVdp2Regs->WCTLD >> 9) & 0x01;
  Win1_mode[SPRITE+1] = (varVdp2Regs->WCTLD >> 10) & 0x01;
  Win1[SPRITE+1] = (varVdp2Regs->WCTLD >> 11) & 0x01;
  WinS_mode[SPRITE+1] = (varVdp2Regs->WCTLD >> 12) & 0x01;
  WinS[SPRITE+1] = (varVdp2Regs->WCTLD >> 13) & 0x01;
  Win_op[SPRITE+1] = (varVdp2Regs->WCTLD >> 15) & 0x01;

  Win0[RBG1] = Win0[NBG0];
  Win0_mode[RBG1] = Win0_mode[NBG0];
  Win1[RBG1] = Win1[NBG0];
  Win1_mode[RBG1] = Win1_mode[NBG0];
  WinS[RBG1] = WinS[NBG0];
  WinS_mode[RBG1] = WinS_mode[NBG0];
  Win_op[RBG1] = Win_op[NBG0];


  for (int i=0; i<enBGMAX+1; i++) {
    if (Win0[i] != _Ygl->Win0[i]) _Ygl->needWinUpdate |= 1;
    if (Win1[i] != _Ygl->Win1[i]) _Ygl->needWinUpdate |= 1;
    if (WinS[i] != _Ygl->WinS[i]) _Ygl->needWinUpdate |= 1;
    if (Win0_mode[i] != _Ygl->Win0_mode[i]) _Ygl->needWinUpdate |= 1;
    if (Win1_mode[i] != _Ygl->Win1_mode[i]) _Ygl->needWinUpdate |= 1;
    if (WinS_mode[i] != _Ygl->WinS_mode[i]) _Ygl->needWinUpdate |= 1;
    if (Win_op[i] != _Ygl->Win_op[i]) _Ygl->needWinUpdate |= 1;
  #ifdef WINDOW_DEBUG
    //DEBUG
    if ((Win0[i] == 1) || (Win1[i] == 1) || (WinS[i] == 1))
      YuiMsg("Windows are used on layer %d (WO:%d, W1:%d, WS:%d, WS mode %s, WS op %s)\n", i, Win0[i], Win1[i], WinS[i], (WinS_mode[i]==0)?"INSIDE":"OUTSIDE", (Win_op[i]==0)?"OR":"AND");
    else
      YuiMsg("Windows are not used on layer %d\n", i);
  #endif
  }
  memcpy(&_Ygl->Win0[0], &Win0[0], (enBGMAX+1)*sizeof(int));
  memcpy(&_Ygl->Win1[0], &Win1[0], (enBGMAX+1)*sizeof(int));
  memcpy(&_Ygl->WinS[0], &WinS[0], (enBGMAX+1)*sizeof(int));
  memcpy(&_Ygl->Win0_mode[0], &Win0_mode[0], (enBGMAX+1)*sizeof(int));
  memcpy(&_Ygl->Win1_mode[0], &Win1_mode[0], (enBGMAX+1)*sizeof(int));
  memcpy(&_Ygl->WinS_mode[0], &WinS_mode[0], (enBGMAX+1)*sizeof(int));
  memcpy(&_Ygl->Win_op[0], &Win_op[0], (enBGMAX+1)*sizeof(int));

  if( _Ygl->win[0] == NULL ){
    _Ygl->win[0] = (u32*)malloc(512 * 4);
  }
  if( _Ygl->win[1] == NULL ){
    _Ygl->win[1] = (u32*)malloc(512 * 4);
  }

  HShift = 0;
  if (_Ygl->rwidth >= 640) HShift = 0; else HShift = 1;

  int step = 1;
  // Line Table mode
  if ((varVdp2Regs->LWTA0.part.U & 0x8000))
  {
    // start address
    LineWinAddr = (u32)((((varVdp2Regs->LWTA0.part.U & 0x07) << 15) | (varVdp2Regs->LWTA0.part.L >> 1)) << 2);
    for (v = 0,p=0; p < _Ygl->rheight; v+=step, p++) {
      if (v >= varVdp2Regs->WPSY0 && v <= varVdp2Regs->WPEY0) {
        short HStart = Vdp2RamReadWord(NULL, Vdp2Ram, LineWinAddr + (v << 2)) & 0xFFFF;
        short HEnd = Vdp2RamReadWord(NULL, Vdp2Ram, LineWinAddr + (v << 2) + 2) & 0xFFFF;
        if ((HEnd < HStart) || (HEnd < 0)) val = 0x000000FF; //END < START
        else {
          val = (HStart>>HShift) | ((HEnd>>HShift) << 16);
        }
      } else {
        val = 0x000000FF; //END < START
      }
      if (val != _Ygl->win[0][p]) {
        _Ygl->win[0][p] = val;
        _Ygl->needWinUpdate = 1;
      }
    }
    // Parameter Mode
  }
  else {
    for (v = 0,p=0; p < _Ygl->rheight; v+=step, p++) {
      if (v >= varVdp2Regs->WPSY0 && v <= varVdp2Regs->WPEY0) {
        if (((short)varVdp2Regs->WPEY0 < (short)varVdp2Regs->WPSY0)  || ((short)varVdp2Regs->WPEY0 < 0)) val = 0x000000FF; //END < START
        else {
          val = (varVdp2Regs->WPSX0 >>HShift) | ((varVdp2Regs->WPEX0>>HShift) << 16);
        }
      } else {
        val = 0x000000FF; //END > START
      }
      if (val != _Ygl->win[0][p]) {
        _Ygl->win[0][p] = val;
        _Ygl->needWinUpdate = 1;
      }
    }
  }
  // Line Table mode
  if ((varVdp2Regs->LWTA1.part.U & 0x8000))
  {
    // start address
    LineWinAddr = (u32)((((varVdp2Regs->LWTA1.part.U & 0x07) << 15) | (varVdp2Regs->LWTA1.part.L >> 1)) << 2);
    for (v = 0,p=0; p < _Ygl->rheight; v+=step, p++) {
      if (v >= varVdp2Regs->WPSY1 && v <= varVdp2Regs->WPEY1) {
        short HStart = Vdp2RamReadWord(NULL, Vdp2Ram, LineWinAddr + (v << 2)) & 0xFFFF;
        short HEnd = Vdp2RamReadWord(NULL, Vdp2Ram, LineWinAddr + (v << 2) + 2) & 0xFFFF;
        if ((HEnd < HStart) || (HEnd < 0)) val = 0x000000FF; //END < START
        else {
          val = (HStart>>HShift) | ((HEnd>>HShift) << 16);
        }
      } else {
        val = 0x000000FF; //END < START
      }
      if (val != _Ygl->win[1][p]) {
        _Ygl->win[1][p] = val;
        _Ygl->needWinUpdate = 1;
      }
    }
    // Parameter Mode
  }
  else {
    for (v = 0,p=0; p < _Ygl->rheight; v+=step, p++) {
      if (v >= varVdp2Regs->WPSY1 && v <= varVdp2Regs->WPEY1) {
        if (((short)varVdp2Regs->WPEY1 < (short)varVdp2Regs->WPSY1) || ((short)varVdp2Regs->WPEY1 < 0)) val = 0x000000FF; //END < START
        else {
          val = (varVdp2Regs->WPSX1 >>HShift) | ((varVdp2Regs->WPEX1>>HShift) << 16);
        }
      } else {
        val = 0x000000FF; //END < START
      }
      if (val != _Ygl->win[1][p]) {
        _Ygl->win[1][p] = val;
        _Ygl->needWinUpdate = 1;
      }
    }
  }
}

// 0 .. outside,1 .. inside
static INLINE int Vdp2CheckWindow(vdp2draw_struct *info, int x, int y, int area, u32* win)
{
  if (y < 0) return 0;

  // if (_Ygl->interlace == DOUBLE_INTERLACE) y >>= 1;

  if (y >= _Ygl->rheight) return 0;
  int upLx = win[y] & 0xFFFF;
  int upRx = (win[y] >> 16) & 0xFFFF;
  // inside
  if (area == 1)
  {
    if (win[y] == 0) return 0;
    if (x >= upLx && x <= upRx)
    {
      return 1;
    }
    else {
      return 0;
    }
    // outside
  }
  else {
    if (win[y] == 0) return 1;
    if (x < upLx) return 1;
    if (x > upRx) return 1;
    return 0;
  }
  return 0;
}

// 0 .. all outsize, 1~3 .. partly inside, 4.. all inside
static int FASTCALL Vdp2CheckWindowRange(Vdp2Ctrl *ctrl, int x, int y, int w, int h)
{
  if (_Ygl->Win0[ctrl->info.idScreen]  != 0 && _Ygl->Win1[ctrl->info.idScreen]  == 0)
  {
    if(Vdp2CheckWindow(&ctrl->info, x, y, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
    if(Vdp2CheckWindow(&ctrl->info, x + w, y, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
    if(Vdp2CheckWindow(&ctrl->info, x + w, y + h, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
    if(Vdp2CheckWindow(&ctrl->info, x, y + h, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
    return 0;
  }
  else if (_Ygl->Win0[ctrl->info.idScreen]  == 0 && _Ygl->Win1[ctrl->info.idScreen]  != 0)
  {
    if(Vdp2CheckWindow(&ctrl->info, x, y, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
    if(Vdp2CheckWindow(&ctrl->info, x + w, y, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
    if(Vdp2CheckWindow(&ctrl->info, x + w, y + h, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
    if(Vdp2CheckWindow(&ctrl->info, x, y + h, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
    return 0;
  }
  else if (_Ygl->Win0[ctrl->info.idScreen]  != 0 && _Ygl->Win1[ctrl->info.idScreen]  != 0)
  {
    if (_Ygl->Win_op[ctrl->info.idScreen] == 0)
    {
      if(Vdp2CheckWindow(&ctrl->info, x, y, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x, y, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x + w, y, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x + w, y, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x + w, y + h, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x + w, y + h, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x, y + h, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x, y + h, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
      return 0;
    }
    else {
      if(Vdp2CheckWindow(&ctrl->info, x, y, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x, y, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x + w, y, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x + w, y, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x + w, y + h, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x + w, y + h, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x, y + h, _Ygl->Win0_mode[ctrl->info.idScreen], _Ygl->win[0])) return 1;
      if(Vdp2CheckWindow(&ctrl->info, x, y + h, _Ygl->Win1_mode[ctrl->info.idScreen], _Ygl->win[1])) return 1;
      return 0;
    }
  }
  return 0;
}

static void Vdp2GenLineinfo(vdp2draw_struct *info)
{
  int bound = 0;
  int i;
  u16 val1, val2;
  int index = 0;
  if (info->lineinc == 0 || info->islinescroll == 0) return;

  if (VDPLINE_SY(info->islinescroll)) bound += 0x04;
  if (VDPLINE_SX(info->islinescroll)) bound += 0x04;
  if (VDPLINE_SZ(info->islinescroll)) bound += 0x04;

  int height = _Ygl->rheight;
  // if (_Ygl->interlace == DOUBLE_INTERLACE) height <<= 1;

  for (i = 0; i < height; i ++)
  {
    index = 0;
    if (VDPLINE_SX(info->islinescroll))
    {
      info->lineinfo[i].LineScrollValH = Vdp2RamReadWord(NULL, Vdp2Ram, info->linescrolltbl + i*bound);
      if ((info->lineinfo[i].LineScrollValH & 0x400)) info->lineinfo[i].LineScrollValH |= 0xF800; else info->lineinfo[i].LineScrollValH &= 0x07FF;
      index += 4;
    }
    else {
      info->lineinfo[i].LineScrollValH = 0;
    }

    if (VDPLINE_SY(info->islinescroll))
    {
      info->lineinfo[i].LineScrollValV = Vdp2RamReadWord(NULL, Vdp2Ram, info->linescrolltbl + i*bound + index);
      if ((info->lineinfo[i].LineScrollValV & 0x400)) info->lineinfo[i].LineScrollValV |= 0xF800; else info->lineinfo[i].LineScrollValV &= 0x07FF;
      index += 4;
    }
    else {
      info->lineinfo[i].LineScrollValV = 0;
    }

    if (VDPLINE_SZ(info->islinescroll))
    {
      val1 = Vdp2RamReadWord(NULL, Vdp2Ram, info->linescrolltbl + i*bound + index);
      val2 = Vdp2RamReadWord(NULL, Vdp2Ram, info->linescrolltbl + i*bound + index + 2);
      info->lineinfo[i].CoordinateIncH = (((int)((val1) & 0x07) << 8) | (int)((val2) >> 8));
      index += 4;
    }
    else {
      info->lineinfo[i].CoordinateIncH = 0x0100;
    }
  }
}

INLINE void Vdp2SetSpecialPriority(vdp2draw_struct *info, u8 dot, u32 *prio, u32 * cramindex ) {
  *prio = info->priority;
  if (info->specialprimode == 2) {
    *prio = info->priority & 0xE;
    if (info->specialfunction & 1) {
      if (PixelIsSpecialPriority(info->specialcode, dot))
      {
        *prio |= 1;
      }
    }
  }
}

static INLINE u32 Vdp2GetCCOn(Vdp2Ctrl *ctrl, u8 dot, u32 cramindex) {

  const int CCMD = ((ctrl->regs->CCCTL >> 8) & 0x01);  // hard/vdp2/hon/p12_14.htm#CCMD_
  int cc = 1;
  if (CCMD == 0) {  // Calculate Rate mode
    switch (ctrl->info.specialcolormode)
    {
    case 1: if (ctrl->info.specialcolorfunction == 0) { cc = 0; } break;
    case 2:
      if (ctrl->info.specialcolorfunction == 0) { cc = 0; }
      else { if ((ctrl->info.specialcode & (1 << ((dot & 0xF) >> 1))) == 0) { cc = 0; } }
      break;
   case 3:
     if (((Vdp2ColorRamGetColorRaw(cramindex) & 0x8000) == 0)) { cc = 0; }
     break;
    }
  }
  else {  // Calculate Add mode
    switch (ctrl->info.specialcolormode)
    {
    case 1:
      if (ctrl->info.specialcolorfunction == 0) { cc = 0; }
      break;
    case 2:
      if (ctrl->info.specialcolorfunction == 0) { cc = 0; }
      else { if ((ctrl->info.specialcode & (1 << ((dot & 0xF) >> 1))) == 0) { cc = 0; } }
      break;
   case 3:
     if (((Vdp2ColorRamGetColorRaw(cramindex) & 0x8000) == 0)) { cc = 0; }
     break;
    }
  }
  return cc;
}


static INLINE u32 Vdp2GetPixel4bpp(Vdp2Ctrl *ctrl, u32 addr) {

  u32 cramindex;
  u16 dotw = Vdp2RamReadWord(NULL, Vdp2Ram, addr);
  u8 dot;
  u32 cc;
  u32 priority = 0;

  dot = (dotw & 0xF000) >> 12;
  if (!(dot & 0xF) && ctrl->info.transparencyenable) {
    *ctrl->texture.textdata++ = 0x00000000;
  } else {
    cramindex = (ctrl->info.coloroffset + ((ctrl->info.paladdr << 4) | (dot & 0xF)));
    Vdp2SetSpecialPriority(&ctrl->info, dot, &priority, &cramindex);
    cc = Vdp2GetCCOn(ctrl, dot, cramindex);
    *ctrl->texture.textdata++ = VDP2COLOR(ctrl->info.idScreen, ctrl->info.alpha, priority, cc, cramindex);
  }

  cc = 1;
  dot = (dotw & 0xF00) >> 8;
  if (!(dot & 0xF) && ctrl->info.transparencyenable) {
    *ctrl->texture.textdata++ = 0x00000000;
  }
  else {
    cramindex = (ctrl->info.coloroffset + ((ctrl->info.paladdr << 4) | (dot & 0xF)));
    Vdp2SetSpecialPriority(&ctrl->info, dot, &priority, &cramindex);
    cc = Vdp2GetCCOn(ctrl, dot, cramindex);
    *ctrl->texture.textdata++ = VDP2COLOR(ctrl->info.idScreen, ctrl->info.alpha, priority, cc, cramindex);
  }

  cc = 1;
  dot = (dotw & 0xF0) >> 4;
  if (!(dot & 0xF) && ctrl->info.transparencyenable) {
    *ctrl->texture.textdata++ = 0x00000000;
  }
  else {
    cramindex = (ctrl->info.coloroffset + ((ctrl->info.paladdr << 4) | (dot & 0xF)));
    Vdp2SetSpecialPriority(&ctrl->info, dot, &priority, &cramindex);
    cc = Vdp2GetCCOn(ctrl, dot, cramindex);
    *ctrl->texture.textdata++ = VDP2COLOR(ctrl->info.idScreen, ctrl->info.alpha, priority, cc, cramindex);
  }

  cc = 1;
  dot = (dotw & 0xF);
  if (!(dot & 0xF) && ctrl->info.transparencyenable) {
    *ctrl->texture.textdata++ = 0x00000000;
  }
  else {
    cramindex = (ctrl->info.coloroffset + ((ctrl->info.paladdr << 4) | (dot & 0xF)));
    Vdp2SetSpecialPriority(&ctrl->info, dot, &priority, &cramindex);
    cc = Vdp2GetCCOn(ctrl, dot, cramindex);
    *ctrl->texture.textdata++ = VDP2COLOR(ctrl->info.idScreen, ctrl->info.alpha, priority, cc, cramindex);
  }
  return 0;
}

static INLINE u32 Vdp2GetPixel8bpp(Vdp2Ctrl *ctrl, u32 addr) {

  u32 cramindex;
  u16 dotw = Vdp2RamReadWord(NULL, Vdp2Ram, addr);
  u8 dot;
  u32 cc;
  u32 priority = 0;

  cc = 1;
  dot = (dotw & 0xFF00)>>8;
  if (!(dot & 0xFF) && ctrl->info.transparencyenable) *ctrl->texture.textdata++ = 0x00000000;
  else {
    cramindex = ctrl->info.coloroffset + ((ctrl->info.paladdr << 4) | (dot & 0xFF));
    Vdp2SetSpecialPriority(&ctrl->info, dot, &priority, &cramindex);
    cc = Vdp2GetCCOn(ctrl, dot, cramindex);
    *ctrl->texture.textdata++ = VDP2COLOR(ctrl->info.idScreen, ctrl->info.alpha, priority, cc, cramindex);
  }
  cc = 1;
  dot = (dotw & 0xFF);
  if (!(dot & 0xFF) && ctrl->info.transparencyenable) *ctrl->texture.textdata++ = 0x00000000;
  else {
    cramindex = ctrl->info.coloroffset + ((ctrl->info.paladdr << 4) | (dot & 0xFF));
    Vdp2SetSpecialPriority(&ctrl->info, dot, &priority, &cramindex);
    cc = Vdp2GetCCOn(ctrl, dot, cramindex);
    *ctrl->texture.textdata++ = VDP2COLOR(ctrl->info.idScreen, ctrl->info.alpha, priority, cc, cramindex);
  }
  return 0;
}


static INLINE u32 Vdp2GetPixel16bpp(Vdp2Ctrl *ctrl, u32 addr) {
  u32 cramindex;
  u8 cc;
  u16 dot = Vdp2RamReadWord(NULL, Vdp2Ram, addr);
  u32 priority = 0;
  if ((dot == 0) && ctrl->info.transparencyenable) return 0x00000000;
  else {
    cramindex = ctrl->info.coloroffset + dot;
    Vdp2SetSpecialPriority(&ctrl->info, dot, &priority, &cramindex);
    cc = Vdp2GetCCOn(ctrl, dot, cramindex);
    return VDP2COLOR(ctrl->info.idScreen, ctrl->info.alpha, priority, cc, cramindex);
  }
}

static INLINE u32 Vdp2GetPixel16bppbmp(Vdp2Ctrl *ctrl, u32 addr) {
  u32 color;
  u16 dot = Vdp2RamReadWord(NULL, Vdp2Ram, addr);
//if (ctrl->info.patternwh == 2) printf("%x\n", dot);
//Ca deconne ici
  int cc = Vdp2GetCCOn(ctrl, dot, 0);
  if (!(dot & 0x8000) && ctrl->info.transparencyenable) color = 0x00000000;
  else color = VDP2COLOR(ctrl->info.idScreen, ctrl->info.alpha, ctrl->info.priority, cc, RGB555_TO_RGB24(dot));
  return color;
}

static INLINE u32 Vdp2GetPixel32bppbmp(Vdp2Ctrl *ctrl, u32 addr) {
  u32 color;
  u16 dot1, dot2;
  int cc;
  dot1 = Vdp2RamReadWord(NULL, Vdp2Ram, addr);
  dot2 = Vdp2RamReadWord(NULL, Vdp2Ram, addr+2);

  cc = Vdp2GetCCOn(ctrl, 0, 0);

  if (!(dot1 & 0x8000) && ctrl->info.transparencyenable) color = 0x00000000;
  else color = VDP2COLOR(ctrl->info.idScreen, ctrl->info.alpha, ctrl->info.priority, cc, (((dot1 & 0xFF) << 16) | (dot2 & 0xFF00) | (dot2 & 0xFF)));
  return color;
}

static u32 getAlpha(vdp2draw_struct *info, int id) {
  int shift = 0;
  if (_Ygl->interlace == DOUBLE_INTERLACE) shift = 1;
  int idx = info->draw_line + id;
  if (idx < 0) idx = 0;
  if ((idx>>shift) > yabsys.VBlankLineCount) idx = yabsys.VBlankLineCount<<shift;
  return info->alpha_per_line[idx>>shift];
}

static void FASTCALL Vdp2DrawCell_in_sync(Vdp2Ctrl *ctrl)
{
  int i, j;


//   if ((vdp2_interlace == 1) && (_Ygl->rheight > 448)) {
//     // Weird... Partly fix True Pinball in case of interlace only but it is breaking Zen Nihon Pro Wres, so use the bad test of the height
//     Vdp2DrawCellInterlace(info, texture, ctrl->regs);
//     return;
//   }
  switch (ctrl->info.colornumber)
  {
  case 0: // 4 BPP
    for (i = 0; i < ctrl->info.cellh; i++)
    {
      ctrl->info.alpha = getAlpha(&ctrl->info, i);
      for (j = 0; j < ctrl->info.cellw; j += 4)
      {
        Vdp2GetPixel4bpp(ctrl, ctrl->info.charaddr);
        ctrl->info.charaddr += 2;
      }
      ctrl->texture.textdata += ctrl->texture.w;
    }
    break;
  case 1: // 8 BPP
    for (i = 0; i < ctrl->info.cellh; i++)
    {
      ctrl->info.alpha = getAlpha(&ctrl->info, i);
      for (j = 0; j < ctrl->info.cellw; j += 2)
      {
        Vdp2GetPixel8bpp(ctrl, ctrl->info.charaddr);
        ctrl->info.charaddr += 2;
      }
      ctrl->texture.textdata += ctrl->texture.w;
    }
    break;
  case 2: // 16 BPP(palette)
    for (i = 0; i < ctrl->info.cellh; i++)
    {
      ctrl->info.alpha = getAlpha(&ctrl->info, i);
      for (j = 0; j < ctrl->info.cellw; j++)
      {
        *ctrl->texture.textdata++ = Vdp2GetPixel16bpp(ctrl, ctrl->info.charaddr);
        ctrl->info.charaddr += 2;
      }
      ctrl->texture.textdata += ctrl->texture.w;
    }
    break;
  case 3: // 16 BPP(RGB)
    for (i = 0; i < ctrl->info.cellh; i++)
    {
      ctrl->info.alpha = getAlpha(&ctrl->info, i);
      for (j = 0; j < ctrl->info.cellw; j++)
      {
        *ctrl->texture.textdata++ = Vdp2GetPixel16bppbmp(ctrl, ctrl->info.charaddr);
        ctrl->info.charaddr += 2;
      }
      ctrl->texture.textdata += ctrl->texture.w;
    }
    break;
  case 4: // 32 BPP
    for (i = 0; i < ctrl->info.cellh; i++)
    {
      ctrl->info.alpha = getAlpha(&ctrl->info, i);
      for (j = 0; j < ctrl->info.cellw; j++)
      {
        *ctrl->texture.textdata++ = Vdp2GetPixel32bppbmp(ctrl, ctrl->info.charaddr);
        ctrl->info.charaddr += 4;
      }
      ctrl->texture.textdata += ctrl->texture.w;
    }
    break;
  }
}


static void FASTCALL Vdp2DrawBitmapLineScroll(Vdp2Ctrl *ctrl, int width, int height)
{
  int i, j;
  int shift = 0;
  if (_Ygl->interlace == DOUBLE_INTERLACE) shift = 1;

  for (i = 0; i < height; i++)
  {
    int sh, sv;
    u32 baseaddr;
    vdp2Lineinfo * line;
    ctrl->info.draw_line = i;
    ctrl->info.alpha = ctrl->info.alpha_per_line[ctrl->info.draw_line>>shift];
    baseaddr = (u32)ctrl->info.charaddr;
    line = &(ctrl->info.lineinfo[i]);

    if (VDPLINE_SX(ctrl->info.islinescroll))
      sh = line->LineScrollValH + ctrl->info.sh;
    else
      sh = ctrl->info.sh;

    if (VDPLINE_SY(ctrl->info.islinescroll))
      sv = line->LineScrollValV + ctrl->info.sv;
    else
      sv = i + ctrl->info.sv;

    sv &= (ctrl->info.cellh - 1);
    sh &= (ctrl->info.cellw - 1);
    if ((line->LineScrollValH >= 0) && (line->LineScrollValH < sh) && (sv >0)) {
      sv -= 1;
    }
    switch (ctrl->info.colornumber) {
    case 0:
      baseaddr += (((sh + sv * ctrl->info.cellw) >> 2) << 1);
      for (j = 0; j < width; j += 4)
      {
        Vdp2GetPixel4bpp(ctrl, baseaddr);
        baseaddr += 2;
      }
      break;
    case 1:
      baseaddr += sh + sv * ctrl->info.cellw;
      for (j = 0; j < width; j += 2)
      {
        Vdp2GetPixel8bpp(ctrl, baseaddr);
        baseaddr += 2;
      }
      break;
    case 2:
      baseaddr += ((sh + sv * ctrl->info.cellw) << 1);
      for (j = 0; j < width; j++)
      {
        *ctrl->texture.textdata++ = Vdp2GetPixel16bpp(ctrl, baseaddr);
        baseaddr += 2;

      }
      break;
    case 3:
      baseaddr += ((sh + sv * ctrl->info.cellw) << 1);
      for (j = 0; j < width; j++)
      {
        *ctrl->texture.textdata++ = Vdp2GetPixel16bppbmp(ctrl, baseaddr);
        baseaddr += 2;
      }
      break;
    case 4:
      baseaddr += ((sh + sv * ctrl->info.cellw) << 2);
      for (j = 0; j < width; j++)
      {
        //if (ctrl->info.isverticalscroll){
        //	sv += T1ReadLong(Vdp2Ram, ctrl->info.verticalscrolltbl+(j>>3) ) >> 16;
        //}
        *ctrl->texture.textdata++ = Vdp2GetPixel32bppbmp(ctrl, baseaddr);
        baseaddr += 4;
      }
      break;
    }
    ctrl->texture.textdata += ctrl->texture.w;
  }
}


static void FASTCALL Vdp2DrawBitmapCoordinateInc(Vdp2Ctrl *ctrl)
{
  u32 color;
  int i, j;
  int shift = 0;
  if (_Ygl->interlace == DOUBLE_INTERLACE) shift = 1;
  int incv = 1.0 / ctrl->info.coordincy*256.0;
  int inch = 1.0 / ctrl->info.coordincx*256.0;
  for (i = 0; i < _Ygl->rheight; i ++)
  {
    int sh, sv;
    int v;
    u32 baseaddr;
    vdp2Lineinfo * line;
    baseaddr = (u32)ctrl->info.charaddr;
    int index = i;
    if (ctrl->info.lineinc > 0) index /= ctrl->info.lineinc;
    line = &(ctrl->info.lineinfo[index]);

    ctrl->info.draw_line = i;

    v = (i*incv) >> 8;
    if (VDPLINE_SZ(ctrl->info.islinescroll))
      inch = line->CoordinateIncH;

    if (inch == 0) inch = 1;

    if (VDPLINE_SX(ctrl->info.islinescroll))
      sh = ctrl->info.sh + line->LineScrollValH;
    else
      sh = ctrl->info.sh;

    if (VDPLINE_SY(ctrl->info.islinescroll))
      sv = ctrl->info.sv + line->LineScrollValV;
    else
      sv = v + ctrl->info.sv;

    //sh &= (ctrl->info.cellw - 1);
    sv &= (ctrl->info.cellh - 1);
    switch (ctrl->info.colornumber) {
    case 0:
      baseaddr = baseaddr + (sh >> 1) + (sv * (ctrl->info.cellw >> 1));
      for (j = 0; j < _Ygl->rwidth; j++)
      {
        u32 h = ((j*inch) >> 8);
        u32 addr = (baseaddr + (h >> 1));
        if (addr >= 0x80000) {
          *ctrl->texture.textdata++ = 0x0000;
        }
        else {
          int cc = 1;
          u8 dot = Vdp2RamReadByte(NULL, Vdp2Ram, addr);
          u32 alpha = ctrl->info.alpha_per_line[ctrl->info.draw_line>>shift];
          if (!(h & 0x01)) dot = dot >> 4;
          if (!(dot & 0xF) && ctrl->info.transparencyenable) *ctrl->texture.textdata++ = 0x00000000;
          else {
            color = (ctrl->info.coloroffset + ((ctrl->info.paladdr << 4) | (dot & 0xF)));
            switch (ctrl->info.specialcolormode)
            {
            case 1: if (ctrl->info.specialcolorfunction == 0) { cc = 0; } break;
            case 2:
              if (ctrl->info.specialcolorfunction == 0) { cc = 0; }
              else { if ((ctrl->info.specialcode & (1 << ((dot & 0xF) >> 1))) == 0) { cc = 0; } }
              break;
            case 3:
              if (((Vdp2ColorRamGetColorRaw(color) & 0x8000) == 0)) { cc = 0; }
              break;
            }
            *ctrl->texture.textdata++ = VDP2COLOR(ctrl->info.idScreen, alpha, ctrl->info.priority, cc, color);
          }
        }
      }
      break;
    case 1:
      baseaddr += sh + sv * ctrl->info.cellw;

      for (j = 0; j < _Ygl->rwidth; j++)
      {
        int h = ((j*inch) >> 8);
        u32 alpha = ctrl->info.alpha_per_line[ctrl->info.draw_line>>shift];
        u8 dot = Vdp2RamReadByte(NULL, Vdp2Ram, baseaddr + h);
        if (!dot && ctrl->info.transparencyenable) {
          *ctrl->texture.textdata++ = 0; continue;
        }
        else {
          int cc = 1;
          color = ctrl->info.coloroffset + ((ctrl->info.paladdr << 4) | (dot & 0xFF));
          switch (ctrl->info.specialcolormode)
          {
          case 1: if (ctrl->info.specialcolorfunction == 0) { cc = 0; } break;
          case 2:
            if (ctrl->info.specialcolorfunction == 0) { cc = 0; }
            else { if ((ctrl->info.specialcode & (1 << ((dot & 0xF) >> 1))) == 0) { cc = 0; } }
            break;
          case 3:
            if (((Vdp2ColorRamGetColorRaw(color) & 0x8000) == 0)) { cc = 0; }
            break;
          }
          *ctrl->texture.textdata++ = VDP2COLOR(ctrl->info.idScreen, alpha, ctrl->info.priority, cc, color);
        }
      }

      break;
    case 2:
      baseaddr += ((sh + sv * ctrl->info.cellw) << 1);
      for (j = 0; j < _Ygl->rwidth; j++)
      {
        int h = ((j*inch) >> 8) << 1;
        *ctrl->texture.textdata++ = Vdp2GetPixel16bpp(ctrl, baseaddr + h);

      }
      break;
    case 3:
      baseaddr += ((sh + sv * ctrl->info.cellw) << 1);
      for (j = 0; j < _Ygl->rwidth; j++)
      {
        int h = ((j*inch) >> 8) << 1;
        *ctrl->texture.textdata++ = Vdp2GetPixel16bppbmp(ctrl, baseaddr + h);
      }
      break;
    case 4:
      baseaddr += ((sh + sv * ctrl->info.cellw) << 2);
      for (j = 0; j < _Ygl->rwidth; j++)
      {
        int h = (j*inch >> 8) << 2;
        *ctrl->texture.textdata++ = Vdp2GetPixel32bppbmp(ctrl, baseaddr + h);
      }
      break;
    }
    ctrl->texture.textdata += ctrl->texture.w;
  }
}

//////////////////////////////////////////////////////////////////////////////

static INLINE u32 Vdp2RotationFetchPixel(vdp2draw_struct *info, int x, int y, int cellw)
{
  u32 dot;
  u32 cramindex;
  int shift = 0;
  if (_Ygl->interlace == DOUBLE_INTERLACE) shift = 1;
  u32 alpha = info->alpha_per_line[info->draw_line>>shift];
  u8 lowdot = 0x00;
  u32 priority = 0;
  switch (info->colornumber)
  {
  case 0: // 4 BPP
    dot = Vdp2RamReadByte(NULL, Vdp2Ram, (info->charaddr + (((y * cellw) + x) >> 1) ));
    if (!(x & 0x1)) dot >>= 4;
    if (!(dot & 0xF) && info->transparencyenable) return 0x00000000;
    else {
      int cc = 1;
      cramindex = (info->coloroffset + ((info->paladdr << 4) | (dot & 0xF)));
      Vdp2SetSpecialPriority(info, dot, &priority, &cramindex);
      switch (info->specialcolormode)
      {
      case 1: if (info->specialcolorfunction == 0) { cc = 0; } break;
      case 2:
        if (info->specialcolorfunction == 0) { cc = 0; }
        else { if ((info->specialcode & (1 << ((dot & 0xF) >> 1))) == 0) { cc = 0; } }
        break;
      case 3:
        if (((Vdp2ColorRamGetColorRaw(cramindex) & 0x8000) == 0)) { cc = 0; }
        break;
      }
      return   VDP2COLOR(info->idScreen, alpha, priority, cc, cramindex);
    }
  case 1: // 8 BPP
    dot = Vdp2RamReadByte(NULL, Vdp2Ram, (info->charaddr + (y * cellw) + x));
    if (!(dot & 0xFF) && info->transparencyenable) return 0x00000000;
    else {
      int cc = 1;
      cramindex = info->coloroffset + ((info->paladdr << 4) | (dot & 0xFF));
      Vdp2SetSpecialPriority(info, dot, &priority, &cramindex);
      switch (info->specialcolormode)
      {
      case 1: if (info->specialcolorfunction == 0) { cc = 0; } break;
      case 2:
        if (info->specialcolorfunction == 0) { cc = 0; }
        else { if ((info->specialcode & (1 << ((dot & 0xF) >> 1))) == 0) { cc = 0; } }
        break;
      case 3:
        if (((Vdp2ColorRamGetColorRaw(cramindex) & 0x8000) == 0)) { cc = 0; }
        break;
      }
      return   VDP2COLOR(info->idScreen, alpha, priority, cc, cramindex);
    }
  case 2: // 16 BPP(palette)
    dot = Vdp2RamReadWord(NULL, Vdp2Ram, (info->charaddr + ((y * cellw) + x) * 2));
    if ((dot == 0) && info->transparencyenable) return 0x00000000;
    else {
      int cc = 1;
      cramindex = (info->coloroffset + dot);
      Vdp2SetSpecialPriority(info, dot, &priority, &cramindex);
      switch (info->specialcolormode)
      {
      case 1: if (info->specialcolorfunction == 0) { cc = 0; } break;
      case 2:
        if (info->specialcolorfunction == 0) { cc = 0; }
        else { if ((info->specialcode & (1 << ((dot & 0xF) >> 1))) == 0) { cc = 0; } }
        break;
      case 3:
        if (((Vdp2ColorRamGetColorRaw(cramindex) & 0x8000) == 0)) { cc = 0; }
        break;
      }
      return   VDP2COLOR(info->idScreen, alpha, priority, cc, cramindex);
    }
  case 3: // 16 BPP(RGB)
    dot = Vdp2RamReadWord(NULL, Vdp2Ram, (info->charaddr + ((y * cellw) + x) * 2));
    if (!(dot & 0x8000) && info->transparencyenable) return 0x00000000;
    else return VDP2COLOR(info->idScreen, alpha, info->priority, 1, RGB555_TO_RGB24(dot & 0xFFFF));
  case 4: // 32 BPP
    dot = Vdp2RamReadLong(NULL, Vdp2Ram, (info->charaddr + ((y * cellw) + x) * 4));
    if (!(dot & 0x80000000) && info->transparencyenable) return 0x00000000;
    else return VDP2COLOR(info->idScreen, alpha, info->priority, 1, dot & 0xFFFFFF);
  default:
    return 0;
  }
}

static int getPriority(int id, Vdp2 *a) {
  switch (id) {
  case NBG0:
      return (a->PRINA & 0x7);
  case NBG1:
    return ((a->PRINA >> 8) & 0x7);
  default:
    return 0;
  }
}
//////////////////////////////////////////////////////////////////////////////

static void Vdp2DrawMapPerLine(Vdp2Ctrl *ctrl) {

  int lineindex = 0;

  int sx; //, sy;
  int mapx, mapy;
  int planex, planey;
  int pagex, pagey;
  int charx, chary;
  int dot_on_planey;
  int dot_on_pagey;
  int dot_on_planex;
  int dot_on_pagex;
  int h, v;
  const int planeh_shift = 9 + (ctrl->info.planeh - 1);
  const int planew_shift = 9 + (ctrl->info.planew - 1);
  const int plane_shift = 9;
  const int plane_mask = 0x1FF;
  const int page_shift = 9 - 7 + (64 / ctrl->info.pagewh);
  const int page_mask = 0x0f >> ((ctrl->info.pagewh / 32) - 1);

  int preplanex = -1;
  int preplaney = -1;
  int prepagex = -1;
  int prepagey = -1;
  int mapid = 0;
  int premapid = -1;

  ctrl->info.patternpixelwh = 8 * ctrl->info.patternwh;
  ctrl->info.draww = _Ygl->rwidth;

  const int incv = 1.0 / ctrl->info.coordincy*256.0;
  const int res_shift = 0;

  int linemask = 0;
  switch (ctrl->info.lineinc) {
  case 1:
    linemask = 0;
    break;
  case 2:
    linemask = 0x01;
    break;
  case 4:
    linemask = 0x03;
    break;
  case 8:
    linemask = 0x07;
    break;
  }
  int screenH = _Ygl->rheight;
  // if (_Ygl->interlace == DOUBLE_INTERLACE) screenH <<= 1;
  for (v = 0; v < screenH; v++) {  // ToDo: ctrl->info.coordincy
    int targetv = 0;

    if (VDPLINE_SX(ctrl->info.islinescroll)) {
      sx = ctrl->info.sh + ctrl->info.lineinfo[lineindex<<res_shift].LineScrollValH;
    }
    else {
      sx = ctrl->info.sh;
    }

    if (VDPLINE_SY(ctrl->info.islinescroll)) {
      targetv = ctrl->info.sv + (v&linemask) + ctrl->info.lineinfo[lineindex<<res_shift].LineScrollValV;
    }
    else {
      targetv = ctrl->info.sv + ((v*incv)>>8);
    }

    if (ctrl->info.isverticalscroll) {
      // this is *wrong*, vertical scroll use a different value per cell
      // ctrl->info.verticalscrolltbl should be incremented by ctrl->info.verticalscrollinc
      // each time there's a cell change and reseted at the end of the line...
      // or something like that :)
      targetv += Vdp2RamReadLong(NULL, Vdp2Ram, ctrl->info.verticalscrolltbl) >> 16;
    }

    if (VDPLINE_SZ(ctrl->info.islinescroll)) {
      ctrl->info.coordincx = ctrl->info.lineinfo[lineindex<<res_shift].CoordinateIncH / 256.0f;
      if (ctrl->info.coordincx == 0) {
        ctrl->info.coordincx = _Ygl->rwidth;
      }
      else {
        ctrl->info.coordincx = 1.0f / ctrl->info.coordincx;
      }
    }

    if (ctrl->info.coordincx < ctrl->info.maxzoom) ctrl->info.coordincx = ctrl->info.maxzoom;

    // determine which chara shoud be used.
    //mapy   = (v+sy) / (512 * ctrl->info.planeh);
    mapy = (targetv) >> planeh_shift;
    //int dot_on_planey = (v + sy) - mapy*(512 * ctrl->info.planeh);
    dot_on_planey = (targetv)-(mapy << planeh_shift);
    mapy = mapy & 0x01;
    //planey = dot_on_planey / 512;
    planey = dot_on_planey >> plane_shift;
    //int dot_on_pagey = dot_on_planey - planey * 512;
    dot_on_pagey = dot_on_planey & plane_mask;
    planey = planey & (ctrl->info.planeh - 1);
    //pagey = dot_on_pagey / (512 / ctrl->info.pagewh);
    pagey = dot_on_pagey >> page_shift;
    //chary = dot_on_pagey - pagey*(512 / ctrl->info.pagewh);
    chary = dot_on_pagey & page_mask;
    if (pagey < 0) pagey = ctrl->info.pagewh - 1 + pagey;

    int inch = 1.0 / ctrl->info.coordincx*256.0;

    for (int j = 0; j < ctrl->info.draww; j += 1) {

      int h = ((j*inch) >> 8);

      //mapx = (h + sx) / (512 * ctrl->info.planew);
      mapx = (h + sx) >> planew_shift;
      //int dot_on_planex = (h + sx) - mapx*(512 * ctrl->info.planew);
      dot_on_planex = (h + sx) - (mapx << planew_shift);
      mapx = mapx & 0x01;

      mapid = ctrl->info.mapwh * mapy + mapx;
      if (mapid != premapid) {
        if (ctrl->info.PlaneAddr == 0) {
          exit(-1);
        }
        ctrl->info.PlaneAddr(&ctrl->info, mapid, ctrl->regs);
        premapid = mapid;
      }

      //planex = dot_on_planex / 512;
      planex = dot_on_planex >> plane_shift;
      //int dot_on_pagex = dot_on_planex - planex * 512;
      dot_on_pagex = dot_on_planex & plane_mask;
      planex = planex & (ctrl->info.planew - 1);
      //pagex = dot_on_pagex / (512 / ctrl->info.pagewh);
      pagex = dot_on_pagex >> page_shift;
      //charx = dot_on_pagex - pagex*(512 / ctrl->info.pagewh);
      charx = dot_on_pagex & page_mask;
      if (pagex < 0) pagex = ctrl->info.pagewh - 1 + pagex;

      if (planex != preplanex || pagex != prepagex || planey != preplaney || pagey != prepagey) {
        if (Vdp2PatternAddrPos(ctrl, planex, pagex, planey, pagey) == 0) continue;
        preplanex = planex;
        preplaney = planey;
        prepagex = pagex;
        prepagey = pagey;
      }
      ctrl->info.draw_line = v;
      if (_Ygl->interlace == DOUBLE_INTERLACE) ctrl->info.draw_line >>= 1;
      ctrl->info.priority = getPriority(ctrl->info.idScreen, &Vdp2Lines[ctrl->info.draw_line]); //MapPerLine is called only for NBG0 and NBG1
      int priority = ctrl->info.priority;
      if (ctrl->info.specialprimode == 1) {
        ctrl->info.priority = (ctrl->info.priority & 0xFFFFFFFE) | ctrl->info.specialfunction;
      }

      int x = charx;
      int y = chary;

      if (ctrl->info.patternwh == 1)
      {
        x &= 8 - 1;
        y &= 8 - 1;

        // vertical flip
        if (ctrl->info.flipfunction & 0x2)
          y = 8 - 1 - y;

        // horizontal flip
        if (ctrl->info.flipfunction & 0x1)
          x = 8 - 1 - x;
      }
      else
      {
        if (ctrl->info.flipfunction)
        {
          y &= 16 - 1;
          if (ctrl->info.flipfunction & 0x2)
          {
            if (!(y & 8))
              y = 8 - 1 - y + 16;
            else
              y = 16 - 1 - y;
          }
          else if (y & 8)
            y += 8;

          if (ctrl->info.flipfunction & 0x1)
          {
            if (!(x & 8))
              y += 8;

            x &= 8 - 1;
            x = 8 - 1 - x;
          }
          else if (x & 8)
          {
            y += 8;
            x &= 8 - 1;
          }
          else
            x &= 8 - 1;
        }
        else
        {
          y &= 16 - 1;
          if (y & 8)
            y += 8;
          if (x & 8)
            y += 8;
          x &= 8 - 1;
        }
      }
      *(ctrl->texture.textdata++) = Vdp2RotationFetchPixel(&ctrl->info, x, y, ctrl->info.cellw);
      ctrl->info.priority = priority;
    }
    if((v & linemask) == linemask) lineindex++;
    ctrl->texture.textdata += ctrl->texture.w;
  }

}

static void Vdp2DrawMapTest(Vdp2Ctrl *ctrl, int delayed) {

  int lineindex = 0;

  int sx = 0; //, sy;
  int mapx = 0, mapy = 0;
  int planex = 0, planey = 0;
  int pagex = 0, pagey = 0;
  int charx = 0, chary = 0;
  int dot_on_planey = 0;
  int dot_on_pagey = 0;
  int dot_on_planex = 0;
  int dot_on_pagex = 0;
  int h, v;
  int cell_count = 0;

  const int planeh_shift = 9 + (ctrl->info.planeh - 1);
  const int planew_shift = 9 + (ctrl->info.planew - 1);
  const int plane_shift = 9;
  const int plane_mask = 0x1FF;
  const int page_shift = 9 - 7 + (64 / ctrl->info.pagewh);
  const int page_mask = 0x0f >> ((ctrl->info.pagewh / 32) - 1);

  ctrl->info.patternpixelwh = 8 * ctrl->info.patternwh;
  ctrl->info.draww = (int)((float)_Ygl->rwidth / ctrl->info.coordincx);
  ctrl->info.drawh = (int)((float)_Ygl->rheight / ctrl->info.coordincy);
  if (_Ygl->interlace == DOUBLE_INTERLACE) ctrl->info.drawh *= 2;
  ctrl->info.lineinc = ctrl->info.patternpixelwh;

  //ctrl->info.coordincx = 1.0f;

  for (v = -ctrl->info.patternpixelwh; v < ctrl->info.drawh + ctrl->info.patternpixelwh; v += ctrl->info.patternpixelwh) {
    int targetv = 0;
    sx = ctrl->info.x;

    if (!ctrl->info.isverticalscroll) {
      targetv = ctrl->info.y + v;
      // determine which chara shoud be used.
      //mapy   = (v+sy) / (512 * ctrl->info.planeh);
      mapy = (targetv) >> planeh_shift;
      //int dot_on_planey = (v + sy) - mapy*(512 * ctrl->info.planeh);
      dot_on_planey = (targetv)-(mapy * (1 << planeh_shift));
      mapy = mapy & 0x01;
      //planey = dot_on_planey / 512;
      planey = dot_on_planey >> plane_shift;
      //int dot_on_pagey = dot_on_planey - planey * 512;
      dot_on_pagey = dot_on_planey & plane_mask;
      planey = planey & (ctrl->info.planeh - 1);
      //pagey = dot_on_pagey / (512 / ctrl->info.pagewh);
      pagey = dot_on_pagey >> page_shift;
      //chary = dot_on_pagey - pagey*(512 / ctrl->info.pagewh);
      chary = dot_on_pagey & page_mask;
      if (pagey < 0) pagey = ctrl->info.pagewh - 1 + pagey;
    }
    else {
      cell_count = 0;
    }
    for (h = -ctrl->info.patternpixelwh; h < ctrl->info.draww + ctrl->info.patternpixelwh; h += ctrl->info.patternpixelwh) {

      if (ctrl->info.isverticalscroll) {
        targetv = ctrl->info.y + v + (Vdp2RamReadLong(NULL, Vdp2Ram, ctrl->info.verticalscrolltbl + cell_count) >> 16);
        cell_count += ctrl->info.verticalscrollinc;
        // determine which chara shoud be used.
        //mapy   = (v+sy) / (512 * ctrl->info.planeh);
        mapy = (targetv) >> planeh_shift;
        //int dot_on_planey = (v + sy) - mapy*(512 * ctrl->info.planeh);
        dot_on_planey = (targetv)-(mapy << planeh_shift);
        mapy = mapy & 0x01;
        //planey = dot_on_planey / 512;
        planey = dot_on_planey >> plane_shift;
        //int dot_on_pagey = dot_on_planey - planey * 512;
        dot_on_pagey = dot_on_planey & plane_mask;
        planey = planey & (ctrl->info.planeh - 1);
        //pagey = dot_on_pagey / (512 / ctrl->info.pagewh);
        pagey = dot_on_pagey >> page_shift;
        //chary = dot_on_pagey - pagey*(512 / ctrl->info.pagewh);
        chary = dot_on_pagey & page_mask;
        if (pagey < 0) pagey = ctrl->info.pagewh - 1 + pagey;
      }

      //mapx = (h + sx) / (512 * ctrl->info.planew);
      mapx = (h + sx) >> planew_shift;
      //int dot_on_planex = (h + sx) - mapx*(512 * ctrl->info.planew);
      dot_on_planex = (h + sx) - (mapx * (1<<planew_shift));
      mapx = mapx & 0x01;
      //planex = dot_on_planex / 512;
      planex = dot_on_planex >> plane_shift;
      //int dot_on_pagex = dot_on_planex - planex * 512;
      dot_on_pagex = dot_on_planex & plane_mask;
      planex = planex & (ctrl->info.planew - 1);
      //pagex = dot_on_pagex / (512 / ctrl->info.pagewh);
      pagex = dot_on_pagex >> page_shift;
      //charx = dot_on_pagex - pagex*(512 / ctrl->info.pagewh);
      charx = dot_on_pagex & page_mask;

      if (ctrl->info.PlaneAddr == 0) {
        exit(-1);
      }
      ctrl->info.PlaneAddr(&ctrl->info, ctrl->info.mapwh * mapy + mapx, ctrl->regs);
      if (Vdp2PatternAddrPos(ctrl, planex, pagex, planey, pagey) != 0) {
        //Only draw if there is a valid character pattern VRAM access for the current layer
        int charAddrBk = (((ctrl->info.charaddr >> 16)& 0xF) >> ((ctrl->regs->VRSIZE >> 15)&0x1)) >> 1;
        if (ctrl->info.char_bank[charAddrBk] == 1) {
          int x = h - charx;
          int y = v - chary;
          ctrl->info.draw_line =  y;
          if (delayed && (h == -ctrl->info.patternpixelwh)) continue;
          Vdp2DrawPatternPos(ctrl, x+delayed*8, y, 0, 0, ctrl->info.lineinc);
        }
      }
    }
    lineindex++;
  }

}

//////////////////////////////////////////////////////////////////////////////

static u32 FASTCALL DoNothing(UNUSED void *info, u32 pixel)
{
  return pixel;
}

//////////////////////////////////////////////////////////////////////////////

static u32 FASTCALL DoColorOffset(void *info, u32 pixel)
{
  return pixel;
}

//////////////////////////////////////////////////////////////////////////////

static INLINE void ReadVdp2ColorOffset(Vdp2 * regs, vdp2draw_struct *info, int mask)
{
  if (regs->CLOFEN & mask)
  {
    // color offset enable
    if (regs->CLOFSL & mask)
    {
      // color offset B
      info->cor = regs->COBR & 0xFF;
      if (regs->COBR & 0x100)
        info->cor |= 0xFFFFFF00;

      info->cog = regs->COBG & 0xFF;
      if (regs->COBG & 0x100)
        info->cog |= 0xFFFFFF00;

      info->cob = regs->COBB & 0xFF;
      if (regs->COBB & 0x100)
        info->cob |= 0xFFFFFF00;
    }
    else
    {
      // color offset A
      info->cor = regs->COAR & 0xFF;
      if (regs->COAR & 0x100)
        info->cor |= 0xFFFFFF00;

      info->cog = regs->COAG & 0xFF;
      if (regs->COAG & 0x100)
        info->cog |= 0xFFFFFF00;

      info->cob = regs->COAB & 0xFF;
      if (regs->COAB & 0x100)
        info->cob |= 0xFFFFFF00;
    }
    info->PostPixelFetchCalc = &DoColorOffset;
  }
  else { // color offset disable

    info->PostPixelFetchCalc = &DoNothing;
    info->cor = 0;
    info->cob = 0;
    info->cog = 0;

  }
}


static void Vdp2DrawBackScreen(Vdp2 *varVdp2Regs)
{
  u32 scrAddr;
  int dot;
  u32 * linebuf;
  vdp2draw_struct info = {0};

  static int line[512 * 4];

  if (varVdp2Regs->VRSIZE & 0x8000)
    scrAddr = (((varVdp2Regs->BKTAU & 0x7) << 16) | varVdp2Regs->BKTAL) * 2;
  else
    scrAddr = (((varVdp2Regs->BKTAU & 0x3) << 16) | varVdp2Regs->BKTAL) * 2;

#if defined(__ANDROID__) || defined(_OGLES3_) || defined(_OGLES31_) || defined(_OGL3_)
  if ((varVdp2Regs->BKTAU & 0x8000) != 0 ) {
    // per line background color
    u32* back_pixel_data = YglGetBackColorPointer();
    if (back_pixel_data != NULL) {
      int line_shift = 0;
      if (_Ygl->rheight > 256) {
        line_shift = 1;
      }
      else {
        line_shift = 0;
      }
      for (int i = 0; i < _Ygl->rheight; i++) {
        u8 r, g, b, a;
        ReadVdp2ColorOffset(&Vdp2Lines[i >> line_shift], &info, 0x20);
        dot = Vdp2RamReadWord(NULL, Vdp2Ram, (scrAddr + 2 * i));
        r = Y_MAX(((dot & 0x1F) << 3) + info.cor, 0);
        g = Y_MAX((((dot & 0x3E0) >> 5) << 3) + info.cog, 0);
        b = Y_MAX((((dot & 0x7C00) >> 10) << 3) + info.cob, 0);
        a = ((~varVdp2Regs->CCRLB & 0x1F00) >> 5)|NONE;
        *back_pixel_data++ = (a << 24) | ((b&0xFF) << 16) | ((g&0xFF) << 8) | (r&0xFF);
        if (i == _Ygl->rheight-1) {
          _Ygl->last_back_color[0] = (float)r/255.0;
          _Ygl->last_back_color[1] = (float)g/255.0;
          _Ygl->last_back_color[2] = (float)b/255.0;
          _Ygl->last_back_color[3] = 1.0;
        }
      }
      YglSetBackTextureColor(_Ygl->rheight);
    }
  }
  else {
    dot = Vdp2RamReadWord(NULL, Vdp2Ram, scrAddr);
    YglSetBackColor(
      (float)(((dot & 0x1F) << 3) + info.cor) / (float)(0xFF),
      (float)((((dot & 0x3E0) >> 5) << 3) + info.cog) / (float)(0xFF),
      (float)((((dot & 0x7C00) >> 10) << 3) + info.cob) / (float)(0xFF)
    );
  }
#else
  if (varVdp2Regs->BKTAU & 0x8000)
  {
    int y;

    for (y = 0; y < _Ygl->rheight; y++)
    {
      dot = Vdp2RamReadWord(NULL, Vdp2Ram, scrAddr);
      scrAddr += 2;

      lineColors[3 * y + 0] = (dot & 0x1F) << 3;
      lineColors[3 * y + 1] = (dot & 0x3E0) >> 2;
      lineColors[3 * y + 2] = (dot & 0x7C00) >> 7;
      line[4 * y + 0] = 0;
      line[4 * y + 1] = y;
      line[4 * y + 2] = _Ygl->rwidth;
      line[4 * y + 3] = y;
    }

    glColorPointer(3, GL_UNSIGNED_BYTE, 0, lineColors);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_INT, 0, line);
    glEnableClientState(GL_VERTEX_ARRAY);
    glDrawArrays(GL_LINES, 0, _Ygl->rheight * 2);
    glDisableClientState(GL_COLOR_ARRAY);
    glColor3ub(0xFF, 0xFF, 0xFF);
  }
  else
  {
    dot = Vdp2RamReadWord(NULL, Vdp2Ram, scrAddr);

    glColor3ub((dot & 0x1F) << 3, (dot & 0x3E0) >> 2, (dot & 0x7C00) >> 7);

    line[0] = 0;
    line[1] = 0;
    line[2] = _Ygl->rwidth;
    line[3] = 0;
    line[4] = _Ygl->rwidth;
    line[5] = _Ygl->rheight;
    line[6] = 0;
    line[7] = _Ygl->rheight;

    glVertexPointer(2, GL_INT, 0, line);
    glEnableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 8);
    glColor3ub(0xFF, 0xFF, 0xFF);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}
#endif
  }

//////////////////////////////////////////////////////////////////////////////
// 11.3 Line Color insertion
//  7.1 Line Color Screen
static void Vdp2DrawLineColorScreen(Vdp2 *varVdp2Regs)
{

  u32 cacheaddr = 0xFFFFFFFF;
  int inc = 0;
  int line_cnt = _Ygl->rheight;
  int i;
  u32 * line_pixel_data;
  u32 addr;

  if (varVdp2Regs->LNCLEN == 0) return;

  line_pixel_data = YglGetLineColorScreenPointer();
  if (line_pixel_data == NULL) {
    return;
  }

  if ((varVdp2Regs->LCTA.part.U & 0x8000)) {
    inc = 0x02; // color per line
  }
  else {
    inc = 0x00; // single color
  }

  u8 alpha = ((~varVdp2Regs->CCRLB & 0x1F) << 3) | NONE;

  addr = (varVdp2Regs->LCTA.all & 0x7FFFF)<<1;
  for (i = 0; i < line_cnt; i++) {
    u16 LineColorRamAdress = Vdp2RamReadWord(NULL, Vdp2Ram, addr);
    *(line_pixel_data) = Vdp2ColorRamGetLineColor(LineColorRamAdress, alpha);
    line_pixel_data++;
    addr += inc;
  }

  YglSetLineColorScreen(line_pixel_data, line_cnt);

}

//////////////////////////////////////////////////////////////////////////////

static int Vdp2CheckCharAccessPenalty(int char_access, int ptn_access) {
  if (_Ygl->rwidth >= 640) {
    //if (char_access < ptn_access) {
    //  return -1;
    //}
    if (ptn_access & 0x01) { // T0
      // T0-T2
      if ((char_access & 0x07) != 0) {
        if (char_access < ptn_access) {
          return -1;
        }
        return 0;
      }
    }

    if (ptn_access & 0x02) { // T1
      // T1-T3
      if ((char_access & 0x0E) != 0) {
        if (char_access < ptn_access) {
          return -1;
        }
        return 0;
      }
    }

    if (ptn_access & 0x04) { // T2
      // T0,T2,T3
      if ((char_access & 0x0D) != 0) {
        if (char_access < ptn_access) {
          return -1;
        }
        return 0;
      }
    }

    if (ptn_access & 0x08) { // T3
      // T0,T1,T3
      if ((char_access & 0xB) != 0) {
        if (char_access < ptn_access) {
          return -1;
        }
        return 0;
      }
    }
    return -1;
  }
  else {

    if (ptn_access & 0x01) { // T0
      // T0-T2, T4-T7
      if ((char_access & 0xF7) != 0) {
        return 0;
      }
    }

    if (ptn_access & 0x02) { // T1
      // T0-T3, T5-T7
      if ((char_access & 0xEF) != 0) {
        return 0;
      }
    }

    if (ptn_access & 0x04) { // T2
      // T0-T3, T6-T7
      if ((char_access & 0xCF) != 0) {
        return 0;
      }
    }

    if (ptn_access & 0x08) { // T3
      // T0-T3, T7
      if ((char_access & 0x8F) != 0) {
        return 0;
      }
    }

    if (ptn_access & 0x10) { // T4
      // T0-T3
      if ((char_access & 0x0F) != 0) {
        return 0;
      }
    }

    if (ptn_access & 0x20) { // T5
      // T1-T3
      if ((char_access & 0x0E) != 0) {
        return 0;
      }
    }

    if (ptn_access & 0x40) { // T6
      // T2,T3
      if ((char_access & 0x0C) != 0) {
        return 0;
      }
    }

    if (ptn_access & 0x80) { // T7
      // T3
      if ((char_access & 0x08) != 0) {
        return 0;
      }
    }
    return -1;
  }
  return 0;
}

//////////////////////////////////////////////////////////////////////////////

static void Vdp2DrawRBG1_part(RBGDrawInfo *rbg)
{
  YglTexture texture;
  YglCache tmpc;
  vdp2draw_struct* info = &rbg->ctrl.info;
  int shift = 0;
  if (_Ygl->interlace == DOUBLE_INTERLACE) shift = 1;

  info->dst = 0;
  info->idScreen = RBG1;
  info->cor = 0;
  info->cog = 0;
  info->cob = 0;
  info->specialcolorfunction = 0;

  int i;
  info->enable = 0;

  info->cellh = 256 << shift;

// RBG1 mode
  info->enable = ((rbg->ctrl.regs->BGON & 0x20)!=0);
  // RBG1 shall not work without RGB0 but it looks like the HW is able to... MechWarrior 2 - 31st Century Combat - Arcade Combat Edition is using this capability...
  //if (!(varVdp2Regs->BGON & 0x10)) info->enable = 0; //When both R0ON and R1ON are 1, the normal scroll screen can no longer be displayed vdp2 pdf, section 4.1 Screen Display Control

  if (!info->enable) {
   pushRBG(rbg);
   return;
  }
  for (int i=info->startLine; i<info->endLine; i++) {
    info->display[i] = info->enable;
    // Color calculation ratio
    rbg->alpha[i] = (~Vdp2Lines[i].CCRNA & 0x1F)<<3;
    info->alpha_per_line[i] = rbg->alpha[i];
  }

    // Read in Parameter B
    Vdp2ReadRotationTable(1, &rbg->paraB, rbg->ctrl.regs, Vdp2Ram);

    if ((info->isbitmap = rbg->ctrl.regs->CHCTLA & 0x2) != 0)
    {
      // Bitmap Mode

      ReadBitmapSize(info, rbg->ctrl.regs->CHCTLA >> 2, 0x3);
      if (shift) info->cellh *= 2;

      info->charaddr = (rbg->ctrl.regs->MPOFR & 0x70) * 0x2000;
      info->paladdr = (rbg->ctrl.regs->BMPNA & 0x7) << 4;
      info->flipfunction = 0;
      info->specialfunction = 0;
    }
    else
    {
      // Tile Mode
      info->mapwh = 4;
      ReadPlaneSize(info, rbg->ctrl.regs->PLSZ >> 12);
      ReadPatternData(info, rbg->ctrl.regs->PNCN0, rbg->ctrl.regs->CHCTLA & 0x1);

      rbg->paraB.ShiftPaneX = 8 + info->planew;
      rbg->paraB.ShiftPaneY = 8 + info->planeh;
      rbg->paraB.MskH = (8 * 64 * info->planew) - 1;
      rbg->paraB.MskV = (8 * 64 * info->planeh) - 1;
      rbg->paraB.MaxH = 8 * 64 * info->planew * 4;
      rbg->paraB.MaxV = 8 * 64 * info->planeh * 4;

    }

    info->rotatenum = 1;
    //rbg->paraB.PlaneAddr = (void FASTCALL(*)(void *, int, Vdp2*))&Vdp2ParameterBPlaneAddr;
    rbg->paraB.coefenab = rbg->ctrl.regs->KTCTL & 0x100;
    rbg->paraB.charaddr = (rbg->ctrl.regs->MPOFR & 0x70) * 0x2000;
    ReadPlaneSizeR(&rbg->paraB, rbg->ctrl.regs->PLSZ >> 12);
    for (i = 0; i < 16; i++)
    {
      Vdp2ParameterBPlaneAddr(info, i, rbg->ctrl.regs);
      rbg->paraB.PlaneAddrv[i] = info->addr;
    }

  ReadMosaicData(info, 0x1, rbg->ctrl.regs);

  info->transparencyenable = !(rbg->ctrl.regs->BGON & 0x100);
  info->specialprimode = rbg->ctrl.regs->SFPRMD & 0x3;
  info->specialcolormode = rbg->ctrl.regs->SFCCMD & 0x3;
  if (rbg->ctrl.regs->SFSEL & 0x1)
    info->specialcode = rbg->ctrl.regs->SFCODE >> 8;
  else
    info->specialcode = rbg->ctrl.regs->SFCODE & 0xFF;

  info->colornumber = (rbg->ctrl.regs->CHCTLA & 0x70) >> 4;
  int dest_alpha = ((rbg->ctrl.regs->CCCTL >> 9) & 0x01);

  info->coloroffset = (rbg->ctrl.regs->CRAOFA & 0x7) << 8;

  info->linecheck_mask = 0x01;
  info->priority = rbg->ctrl.regs->PRINA & 0x7;

  LOG_AREA("RGB1 prio = %d\n", info->priority);

  if (info->priority == 0) {
    pushRBG(rbg);
    return;
  }

  ReadLineScrollData(info, rbg->ctrl.regs->SCRCTL & 0xFF, rbg->ctrl.regs->LSTA0.all);
  info->lineinfo = lineNBG0;
  Vdp2GenLineinfo(info);
  if (rbg->ctrl.regs->SCRCTL & 1)
  {
    info->isverticalscroll = 1;
    info->verticalscrolltbl = (rbg->ctrl.regs->VCSTA.all & 0x7FFFE) << 1;
    if (rbg->ctrl.regs->SCRCTL & 0x100)
      info->verticalscrollinc = 8;
    else
      info->verticalscrollinc = 4;
  }
  else
    info->isverticalscroll = 0;

  // RBG1 draw
  Vdp2DrawRotation(rbg);
}

static int sameVDP2RegRBG0(Vdp2 *a, Vdp2 *b)
{
  if ((a->BGON & 0x1010) != (b->BGON & 0x1010)) return 0;
  if ((a->PRIR & 0x7) != (b->PRIR & 0x7)) return 0;
//  if ((a->CCCTL & 0xFF10) != (b->CCCTL & 0xFF10)) return 0;
//  if ((a->SFPRMD & 0x300) != (b->SFPRMD & 0x300)) return 0;
//  if ((a->CHCTLB & 0x7700) != (b->CHCTLB & 0x7700)) return 0;
//  if ((a->WCTLC & 0xFF) != (b->WCTLC & 0xFF)) return 0;
  if ((a->RPTA.all) != (b->RPTA.all)) return 0;
//  if ((a->VRSIZE & 0x8000) != (b->VRSIZE & 0x8000)) return 0;
//  if ((a->RAMCTL & 0x80FF) != (b->RAMCTL & 0x80FF)) return 0;
//  if ((a->KTCTL & 0xFFFF) != (b->KTCTL & 0xFFFF)) return 0;
//  if ((a->PLSZ & 0xFF00) != (b->PLSZ & 0xFF00)) return 0;
//  if ((a->KTAOF & 0x707) != (b->KTAOF & 0x707)) return 0;
//  if ((a->MPOFR & 0x77) != (b->MPOFR & 0x77)) return 0;
  if ((a->RPMD & 0x3) != (b->RPMD & 0x3)) return 0;
//  if ((a->WCTLD & 0xF) != (b->WCTLD & 0xF)) return 0;
//  if ((a->BMPNB & 0x7) != (b->BMPNB & 0x7)) return 0;
//  if ((a->PNCR & 0xFFFF) != (b->PNCR & 0xFFFF)) return 0;
//  if ((a->MZCTL & 0xFF10) != (b->MZCTL & 0xFF10)) return 0;
//  if ((a->SFCCMD &0x300) != (b->SFCCMD &0x300)) return 0;
//  if ((a->SFSEL & 0x10) != (b->SFSEL & 0x10)) return 0;
//  if ((a->SFCODE & 0xFFFF) != (b->SFCODE & 0xFFFF)) return 0;
//  if ((a->LNCLEN & 0x10) != (b->LNCLEN & 0x10)) return 0;
//  if ((a->LCTA.all) != (b->LCTA.all)) return 0;
//  if ((a->CRAOFB & 0x7) != (b->CRAOFB & 0x7)) return 0;
//  if ((a->CLOFSL & 0x10) != (b->CLOFSL & 0x10)) return 0;
//  if ((a->COBR & 0x1FF) != (b->COBR & 0x1FF)) return 0;
//  if ((a->COBG & 0x1FF) != (b->COBG & 0x1FF)) return 0;
//  if ((a->COBB & 0x1FF) != (b->COBB & 0x1FF)) return 0;
//  if ((a->COAR & 0x1FF) != (b->COAR & 0x1FF)) return 0;
//  if ((a->COAG & 0x1FF) != (b->COAG & 0x1FF)) return 0;
//  if ((a->COAB & 0x1FF) != (b->COAB & 0x1FF)) return 0;
  return 1;
}

static int sameVDP2RegRBG1(Vdp2 *a, Vdp2 *b)
{
  if ((a->BGON & 0x130) != (b->BGON & 0x130)) return 0;
  if ((a->PRINA & 0x7) != (b->PRINA & 0x7)) return 0;
//  if ((a->CCCTL & 0xFF01) != (b->CCCTL & 0xFF01)) return 0;
//  if ((a->BMPNA & 0x7) != (b->BMPNA & 0x7)) return 0;
//  if ((a->MPOFR & 0x77) != (b->MPOFR & 0x77)) return 0;
//  if ((a->VRSIZE & 0x8000) != (b->VRSIZE & 0x8000)) return 0;
//  if ((a->RAMCTL & 0x80FF) != (b->RAMCTL & 0x80FF)) return 0;
//  if ((a->KTCTL & 0xFFFF) != (b->KTCTL & 0xFFFF)) return 0;
//  if ((a->PLSZ & 0xFF00) != (b->PLSZ & 0xFF00)) return 0;
//  if ((a->SFPRMD & 0x3) != (b->SFPRMD & 0x3)) return 0;
//  if ((a->CHCTLA & 0x7F) != (b->CHCTLA & 0x7F)) return 0;
//  if ((a->MZCTL & 0xFF01) != (b->MZCTL & 0xFF01)) return 0;
 if ((a->CCRNA &0x1F) != (b->CCRNA &0x1F)) return 0;
 if ((a->SFCCMD &0x3) != (b->SFCCMD &0x3)) return 0;
//  if ((a->SFSEL & 0x1) != (b->SFSEL & 0x1)) return 0;
//  if ((a->SFCODE & 0xFFFF) != (b->SFCODE & 0xFFFF)) return 0;
//  if ((a->LNCLEN & 0x1) != (b->LNCLEN & 0x1)) return 0;
//  if ((a->CRAOFA & 0x7) != (b->CRAOFA & 0x7)) return 0;
//  if ((a->CLOFSL & 0x1) != (b->CLOFSL & 0x1)) return 0;
//  if ((a->WCTLA & 0xFF) != (b->WCTLA & 0xFF)) return 0;
//  if ((a->PNCN0 & 0xFFFF) != (b->PNCN0 & 0xFFFF)) return 0;
//  if ((a->SCRCTL & 0x3F) != (b->SCRCTL & 0x3F)) return 0;
//  if ((a->LSTA0.all) != (b->LSTA0.all)) return 0;
//  if ((a->VCSTA.all) != (b->VCSTA.all)) return 0;
  if ((a->RPTA.all) != (b->RPTA.all)) return 0;
//  if ((a->WCTLD & 0xF) != (b->WCTLD & 0xF)) return 0;
//  if ((a->LCTA.all) != (b->LCTA.all)) return 0;
//  if ((a->COBR & 0x1FF) != (b->COBR & 0x1FF)) return 0;
//  if ((a->COBG & 0x1FF) != (b->COBG & 0x1FF)) return 0;
//  if ((a->COBB & 0x1FF) != (b->COBB & 0x1FF)) return 0;
//  if ((a->COAR & 0x1FF) != (b->COAR & 0x1FF)) return 0;
//  if ((a->COAG & 0x1FF) != (b->COAG & 0x1FF)) return 0;
//  if ((a->COAB & 0x1FF) != (b->COAB & 0x1FF)) return 0;
  return 1;
}

static int sameVDP2Reg(int id, Vdp2 *a, Vdp2 *b)
{
  switch (id) {
    case RBG0: return sameVDP2RegRBG0(a, b);
    case RBG1: return sameVDP2RegRBG1(a, b);
    default:
    break;
  }
  return 1;
}

static void Vdp2DrawRBG1()
{
  int nbZone = 1;
  int lastLine = 0;
  int line;
  int max = (yabsys.VBlankLineCount >= 270)?270:yabsys.VBlankLineCount;
  RBGDrawInfo *rbg = NULL;
  for (line = 2; line<max; line++) {
    if (!sameVDP2Reg(RBG1, &Vdp2Lines[line-1], &Vdp2Lines[line])) {
      rbg = popRBG();
      rbg->rbg_type = 0x04;
      rbg->ctrl.info.startLine = lastLine;
      rbg->ctrl.info.endLine = line;
      rbg->ctrl.regs = &Vdp2Lines[rbg->ctrl.info.startLine];
      lastLine = line;
      LOG_AREA("RBG1 Draw from %d to %d %x\n", rbg->ctrl.info.startLine, rbg->ctrl.info.endLine, rbg->ctrl.regs->BGON);
      Vdp2DrawRBG1_part(rbg);
    }
  }
  rbg = popRBG();
  rbg->rbg_type = 0x04;
  rbg->ctrl.info.startLine = lastLine;
  rbg->ctrl.info.endLine = line;
  rbg->ctrl.regs = &Vdp2Lines[rbg->ctrl.info.startLine];
  LOG_AREA("RBG1 Draw from %d to %d %x\n", rbg->ctrl.info.startLine, rbg->ctrl.info.endLine, rbg->ctrl.regs->BGON);
  Vdp2DrawRBG1_part(rbg);
}

static int isEnabled(int id, Vdp2* varVdp2Regs) {
  int display = 1;
  switch(id) {
    case NBG0:
      display = ((varVdp2Regs->BGON & 0x1)!=0);
      if ((varVdp2Regs->BGON & 0x20) && (varVdp2Regs->BGON & 0x10)) display = 0; //When both R0ON and R1ON are 1, the normal scroll screen can no longer be displayed vdp2 pdf, section 4.1 Screen Display Control
      break;
    case NBG1:
      display = ((varVdp2Regs->BGON & 0x2)!=0);
      if ((varVdp2Regs->BGON & 0x20) && (varVdp2Regs->BGON & 0x10)) display = 0; //When both R0ON and R1ON are 1, the normal scroll screen can no longer be displayed vdp2 pdf, section 4.1 Screen Display Control
      break;
    case NBG2:
      display = ((varVdp2Regs->BGON & 0x4)!=0);
      if ((varVdp2Regs->BGON & 0x20) && (varVdp2Regs->BGON & 0x10)) display = 0; //When both R0ON and R1ON are 1, the normal scroll screen can no longer be displayed vdp2 pdf, section 4.1 Screen Display Control
      break;
    case NBG3:
      display = ((varVdp2Regs->BGON & 0x8)!=0);
      if ((varVdp2Regs->BGON & 0x20) && (varVdp2Regs->BGON & 0x10)) display = 0; //When both R0ON and R1ON are 1, the normal scroll screen can no longer be displayed vdp2 pdf, section 4.1 Screen Display Control
      break;
    case RBG0:
      display = ((varVdp2Regs->BGON & 0x10)!=0);
      break;
    case RBG1:
      display = ((varVdp2Regs->BGON & 0x20)!=0);
      break;
    default:
      display = 1;
  }
  return display;
}

static pixel_t *VIDCSGetVdp2ScreenExtract(u32 screen, int * w, int * h)
{
  if ((screen >= NBG0) && (screen <= NBG3)) {
    pixel_t* pixels = (pixel_t*)malloc(_Ygl->rwidth*_Ygl->rheight * sizeof(pixel_t));
    *w = _Ygl->rwidth;
    *h = _Ygl->rheight;
    glBindTexture(GL_TEXTURE_2D, _Ygl->screen_fbotex[screen]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return pixels;
  }
  if ((screen == RBG0) || (screen == RBG1))
  {
    pixel_t* pixels = (pixel_t*)malloc(_Ygl->width*_Ygl->height * sizeof(pixel_t));
    *w = _Ygl->width;
    *h = _Ygl->height;
    glBindTexture(GL_TEXTURE_2D, _Ygl->rbg_compute_fbotex[screen-RBG0]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return pixels;
  }
  if (screen == SPRITE)
  {
    pixel_t* pixels = (pixel_t*)malloc(_Ygl->vdp1width*_Ygl->vdp1height * sizeof(pixel_t));
    *w = _Ygl->vdp1width;
    *h = _Ygl->vdp1height;
    glBindTexture(GL_TEXTURE_2D, GetCSVDP1fb(0));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int i = 0; i<_Ygl->vdp1width*_Ygl->vdp1height; i++) {
      if (pixels[i] != 0) pixels[i] |= 0xFF000000;
    }
    return pixels;
  }
  if (screen == 0xFF)
  {
    //Get full framebuffer
    pixel_t* pixels = (pixel_t*)malloc(_Ygl->width*_Ygl->height * sizeof(pixel_t));
    *w = _Ygl->width;
    *h = _Ygl->height;
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
    glReadPixels(0,0,_Ygl->width,_Ygl->height,GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return pixels;
  }
}

#endif


