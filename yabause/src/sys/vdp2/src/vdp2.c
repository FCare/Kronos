
/*  Copyright 2003-2005 Guillaume Duhamel
    Copyright 2004-2007 Theo Berkau
    Copyright 2015 Shinya Miyamoto(devmiyax)

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

/*! \file vdp2.c
    \brief VDP2 emulation functions
*/

#include <stdlib.h>
#include "vdp2.h"
#include "debug.h"
#include "peripheral.h"
#include "scu.h"
#include "sh2core.h"
#include "smpc.h"
#include "vdp1.h"
#include "yabause.h"
#include "movie.h"
#include "osdcore.h"
#include "threads.h"
#include "yui.h"
#include "ygl.h"

u8 * Vdp2Ram;
u8 * Vdp2ColorRam;
Vdp2 * Vdp2Regs;
Vdp2Internal_struct Vdp2Internal;
Vdp2External_struct Vdp2External;

int addrToUpdate[0x1000];
int nbAddrToUpdate = 0;

static u8 AC_VRAM[4][8] = {0}; //4 banks, 8 timings

extern void waitVdp2DrawScreensEnd(int sync);

int isSkipped = 0;

u8 Vdp2ColorRamUpdated[512] = {0};
u8 Vdp2ColorRamToSync[512] = {0};
u8 Vdp2Ram_Updated = 0;

struct CellScrollData cell_scroll_data[270];
Vdp2 Vdp2Lines[270];

int vdp2_is_odd_frame = 0;

int g_frame_count = 0;

//#define LOG yprintf

//////////////////////////////////////////////////////////////////////////////

u8 Vdp2RamIsUpdated(void)
{
  return Vdp2Ram_Updated;
}

static void vdp2RamAccessCheck(SH2_struct *context, u32 addr){
  int BlockedAccess = 1;

  if (context == NULL) return;

  int bank = Vdp2GetBank(Vdp2Regs, addr);

  for (int i = 0; i<8; i++) {
    if(Vdp2External.AC_VRAM[bank][i] == 0xE) BlockedAccess = 0;
  }

  if ((context != NULL) && (yabsys.LineCount < yabsys.VBlankLineCount) && (Vdp2Regs->TVSTAT & 0x0004) == 0) {
    // Visible area, cpu shall have a valid time slot, otherwise it is blocked
    if (BlockedAccess) {
      SH2SetCPUConcurrency(context, VDP2_RAM_A0_LOCK << bank);
    }
  }
}

u8 FASTCALL Vdp2RamReadByte(SH2_struct *context, u8* mem, u32 addr) {
  if (Vdp2Regs->VRSIZE & 0x8000)
    addr &= 0xEFFFF;
  else
    addr &= 0x7FFFF;

   vdp2RamAccessCheck(context, addr);

   return T1ReadByte(mem, addr);
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL Vdp2RamReadWord(SH2_struct *context, u8* mem, u32 addr) {
  if (Vdp2Regs->VRSIZE & 0x8000)
    addr &= 0xEFFFF;
  else
    addr &= 0x7FFFF;

    vdp2RamAccessCheck(context, addr);

   return T1ReadWord(mem, addr);
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL Vdp2RamReadLong(SH2_struct *context, u8* mem, u32 addr) {
  if (Vdp2Regs->VRSIZE & 0x8000)
    addr &= 0xEFFFF;
  else
    addr &= 0x7FFFF;

   vdp2RamAccessCheck(context, addr);

   return T1ReadLong(mem, addr);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp2RamWriteByte(SH2_struct *context, u8* mem, u32 addr, u8 val) {
  if (Vdp2Regs->VRSIZE & 0x8000)
    addr &= 0xEFFFF;
  else
    addr &= 0x7FFFF;

    vdp2RamAccessCheck(context, addr);
   Vdp2Ram_Updated = 1;
   T1WriteByte(mem, addr, val);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp2RamWriteWord(SH2_struct *context, u8* mem, u32 addr, u16 val) {
  int BlockedAccess = 0;
  int A1BlockedAccess = 0;
  int B0BlockedAccess = 0;
  int B1BlockedAccess = 0;
  if (Vdp2Regs->VRSIZE & 0x8000)
    addr &= 0xEFFFF;
  else
    addr &= 0x7FFFF;

   vdp2RamAccessCheck(context, addr);
   Vdp2Ram_Updated = 1;
   T1WriteWord(mem, addr, val);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp2RamWriteLong(SH2_struct *context, u8* mem, u32 addr, u32 val) {
  if (Vdp2Regs->VRSIZE & 0x8000)
    addr &= 0xEFFFF;
  else
    addr &= 0x7FFFF;

    vdp2RamAccessCheck(context, addr);
    Vdp2Ram_Updated = 1;
   T1WriteLong(mem, addr, val);
}

//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL Vdp2ColorRamReadByte(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0xFFF;
   return T2ReadByte(mem, addr);
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL Vdp2ColorRamReadWord(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0xFFF;
   return T2ReadWord(mem, addr);
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL Vdp2ColorRamReadLong(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0xFFF;
   return T2ReadLong(mem, addr);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp2ColorRamWriteByte(SH2_struct *context, u8* mem, u32 addr, u8 val) {
   addr &= 0xFFF;
   // printf("[VDP2] Update Coloram Byte %08X:%02X", addr, val);
   if (val != T2ReadByte(mem, addr)) {
     T2WriteByte(mem, addr, val);
     //A EXTRAIRE
#if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)
     addrToUpdate[addr] = 1;
     nbAddrToUpdate = 1;
#endif
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp2ColorRamWriteWord(SH2_struct *context, u8* mem, u32 addr, u16 val) {
   addr &= 0xFFF;
   // printf("[VDP2] Update Coloram [mode %d] Word %08X:%04X\n", Vdp2Internal.ColorMode, addr, val);
   if (val != T2ReadWord(mem, addr)) {
     T2WriteWord(mem, addr, val);

#if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)
    addrToUpdate[addr] = 1;
    addrToUpdate[addr+1] = 1;
    nbAddrToUpdate = 1;
#endif
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp2ColorRamWriteLong(SH2_struct *context, u8* mem, u32 addr, u32 val) {
   addr &= 0xFFF;
   // printf("[VDP2] Update Coloram Long %08X:%08X\n", addr, val);
   T2WriteLong(mem, addr, val);
#if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)
   if (Vdp2Internal.ColorMode == 2) {
     addrToUpdate[addr] = 1;
     addrToUpdate[addr+1] = 1;
     nbAddrToUpdate = 1;
   }
   else {
     addrToUpdate[addr] = 1;
     addrToUpdate[addr+1] = 1;
     addrToUpdate[addr+2] = 1;
     addrToUpdate[addr+3] = 1;
     nbAddrToUpdate = 1;
   }
#endif
}

//////////////////////////////////////////////////////////////////////////////

int Vdp2Init(void) {
   if ((Vdp2Regs = (Vdp2 *) calloc(1, sizeof(Vdp2))) == NULL)
      return -1;

   if ((Vdp2Ram = T1MemoryInit(0x100000)) == NULL)
      return -1;

   if ((Vdp2ColorRam = T2MemoryInit(0x1000)) == NULL)
      return -1;

   Vdp2Reset();

   memset(Vdp2ColorRam, 0xFF, 0x1000);
#if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)
   for (int i = 0; i < 0x1000; i ++) {
     YglOnUpdateColorRamWord(i);
   }
#endif

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void Vdp2DeInit(void) {
   if (Vdp2Regs)
      free(Vdp2Regs);
   Vdp2Regs = NULL;

   if (Vdp2Ram)
      T1MemoryDeInit(Vdp2Ram);
   Vdp2Ram = NULL;

   if (Vdp2ColorRam)
      T2MemoryDeInit(Vdp2ColorRam);
   Vdp2ColorRam = NULL;


}

//////////////////////////////////////////////////////////////////////////////

static unsigned long nextFrameTime = 0;

void Vdp2Reset(void) {
   Vdp2Regs->TVMD = 0x0000;
   Vdp2Regs->EXTEN = 0x0000;
   Vdp2Regs->TVSTAT = Vdp2Regs->TVSTAT & 0x1;
   Vdp2Regs->VRSIZE = 0x0000; // fix me(version should be set)
   Vdp2Regs->RAMCTL = 0x0000;
   Vdp2Regs->BGON = 0x0000;
   Vdp2Regs->CHCTLA = 0x0000;
   Vdp2Regs->CHCTLB = 0x0000;
   Vdp2Regs->BMPNA = 0x0000;
   Vdp2Regs->MPOFN = 0x0000;
   Vdp2Regs->MPABN2 = 0x0000;
   Vdp2Regs->MPCDN2 = 0x0000;
   Vdp2Regs->SCXIN0 = 0x0000;
   Vdp2Regs->SCXDN0 = 0x0000;
   Vdp2Regs->SCYIN0 = 0x0000;
   Vdp2Regs->SCYDN0 = 0x0000;
   Vdp2Regs->ZMXN0.all = 0x00000000;
   Vdp2Regs->ZMYN0.all = 0x00000000;
   Vdp2Regs->SCXIN1 = 0x0000;
   Vdp2Regs->SCXDN1 = 0x0000;
   Vdp2Regs->SCYIN1 = 0x0000;
   Vdp2Regs->SCYDN1 = 0x0000;
   Vdp2Regs->ZMXN1.all = 0x00000000;
   Vdp2Regs->ZMYN1.all = 0x00000000;
   Vdp2Regs->SCXN2 = 0x0000;
   Vdp2Regs->SCYN2 = 0x0000;
   Vdp2Regs->SCXN3 = 0x0000;
   Vdp2Regs->SCYN3 = 0x0000;
   Vdp2Regs->ZMCTL = 0x0000;
   Vdp2Regs->SCRCTL = 0x0000;
   Vdp2Regs->VCSTA.all = 0x00000000;
   Vdp2Regs->BKTAU = 0x0000;
   Vdp2Regs->BKTAL = 0x0000;
   Vdp2Regs->RPMD = 0x0000;
   Vdp2Regs->RPRCTL = 0x0000;
   Vdp2Regs->KTCTL = 0x0000;
   Vdp2Regs->KTAOF = 0x0000;
   Vdp2Regs->OVPNRA = 0x0000;
   Vdp2Regs->OVPNRB = 0x0000;
   Vdp2Regs->WPSX0 = 0x0000;
   Vdp2Regs->WPSY0 = 0x0000;
   Vdp2Regs->WPEX0 = 0x0000;
   Vdp2Regs->WPEY0 = 0x0000;
   Vdp2Regs->WPSX1 = 0x0000;
   Vdp2Regs->WPSY1 = 0x0000;
   Vdp2Regs->WPEX1 = 0x0000;
   Vdp2Regs->WPEY1 = 0x0000;
   Vdp2Regs->WCTLA = 0x0000;
   Vdp2Regs->WCTLB = 0x0000;
   Vdp2Regs->WCTLC = 0x0000;
   Vdp2Regs->WCTLD = 0x0000;
   Vdp2Regs->SPCTL = 0x0000;
   Vdp2Regs->SDCTL = 0x0000;
   Vdp2Regs->CRAOFA = 0x0000;
   Vdp2Regs->CRAOFB = 0x0000;
   Vdp2Regs->LNCLEN = 0x0000;
   Vdp2Regs->SFPRMD = 0x0000;
   Vdp2Regs->CCCTL = 0x0000;
   Vdp2Regs->SFCCMD = 0x0000;
   Vdp2Regs->PRISA = 0x0000;
   Vdp2Regs->PRISB = 0x0000;
   Vdp2Regs->PRISC = 0x0000;
   Vdp2Regs->PRISD = 0x0000;
   Vdp2Regs->PRINA = 0x0000;
   Vdp2Regs->PRINB = 0x0000;
   Vdp2Regs->PRIR = 0x0000;
   Vdp2Regs->CCRNA = 0x0000;
   Vdp2Regs->CCRNB = 0x0000;
   Vdp2Regs->CLOFEN = 0x0000;
   Vdp2Regs->CLOFSL = 0x0000;
   Vdp2Regs->COAR = 0x0000;
   Vdp2Regs->COAG = 0x0000;
   Vdp2Regs->COAB = 0x0000;
   Vdp2Regs->COBR = 0x0000;
   Vdp2Regs->COBG = 0x0000;
   Vdp2Regs->COBB = 0x0000;

   yabsys.VBlankLineCount = 225;
   Vdp2Internal.ColorMode = 0;

   Vdp2External.disptoggle = 0xFF;

   nextFrameTime = 0;

}

//////////////////////////////////////////////////////////////////////////////

static int checkFrameSkip(void) {
#if 0
  int ret = 0;
  if (isAutoFrameSkip() != 0) return ret;
  unsigned long now = YabauseGetTicks();
  if (nextFrameTime == 0) nextFrameTime = YabauseGetTicks();
  if(nextFrameTime < now) ret = 1;
  return ret;
#endif
  return !(yabsys.frame_count % (yabsys.skipframe+1) == 0);
}

void VDP2genVRamCyclePattern() {
  int i = 0;

  Vdp2External.AC_VRAM[0][0] = (Vdp2Regs->CYCA0L >> 12) & 0x0F;
  Vdp2External.AC_VRAM[0][1] = (Vdp2Regs->CYCA0L >> 8) & 0x0F;
  Vdp2External.AC_VRAM[0][2] = (Vdp2Regs->CYCA0L >> 4) & 0x0F;
  Vdp2External.AC_VRAM[0][3] = (Vdp2Regs->CYCA0L >> 0) & 0x0F;
  Vdp2External.AC_VRAM[0][4] = (Vdp2Regs->CYCA0U >> 12) & 0x0F;
  Vdp2External.AC_VRAM[0][5] = (Vdp2Regs->CYCA0U >> 8) & 0x0F;
  Vdp2External.AC_VRAM[0][6] = (Vdp2Regs->CYCA0U >> 4) & 0x0F;
  Vdp2External.AC_VRAM[0][7] = (Vdp2Regs->CYCA0U >> 0) & 0x0F;

  if (Vdp2Regs->RAMCTL & 0x100) {
    int fcnt = 0;
    Vdp2External.AC_VRAM[1][0] = (Vdp2Regs->CYCA1L >> 12) & 0x0F;
    Vdp2External.AC_VRAM[1][1] = (Vdp2Regs->CYCA1L >> 8) & 0x0F;
    Vdp2External.AC_VRAM[1][2] = (Vdp2Regs->CYCA1L >> 4) & 0x0F;
    Vdp2External.AC_VRAM[1][3] = (Vdp2Regs->CYCA1L >> 0) & 0x0F;
    Vdp2External.AC_VRAM[1][4] = (Vdp2Regs->CYCA1U >> 12) & 0x0F;
    Vdp2External.AC_VRAM[1][5] = (Vdp2Regs->CYCA1U >> 8) & 0x0F;
    Vdp2External.AC_VRAM[1][6] = (Vdp2Regs->CYCA1U >> 4) & 0x0F;
    Vdp2External.AC_VRAM[1][7] = (Vdp2Regs->CYCA1U >> 0) & 0x0F;

  }
  else {
    Vdp2External.AC_VRAM[1][0] = Vdp2External.AC_VRAM[0][0];
    Vdp2External.AC_VRAM[1][1] = Vdp2External.AC_VRAM[0][1];
    Vdp2External.AC_VRAM[1][2] = Vdp2External.AC_VRAM[0][2];
    Vdp2External.AC_VRAM[1][3] = Vdp2External.AC_VRAM[0][3];
    Vdp2External.AC_VRAM[1][4] = Vdp2External.AC_VRAM[0][4];
    Vdp2External.AC_VRAM[1][5] = Vdp2External.AC_VRAM[0][5];
    Vdp2External.AC_VRAM[1][6] = Vdp2External.AC_VRAM[0][6];
    Vdp2External.AC_VRAM[1][7] = Vdp2External.AC_VRAM[0][7];
  }

  Vdp2External.AC_VRAM[2][0] = (Vdp2Regs->CYCB0L >> 12) & 0x0F;
  Vdp2External.AC_VRAM[2][1] = (Vdp2Regs->CYCB0L >> 8) & 0x0F;
  Vdp2External.AC_VRAM[2][2] = (Vdp2Regs->CYCB0L >> 4) & 0x0F;
  Vdp2External.AC_VRAM[2][3] = (Vdp2Regs->CYCB0L >> 0) & 0x0F;
  Vdp2External.AC_VRAM[2][4] = (Vdp2Regs->CYCB0U >> 12) & 0x0F;
  Vdp2External.AC_VRAM[2][5] = (Vdp2Regs->CYCB0U >> 8) & 0x0F;
  Vdp2External.AC_VRAM[2][6] = (Vdp2Regs->CYCB0U >> 4) & 0x0F;
  Vdp2External.AC_VRAM[2][7] = (Vdp2Regs->CYCB0U >> 0) & 0x0F;

  if (Vdp2Regs->RAMCTL & 0x200) {
    int fcnt = 0;
    Vdp2External.AC_VRAM[3][0] = (Vdp2Regs->CYCB1L >> 12) & 0x0F;
    Vdp2External.AC_VRAM[3][1] = (Vdp2Regs->CYCB1L >> 8) & 0x0F;
    Vdp2External.AC_VRAM[3][2] = (Vdp2Regs->CYCB1L >> 4) & 0x0F;
    Vdp2External.AC_VRAM[3][3] = (Vdp2Regs->CYCB1L >> 0) & 0x0F;
    Vdp2External.AC_VRAM[3][4] = (Vdp2Regs->CYCB1U >> 12) & 0x0F;
    Vdp2External.AC_VRAM[3][5] = (Vdp2Regs->CYCB1U >> 8) & 0x0F;
    Vdp2External.AC_VRAM[3][6] = (Vdp2Regs->CYCB1U >> 4) & 0x0F;
    Vdp2External.AC_VRAM[3][7] = (Vdp2Regs->CYCB1U >> 0) & 0x0F;
  }
  else {
    Vdp2External.AC_VRAM[3][0] = Vdp2External.AC_VRAM[2][0];
    Vdp2External.AC_VRAM[3][1] = Vdp2External.AC_VRAM[2][1];
    Vdp2External.AC_VRAM[3][2] = Vdp2External.AC_VRAM[2][2];
    Vdp2External.AC_VRAM[3][3] = Vdp2External.AC_VRAM[2][3];
    Vdp2External.AC_VRAM[3][4] = Vdp2External.AC_VRAM[2][4];
    Vdp2External.AC_VRAM[3][5] = Vdp2External.AC_VRAM[2][5];
    Vdp2External.AC_VRAM[3][6] = Vdp2External.AC_VRAM[2][6];
    Vdp2External.AC_VRAM[3][7] = Vdp2External.AC_VRAM[2][7];
  }
  //unblock A0
  for (int i = 0; i<8; i++) {
    if(Vdp2External.AC_VRAM[0][i] == 0xE) {
      SH2ClearCPUConcurrency(MSH2, VDP2_RAM_A0_LOCK);
      SH2ClearCPUConcurrency(SSH2, VDP2_RAM_A0_LOCK);;
    }
  }
  //unblock A1
  for (int i = 0; i<8; i++) {
    if(Vdp2External.AC_VRAM[1][i] == 0xE) {
      SH2ClearCPUConcurrency(MSH2, VDP2_RAM_A1_LOCK);
      SH2ClearCPUConcurrency(SSH2, VDP2_RAM_A1_LOCK);;
    }
  }
  //unblock B0
  for (int i = 0; i<8; i++) {
    if(Vdp2External.AC_VRAM[2][i] == 0xE) {
      SH2ClearCPUConcurrency(MSH2, VDP2_RAM_B0_LOCK);
      SH2ClearCPUConcurrency(SSH2, VDP2_RAM_B0_LOCK);;
    }
  }
  //unblock B1
  for (int i = 0; i<8; i++) {
    if(Vdp2External.AC_VRAM[3][i] == 0xE) {
      SH2ClearCPUConcurrency(MSH2, VDP2_RAM_B1_LOCK);
      SH2ClearCPUConcurrency(SSH2, VDP2_RAM_B1_LOCK);;
    }
  }
}

void resetFrameSkip(void) {
  nextFrameTime = 0;
}

void Vdp2VBlankIN_It(void) {
  Vdp2Regs->TVSTAT |= 0x0008;
  ScuSendVBlankIN();
}

void Vdp2VBlankIN(void) {
  FRAMELOG("***** VIN *****");

  if (Vdp2Regs->EXTEN & 0x200) // Should be revised for accuracy(should occur only occur on the line it happens at, etc.)
  {
    if ((SmpcRegs->EXLE & 0x8) == 0){
      // Use unused bit to detect latch already done
      // Only Latch if EXLTEN is enabled
      if (SmpcRegs->EXLE & 0x1){
        Vdp2SendExternalLatch(((PORTDATA1.data[2] & 0x40) == 0), (PORTDATA1.data[3]<<8)|PORTDATA1.data[4], (PORTDATA1.data[5]<<8)|PORTDATA1.data[6]);
      }
      if (SmpcRegs->EXLE & 0x2){
        Vdp2SendExternalLatch(((PORTDATA2.data[2] & 0x40) == 0), (PORTDATA2.data[3]<<8)|PORTDATA2.data[4], (PORTDATA2.data[5]<<8)|PORTDATA2.data[6]);
      }
      SmpcRegs->EXLE |= 0x8;
    }
  }

   /* this should be done after a frame change or a plot trigger */

   /* I'm not 100% sure about this, but it seems that when using manual change
   we should swap framebuffers in the "next field" and thus, clear the CEF...
   now we're lying a little here as we're not swapping the framebuffers. */
   //if (Vdp1External.manualchange) Vdp1Regs->EDSR >>= 1;

   if (checkFrameSkip() != 0) {
     dropFrameDisplay();
     isSkipped = 1;
   } else {
     VIDCore->Vdp2Draw();
     isSkipped = 0;
   }
   nextFrameTime  += yabsys.OneFrameTime;

   VIDCore->Sync();

}

//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////


void Vdp2HBlankIN_It(void) {
  if (yabsys.LineCount < yabsys.VBlankLineCount) {
    Vdp2Regs->TVSTAT |= 0x0004;
    ScuSendHBlankIN();
  }
  MSH2->isAccessingCPUBUS = 0;
  SSH2->isAccessingCPUBUS = 0;
  SH2ClearCPUConcurrency(MSH2, VDP2_RAM_LOCK);
  SH2ClearCPUConcurrency(SSH2, VDP2_RAM_LOCK);
}
void Vdp2HBlankIN(void) {

  if (yabsys.LineCount < yabsys.VBlankLineCount) {
    u32 cell_scroll_table_start_addr = (Vdp2Regs->VCSTA.all & 0x7FFFE) << 1;
    memcpy(Vdp2Lines + yabsys.LineCount, Vdp2Regs, sizeof(Vdp2));
    for (int i = 0; i < 88; i++)
    {
      cell_scroll_data[yabsys.LineCount].data[i] = Vdp2RamReadLong(NULL, Vdp2Ram, cell_scroll_table_start_addr + i * 4);
    }
  } else {
// Fix : Function doesn't exist without those defines
#if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)
  if (isSkipped == 0) waitVdp2DrawScreensEnd(yabsys.LineCount == yabsys.VBlankLineCount);
#endif
  }
}

extern int vdp1_clock;
void Vdp2StartVisibleLine(void) {
  #if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)
  if (nbAddrToUpdate != 0){
    for (int i=0; i<0x1000; i++) {
      if (addrToUpdate[i] != 0) {
        YglOnUpdateColorRamWord(i);
        addrToUpdate[i] = 0;
      }
    }
    nbAddrToUpdate = 0;
  }
  if(yabsys.LineCount == yabsys.MaxLineCount - 1) {
    YglUpdateColorRam();
  }
  #endif

  updateVdp2ColorRam(yabsys.LineCount);
  if (yabsys.LineCount < yabsys.VBlankLineCount)
  {
    Vdp2Regs->TVSTAT &= ~0x0004;
  }
  if (yabsys.LineCount == 1) {
    VDP2genVRamCyclePattern();
  }
}

//////////////////////////////////////////////////////////////////////////////

Vdp2 * Vdp2RestoreRegs(int line, Vdp2* lines) {
   return line > 270 ? NULL : lines + line;
}

//////////////////////////////////////////////////////////////////////////////
void Vdp2VBlankOUT_It(void) {
  Vdp2Regs->TVSTAT = ((Vdp2Regs->TVSTAT & ~0x0008) & ~0x0002) | (vdp2_is_odd_frame << 1);
  ScuSendVBlankOUT();
}
void Vdp2VBlankOUT(void) {

  g_frame_count++;
  yabsys.VBlankLineCount = 225+(Vdp2Regs->TVMD & 0x30);
  if (yabsys.VBlankLineCount > 256) yabsys.VBlankLineCount = 256;

  FRAMELOG("***** VOUT %d *****", g_frame_count);

   if (_Ygl->interlace == NORMAL_INTERLACE){
     vdp2_is_odd_frame = 1;
   }else{ // p02_50.htm#TVSTAT_
     if (vdp2_is_odd_frame)
       vdp2_is_odd_frame = 0;
     else
       vdp2_is_odd_frame = 1;
   }

}

//////////////////////////////////////////////////////////////////////////////

void Vdp2SendExternalLatch(int trigger, int hcnt, int vcnt)
{
  if (trigger) {
    Vdp2Regs->HCNT = hcnt << 1; //pourquoi?
    Vdp2Regs->VCNT = vcnt;
    Vdp2Regs->TVSTAT |= 0x200;
  }
}

//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL Vdp2ReadByte(SH2_struct *context, u8* mem, u32 addr) {
   YuiMsg("Non supported VDP2 register byte read = %08X\n", addr);
   addr &= 0x1FF;
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL Vdp2ReadWord(SH2_struct *context, u8* mem, u32 addr) {
  LOG("VDP2 register long read = %08X\n", addr);
   addr &= 0x1FF;

   switch (addr)
   {
      case 0x000:
         return Vdp2Regs->TVMD;
      case 0x002:
         if (!(Vdp2Regs->EXTEN & 0x200))
         {
            // Latch HV counter on read
            // Vdp2Regs->HCNT = (yabsys.DecilineCount * _Ygl->rwidth / DECILINE_STEP) << 1;
            Vdp2Regs->VCNT = yabsys.LineCount;
            Vdp2Regs->TVSTAT |= 0x200;
         }

         return Vdp2Regs->EXTEN;
      case 0x004:
      {
         u16 tvstat = Vdp2Regs->TVSTAT;

         // Clear External latch and sync flags
         Vdp2Regs->TVSTAT &= 0xFCFF;

         // if TVMD's DISP bit is cleared, TVSTAT's VBLANK bit is always set
         if ((Vdp2Regs->TVMD & 0x8000)!=0)
            return tvstat;
         else
            return (tvstat | 0x8);
      }
      case 0x006:
         return Vdp2Regs->VRSIZE;
      case 0x008:
		    return Vdp2Regs->HCNT;
      case 0x00A:
         return Vdp2Regs->VCNT;
     case 0x00E:
        return Vdp2Regs->RAMCTL;
      default:
      {
         YuiMsg("Unhandled VDP2 word read: %08X\n", addr);
         break;
      }
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL Vdp2ReadLong(SH2_struct *context, u8* mem, u32 addr) {
   LOG("VDP2 register long read = %08X\n", addr);
   addr &= 0x1FF;
   u16 hi = Vdp2ReadWord(context, mem, addr);
   u16 lo = Vdp2ReadWord(context, mem, addr+2);
   return (hi<<16)|lo;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp2WriteByte(SH2_struct *context, u8* mem, u32 addr, UNUSED u8 val) {
   LOG("VDP2 register byte write = %08X\n", addr);
   addr &= 0x1FF;
}

#define SHOW_PATTERN_CYCLE
void updateCyclePattern() {
  u8 *ac;
  SHOW_PATTERN_CYCLE("************************** LINE %d\n", yabsys.LineCount);
  AC_VRAM[0][0] = (Vdp2Regs->CYCA0L >> 12) & 0x0F;
  AC_VRAM[0][1] = (Vdp2Regs->CYCA0L >> 8) & 0x0F;
  AC_VRAM[0][2] = (Vdp2Regs->CYCA0L >> 4) & 0x0F;
  AC_VRAM[0][3] = (Vdp2Regs->CYCA0L >> 0) & 0x0F;
  AC_VRAM[0][4] = (Vdp2Regs->CYCA0U >> 12) & 0x0F;
  AC_VRAM[0][5] = (Vdp2Regs->CYCA0U >> 8) & 0x0F;
  AC_VRAM[0][6] = (Vdp2Regs->CYCA0U >> 4) & 0x0F;
  AC_VRAM[0][7] = (Vdp2Regs->CYCA0U >> 0) & 0x0F;

  ac = AC_VRAM[0];
  SHOW_PATTERN_CYCLE("%x %x %x %x %x %x %x %x\n",ac[0],ac[1],ac[2],ac[3],ac[4],ac[5],ac[6],ac[7]);

  AC_VRAM[1][0] = (Vdp2Regs->CYCA1L >> 12) & 0x0F;
  AC_VRAM[1][1] = (Vdp2Regs->CYCA1L >> 8) & 0x0F;
  AC_VRAM[1][2] = (Vdp2Regs->CYCA1L >> 4) & 0x0F;
  AC_VRAM[1][3] = (Vdp2Regs->CYCA1L >> 0) & 0x0F;
  AC_VRAM[1][4] = (Vdp2Regs->CYCA1U >> 12) & 0x0F;
  AC_VRAM[1][5] = (Vdp2Regs->CYCA1U >> 8) & 0x0F;
  AC_VRAM[1][6] = (Vdp2Regs->CYCA1U >> 4) & 0x0F;
  AC_VRAM[1][7] = (Vdp2Regs->CYCA1U >> 0) & 0x0F;

  ac = AC_VRAM[1];
  SHOW_PATTERN_CYCLE("%x %x %x %x %x %x %x %x\n",ac[0],ac[1],ac[2],ac[3],ac[4],ac[5],ac[6],ac[7]);

  AC_VRAM[2][0] = (Vdp2Regs->CYCB0L >> 12) & 0x0F;
  AC_VRAM[2][1] = (Vdp2Regs->CYCB0L >> 8) & 0x0F;
  AC_VRAM[2][2] = (Vdp2Regs->CYCB0L >> 4) & 0x0F;
  AC_VRAM[2][3] = (Vdp2Regs->CYCB0L >> 0) & 0x0F;
  AC_VRAM[2][4] = (Vdp2Regs->CYCB0U >> 12) & 0x0F;
  AC_VRAM[2][5] = (Vdp2Regs->CYCB0U >> 8) & 0x0F;
  AC_VRAM[2][6] = (Vdp2Regs->CYCB0U >> 4) & 0x0F;
  AC_VRAM[2][7] = (Vdp2Regs->CYCB0U >> 0) & 0x0F;

  ac = AC_VRAM[2];
  SHOW_PATTERN_CYCLE("%x %x %x %x %x %x %x %x\n",ac[0],ac[1],ac[2],ac[3],ac[4],ac[5],ac[6],ac[7]);

  AC_VRAM[3][0] = (Vdp2Regs->CYCB1L >> 12) & 0x0F;
  AC_VRAM[3][1] = (Vdp2Regs->CYCB1L >> 8) & 0x0F;
  AC_VRAM[3][2] = (Vdp2Regs->CYCB1L >> 4) & 0x0F;
  AC_VRAM[3][3] = (Vdp2Regs->CYCB1L >> 0) & 0x0F;
  AC_VRAM[3][4] = (Vdp2Regs->CYCB1U >> 12) & 0x0F;
  AC_VRAM[3][5] = (Vdp2Regs->CYCB1U >> 8) & 0x0F;
  AC_VRAM[3][6] = (Vdp2Regs->CYCB1U >> 4) & 0x0F;
  AC_VRAM[3][7] = (Vdp2Regs->CYCB1U >> 0) & 0x0F;

  ac = AC_VRAM[3];
  SHOW_PATTERN_CYCLE("%x %x %x %x %x %x %x %x\n",ac[0],ac[1],ac[2],ac[3],ac[4],ac[5],ac[6],ac[7]);
  SHOW_PATTERN_CYCLE("========================================\n");
}

void FASTCALL Vdp2WriteWord(SH2_struct *context, u8* mem, u32 addr, u16 val) {
   addr &= 0x1FF;
   switch (addr)
   {
      case 0x000:
         Vdp2Regs->TVMD = val;
         if ((yabsys.LineCount < yabsys.VBlankLineCount) && (yabsys.LineCount < 225+(Vdp2Regs->TVMD & 0x30)) && ((Vdp2Regs->TVMD & 0x30)<(yabsys.VBlankLineCount - 225))) {
           //Safe to change right now
           yabsys.VBlankLineCount = 225+(Vdp2Regs->TVMD & 0x30);
           if (yabsys.VBlankLineCount > 256) yabsys.VBlankLineCount = 256;
         }
         Vdp1SetRaster(Vdp2Regs->TVMD & 0x1);
         return;
      case 0x002:
         Vdp2Regs->EXTEN = val;
         return;
      case 0x004:
         // TVSTAT is read-only
         return;
      case 0x006:
         Vdp2Regs->VRSIZE = val;
         return;
      case 0x008:
         // HCNT is read-only
         return;
      case 0x00A:
         // VCNT is read-only
         return;
      case 0x00C:
         // Reserved
         return;
      case 0x00E:
         Vdp2Regs->RAMCTL = val;
         if (Vdp2Internal.ColorMode != ((val >> 12) & 0x3) ) {
           Vdp2Internal.ColorMode = (val >> 12) & 0x3;
           //A EXTRAIRE
#if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)
           for (int i = 0; i < 0x1000; i ++) {
             addrToUpdate[i] = 1;
           }
           nbAddrToUpdate = 1;
#endif
         }
         return;
      case 0x010:
         Vdp2Regs->CYCA0L = val;
         updateCyclePattern();
         return;
      case 0x012:
         Vdp2Regs->CYCA0U = val;
         updateCyclePattern();
         return;
      case 0x014:
         Vdp2Regs->CYCA1L = val;
         updateCyclePattern();
         return;
      case 0x016:
         Vdp2Regs->CYCA1U = val;
         updateCyclePattern();
         return;
      case 0x018:
         Vdp2Regs->CYCB0L = val;
         updateCyclePattern();
         return;
      case 0x01A:
         Vdp2Regs->CYCB0U = val;
         updateCyclePattern();
         return;
      case 0x01C:
         Vdp2Regs->CYCB1L = val;
         updateCyclePattern();
         return;
      case 0x01E:
         Vdp2Regs->CYCB1U = val;
         updateCyclePattern();
         return;
      case 0x020:
         Vdp2Regs->BGON = val;
         return;
      case 0x022:
         Vdp2Regs->MZCTL = val;
         return;
      case 0x024:
         Vdp2Regs->SFSEL = val;
         return;
      case 0x026:
         Vdp2Regs->SFCODE = val;
         return;
      case 0x028:
         Vdp2Regs->CHCTLA = val;
         return;
      case 0x02A:
         Vdp2Regs->CHCTLB = val;
         return;
      case 0x02C:
         Vdp2Regs->BMPNA = val;
         return;
      case 0x02E:
         Vdp2Regs->BMPNB = val;
         return;
      case 0x030:
         Vdp2Regs->PNCN0 = val;
         return;
      case 0x032:
         Vdp2Regs->PNCN1 = val;
         return;
      case 0x034:
         Vdp2Regs->PNCN2 = val;
         return;
      case 0x036:
         Vdp2Regs->PNCN3 = val;
         return;
      case 0x038:
         Vdp2Regs->PNCR = val;
         return;
      case 0x03A:
         Vdp2Regs->PLSZ = val;
         return;
      case 0x03C:
         Vdp2Regs->MPOFN = val;
         return;
      case 0x03E:
         Vdp2Regs->MPOFR = val;
         return;
      case 0x040:
         Vdp2Regs->MPABN0 = val;
         return;
      case 0x042:
         Vdp2Regs->MPCDN0 = val;
         return;
      case 0x044:
         Vdp2Regs->MPABN1 = val;
         return;
      case 0x046:
         Vdp2Regs->MPCDN1 = val;
         return;
      case 0x048:
         Vdp2Regs->MPABN2 = val;
         return;
      case 0x04A:
         Vdp2Regs->MPCDN2 = val;
         return;
      case 0x04C:
         Vdp2Regs->MPABN3 = val;
         return;
      case 0x04E:
         Vdp2Regs->MPCDN3 = val;
         return;
      case 0x050:
         Vdp2Regs->MPABRA = val;
         return;
      case 0x052:
         Vdp2Regs->MPCDRA = val;
         return;
      case 0x054:
         Vdp2Regs->MPEFRA = val;
         return;
      case 0x056:
         Vdp2Regs->MPGHRA = val;
         return;
      case 0x058:
         Vdp2Regs->MPIJRA = val;
         return;
      case 0x05A:
         Vdp2Regs->MPKLRA = val;
         return;
      case 0x05C:
         Vdp2Regs->MPMNRA = val;
         return;
      case 0x05E:
         Vdp2Regs->MPOPRA = val;
         return;
      case 0x060:
         Vdp2Regs->MPABRB = val;
         return;
      case 0x062:
         Vdp2Regs->MPCDRB = val;
         return;
      case 0x064:
         Vdp2Regs->MPEFRB = val;
         return;
      case 0x066:
         Vdp2Regs->MPGHRB = val;
         return;
      case 0x068:
         Vdp2Regs->MPIJRB = val;
         return;
      case 0x06A:
         Vdp2Regs->MPKLRB = val;
         return;
      case 0x06C:
         Vdp2Regs->MPMNRB = val;
         return;
      case 0x06E:
         Vdp2Regs->MPOPRB = val;
         return;
      case 0x070:
         Vdp2Regs->SCXIN0 = val;
         return;
      case 0x072:
         Vdp2Regs->SCXDN0 = val;
         return;
      case 0x074:
         Vdp2Regs->SCYIN0 = val;
         return;
      case 0x076:
         Vdp2Regs->SCYDN0 = val;
         return;
      case 0x078:
         Vdp2Regs->ZMXN0.part.I = val;
         return;
      case 0x07A:
         Vdp2Regs->ZMXN0.part.D = val;
         return;
      case 0x07C:
         Vdp2Regs->ZMYN0.part.I = val;
         return;
      case 0x07E:
         Vdp2Regs->ZMYN0.part.D = val;
         return;
      case 0x080:
         Vdp2Regs->SCXIN1 = val;
         return;
      case 0x082:
         Vdp2Regs->SCXDN1 = val;
         return;
      case 0x084:
         Vdp2Regs->SCYIN1 = val;
         return;
      case 0x086:
         Vdp2Regs->SCYDN1 = val;
         return;
      case 0x088:
         Vdp2Regs->ZMXN1.part.I = val;
         return;
      case 0x08A:
         Vdp2Regs->ZMXN1.part.D = val;
         return;
      case 0x08C:
         Vdp2Regs->ZMYN1.part.I = val;
         return;
      case 0x08E:
         Vdp2Regs->ZMYN1.part.D = val;
         return;
      case 0x090:
         Vdp2Regs->SCXN2 = val;
         return;
      case 0x092:
         Vdp2Regs->SCYN2 = val;
         return;
      case 0x094:
         Vdp2Regs->SCXN3 = val;
         return;
      case 0x096:
         Vdp2Regs->SCYN3 = val;
         return;
      case 0x098:
         Vdp2Regs->ZMCTL = val;
         return;
      case 0x09A:
         Vdp2Regs->SCRCTL = val;
         return;
      case 0x09C:
         Vdp2Regs->VCSTA.part.U = val;
         return;
      case 0x09E:
         Vdp2Regs->VCSTA.part.L = val;
         return;
      case 0x0A0:
         Vdp2Regs->LSTA0.part.U = val;
         return;
      case 0x0A2:
         Vdp2Regs->LSTA0.part.L = val;
         return;
      case 0x0A4:
         Vdp2Regs->LSTA1.part.U = val;
         return;
      case 0x0A6:
         Vdp2Regs->LSTA1.part.L = val;
         return;
      case 0x0A8:
         Vdp2Regs->LCTA.part.U = val;
         return;
      case 0x0AA:
         Vdp2Regs->LCTA.part.L = val;
         return;
      case 0x0AC:
         Vdp2Regs->BKTAU = val;
         return;
      case 0x0AE:
         Vdp2Regs->BKTAL = val;
         return;
      case 0x0B0:
         Vdp2Regs->RPMD = val;
         return;
      case 0x0B2:
         Vdp2Regs->RPRCTL = val;
         return;
      case 0x0B4:
         Vdp2Regs->KTCTL = val;
         return;
      case 0x0B6:
         Vdp2Regs->KTAOF = val;
         return;
      case 0x0B8:
         Vdp2Regs->OVPNRA = val;
         return;
      case 0x0BA:
         Vdp2Regs->OVPNRB = val;
         return;
      case 0x0BC:
         Vdp2Regs->RPTA.part.U = val;
         return;
      case 0x0BE:
         Vdp2Regs->RPTA.part.L = val;
         return;
      case 0x0C0:
         Vdp2Regs->WPSX0 = val;
         return;
      case 0x0C2:
         Vdp2Regs->WPSY0 = val;
         return;
      case 0x0C4:
         Vdp2Regs->WPEX0 = val;
         return;
      case 0x0C6:
         Vdp2Regs->WPEY0 = val;
         return;
      case 0x0C8:
         Vdp2Regs->WPSX1 = val;
         return;
      case 0x0CA:
         Vdp2Regs->WPSY1 = val;
         return;
      case 0x0CC:
         Vdp2Regs->WPEX1 = val;
         return;
      case 0x0CE:
         Vdp2Regs->WPEY1 = val;
         return;
      case 0x0D0:
         Vdp2Regs->WCTLA = val;
         return;
      case 0x0D2:
         Vdp2Regs->WCTLB = val;
         return;
      case 0x0D4:
         Vdp2Regs->WCTLC = val;
         return;
      case 0x0D6:
         Vdp2Regs->WCTLD = val;
         return;
      case 0x0D8:
         Vdp2Regs->LWTA0.part.U = val;
         return;
      case 0x0DA:
         Vdp2Regs->LWTA0.part.L = val;
         return;
      case 0x0DC:
         Vdp2Regs->LWTA1.part.U = val;
         return;
      case 0x0DE:
         Vdp2Regs->LWTA1.part.L = val;
         return;
      case 0x0E0:
         Vdp2Regs->SPCTL = val;
         return;
      case 0x0E2:
         Vdp2Regs->SDCTL = val;
         return;
      case 0x0E4:
         Vdp2Regs->CRAOFA = val;
         return;
      case 0x0E6:
         Vdp2Regs->CRAOFB = val;
         return;
      case 0x0E8:
         Vdp2Regs->LNCLEN = val;
         return;
      case 0x0EA:
         Vdp2Regs->SFPRMD = val;
         return;
      case 0x0EC:
         Vdp2Regs->CCCTL = val;
         return;
      case 0x0EE:
         Vdp2Regs->SFCCMD = val;
         return;
      case 0x0F0:
         Vdp2Regs->PRISA = val;
         return;
      case 0x0F2:
         Vdp2Regs->PRISB = val;
         return;
      case 0x0F4:
         Vdp2Regs->PRISC = val;
         return;
      case 0x0F6:
         Vdp2Regs->PRISD = val;
         return;
      case 0x0F8:
         Vdp2Regs->PRINA = val;
         return;
      case 0x0FA:
         Vdp2Regs->PRINB = val;
         return;
      case 0x0FC:
         Vdp2Regs->PRIR = val;
         return;
      case 0x0FE:
         // Reserved
         return;
      case 0x100:
         Vdp2Regs->CCRSA = val;
         return;
      case 0x102:
         Vdp2Regs->CCRSB = val;
         return;
      case 0x104:
         Vdp2Regs->CCRSC = val;
         return;
      case 0x106:
         Vdp2Regs->CCRSD = val;
         return;
      case 0x108:
         Vdp2Regs->CCRNA = val;
         return;
      case 0x10A:
         Vdp2Regs->CCRNB = val;
         return;
      case 0x10C:
         Vdp2Regs->CCRR = val;
         return;
      case 0x10E:
         Vdp2Regs->CCRLB = val;
         return;
      case 0x110:
         Vdp2Regs->CLOFEN = val;
         return;
      case 0x112:
         Vdp2Regs->CLOFSL = val;
         return;
      case 0x114:
         Vdp2Regs->COAR = val;
         return;
      case 0x116:
         Vdp2Regs->COAG = val;
         return;
      case 0x118:
         Vdp2Regs->COAB = val;
         return;
      case 0x11A:
         Vdp2Regs->COBR = val;
         return;
      case 0x11C:
         Vdp2Regs->COBG = val;
         return;
      case 0x11E:
         Vdp2Regs->COBB = val;
         return;
      default:
      {
         LOG("Unhandled VDP2 word write: %08X\n", addr);
         break;
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp2WriteLong(SH2_struct *context, u8* mem, u32 addr, u32 val) {

   Vdp2WriteWord(context, mem, addr,val>>16);
   Vdp2WriteWord(context, mem, addr+2,val&0xFFFF);
   return;
}

//////////////////////////////////////////////////////////////////////////////

int Vdp2SaveState(void ** stream)
{
   int offset;

   offset = MemStateWriteHeader(stream, "VDP2", 1);

   // Write registers
   MemStateWrite((void *)Vdp2Regs, sizeof(Vdp2), 1, stream);

   // Write VDP2 ram
   MemStateWrite((void *)Vdp2Ram, 0x100000, 1, stream);

   // Write CRAM
   MemStateWrite((void *)Vdp2ColorRam, 0x1000, 1, stream);

   // Write internal variables
   MemStateWrite((void *)&Vdp2Internal, sizeof(Vdp2Internal_struct), 1, stream);

   return MemStateFinishHeader(stream, offset);
}

//////////////////////////////////////////////////////////////////////////////

int Vdp2LoadState(const void * stream, UNUSED int version, int size)
{
   // Read registers
   MemStateRead((void *)Vdp2Regs, sizeof(Vdp2), 1, stream);

   // Read VDP2 ram
   MemStateRead((void *)Vdp2Ram, 0x100000, 1, stream);

   // Read CRAM
   MemStateRead((void *)Vdp2ColorRam, 0x1000, 1, stream);

   // Read internal variables
   MemStateRead((void *)&Vdp2Internal, sizeof(Vdp2Internal_struct), 1, stream);

   Vdp2Ram_Updated = 1;

   if(VIDCore) VIDCore->Resize(0,0,0,0,0);

#if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)
   YglDirtyColorRamWord();
#endif

   return size;
}

//////////////////////////////////////////////////////////////////////////////

void ToggleNBG0(void)
{
   Vdp2External.disptoggle ^= 0x1;
}

//////////////////////////////////////////////////////////////////////////////

void ToggleNBG1(void)
{
   Vdp2External.disptoggle ^= 0x2;
}

//////////////////////////////////////////////////////////////////////////////

void ToggleNBG2(void)
{
   Vdp2External.disptoggle ^= 0x4;
}

//////////////////////////////////////////////////////////////////////////////

void ToggleNBG3(void)
{
   Vdp2External.disptoggle ^= 0x8;
}

//////////////////////////////////////////////////////////////////////////////

void ToggleRBG0(void)
{
   Vdp2External.disptoggle ^= 0x10;
}

//////////////////////////////////////////////////////////////////////////////

void ToggleRBG1(void)
{
   Vdp2External.disptoggle ^= 0x20;
}

//////////////////////////////////////////////////////////////////////////////

void ToggleFullScreen(void)
{
   if (VIDCore->IsFullscreen())
   {
      VIDCore->Resize(0,0,320, 224, 0);
   }
   else
   {
      VIDCore->Resize(0,0,320, 224, 1);
   }
}
