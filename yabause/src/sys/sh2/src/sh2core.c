/*  Copyright 2003-2005 Guillaume Duhamel
    Copyright 2004-2005, 2013 Theo Berkau

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

/*! \file sh2core.c
    \brief SH2 shared emulation functions.
*/

#include <stdlib.h>
#include "sh2core.h"
#include "debug.h"
#include "memory.h"
#include "yabause.h"
#include <scu.h>
#include <error.h>

SH2_struct *SSH2=NULL;
SH2_struct *MSH2=NULL;
SH2Interface_struct *SH2Core=NULL;
extern SH2Interface_struct *SH2CoreList[];

void OnchipReset(SH2_struct *context);
static void FRTExec(SH2_struct *context);
static void WDTExec(SH2_struct *context);
u8 SCIReceiveByte(void);
void SCITransmitByte(u8);


void enableCache(SH2_struct *ctx);
void disableCache(SH2_struct *ctx);
void InvalidateCache(SH2_struct *ctx);

static void (*SH2BlockableExec)(SH2_struct *context, u32 cycles);
static void (*SH2StandardExec)(SH2_struct *context, u32 cycles);

#define CACHE_LOG

void DMATransferCycles(SH2_struct *context, Dmac * dmac, int cycles);
int DMAProc(SH2_struct *context, int cycles );

//////////////////////////////////////////////////////////////////////////////

void SH2IntcSetIrl(SH2_struct *sh, u8 irl, u8 d)
{
  if (sh->intc.irl != irl) {
    sh->intc.d = d;
    sh->intc.irl = irl;
    SH2EvaluateInterrupt(sh);
  }
}
void SH2IntcSetNmi(SH2_struct *sh)
{
  sh->intc.nmi = 0x1;
  SH2EvaluateInterrupt(sh);
}

void SH2EvaluateInterrupt(SH2_struct *sh) {
  if (sh->intPriority != 0) return;
  sh->intVector = 0xFF;

  if (sh->intc.nmi != 0) {
    sh->intVector = 0xB;
    sh->intPriority = 0xF;
    sh->intc.nmi = 0;
  }
  else if ((sh->intc.irl != 0)&&(sh->intc.irl > sh->regs.SR.part.I)) //Test IRL
  {
    //interrupt on IRL, determine the priority
    sh->intPriority = sh->intc.irl;
    if (sh->onchip.ICR & 0x1) {
      sh->intVector = sh->intc.d;
      ScuAcceptInterrupt(sh);
    }
    else {
      sh->intVector = 0x40+(sh->intc.irl>>1);
    }
    sh->intc.irl = 0;
  }
  else if (((sh->onchip.DVCR & 0x3)==0x3) && (((sh->onchip.IPRA >> 12) & 0xF) > sh->regs.SR.part.I)) //DIVU
  {
    sh->intVector = sh->onchip.VCRDIV & 0x7F;
    sh->intPriority = ((sh->onchip.IPRA >> 12) & 0xF);
  }
  else if (((sh->onchip.CHCR0 & 0x6)==0x6) && (((sh->onchip.IPRA & 0xF00) >> 8) > sh->regs.SR.part.I)) //DMAC0
  {
      sh->intVector = sh->onchip.VCRDMA0;
      sh->intPriority = ((sh->onchip.IPRA & 0xF00) >> 8);
  }
  else if (((sh->onchip.CHCR1 & 0x6)==0x6) && (((sh->onchip.IPRA & 0xF00) >> 8) > sh->regs.SR.part.I)) //DMAC1
  {
      sh->intVector = sh->onchip.VCRDMA1;
      sh->intPriority = ((sh->onchip.IPRA & 0xF00) >> 8);
  }
  else if ((sh->wdt.isinterval != 0) && ((sh->onchip.WTCSR & 0x80) != 0) && (((sh->onchip.IPRA >> 4) & 0xF) > sh->regs.SR.part.I)) //WDT
  {
    sh->intVector = (sh->onchip.VCRWDT >> 8) & 0x7F;
    sh->intPriority = ((sh->onchip.IPRA >> 4) & 0xF);
  }
  //BSC not implemented
  //SCI not implemented
  else if (((sh->onchip.TIER & 0x80)!=0) && ((sh->onchip.FTCSR & 0x80)!=0) && (((sh->onchip.IPRB >> 8) & 0xF) > sh->regs.SR.part.I)) //FRT ICI
  {
    sh->intVector = (sh->onchip.VCRC >> 8) & 0x7F;
    sh->intPriority = ((sh->onchip.IPRB >> 8) & 0xF);
  }
  else if (((sh->onchip.TIER & 0x8)!=0) && ((sh->onchip.FTCSR & 0x8)!=0) && (((sh->onchip.IPRB >> 8) & 0xF) > sh->regs.SR.part.I)) //FRT OCIA
  {
     sh->intVector = sh->onchip.VCRC & 0x7F;
     sh->intPriority = ((sh->onchip.IPRB >> 8) & 0xF);
  }
  else if (((sh->onchip.TIER & 0x2)!=0) && ((sh->onchip.FTCSR & 0x2)!=0) && (((sh->onchip.IPRB >> 8) & 0xF) > sh->regs.SR.part.I)) //FRT OVI
  {
     sh->intVector = (sh->onchip.VCRD >> 8) & 0x7F;
     sh->intPriority = ((sh->onchip.IPRB >> 8) & 0xF);
  }
  if (sh->intPriority != 0x0) {
    // force the next PC to be decodeWithInterrupt so that interrupt is evaluated asap
    if (SH2Core->notifyInterrupt != NULL) SH2Core->notifyInterrupt(sh);
  }
}


static void SH2StandardExecFast(SH2_struct *context, u32 cycles) {
  int oldbp = context->bp.inbreakpoint;
  SH2Core->Exec(context, cycles);
  if (context->bp.inbreakpoint && !oldbp) {
    context->bp.BreakpointCallBack(context, 0, context->bp.BreakpointUserData);
    context->bp.inbreakpoint = 0;
  }
}
static void SH2StandardExecDebug(SH2_struct *context, u32 cycles) {
  SH2Core->Exec(context, cycles);
}

static sh2regs_struct oldRegs;
static void SH2BlockableExecDebug(SH2_struct *context, u32 cycles) {
  if (context->isBlocked == 0) {
    int oldbp = context->bp.inbreakpoint;
    SH2Core->ExecSave(context, cycles, &oldRegs);
    if (context->bp.inbreakpoint && !oldbp) {
      context->bp.BreakpointCallBack(context, 0, context->bp.BreakpointUserData);
      context->bp.inbreakpoint = 0;
    }
  } else {
    context->cycles += cycles;
  }
}
static void SH2BlockableExecFast(SH2_struct *context, u32 cycles) {
  if (context->isBlocked == 0) {
    SH2Core->ExecSave(context, cycles, &oldRegs);
  } else {
    context->cycles += cycles;
  }
}

void SH2SetExecSet(int debug) {
  if (debug == 0) {
    SH2BlockableExec = SH2BlockableExecFast;
    SH2StandardExec = SH2StandardExecFast;
  } else {
    SH2BlockableExec = SH2BlockableExecDebug;
    SH2StandardExec = SH2StandardExecDebug;
  }
}

void SH2UpdateABusAccess(SH2_struct *context, int on) {
  if (context->isAccessingCPUBUS != on) {
    context->isAccessingCPUBUS = on;
    SH2UpdateBlockedState(context);
  }
}

void SH2SetVRamAccess(SH2_struct *context, int mask) {
  if (!(context->isAccessingVram & mask)) {
    context->isAccessingVram |= mask;
    SH2UpdateBlockedState(context);
  }
}
void SH2ClearVRamAccess(SH2_struct *context, int mask) {
  if (context->isAccessingVram & mask) {
    context->isAccessingVram &= ~mask;
    SH2UpdateBlockedState(context);
  }
}

static int isDMABlocked(SH2_struct *context) {
  return (context->isAccessingCPUBUS != 0)&&((context->blockingMask & A_BUS_ACCESS)!=0);
}

void SH2UpdateBlockedState(SH2_struct *context){
  context->isBlocked =  (context->isAccessingCPUBUS != 0)||((context->blockingMask & A_BUS_ACCESS)!=0);
  context->isBlocked |= ((context->isAccessingVram & context->blockingMask)!=0);
}

void SH2SetCPUConcurrency(SH2_struct *context, u8 mask) {
  if ((context->SH2InterruptibleExec != SH2BlockableExec) || !(context->blockingMask & mask)) {
    context->blockingMask |= mask;
    if (context->blockingMask != 0) context->SH2InterruptibleExec = SH2BlockableExec;
    if (mask == A_BUS_ACCESS) SH2UpdateABusAccess(context, 0);
    else SH2ClearVRamAccess(context, mask);
  }
}

void SH2ClearCPUConcurrency(SH2_struct *context, u8 mask) {
  if ((context->SH2InterruptibleExec != SH2StandardExec) && (context->blockingMask & mask)) {
    context->blockingMask &= ~mask;
    if (context->blockingMask == 0) context->SH2InterruptibleExec = SH2StandardExec;
    if (mask == A_BUS_ACCESS) SH2UpdateABusAccess(context, 0);
    else SH2ClearVRamAccess(context, mask);
  }
}

int SH2Init(int coreid)
{
   int i;
   // MSH2
   if ((MSH2 = (SH2_struct *)calloc(1, sizeof(SH2_struct))) == NULL)
      return -1;
  MSH2->SH2InterruptibleExec = SH2StandardExec;

   if (SH2TrackInfLoopInit(MSH2) != 0)
      return -1;

   MSH2->onchip.BCR1 = 0x0000;
   MSH2->isslave = 0;
   MSH2->isAccessingCPUBUS = 0;
   MSH2->interruptReturnAddress = 0;
   MSH2->isAccessingVram = 0;
   MSH2->isBlocked = 0;

    MSH2->dma_ch0.CHCR = &MSH2->onchip.CHCR0;
    MSH2->dma_ch0.CHCRM = &MSH2->onchip.CHCR0M;
    MSH2->dma_ch0.SAR = &MSH2->onchip.SAR0;
    MSH2->dma_ch0.DAR = &MSH2->onchip.DAR0;
    MSH2->dma_ch0.TCR = &MSH2->onchip.TCR0;
    MSH2->dma_ch0.VCRDMA = &MSH2->onchip.VCRDMA0;
    MSH2->dma_ch1.CHCR = &MSH2->onchip.CHCR1;
    MSH2->dma_ch1.CHCRM = &MSH2->onchip.CHCR1M;
    MSH2->dma_ch1.SAR = &MSH2->onchip.SAR1;
    MSH2->dma_ch1.DAR = &MSH2->onchip.DAR1;
    MSH2->dma_ch1.TCR = &MSH2->onchip.TCR1;
    MSH2->dma_ch1.VCRDMA = &MSH2->onchip.VCRDMA1;

   // SSH2
   if ((SSH2 = (SH2_struct *)calloc(1, sizeof(SH2_struct))) == NULL)
      return -1;

  SSH2->SH2InterruptibleExec = SH2StandardExec;

   if (SH2TrackInfLoopInit(SSH2) != 0)
      return -1;

    SSH2->interruptReturnAddress = 0;
    SSH2->onchip.BCR1 = 0x8000;
    SSH2->isslave = 1;
    SSH2->isAccessingCPUBUS = 0;
    SSH2->isAccessingVram = 0;
    SSH2->isBlocked = 0;

    SSH2->dma_ch0.CHCR = &SSH2->onchip.CHCR0;
    SSH2->dma_ch0.CHCRM = &SSH2->onchip.CHCR0M;
    SSH2->dma_ch0.SAR = &SSH2->onchip.SAR0;
    SSH2->dma_ch0.DAR = &SSH2->onchip.DAR0;
    SSH2->dma_ch0.TCR = &SSH2->onchip.TCR0;
    SSH2->dma_ch0.VCRDMA = &SSH2->onchip.VCRDMA0;
    SSH2->dma_ch1.CHCR = &SSH2->onchip.CHCR1;
    SSH2->dma_ch1.CHCRM = &SSH2->onchip.CHCR1M;
    SSH2->dma_ch1.SAR = &SSH2->onchip.SAR1;
    SSH2->dma_ch1.DAR = &SSH2->onchip.DAR1;
    SSH2->dma_ch1.TCR = &SSH2->onchip.TCR1;
    SSH2->dma_ch1.VCRDMA = &SSH2->onchip.VCRDMA1;

   MSH2->cacheOn = 0;
   SSH2->cacheOn = 0;

#ifdef USE_CACHE
   memset(MSH2->tagWay, 0x4, 64*0x80000);
   memset(MSH2->cacheTagArray, 0x0, 64*4*sizeof(u32));
   memset(SSH2->tagWay, 0x4, 64*0x80000);
   memset(SSH2->cacheTagArray, 0x0, 64*4*sizeof(u32));
#endif
   // So which core do we want?
   if (coreid == SH2CORE_DEFAULT)
      coreid = 0; // Assume we want the first one

   // Go through core list and find the id
   for (i = 0; SH2CoreList[i] != NULL; i++)
   {
      if (SH2CoreList[i]->id == coreid)
      {
         // Set to current core
         SH2Core = SH2CoreList[i];
         break;
      }
   }

   if ((SH2Core == NULL) || (SH2Core->Init() != 0)) {
      free(MSH2);
      free(SSH2);
      MSH2 = SSH2 = NULL;
      return -1;
   }

   SH2Reset(MSH2);
   SH2Reset(SSH2);

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void SH2DeInit()
{
   if (SH2Core)
      SH2Core->DeInit();
   SH2Core = NULL;

   if (MSH2)
   {
      SH2TrackInfLoopDeInit(MSH2);
      free(MSH2);
   }
   MSH2 = NULL;

   if (SSH2)
   {
      SH2TrackInfLoopDeInit(SSH2);
      free(SSH2);
   }
   SSH2 = NULL;
}

//////////////////////////////////////////////////////////////////////////////

void SH2Reset(SH2_struct *context)
{
   int i;
CACHE_LOG("%s reset\n", (context==SSH2)?"SSH2":"MSH2" );
   context->SH2InterruptibleExec = SH2StandardExec;
   SH2Core->Reset(context);

   // Reset general registers
   for (i = 0; i < 15; i++)
      SH2Core->SetGPR(context, i, 0x00000000);

   SH2Core->SetSR(context, 0x000000F0);
   SH2Core->SetGBR(context, 0x00000000);
   SH2Core->SetVBR(context, 0x00000000);
   SH2Core->SetMACH(context, 0x00000000);
   SH2Core->SetMACL(context, 0x00000000);
   SH2Core->SetPR(context, 0x00000000);

   // Internal variables
   context->target_cycles = 0x00000000;
   context->cycles = 0;
   context->frtcycles = 0;
   context->wdtcycles = 0;

   context->frc.leftover = 0;
   context->frc.shift = 3;

   context->wdt.isenable = 0;
   context->wdt.isinterval = 1;
   context->wdt.shift = 1;
   context->wdt.leftover = 0;

   context->cycleFrac = 0;

   // Reset Onchip modules
   OnchipReset(context);
   InvalidateCache(context);

   // Reset backtrace
   context->bt.numbacktrace = 0;

   SH2EvaluateInterrupt(context);

#ifdef DMPHISTORY
   memset(context->pchistory, 0, sizeof(context->pchistory));
   context->pchistory_index = 0;
#endif
}

//////////////////////////////////////////////////////////////////////////////

void SH2PowerOn(SH2_struct *context) {
   u32 VBR = SH2Core->GetVBR(context);
   SH2Core->SetPC(context, SH2MappedMemoryReadLong(context,VBR));
   SH2Core->SetGPR(context, 15, SH2MappedMemoryReadLong(context,VBR+4));
   CACHE_LOG("%s start\n", (context==SSH2)?"SSH2":"MSH2" );
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL SH2TestExec(SH2_struct *context, u32 cycles)
{
   SH2Core->TestExec(context, cycles);
}

void FASTCALL SH2Exec(SH2_struct *context, u32 cycles)
{
   int sh2start = context->cycles;
   context->SH2InterruptibleExec(context, cycles);
   FRTExec(context);
   WDTExec(context);
   DMAProc(context, context->cycles-sh2start);
}

void FASTCALL SH2OnFrame(SH2_struct *context) {
  SH2Core->OnFrame(context);
}
//////////////////////////////////////////////////////////////////////////////


void SH2Step(SH2_struct *context)
{
   if (SH2Core)
   {
      u32 tmp = SH2Core->GetPC(context);

      // Execute 1 instruction
      SH2Exec(context, 1);

      // Sometimes it doesn't always execute one instruction,
      // let's make sure it did
      if (tmp == SH2Core->GetPC(context))
         SH2Exec(context, 1);
   }
}

//////////////////////////////////////////////////////////////////////////////

int SH2StepOver(SH2_struct *context, void (*func)(void *, u32, void *))
{
   if (SH2Core)
   {
      u32 tmp = SH2Core->GetPC(context);
      u16 inst= SH2MappedMemoryReadWord(context, context->regs.PC);

      // If instruction is jsr, bsr, or bsrf, step over it
      if ((inst & 0xF000) == 0xB000 || // BSR
         (inst & 0xF0FF) == 0x0003 || // BSRF
         (inst & 0xF0FF) == 0x400B)   // JSR
      {
         // Set breakpoint after at PC + 4
         context->stepOverOut.callBack = func;
         context->stepOverOut.type = SH2ST_STEPOVER;
         context->stepOverOut.enabled = 1;
         context->stepOverOut.address = context->regs.PC+4;
         return 1;
      }
      else
      {
         // Execute 1 instruction instead
         SH2Exec(context, 1);

         // Sometimes it doesn't always execute one instruction,
         // let's make sure it did
         if (tmp == SH2Core->GetPC(context))
            SH2Exec(context, 1);
      }
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void SH2StepOut(SH2_struct *context, void (*func)(void *, u32, void *))
{
   if (SH2Core)
   {
      context->stepOverOut.callBack = func;
      context->stepOverOut.type = SH2ST_STEPOUT;
      context->stepOverOut.enabled = 1;
      context->stepOverOut.address = 0;
   }
}

//////////////////////////////////////////////////////////////////////////////

int SH2TrackInfLoopInit(SH2_struct *context)
{
   context->trackInfLoop.maxNum = 100;
   if ((context->trackInfLoop.match = calloc(context->trackInfLoop.maxNum, sizeof(tilInfo_struct))) == NULL)
      return -1;

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void SH2TrackInfLoopDeInit(SH2_struct *context)
{
   if (context->trackInfLoop.match)
      free(context->trackInfLoop.match);
}

//////////////////////////////////////////////////////////////////////////////

void SH2TrackInfLoopStart(SH2_struct *context)
{
   context->trackInfLoop.enabled = 1;
}

//////////////////////////////////////////////////////////////////////////////

void SH2TrackInfLoopStop(SH2_struct *context)
{
   context->trackInfLoop.enabled = 0;
}

//////////////////////////////////////////////////////////////////////////////

void SH2TrackInfLoopClear(SH2_struct *context)
{
   memset(context->trackInfLoop.match, 0, sizeof(tilInfo_struct) * context->trackInfLoop.maxNum);
   context->trackInfLoop.num = 0;
}

//////////////////////////////////////////////////////////////////////////////

void SH2HandleStepOverOut(SH2_struct *context)
{
   if (context->stepOverOut.enabled)
   {
      switch ((int)context->stepOverOut.type)
      {
      case SH2ST_STEPOVER: // Step Over
         if (context->regs.PC == context->stepOverOut.address)
         {
            context->stepOverOut.enabled = 0;
            context->stepOverOut.callBack(context, context->regs.PC, (void *)context->stepOverOut.type);
         }
         break;
      case SH2ST_STEPOUT: // Step Out
         {
            u16 inst;

            if (context->stepOverOut.levels < 0 && context->regs.PC == context->regs.PR)
            {
               context->stepOverOut.enabled = 0;
               context->stepOverOut.callBack(context, context->regs.PC, (void *)context->stepOverOut.type);
               return;
            }

            inst = context->instruction;;

            if ((inst & 0xF000) == 0xB000 || // BSR
               (inst & 0xF0FF) == 0x0003 || // BSRF
               (inst & 0xF0FF) == 0x400B)   // JSR
               context->stepOverOut.levels++;
            else if (inst == 0x000B || // RTS
                     inst == 0x002B)   // RTE
               context->stepOverOut.levels--;

            break;
         }
      default: break;
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

void SH2HandleTrackInfLoop(SH2_struct *context)
{
   if (context->trackInfLoop.enabled)
   {
      // Look for specific bf/bt/bra instructions that branch to address < PC
      if ((context->instruction & 0x8B80) == 0x8B80 || // bf
          (context->instruction & 0x8F80) == 0x8F80 || // bf/s
          (context->instruction & 0x8980) == 0x8980 || // bt
          (context->instruction & 0x8D80) == 0x8D80 || // bt/s
          (context->instruction & 0xA800) == 0xA800)   // bra
      {
         int i;

         // See if it's already on match list
         for (i = 0; i < context->trackInfLoop.num; i++)
         {
            if (context->regs.PC == context->trackInfLoop.match[i].addr)
            {
               context->trackInfLoop.match[i].count++;
               return;
            }
         }

         if (context->trackInfLoop.num >= context->trackInfLoop.maxNum)
         {
            context->trackInfLoop.match = realloc(context->trackInfLoop.match, sizeof(tilInfo_struct) * (context->trackInfLoop.maxNum * 2));
            context->trackInfLoop.maxNum *= 2;
         }

         // Add new
         i=context->trackInfLoop.num;
         context->trackInfLoop.match[i].addr = context->regs.PC;
         context->trackInfLoop.match[i].count = 1;
         context->trackInfLoop.num++;
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

void SH2NMI(SH2_struct *context)
{
   context->onchip.ICR |= 0x8000;
   SH2IntcSetNmi(context);
   SH2EvaluateInterrupt(context);
}

//////////////////////////////////////////////////////////////////////////////

void SH2GetRegisters(SH2_struct *context, sh2regs_struct * r)
{
   if (r != NULL) {
      SH2Core->GetRegisters(context, r);
   }
}

//////////////////////////////////////////////////////////////////////////////

void SH2SetRegisters(SH2_struct *context, sh2regs_struct * r)
{
   if (r != NULL) {
      SH2Core->SetRegisters(context, r);
   }
}

//////////////////////////////////////////////////////////////////////////////

void SH2WriteNotify(SH2_struct *context, u32 start, u32 length) {
   if (context == NULL) return;
   if (SH2Core->WriteNotify)
      SH2Core->WriteNotify(context, start, length);
}

//////////////////////////////////////////////////////////////////////////////
// Onchip specific
//////////////////////////////////////////////////////////////////////////////

void OnchipReset(SH2_struct *context) {
   context->onchip.SMR = 0x00;
   context->onchip.BRR = 0xFF;
   context->onchip.SCR = 0x00;
   context->onchip.TDR = 0xFF;
   context->onchip.SSR = 0x84;
   context->onchip.RDR = 0x00;
   context->onchip.TIER = 0x01;
   context->onchip.FTCSR = 0x00;
   context->onchip.FTCSRM = 0x00;
   context->onchip.FRC.all = 0x0000;
   context->onchip.OCRA = 0xFFFF;
   context->onchip.OCRB = 0xFFFF;
   context->onchip.TCR = 0x00;
   context->onchip.TOCR = 0xE0;
   context->onchip.FICR = 0x0000;
   context->onchip.IPRB = 0x0000;
   context->onchip.VCRA = 0x0000;
   context->onchip.VCRB = 0x0000;
   context->onchip.VCRC = 0x0000;
   context->onchip.VCRD = 0x0000;
   context->onchip.DRCR0 = 0x00;
   context->onchip.DRCR1 = 0x00;
   context->onchip.WTCSR = 0x18;
   context->onchip.WTCSRM = 0x0;
   context->onchip.WTCNT = 0x00;
   context->onchip.RSTCSR = 0x1F;
   context->onchip.SBYCR = 0x60;
   context->onchip.CCR = 0x00;
   context->onchip.ICR = 0x0000;
   context->onchip.IPRA = 0x0000;
   context->onchip.VCRWDT = 0x0000;
   context->onchip.DVCR = 0x00000000;
   context->onchip.VCRDIV = 0x00000000;
   context->onchip.BARA.all = 0x00000000;
   context->onchip.BAMRA.all = 0x00000000;
   context->onchip.BBRA = 0x0000;
   context->onchip.BARB.all = 0x00000000;
   context->onchip.BAMRB.all = 0x00000000;
   context->onchip.BDRB.all = 0x00000000;
   context->onchip.BDMRB.all = 0x00000000;
   context->onchip.BBRB = 0x0000;
   context->onchip.BRCR = 0x0000;
   context->onchip.CHCR0 = 0x00000000;
   context->onchip.CHCR1 = 0x00000000;
   context->onchip.DMAOR = 0x00000000;
   context->onchip.BCR1 &= 0x8000; // preserve MASTER bit
   context->onchip.BCR1 |= 0x03F0;
   context->onchip.BCR2 = 0x00FC;
   context->onchip.WCR = 0xAAFF;
   context->onchip.MCR = 0x0000;
   context->onchip.RTCSR = 0x0000;
   context->onchip.RTCNT = 0x0000;
   context->onchip.RTCOR = 0x0000;
}

//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL OnchipReadByte(SH2_struct *context, u32 addr) {
  switch(addr) {
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
      FRTExec(context);
      break;
    default:
      break;
  }
   switch(addr)
   {
      case 0x000:
         return context->onchip.SMR;
      case 0x001:
//         LOG("Bit Rate Register read: %02X\n", context->onchip.BRR);
         return context->onchip.BRR;
      case 0x002:
//         LOG("Serial Control Register read: %02X\n", context->onchip.SCR);
         return context->onchip.SCR;
      case 0x003:
//         LOG("Transmit Data Register read: %02X\n", context->onchip.TDR);
         return context->onchip.TDR;
      case 0x004:
//         LOG("Serial Status Register read: %02X\n", context->onchip.SSR);

/*
         // if Receiver is enabled, clear SSR's TDRE bit, set SSR's RDRF and update RDR.

         if (context->onchip.SCR & 0x10)
         {
            context->onchip.RDR = SCIReceiveByte();
            context->onchip.SSR = (context->onchip.SSR & 0x7F) | 0x40;
         }
         // if Transmitter is enabled, clear SSR's RDRF bit, and set SSR's TDRE bit.
         else if (context->onchip.SCR & 0x20)
         {
            context->onchip.SSR = (context->onchip.SSR & 0xBF) | 0x80;
         }
*/
         return context->onchip.SSR;
      case 0x005:
//         LOG("Receive Data Register read: %02X PC = %08X\n", context->onchip.RDR, SH2Core->GetPC(context));
         return context->onchip.RDR;
      case 0x010:
         return context->onchip.TIER;
      case 0x011:{
        context->onchip.FTCSRM = 0x00;
        return context->onchip.FTCSR;
      }
      case 0x012:
         return context->onchip.FRC.part.H;
      case 0x013:
         return context->onchip.FRC.part.L;
      case 0x014:
         if (!(context->onchip.TOCR & 0x10))
            return context->onchip.OCRA >> 8;
         else
            return context->onchip.OCRB >> 8;
      case 0x015:
         if (!(context->onchip.TOCR & 0x10))
            return context->onchip.OCRA & 0xFF;
         else
            return context->onchip.OCRB & 0xFF;
      case 0x016:
         return context->onchip.TCR;
      case 0x017:
         return context->onchip.TOCR;
      case 0x018:
         return context->onchip.FICR >> 8;
      case 0x019:
         return context->onchip.FICR & 0xFF;
      case 0x060:
         return context->onchip.IPRB >> 8;
      case 0x062:
         return context->onchip.VCRA >> 8;
      case 0x063:
         return context->onchip.VCRA & 0xFF;
      case 0x064:
         return context->onchip.VCRB >> 8;
      case 0x065:
         return context->onchip.VCRB & 0xFF;
      case 0x066:
         return context->onchip.VCRC >> 8;
      case 0x067:
         return context->onchip.VCRC & 0xFF;
      case 0x068:
         return context->onchip.VCRD >> 8;
      case 0x080:
         return context->onchip.WTCSR; // & 0x18;
      case 0x081:
         return context->onchip.WTCNT;
      case 0x092:
         return context->onchip.CCR;
      case 0x0E0:
         return context->onchip.ICR >> 8;
      case 0x0E1:
         return context->onchip.ICR & 0xFF;
      case 0x0E2:
         return context->onchip.IPRA >> 8;
      case 0x0E3:
         return context->onchip.IPRA & 0xFF;
      case 0x0E4:
         return context->onchip.VCRWDT >> 8;
      case 0x0E5:
         return context->onchip.VCRWDT & 0xFF;
      default:
         LOG("Unhandled Onchip byte read %08X\n", (int)addr);
         break;
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL OnchipReadWord(SH2_struct *context, u32 addr) {
  switch(addr) {
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
      FRTExec(context);
      break;
    default:
      break;
  }
   switch(addr)
   {
      case 0x012:
         return context->onchip.FRC.all;
      case 0x014:
         if (!(context->onchip.TOCR & 0x10))
            return context->onchip.OCRA;
         else
            return context->onchip.OCRB;
      case 0x060:
         return context->onchip.IPRB;
      case 0x062:
         return context->onchip.VCRA;
      case 0x064:
         return context->onchip.VCRB;
      case 0x066:
         return context->onchip.VCRC;
      case 0x068:
         return context->onchip.VCRD;
      case 0x0E0:
         return context->onchip.ICR;
      case 0x0E2:
         return context->onchip.IPRA;
      case 0x0E4:
         return context->onchip.VCRWDT;
      case 0x1E2: // real BCR1 register is located at 0x1E2-0x1E3; Sega Rally OK
         return context->onchip.BCR1;
      case 0x1E6:
         return context->onchip.BCR2;
      case 0x1EA:
         return context->onchip.WCR;
      case 0x1EE:
         return context->onchip.MCR;
      case 0x1F2:
         return context->onchip.RTCSR;
      case 0x1F6:
         return context->onchip.RTCNT;
      case 0x1FA:
         return context->onchip.RTCOR;
      default:
         LOG("Unhandled Onchip word read %08X\n", (int)addr);
         return 0;
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL OnchipReadLong(SH2_struct *context, u32 addr) {
  switch(addr) {
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
      FRTExec(context);
      break;
    default:
      break;
  }
   switch(addr)
   {
      case 0x100:
      case 0x120:
         return context->onchip.DVSR;
      case 0x104: // DVDNT
      case 0x124:
         return context->onchip.DVDNTL;
      case 0x108:
      case 0x128:
         return context->onchip.DVCR;
      case 0x10C:
      case 0x12C:
         return context->onchip.VCRDIV;
      case 0x110:
      case 0x130:
         return context->onchip.DVDNTH;
      case 0x114:
      case 0x134:
         return context->onchip.DVDNTL;
      case 0x118: // Acts as a separate register, but is set to the same value
      case 0x138: // as DVDNTH after division
         return context->onchip.DVDNTUH;
      case 0x11C: // Acts as a separate register, but is set to the same value
      case 0x13C: // as DVDNTL after division
         return context->onchip.DVDNTUL;
      case 0x180:
         return context->onchip.SAR0;
      case 0x184:
         return context->onchip.DAR0;
      case 0x188:
         return context->onchip.TCR0;
      case 0x18C:
         context->onchip.CHCR0M = 0;
         return context->onchip.CHCR0;
      case 0x190:
         return context->onchip.SAR1;
      case 0x194:
         return context->onchip.DAR1;
      case 0x198:
         return context->onchip.TCR1;
      case 0x19C:
          context->onchip.CHCR1M = 0;
         return context->onchip.CHCR1;
      case 0x1A0:
         return context->onchip.VCRDMA0;
      case 0x1A8:
         return context->onchip.VCRDMA1;
      case 0x1B0:
         return context->onchip.DMAOR;
      case 0x1E0:
         return context->onchip.BCR1;
      case 0x1E4:
         return context->onchip.BCR2;
      case 0x1E8:
         return context->onchip.WCR;
      case 0x1EC:
         return context->onchip.MCR;
      case 0x1F0:
         return context->onchip.RTCSR;
      case 0x1F4:
         return context->onchip.RTCNT;
      case 0x1F8:
         return context->onchip.RTCOR;
      default:
         LOG("Unhandled Onchip long read %08X\n", (int)addr);
         return 0;
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL OnchipWriteByte(SH2_struct *context, u32 addr, u8 val) {
  switch(addr) {
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
      FRTExec(context);
      break;
    default:
      break;
  }
   switch(addr) {
      case 0x000:
//         LOG("Serial Mode Register write: %02X\n", val);
         context->onchip.SMR = val;
         return;
      case 0x001:
//         LOG("Bit Rate Register write: %02X\n", val);
         context->onchip.BRR = val;
         return;
      case 0x002:
//         LOG("Serial Control Register write: %02X\n", val);

         // If Transmitter is getting disabled, set TDRE
         if (!(val & 0x20))
            context->onchip.SSR |= 0x80;

         context->onchip.SCR = val;
         return;
      case 0x003:
//         LOG("Transmit Data Register write: %02X. PC = %08X\n", val, SH2Core->GetPC(context));
         context->onchip.TDR = val;
         return;
      case 0x004:
//         LOG("Serial Status Register write: %02X\n", val);

         if (context->onchip.SCR & 0x20)
         {
            // Transmitter Mode

            // If the TDRE bit cleared, let's do a transfer
            if (!(val & 0x80))
               SCITransmitByte(context->onchip.TDR);

            // Generate an interrupt if need be here
         }
         return;
      case 0x010:

         context->onchip.TIER = (val & 0x8E) | 0x1;
         SH2EvaluateInterrupt(context);
         return;
      case 0x011:
         context->onchip.FTCSR = (context->onchip.FTCSR & ((val|context->onchip.FTCSRM) & 0x8E)) | (val & 0x1);
         SH2EvaluateInterrupt(context);
         return;
      case 0x012:
         context->onchip.FRC.part.H = val;
         context->frtcycles = context->cycles;
         return;
      case 0x013:
         context->onchip.FRC.part.L = val;
         context->frtcycles = context->cycles;
         return;
      case 0x014:
         if (!(context->onchip.TOCR & 0x10))
            context->onchip.OCRA = (val << 8) | (context->onchip.OCRA & 0xFF);
         else
            context->onchip.OCRB = (val << 8) | (context->onchip.OCRB & 0xFF);
         return;
      case 0x015:
         if (!(context->onchip.TOCR & 0x10))
            context->onchip.OCRA = (context->onchip.OCRA & 0xFF00) | val;
         else
            context->onchip.OCRB = (context->onchip.OCRB & 0xFF00) | val;
         return;
      case 0x016:
         context->onchip.TCR = val & 0x83;
         switch (val & 3)
         {
            case 0:
               context->frc.shift = 3;
               break;
            case 1:
               context->frc.shift = 5;
               break;
            case 2:
               context->frc.shift = 7;
               break;
            case 3:
               LOG("FRT external input clock not implemented.\n");
               break;
         }
         return;
      case 0x017:
         context->onchip.TOCR = 0xE0 | (val & 0x13);
         return;
      case 0x060:
         context->onchip.IPRB = (val << 8);
         SH2EvaluateInterrupt(context);
         return;
      case 0x061:
         return;
      case 0x062:
         context->onchip.VCRA = ((val & 0x7F) << 8) | (context->onchip.VCRA & 0x00FF);
         return;
      case 0x063:
         context->onchip.VCRA = (context->onchip.VCRA & 0xFF00) | (val & 0x7F);
         return;
      case 0x064:
         context->onchip.VCRB = ((val & 0x7F) << 8) | (context->onchip.VCRB & 0x00FF);
         return;
      case 0x065:
         context->onchip.VCRB = (context->onchip.VCRB & 0xFF00) | (val & 0x7F);
         return;
      case 0x066:
         context->onchip.VCRC = ((val & 0x7F) << 8) | (context->onchip.VCRC & 0x00FF);
         SH2EvaluateInterrupt(context);
         return;
      case 0x067:
         context->onchip.VCRC = (context->onchip.VCRC & 0xFF00) | (val & 0x7F);
         SH2EvaluateInterrupt(context);
         return;
      case 0x068:
         context->onchip.VCRD = (val & 0x7F) << 8;
         SH2EvaluateInterrupt(context);
         return;
      case 0x069:
         return;
      case 0x071:
         context->onchip.DRCR0 = val & 0x3;
         return;
      case 0x072:
         context->onchip.DRCR1 = val & 0x3;
         return;
      case 0x091:
         context->onchip.SBYCR = val & 0xDF;
         return;
      case 0x092:
         context->onchip.CCR = val & 0xCF;
		 if (val & 0x10){
			 InvalidateCache(context);
		 }
		 if ( (context->onchip.CCR & 0x01)  ){
                         enableCache(context);
		 }
		 else{
                         disableCache(context);
		 }
         return;
      case 0x0E0:
         context->onchip.ICR = ((val & 0x1) << 8) | (context->onchip.ICR & 0xFEFF);
         SH2EvaluateInterrupt(context);
         return;
      case 0x0E1:
         context->onchip.ICR = (context->onchip.ICR & 0xFFFE) | (val & 0x1);
         SH2EvaluateInterrupt(context);
         return;
      case 0x0E2:
         context->onchip.IPRA = (val << 8) | (context->onchip.IPRA & 0x00FF);
         SH2EvaluateInterrupt(context);
         return;
      case 0x0E3:
         context->onchip.IPRA = (context->onchip.IPRA & 0xFF00) | (val & 0xF0);
         SH2EvaluateInterrupt(context);
         return;
      case 0x0E4:
         context->onchip.VCRWDT = ((val & 0x7F) << 8) | (context->onchip.VCRWDT & 0x00FF);
         SH2EvaluateInterrupt(context);
         return;
      case 0x0E5:
         context->onchip.VCRWDT = (context->onchip.VCRWDT & 0xFF00) | (val & 0x7F);
         SH2EvaluateInterrupt(context);
         return;
      default:
         LOG("Unhandled Onchip byte write %08X\n", (int)addr);
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL OnchipWriteWord(SH2_struct *context, u32 addr, u16 val) {
  switch(addr) {
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
      FRTExec(context);
      break;
    default:
      break;
  }
   switch(addr)
   {
      case 0x060:
         context->onchip.IPRB = val & 0xFF00;
         SH2EvaluateInterrupt(context);
         return;
      case 0x062:
         context->onchip.VCRA = val & 0x7F7F;
         return;
      case 0x064:
         context->onchip.VCRB = val & 0x7F7F;
         return;
      case 0x066:
         context->onchip.VCRC = val & 0x7F7F;
         SH2EvaluateInterrupt(context);
         return;
      case 0x068:
         context->onchip.VCRD = val & 0x7F7F;
         SH2EvaluateInterrupt(context);
         return;
      case 0x080:
         // This and RSTCSR have got to be the most wackiest register
         // mappings I've ever seen

         if (val >> 8 == 0xA5)
         {
            // WTCSR
            switch (val & 7)
            {
               case 0:
                  context->wdt.shift = 1;
                  break;
               case 1:
                  context->wdt.shift = 6;
                  break;
               case 2:
                  context->wdt.shift = 7;
                  break;
               case 3:
                  context->wdt.shift = 8;
                  break;
               case 4:
                  context->wdt.shift = 9;
                  break;
               case 5:
                  context->wdt.shift = 10;
                  break;
               case 6:
                  context->wdt.shift = 12;
                  break;
               case 7:
                  context->wdt.shift = 13;
                  break;
            }

            context->wdt.isenable = (val & 0x20);
            context->wdt.isinterval = (~val & 0x40);

            context->onchip.WTCSR = (context->onchip.WTCSR & (context->onchip.WTCSRM | val) & 0x80) | (val & 0x67);
            context->onchip.WTCSR &= ~0x80;
            if(context->onchip.WTCSR & 0x20){
               context->onchip.SBYCR &= 0x7F;
            }else {
               context->onchip.WTCSR &= ~0x80;
               context->onchip.WTCNT = 0;
            }
            SH2EvaluateInterrupt(context);
         }
         else if (val >> 8 == 0x5A)
         {
            // WTCNT
            if(context->onchip.WTCSR & 0x20)
               context->onchip.WTCNT = (u8)val;
         }
         return;
      case 0x082:
         if (val == 0xA500)
            // clear WOVF bit
            context->onchip.RSTCSR &= 0x7F;
         else if (val >> 8 == 0x5A)
            // RSTE and RSTS bits
            context->onchip.RSTCSR = (context->onchip.RSTCSR & 0x80) | (val & 0x60) | 0x1F;
         return;
      case 0x092:
         context->onchip.CCR = val & 0xCF;
		 if (val & 0x10){
			 InvalidateCache(context);
		 }
		 if ( (context->onchip.CCR & 0x01)  ){
                         enableCache(context);
		 }
		 else{
                         disableCache(context);
		 }
         return;
      case 0x0E0:
         context->onchip.ICR = val & 0x0101;
         SH2EvaluateInterrupt(context);
         return;
      case 0x0E2:
         context->onchip.IPRA = val & 0xFFF0;
         SH2EvaluateInterrupt(context);
         return;
      case 0x0E4:
      case 0x0E5:
         context->onchip.VCRWDT = val & 0x7F7F;
         SH2EvaluateInterrupt(context);
         return;
      case 0x108:
      case 0x128:
         context->onchip.DVCR = val & 0x3;
         SH2EvaluateInterrupt(context);
         return;
      case 0x148:
         context->onchip.BBRA = val & 0xFF;
         return;
      case 0x178:
         context->onchip.BRCR = val & 0xF4DC;
         return;
      default:
         LOG("Unhandled Onchip word write %08X(%04X)\n", (int)addr, val);
         return;
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL OnchipWriteLong(SH2_struct *context, u32 addr, u32 val)  {
  switch(addr) {
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
      FRTExec(context);
      break;
    default:
      break;
  }
   switch (addr)
   {
   case 0x010:
     context->onchip.TIER = (val & 0x8E) | 0x1;
     SH2EvaluateInterrupt(context);
     break;
   case 0x060:
     context->onchip.IPRB = val & 0xFF00;
     SH2EvaluateInterrupt(context);
     break;
      case 0x100:
      case 0x120:
         context->onchip.DVSR = val;
         return;
      case 0x104: // 32-bit / 32-bit divide operation
      case 0x124:
      {
         s32 divisor = (s32) context->onchip.DVSR;
         if (divisor == 0)
         {
            // Regardless of what DVDNTL is set to, the top 3 bits
            // are used to create the new DVDNTH value
            if (val & 0x80000000)
            {
               context->onchip.DVDNTL = 0x80000000;
               context->onchip.DVDNTH = 0xFFFFFFFC | ((val >> 29) & 0x3);
            }
            else
            {
               context->onchip.DVDNTL = 0x7FFFFFFF;
               context->onchip.DVDNTH = 0 | (val >> 29);
            }
            context->onchip.DVDNTUL = context->onchip.DVDNTL;
            context->onchip.DVDNTUH = context->onchip.DVDNTH;
            context->onchip.DVCR |= 1;
            SH2EvaluateInterrupt(context);
         }
         else
         {
            s32 quotient = ((s32) val) / divisor;
            s32 remainder = ((s32) val) % divisor;

            if (quotient > 0x7FFFFFFF)
            {
               context->onchip.DVCR |= 1;
               context->onchip.DVDNTL = 0x7FFFFFFF;
               context->onchip.DVDNTH = 0xFFFFFFFE; // fix me
               SH2EvaluateInterrupt(context);
            }
            else if ((s32)((s64)quotient >> 32) < -1)
            {
               context->onchip.DVCR |= 1;
               context->onchip.DVDNTL = 0x80000000;
               context->onchip.DVDNTH = 0xFFFFFFFE; // fix me
               SH2EvaluateInterrupt(context);
            }
            else
            {
               context->onchip.DVDNTL = quotient;
               context->onchip.DVDNTH = remainder;
            }
            context->onchip.DVDNT = context->onchip.DVDNTL;
            context->onchip.DVDNTUL = context->onchip.DVDNTL;
            context->onchip.DVDNTUH = context->onchip.DVDNTH;
         }
         return;
      }
      case 0x108:
      case 0x128:
         context->onchip.DVCR = val & 0x3;
         SH2EvaluateInterrupt(context);
         return;
      case 0x10C:
      case 0x12C:
         context->onchip.VCRDIV = val & 0xFFFF;
         SH2EvaluateInterrupt(context);
         return;
      case 0x110:
      case 0x130:
         context->onchip.DVDNTH = val;
         return;
      case 0x114:
      case 0x134: { // 64-bit / 32-bit divide operation
         s32 divisor = (s32) context->onchip.DVSR;
         s64 dividend = context->onchip.DVDNTH;
         dividend = (s64)(((u64)dividend) << 32);
         dividend |= val;

         if (divisor == 0)
         {
            if (context->onchip.DVDNTH & 0x80000000)
            {
               context->onchip.DVDNTL = 0x80000000;
               context->onchip.DVDNTH = context->onchip.DVDNTH << 3; // fix me
            }
            else
            {
               context->onchip.DVDNTL = 0x7FFFFFFF;
               context->onchip.DVDNTH = context->onchip.DVDNTH << 3; // fix me
            }

            context->onchip.DVDNTUL = context->onchip.DVDNTL;
            context->onchip.DVDNTUH = context->onchip.DVDNTH;
            context->onchip.DVCR |= 1;
            SH2EvaluateInterrupt(context);
         }
         else
         {
            s64 quotient = dividend / divisor;
            s32 remainder = dividend % divisor;

            if (quotient > 0x7FFFFFFF)
            {
               context->onchip.DVCR |= 1;
               context->onchip.DVDNTL = 0x7FFFFFFF;
               context->onchip.DVDNTH = 0xFFFFFFFE; // fix me
               SH2EvaluateInterrupt(context);
            }
            else if ((s32)(quotient >> 32) < -1)
            {
               context->onchip.DVCR |= 1;
               context->onchip.DVDNTL = 0x80000000;
               context->onchip.DVDNTH = 0xFFFFFFFE; // fix me
               SH2EvaluateInterrupt(context);
            }
            else
            {
               context->onchip.DVDNTL = quotient;
               context->onchip.DVDNTH = remainder;
            }
            context->onchip.DVDNT = context->onchip.DVDNTL;
            context->onchip.DVDNTUL = context->onchip.DVDNTL;
            context->onchip.DVDNTUH = context->onchip.DVDNTH;
         }
         return;
      }
      case 0x118:
      case 0x138:
         context->onchip.DVDNTUH = val;
         return;
      case 0x11C:
      case 0x13C:
         context->onchip.DVDNTUL = val;
         return;
      case 0x140:
         context->onchip.BARA.all = val;
         return;
      case 0x144:
         context->onchip.BAMRA.all = val;
         return;
      case 0x180:
         context->onchip.SAR0 = val;
         return;
      case 0x184:
         context->onchip.DAR0 = val;
         return;
      case 0x188:
         context->onchip.TCR0 = val & 0xFFFFFF;
         return;
      case 0x18C:
        if (context->onchip.TCR0 != 0) {
          DMAProc(context, 0x7FFFFFFF);
        }
//         context->onchip.CHCR0 = val & 0xFFFF;

         context->onchip.CHCR0 = (val & ~2) | (context->onchip.CHCR0 & (val| context->onchip.CHCR0M) & 2);
         SH2EvaluateInterrupt(context);

         // If the DMAOR DME bit is set and AE and NMIF bits are cleared,
         // and CHCR's DE bit is set and TE bit is cleared,
         // do a dma transfer
         if ((context->onchip.DMAOR & 7) == 1 && (val & 0x3) == 1) {
            context->dma_ch0.copy_clock = 0;
            DMAExec(context);
         }
         return;
      case 0x190:
         context->onchip.SAR1 = val;
         return;
      case 0x194:
         context->onchip.DAR1 = val;
         return;
      case 0x198:
         context->onchip.TCR1 = val & 0xFFFFFF;
         return;
      case 0x19C:
        if (context->onchip.TCR1 != 0) {
          DMAProc(context, 0x7FFFFFFF);
        }
//         context->onchip.CHCR1 = val & 0xFFFF;

         context->onchip.CHCR1 = (val & ~2) | (context->onchip.CHCR1 & (val| context->onchip.CHCR1M) & 2);
         SH2EvaluateInterrupt(context);

         // If the DMAOR DME bit is set and AE and NMIF bits are cleared,
         // and CHCR's DE bit is set and TE bit is cleared,
         // do a dma transfer
         if ((context->onchip.DMAOR & 7) == 1 && (context->onchip.CHCR1 & 0x3) == 1) {
            context->dma_ch1.copy_clock = 0;
            DMAExec(context);
         }
         return;
      case 0x1A0:
         context->onchip.VCRDMA0 = val & 0xFFFF;
         SH2EvaluateInterrupt(context);
         return;
      case 0x1A8:
         context->onchip.VCRDMA1 = val & 0xFFFF;
         SH2EvaluateInterrupt(context);
         return;
      case 0x1B0:
         context->onchip.DMAOR = val & 0xF;

         // If the DMAOR DME bit is set and AE and NMIF bits are cleared,
         // and CHCR's DE bit is set and TE bit is cleared,
         // do a dma transfer
         if ((val & 7) == 1)
            DMAExec(context);
         return;
      case 0x1E0:
         context->onchip.BCR1 &= 0x8000;
         context->onchip.BCR1 |= val & 0x1FF7;
         return;
      case 0x1E4:
         context->onchip.BCR2 = val & 0xFC;
         return;
      case 0x1E8:
         context->onchip.WCR = val;
         return;
      case 0x1EC:
         context->onchip.MCR = val & 0xFEFC;
         return;
      case 0x1F0:
         context->onchip.RTCSR = val & 0xF8;
         return;
      case 0x1F8:
         context->onchip.RTCOR = val & 0xFF;
         return;
      default:
         LOG("Unhandled Onchip long write %08X,%08X\n", (int)addr, val);
         break;
   }
}

//////////////////////////////////////////////////////////////////////////////
#ifdef USE_CACHE
static void UpdateLRU(SH2_struct *context, u8 line, u8 way) {
//Table 8.3 SH7604_Hardware_Manual.pdf
  switch (way) {
    case 0:
      context->cacheLRU[line] &= 0x7;
    break;
    case 1:
      context->cacheLRU[line] &= 0x19;
      context->cacheLRU[line] |= 0x20;
    break;
    case 2:
      context->cacheLRU[line] &= 0x2A;
      context->cacheLRU[line] |= 0x14;
    break;
    case 3:
      context->cacheLRU[line] |= 0x0B;
    break;
    default:
    break;
  }
  // CACHE_LOG("%s : Update Line %d => way %d\n", (context==SSH2)?"SSH2":"MSH2", line, way);
}

static u8 getLRU(SH2_struct *context, u32 tag, u8 line) {
//Table 8.3 SH7604_Hardware_Manual.pdf
  u8 way = -1;
  if (context->onchip.CCR & (1 << 3))//2-way mode
  {
    if ((context->cacheLRU[line] & 1) == 1)
      return 2;
    else
      return 3;
  }
  else
  {
    if ((context->cacheLRU[line] & 0x38) == 0x38) way=0;
    else if ((context->cacheLRU[line] & 0x26) == 0x6) way=1;
    else if ((context->cacheLRU[line] & 0x15) == 0x1) way=2;
    else if ((context->cacheLRU[line] & 0x0B) == 0x0) way=3;
    //Init phase
    else if (context->cacheLRU[line] == 0xB) way=2;
    //Shall never be reached
    else if (context->cacheLRU[line] == 0x1E) way=1;
    else if (context->cacheLRU[line] == 0x38) way=0;

    // CACHE_LOG("%s : Line %d => way %d\n", (context==SSH2)?"SSH2":"MSH2", line, way);
  }
  return way;
}

static inline void CacheWriteThrough(SH2_struct *context, u8* mem, u32 addr, u32 val, u8 size) {
  SH2UpdateABusAccess(context, 1); //When cpu access CPU-BUs at the same time as SCU, there might be a penalty
  switch(size) {
  case 1:
    WriteByteList[(addr >> 16) & 0xFFF](context, mem, addr, val);
    break;
  case 2:
    WriteWordList[(addr >> 16) & 0xFFF](context, mem, addr, val);
    break;
  case 4:
    WriteLongList[(addr >> 16) & 0xFFF](context, mem, addr, val);
    break;
  }
}

static inline void CacheWriteVal(SH2_struct *context, u32 addr, u32 val, u8 size ) {
  u8 line = (addr>>4)&0x3F;
  u32 tag = (addr>>10)&0x7FFFF;
  u8 byte = addr&0xF;
  u8 way=context->tagWay[line][tag];
  switch(size) {
  case 1:
    WriteByteList[(addr >> 16) & 0xFFF](context,context->cacheData[line][way], byte, val);
    break;
  case 2:
    WriteWordList[(addr >> 16) & 0xFFF](context,context->cacheData[line][way], byte, val);
    break;
  case 4:
    WriteLongList[(addr >> 16) & 0xFFF](context,context->cacheData[line][way], byte, val);
    break;
  }
}

void CacheWrite(SH2_struct *context, u8* mem, u32 addr, u32 val, u8 size) {
  u8 line = (addr>>4)&0x3F;
  u32 tag = (addr>>10)&0x7FFFF;
  u8 byte = addr&0xF;
  u8 way=context->tagWay[line][tag];
  u8 ret = 0;
  if (byte + size > 16) CACHE_LOG("!!!!!!!!!!!!!!!! Warn out of line....\n");
  // CACHE_LOG("Write (%d %x) tag %x **** %x (%x %x)\n", line, way, context->cacheTagArray[line][way], tag, addr, val);
  if ((way <= 0x3) && (context->cacheTagArray[line][way] == tag)) {
    // Cache hit => update cache
    // CACHE_LOG("Hit Write (%d %x) tag %x **** %x (%x %x)\n", line, way, context->cacheTagArray[line][way], tag, addr, val);
    UpdateLRU(context, line, way);
    CacheWriteVal(context, addr, val, size);
    // for (int i =0; i<=0xF; i++) {
    //   printf("%x ", context->cacheData[line][way][i]);
    // }
    // printf("\n");
  }
  // else   CACHE_LOG("Write Miss (%d %x) tag %x **** %x (%x %x)\n", line, way, context->cacheTagArray[line][way], tag, addr, val);
  CacheWriteThrough(context, mem, addr, val, size);
}

void CacheWriteByte(SH2_struct *context, u8* mem, u32 addr, u8 val){
  CacheWrite(context, mem, addr, val, 1);
}
void CacheWriteWord(SH2_struct *context,u8* mem, u32 addr, u16 val){
  CacheWrite(context, mem, addr, val, 2);
}
void CacheWriteLong(SH2_struct *context,u8* mem, u32 addr, u32 val){
  CacheWrite(context, mem, addr, val, 4);
}
#endif

void InvalidateCache(SH2_struct *ctx) {
#ifdef USE_CACHE
  if (yabsys.usecache == 0) return;
  memset(ctx->cacheLRU, 0, 64);
  memset(ctx->tagWay, 0x4, 64*0x80000);
  memset(ctx->cacheTagArray, 0x0, 64*4*sizeof(u32));
  SH2WriteNotify(ctx, 0, 0x1000);
#endif
}

void enableCache(SH2_struct *context) {
#ifdef USE_CACHE
  int i;
  if (yabsys.usecache == 0) return;
  if (context->cacheOn == 0) {
    context->cacheOn = 1;
    context->nbCacheWay = 4;
    for (i=0x20; i < 0x30; i++)
    {
      //LowWRam is cached
       CacheReadByteList[i] = CacheReadByte;
       CacheReadWordList[i] = CacheReadWord;
       CacheReadLongList[i] = CacheReadLong;
       CacheWriteByteList[i] = CacheWriteByte;
       CacheWriteWordList[i] = CacheWriteWord;
       CacheWriteLongList[i] = CacheWriteLong;
    }
    for (i=0x600; i < 0x800; i++)
    {
      //HiWRam is cached
       CacheReadByteList[i] = CacheReadByte;
       CacheReadWordList[i] = CacheReadWord;
       CacheReadLongList[i] = CacheReadLong;
       CacheWriteByteList[i] = CacheWriteByte;
       CacheWriteWordList[i] = CacheWriteWord;
       CacheWriteLongList[i] = CacheWriteLong;
    }
  }
#else
  return;
#endif
}

void disableCache(SH2_struct *context) {
#ifdef USE_CACHE
  int i;
  if (context->cacheOn == 1) {
    context->cacheOn = 0;
    for (i=0x20; i < 0x30; i++)
    {
      //LowWRam is cached
      CacheReadByteList[i] = ReadByteList[i];
      CacheReadWordList[i] = ReadWordList[i];
      CacheReadLongList[i] = ReadLongList[i];
      CacheWriteByteList[i] = WriteByteList[i];
      CacheWriteWordList[i] = WriteWordList[i];
      CacheWriteLongList[i] = WriteLongList[i];
    }
    for (i=0x600; i < 0x800; i++)
    {
      //HiWRam is cached
      CacheReadByteList[i] = ReadByteList[i];
      CacheReadWordList[i] = ReadWordList[i];
      CacheReadLongList[i] = ReadLongList[i];
      CacheWriteByteList[i] = WriteByteList[i];
      CacheWriteWordList[i] = WriteWordList[i];
      CacheWriteLongList[i] = WriteLongList[i];
    }
    InvalidateCache(context);
  }
#else
  return;
#endif
}

#ifdef USE_CACHE
void CacheFetch(SH2_struct *context, u8* memory, u32 addr, u8 way) {
  u8 line = (addr>>4)&0x3F;
  u32 tag = (addr>>10)&0x7FFFF;
  SH2UpdateABusAccess(context, 1); //When cpu access CPU-BUs at the same time as SCU, there might be a penalty
  UpdateLRU(context, line, way);
  context->tagWay[line][tag] = way;
  context->cacheTagArray[line][way] = tag;
  for (int i=0; i<4; i++) {
    u32 ret = ReadLongList[(addr >> 16) & 0xFFF](context, memory,(addr&(~0xF))|(i*4));
    CacheWriteVal(context, (addr&(~0xF))|(i*4), ret, 4);
    // printf("Fetch (%x) (%d)=%x\n", (addr&(~0xF))|(i*4), i, ret);
  }
  SH2WriteNotify(context, (addr&(~0xF)), 4);
  // for (int i =0; i<=0xF; i++) {
  //   printf("%x ", context->cacheData[line][way][i]);
  // }
  // printf("\n");
}

u8 CacheReadByte(SH2_struct *context,u8* memory, u32 addr) {
  u8 line = (addr>>4)&0x3F;
  u32 tag = (addr>>10)&0x7FFFF;
  u8 byte = addr&0xF;
  u8 way = context->tagWay[line][tag];
  if (byte + 1 > 16) CACHE_LOG("!!!!!!!!!!!!!!!! Warn out of line....\n");
  if ((way <= 0x3) && (context->cacheTagArray[line][way] == tag)) {
    UpdateLRU(context, line, way);
    u8 ret = ReadByteList[(addr >> 16) & 0xFFF](context,context->cacheData[line][way],byte);
#ifdef CACHE_DEBUG
    if (ret != ReadByteList[(addr >> 16) & 0xFFF](context, memory, addr)) {
      YuiMsg("Read Byte addr %x from cache = %x (%x)\n", addr, ret, ReadByteList[(addr >> 16) & 0xFFF](context, memory, addr));
      fflush(stdout);
      abort();
    }
#endif
    return ret;
  }
  way = getLRU(context, tag, line);
  CacheFetch(context, memory, addr, way);
  u8 ret = ReadByteList[(addr >> 16) & 0xFFF](context,context->cacheData[line][way],byte);
#ifdef CACHE_DEBUG
  if (ret != ReadByteList[(addr >> 16) & 0xFFF](context, memory, addr)) {
    YuiMsg("Read Byte addr %x out of cache = %x (%x)\n", addr, ret, ReadByteList[(addr >> 16) & 0xFFF](context, memory, addr));
    fflush(stdout);
    abort();
  }
#endif
  return ret;
}

u16 CacheReadWord(SH2_struct *context,u8* memory, u32 addr) {
  u8 line = (addr>>4)&0x3F;
  u32 tag = (addr>>10)&0x7FFFF;
  u8 byte = (addr&0xF);
  u8 way = context->tagWay[line][tag];
  if (byte + 2 > 16) CACHE_LOG("!!!!!!!!!!!!!!!! Warn out of line....\n");
  if ((way <= 0x3) && (context->cacheTagArray[line][way] == tag)) {
    UpdateLRU(context, line, way);
    u16 ret = ReadWordList[(addr >> 16) & 0xFFF](context, context->cacheData[line][way],byte);
#ifdef CACHE_DEBUG
    if (ret != ReadWordList[(addr >> 16) & 0xFFF](context, memory, addr)) {
      YuiMsg("Read Word addr %x (%x) from of cache = %x (%x)\n", addr, (addr >> 16) & 0xFFF, ret, ReadWordList[(addr >> 16) & 0xFFF](context, memory, addr));
      fflush(stdout);
      abort();
    }
#endif
    return ret;
  }
  way = getLRU(context, tag, line);
  CacheFetch(context, memory, addr, way);
  u16 ret = ReadWordList[(addr >> 16) & 0xFFF](context, context->cacheData[line][way],byte);
#ifdef CACHE_DEBUG
  if (ret != ReadWordList[(addr >> 16) & 0xFFF](context, memory, addr)) {
    YuiMsg("Read Word addr %x (%x) out of cache = %x (%x)\n", addr, (addr >> 16) & 0xFFF, ret, ReadWordList[(addr >> 16) & 0xFFF](context, memory, addr));
    fflush(stdout);
    abort();
  }
#endif
  return ret;
}

u32 CacheReadLong(SH2_struct *context,u8* memory, u32 addr) {
  u8 line = (addr>>4)&0x3F;
  u32 tag = (addr>>10)&0x7FFFF;
  u8 byte = (addr&0xF);
  u8 way = context->tagWay[line][tag];
  if (byte + 4 > 16) CACHE_LOG("!!!!!!!!!!!!!!!! Warn out of line....\n");
  if ((way <= 0x3) && (context->cacheTagArray[line][way] == tag)) {
    UpdateLRU(context, line, way);
    u32 ret = ReadLongList[(addr >> 16) & 0xFFF](context,context->cacheData[line][way],byte);
#ifdef CACHE_DEBUG
    if (ret != ReadLongList[(addr >> 16) & 0xFFF](context, memory, addr)) {
      YuiMsg("Read Long addr %x from cache = %x (%x)\n", addr, ret, ReadLongList[(addr >> 16) & 0xFFF](context, memory, addr));
      fflush(stdout);
      abort();
    }
#endif
    return ret;
  }
  way = getLRU(context, tag, line);
  CacheFetch(context, memory, addr, way);
  u32 ret = ReadLongList[(addr >> 16) & 0xFFF](context,context->cacheData[line][way],byte);
#ifdef CACHE_DEBUG
  if (ret != ReadLongList[(addr >> 16) & 0xFFF](context, memory, addr)) {
    YuiMsg("Read Long addr %x out of cache = %x (%x)\n", addr, ret, ReadLongList[(addr >> 16) & 0xFFF](context, memory, addr));
    fflush(stdout);
    abort();
  }
#endif
  return ret;
}
#endif

void CacheInvalidate(SH2_struct *context,u32 addr){
#ifdef USE_CACHE
  if (yabsys.usecache == 0) return;
  u8 line = (addr>>4)&0x3F;
  u32 tag = (addr>>10)&0x7FFFF;
  u8 way = context->tagWay[line][tag];
  context->tagWay[line][tag] = 0x4;
  if (way <= 0x3) context->cacheTagArray[line][way] = 0x0;
  context->cacheLRU[line] = 0;
#endif
}

u32 FASTCALL AddressArrayReadLong(SH2_struct *context,u32 addr) {
#ifdef USE_CACHE
  if (yabsys.usecache == 0) return 0;
  u8 line = (addr>>4)&0x3F;
  u8 way = (context->onchip.CCR>>6)&0x3;
  return ((context->cacheLRU[line]&0x3F)<<4) | ((context->cacheTagArray[line][way]&0x7FFFF)<<10) | ((context->cacheTagArray[line][way]!= 0x0)<<1);
#else
  return 0;
#endif
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL AddressArrayWriteLong(SH2_struct *context,u32 addr, u32 val)  {
#ifdef USE_CACHE
  if (yabsys.usecache == 0) return;
  u8 line = (addr>>4)&0x3F;
  u32 tag = (addr>>10)&0x7FFFF;
  u8 valid = (addr>>2)&0x1;
  u8 way = (context->onchip.CCR>>6)&0x3;
  context->cacheLRU[line] = (val>>4)&0x3F;
  if (valid) {
    context->tagWay[line][tag] = way;
    context->cacheTagArray[line][way] = tag;
  } else {
    context->tagWay[line][tag] = 0x4;
    context->cacheTagArray[line][way] = 0x0;
  }
#endif
}

//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL DataArrayReadByte(SH2_struct *context,u32 addr) {
  return T2ReadByte(context->DataArray, addr & 0xFFF);
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL DataArrayReadWord(SH2_struct *context,u32 addr) {
  return T2ReadWord(context->DataArray, addr & 0xFFF);
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL DataArrayReadLong(SH2_struct *context,u32 addr) {
  return T2ReadLong(context->DataArray, addr & 0xFFF);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL DataArrayWriteByte(SH2_struct *context,u32 addr, u8 val)  {
  T2WriteByte(context->DataArray, addr & 0xFFF, val);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL DataArrayWriteWord(SH2_struct *context,u32 addr, u16 val)  {
  T2WriteWord(context->DataArray, addr & 0xFFF, val);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL DataArrayWriteLong(SH2_struct *context,u32 addr, u32 val)  {
  T2WriteLong(context->DataArray, addr & 0xFFF, val);
}

//////////////////////////////////////////////////////////////////////////////

void FRTExec(SH2_struct *context)
{
   u32 frcold;
   u32 frctemp;
   u32 mask;

   u32 cycles = context->cycles - context->frtcycles;

   context->frtcycles = context->cycles;

   frcold = frctemp = (u32)context->onchip.FRC.all;
   mask = (1 << context->frc.shift) - 1;

   // Increment FRC
   frctemp += ((cycles + context->frc.leftover) >> context->frc.shift);
   context->frc.leftover = (cycles + context->frc.leftover) & mask;

   // Check to see if there is or was a Output Compare A match
   if ((frctemp >= context->onchip.OCRA) && (frcold < context->onchip.OCRA) && ((context->onchip.FTCSR & 0x8)==0))
   {
      // Do we need to clear the FRC?
      if (context->onchip.FTCSR & 0x1)
      {
         frctemp = 0;
         context->frc.leftover = (context->frc.leftover+((frctemp-context->onchip.OCRA) << context->frc.shift));
      }

      // Set OCFA flag
      context->onchip.FTCSR |= 0x8;
      context->onchip.FTCSRM |= 0x8;
      SH2EvaluateInterrupt(context);
   }

   // Check to see if there is or was a Output Compare B match
   if ((frctemp >= context->onchip.OCRB) && (frcold < context->onchip.OCRB) && ((context->onchip.FTCSR & 0x4)==0))
   {
      // Set OCFB flag
      context->onchip.FTCSR |= 0x4;
      context->onchip.FTCSRM |= 0x4;
      SH2EvaluateInterrupt(context);
   }

   // If FRC overflows, set overflow flag
   if (frctemp > 0xFFFF) {
     if ((context->onchip.FTCSR & 0x2)== 0x0)
     {
       context->onchip.FTCSR |= 0x2;
       context->onchip.FTCSRM |= 0x2;
       SH2EvaluateInterrupt(context);
     }
     frctemp = 0;
     context->frc.leftover = (context->frc.leftover+((frctemp>>16) << context->frc.shift));
   }

   // Write new FRC value
   context->onchip.FRC.all = frctemp;
}

//////////////////////////////////////////////////////////////////////////////

void WDTExec(SH2_struct *context) {
   u32 wdttemp;
   u32 mask;

   u32 cycles = context->cycles - context->wdtcycles;

   context->wdtcycles = context->cycles;

   if ((!context->wdt.isenable) || (context->onchip.WTCSR & 0x80) || (context->onchip.RSTCSR & 0x80))
      return;

   wdttemp = (u32)context->onchip.WTCNT;
   mask = (1 << context->wdt.shift) - 1;
   wdttemp += ((cycles + context->wdt.leftover) >> context->wdt.shift);
   context->wdt.leftover = (cycles + context->wdt.leftover) & mask;

   // Are we overflowing?
   if (wdttemp > 0xFF)
   {
      // Obviously depending on whether or not we're in Watchdog or Interval
      // Modes, they'll handle an overflow differently.
      if ((context->onchip.WTCSR & 0x80)==0) {
        if (context->wdt.isinterval)
        {
          // Interval Timer Mode

          // Set OVF flag
          context->onchip.WTCSR |= 0x80;
          SH2EvaluateInterrupt(context);
        }
        else
        {
          // Watchdog Timer Mode(untested)
          YabErrorMsg("Watchdog timer(WDT mode) overflow not implemented\n");
        }
      }
   }

   // Write new WTCNT value
   context->onchip.WTCNT = (u8)wdttemp;
}

//////////////////////////////////////////////////////////////////////////////

void DMAExec(SH2_struct *context) {
  DMAProc(context, 200);
}

int DMAProc(SH2_struct *context, int cycles ){

   if (context->onchip.DMAOR & 0x6)
      return 0;


   if ( ((context->onchip.CHCR0 & 0x3)==0x01)  && ((context->onchip.CHCR1 & 0x3)==0x01) ) { // both channel wants DMA
      if (context->onchip.DMAOR & 0x8) { // round robin priority

        if ((context->onchip.CHCR0 & 0x08) == 0) { cycles <<= 1; } //Dual Chanel

        DMATransferCycles(context, &context->dma_ch0, cycles);
        DMATransferCycles(context, &context->dma_ch1, cycles);

       }
      else { // channel 0 > channel 1 priority

         if( (context->onchip.CHCR0 & 0x03) == 0x01 ){
           if ((context->onchip.CHCR0 & 0x08) == 0) { cycles <<= 1; } //Dual Chanel
           DMATransferCycles(context, &context->dma_ch0, cycles);
         }else if( (context->onchip.CHCR1 &0x03) == 0x01 ) {
           if ((context->onchip.CHCR1 & 0x08) == 0) { cycles <<= 1; } //Dual Chanel
           DMATransferCycles(context, &context->dma_ch1, cycles);
         }
      }
   }
   else { // only one channel wants DMA
	   if (((context->onchip.CHCR0 & 0x3) == 0x01)) { // DMA for channel 0

       if ((context->onchip.CHCR0 & 0x08) == 0) { cycles <<= 1;  } //Dual Chanel
       DMATransferCycles(context, &context->dma_ch0, cycles);
       return 0;
      }else if (((context->onchip.CHCR1 & 0x3) == 0x01)) { // DMA for channel 1
        if ((context->onchip.CHCR1 & 0x08) == 0) { cycles <<= 1; } //Dual Chanel
         DMATransferCycles(context, &context->dma_ch1, cycles);
         return 0;
      }
   }
   return 0;
}

int getEatClock(u32 src, u32 dst) {
  switch (src & 0x0FF00000) {
  case 0x05800000:
    return 1;
    break;
  case 0x05E00000: // VDP2 RAM
    switch (dst & 0x0FF00000) {
    case 0x06000000: // High
      return 44;
      break;
    case 0x00200000: // Low
      return 50;
      break;
    case 0x05A00000: // SOUND RAM
    case 0x05B00000: // SOUND REG
      return 427;
      break;
    case 0x05C00000: // VDP1 RAM
      return 427;
    case 0x05D00000: // VDP1 REG
      return 427;
      break;
    case 0x05E00000: // VDP2 RAM
      return 1;
      break;
    case 0x05F00000: // VDP2 REG
      return 50;
      break;
    default:
      return 44;
      break;
    }
    break;
  case 0x05C00000: // VDP1 RAM
    switch (dst & 0x0FF00000) {
    case 0x06000000: // High
      return 50;
      break;
    case 0x00200000: // Low
      return 50;
      break;
    case 0x05A00000: // SOUND RAM
    case 0x05B00000: // SOUND REG
      return 50;
      break;
    case 0x05C00000: // VDP1 RAM
      return 570;
    case 0x05D00000: // VDP1 REG
      return 570;
      break;
    case 0x05E00000: // VDP2 RAM
      return 225;
      break;
    case 0x05F00000: // VDP2 REG
      return 50;
      break;
    default:
      return 44;
      break;
    }
    break;
  case 0x06000000: // High
  case 0x00200000: // Low
  default:
    switch (dst & 0x0FF00000) {
    case 0x06000000: // High
    case 0x00200000: // Low
      return 14;
      break;
    case 0x05A00000: // SOUND RAM
    case 0x05B00000: // SOUND REG
      return 20;
      break;
    case 0x05C00000: // VDP1 RAM
      return 14;
    case 0x05D00000: // VDP1 REG
      return 30;
      break;
    case 0x05E00000: // VDP2 RAM
      return 82;
      break;
    case 0x05F00000: // VDP2 REG
      return 14;
      break;
    default:
      return 14;
      break;
    }
    break;
  }

  return 14;

}

void DMATransferCycles(SH2_struct *context, Dmac * dmac, int cycles ){
   int size;
   u32 i = 0;
   int count;

   //LOG("sh2 dma src=%08X,dst=%08X,%d type:%d cycle:%d\n", *dmac->SAR, *dmac->DAR, *dmac->TCR, ((*dmac->CHCR & 0x0C00) >> 10), cycles);
   if (isDMABlocked(context)) {
     return;
   }

   if (!(*dmac->CHCR & 0x2)) { // TE is not set
      int srcInc;
      int destInc;

      int type = ((*dmac->CHCR & 0x0C00) >> 10);
      int eat = getEatClock(*dmac->SAR, *dmac->DAR);

      dmac->copy_clock += cycles;
      if (dmac->copy_clock < eat) return;

      switch(*dmac->CHCR & 0x3000) {
         case 0x0000: srcInc = 0; break;
         case 0x1000: srcInc = 1; break;
         case 0x2000: srcInc = -1; break;
         default: srcInc = 0; break;
      }

      switch(*dmac->CHCR & 0xC000) {
         case 0x0000: destInc = 0; break;
         case 0x4000: destInc = 1; break;
         case 0x8000: destInc = -1; break;
         default: destInc = 0; break;
      }

      switch (type) {
         case 0:
            while( dmac->copy_clock >= 0 )  {
               dmac->copy_clock -= eat;
				       DMAMappedMemoryWriteByte(*dmac->DAR, DMAMappedMemoryReadByte(*dmac->SAR));
               *dmac->SAR += srcInc;
               *dmac->DAR += destInc;
               *dmac->TCR -= 1;
               i++;
               if( *dmac->TCR <= 0 ){
                 LOG("DMA finished");
                  // Set Transfer End bit
                  *dmac->CHCR |= 0x2;
                  *dmac->CHCRM |= 0x2;
                  SH2EvaluateInterrupt(context);
                  if (context->cacheOn == 0) SH2WriteNotify(context, destInc<0 ? *dmac->DAR : *dmac->DAR - i*destInc, i*abs(destInc));
                  return;
               }
            }
            break;
         case 1:
            destInc *= 2;
            srcInc *= 2;
            while (dmac->copy_clock >= 0) {
              dmac->copy_clock -= eat;
				      DMAMappedMemoryWriteWord(*dmac->DAR, DMAMappedMemoryReadWord(*dmac->SAR));
               *dmac->SAR += srcInc;
               *dmac->DAR += destInc;
               *dmac->TCR -= 1;
               i++;
               if( *dmac->TCR <= 0 ){
                  LOG("DMA finished");
                  // Set Transfer End bit
                  *dmac->CHCR |= 0x2;
                  *dmac->CHCRM |= 0x2;
                  SH2EvaluateInterrupt(context);
                  if (context->cacheOn == 0) SH2WriteNotify(context, destInc<0 ? *dmac->DAR : *dmac->DAR - i*destInc, i*abs(destInc));
                  return;
               }
            }
            break;
         case 2:
            destInc *= 4;
            srcInc *= 4;
            while (dmac->copy_clock >= 0) {
              dmac->copy_clock -= eat;
               u32 val = DMAMappedMemoryReadLong(*dmac->SAR);
               //printf("CPU DMA src:%08X dst:%08X val:%08X\n", *SAR, *DAR, val);
				       DMAMappedMemoryWriteLong(*dmac->DAR,val);
               *dmac->DAR += destInc;
               *dmac->SAR += srcInc;
               *dmac->TCR -= 1;
               i++;
               if( *dmac->TCR <= 0 ){
                 LOG("DMA finished");
                  *dmac->CHCR |= 0x2;
                  *dmac->CHCRM |= 0x2;
                  SH2EvaluateInterrupt(context);
                  if (context->cacheOn == 0) SH2WriteNotify(context, destInc<0 ? *dmac->DAR : *dmac->DAR - i*destInc, i*abs(destInc));
                  return;
               }
            }
            break;
         case 3:
           destInc *= 4;
           srcInc *= 4;
           while (dmac->copy_clock >= 0) {
             dmac->copy_clock -= (eat>>2);
             u32 val = DMAMappedMemoryReadLong(*dmac->SAR);
             //printf("CPU DMA src:%08X dst:%08X val:%08X\n", *SAR, *DAR, val);
             DMAMappedMemoryWriteLong(*dmac->DAR, val);
             *dmac->DAR += destInc;
             *dmac->SAR += srcInc;
             *dmac->TCR -= 1;
             i++;
             if (*dmac->TCR <= 0) {
               LOG("DMA finished");
               *dmac->CHCR |= 0x2;
               *dmac->CHCRM |= 0x2;
               SH2EvaluateInterrupt(context);
               if (context->cacheOn == 0) SH2WriteNotify(context, destInc<0 ? *dmac->DAR : *dmac->DAR - i*destInc, i*abs(destInc));
               return;
             }
           }
           break;
      }
      if (context->cacheOn == 0) SH2WriteNotify(context, destInc<0?*dmac->DAR:*dmac->DAR-i*destInc,i*abs(destInc));
   }

}

//////////////////////////////////////////////////////////////////////////////
// Input Capture Specific
//////////////////////////////////////////////////////////////////////////////

void FASTCALL MSH2InputCaptureWriteWord(SH2_struct *context, UNUSED u8* memory, UNUSED u32 addr, UNUSED u16 data)
{
   FRTExec(MSH2);
   // Set Input Capture Flag
   MSH2->onchip.FTCSR |= 0x80;
   MSH2->onchip.FTCSRM |= 0x80;

   // Copy FRC register to FICR
   MSH2->onchip.FICR = MSH2->onchip.FRC.all;
   //Ensure there is some delay between input capture flag and effective interrupt handling
   //Docs says it takes around 4 instructions to accept an interrupt
   //And some games like Scorcher are using this delay to write some usefull values for the slave
   if ((context->target_cycles - context->cycles)<10) context->target_cycles += 10;
   SH2EvaluateInterrupt(MSH2);

}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL SSH2InputCaptureWriteWord(SH2_struct *context, UNUSED u8* memory, UNUSED u32 addr, UNUSED u16 data)
{
   FRTExec(SSH2);
   // Set Input Capture Flag
   SSH2->onchip.FTCSR |= 0x80;
   SSH2->onchip.FTCSRM |= 0x80;

   // Copy FRC register to FICR
   SSH2->onchip.FICR = SSH2->onchip.FRC.all;

   //Ensure there is some delay between input capture flag and effective interrupt handling
   //Docs says it takes around 4 instructions to accept an interrupt
   //And some games like Scorcher are using this delay to write some usefull values for the slave
   if ((context->target_cycles - context->cycles)<10) context->target_cycles += 10;
   SH2EvaluateInterrupt(SSH2);
}

//////////////////////////////////////////////////////////////////////////////
// SCI Specific
//////////////////////////////////////////////////////////////////////////////

u8 SCIReceiveByte(void) {
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void SCITransmitByte(UNUSED u8 val) {
}

//////////////////////////////////////////////////////////////////////////////

int SH2SaveState(SH2_struct *context, void ** stream)
{
   int offset;
   sh2regs_struct regs;

   // Write header
   if (context->isslave == 0)
      offset = MemStateWriteHeader(stream, "MSH2", 3);
   else
   {
      offset = MemStateWriteHeader(stream, "SSH2", 3);
      MemStateWrite((void *)&yabsys.IsSSH2Running, 1, 1, stream);
   }

   // Write registers
   SH2GetRegisters(context, &regs);
   MemStateWrite((void *)&regs, sizeof(sh2regs_struct), 1, stream);

   // Write onchip registers
   MemStateWrite((void *)&context->onchip, sizeof(Onchip_struct), 1, stream);

   // Write internal variables
   // FIXME: write the clock divisor rather than the shift amount for
   // backward compatibility (fix this next time the save state version
   // is updated)
   context->frc.shift = 1 << context->frc.shift;
   MemStateWrite((void *)&context->frc, sizeof(context->frc), 1, stream);
   {
      u32 div = context->frc.shift;
      context->frc.shift = 0;
      while ((div >>= 1) != 0)
         context->frc.shift++;
   }
   MemStateWrite((void *)context->AddressArray, sizeof(u32), 0x100, stream);
   MemStateWrite((void *)context->DataArray, sizeof(u8), 0x1000, stream);
   MemStateWrite((void *)&context->target_cycles, sizeof(u32), 1, stream);
   MemStateWrite((void *)&context->cycles, sizeof(u32), 1, stream);
   MemStateWrite((void *)&context->isslave, sizeof(u8), 1, stream);
   MemStateWrite((void *)&context->instruction, sizeof(u16), 1, stream);

   MemStateWrite((void *)&context->dma_ch0.copy_clock, sizeof(u32), 1, stream);
   MemStateWrite((void *)&context->dma_ch1.copy_clock, sizeof(u32), 1, stream);

   return MemStateFinishHeader(stream, offset);
}

//////////////////////////////////////////////////////////////////////////////

int SH2LoadState(SH2_struct *context, const void * stream, UNUSED int version, int size)
{
   sh2regs_struct regs;

   SH2Reset(context);

   if (context->isslave == 1)
      MemStateRead((void *)&yabsys.IsSSH2Running, 1, 1, stream);

   // Read registers
   MemStateRead((void *)&regs, sizeof(sh2regs_struct), 1, stream);
   SH2SetRegisters(context, &regs);

   // Read onchip registers
   if (version < 2) {
      MemStateRead((void *)&context->onchip, sizeof(Onchip_struct)-sizeof(u32)/*WTCSRM*/, 1, stream);
   }else {
     MemStateRead((void *)&context->onchip, sizeof(Onchip_struct), 1, stream);
   }

   // Read internal variables
   MemStateRead((void *)&context->frc, sizeof(context->frc), 1, stream);
   {  // FIXME: backward compatibility hack (see SH2SaveState() comment)
      u32 div = context->frc.shift;
      context->frc.shift = 0;
      while ((div >>= 1) != 0)
         context->frc.shift++;
   }
   MemStateRead((void *)context->AddressArray, sizeof(u32), 0x100, stream);
   MemStateRead((void *)context->DataArray, sizeof(u8), 0x1000, stream);
   MemStateRead((void *)&context->target_cycles, sizeof(u32), 1, stream);
   MemStateRead((void *)&context->cycles, sizeof(u32), 1, stream);
   MemStateRead((void *)&context->isslave, sizeof(u8), 1, stream);
   MemStateRead((void *)&context->instruction, sizeof(u16), 1, stream);

   if (version >= 3) {
     MemStateRead((void *)&context->dma_ch0.copy_clock, sizeof(u32), 1, stream);
     MemStateRead((void *)&context->dma_ch1.copy_clock, sizeof(u32), 1, stream);
   }

   return size;
}



void SH2DumpHistory(SH2_struct *context){

#ifdef DMPHISTORY
	FILE * history = NULL;
	history = fopen("history.txt", "w");
	if (history){
		int i;
		int index = context->pchistory_index;
		for (i = 0; i < (MAX_DMPHISTORY - 1); i++){
		  char lineBuf[128];
		  SH2Disasm(context->pchistory[(index & (MAX_DMPHISTORY - 1))], SH2MappedMemoryReadWord(context->pchistory[(index & (MAX_DMPHISTORY - 1))]), 0, NULL /*&context->regshistory[index & 0xFF]*/, lineBuf);
		  fprintf(history,lineBuf);
		  fprintf(history, "\n");
		  index--;
	    }
		fclose(history);
	}
#endif
}

//////////////////////////////////////////////////////////////////////////////

// DEBUG stuff

//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////

void SH2SetBreakpointCallBack(SH2_struct *context, void (*func)(void *, u32, void *), void *userdata) {
   context->bp.BreakpointCallBack = func;
   context->bp.BreakpointUserData = userdata;
}

//////////////////////////////////////////////////////////////////////////////

int SH2AddCodeBreakpoint(SH2_struct *context, u32 addr) {
   int i;

   if (context->bp.numcodebreakpoints < MAX_BREAKPOINTS) {
      // Make sure it isn't already on the list
      for (i = 0; i < context->bp.numcodebreakpoints; i++)
      {
         if (addr == context->bp.codebreakpoint[i].addr)
            return -1;
      }

      context->bp.codebreakpoint[context->bp.numcodebreakpoints].addr = addr;
      context->bp.numcodebreakpoints++;

      return 0;
   }

   return -1;
}

//////////////////////////////////////////////////////////////////////////////

static void SH2SortCodeBreakpoints(SH2_struct *context) {
   int i, i2;
   u32 tmp;

   for (i = 0; i < (MAX_BREAKPOINTS-1); i++)
   {
      for (i2 = i+1; i2 < MAX_BREAKPOINTS; i2++)
      {
         if (context->bp.codebreakpoint[i].addr == 0xFFFFFFFF &&
             context->bp.codebreakpoint[i2].addr != 0xFFFFFFFF)
         {
            tmp = context->bp.codebreakpoint[i].addr;
            context->bp.codebreakpoint[i].addr = context->bp.codebreakpoint[i2].addr;
            context->bp.codebreakpoint[i2].addr = tmp;
         }
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

int SH2DelCodeBreakpoint(SH2_struct *context, u32 addr) {
   int i, i2;

   LOG("Deleting breakpoint %08X...\n", addr);

   if (context->bp.numcodebreakpoints > 0) {
      for (i = 0; i < context->bp.numcodebreakpoints; i++) {
         if (context->bp.codebreakpoint[i].addr == addr)
         {
            context->bp.codebreakpoint[i].addr = 0xFFFFFFFF;
            SH2SortCodeBreakpoints(context);
            context->bp.numcodebreakpoints--;

            LOG("Remaining breakpoints: \n");

            for (i2 = 0; i2 < context->bp.numcodebreakpoints; i2++)
            {
               LOG("%08X", context->bp.codebreakpoint[i2].addr);
            }

            return 0;
         }
      }
   }

   LOG("Failed deleting breakpoint\n");

   return -1;
}

//////////////////////////////////////////////////////////////////////////////

codebreakpoint_struct *SH2GetBreakpointList(SH2_struct *context) {
   return context->bp.codebreakpoint;
}

//////////////////////////////////////////////////////////////////////////////

void SH2ClearCodeBreakpoints(SH2_struct *context) {
   int i;
   for (i = 0; i < MAX_BREAKPOINTS; i++) {
      context->bp.codebreakpoint[i].addr = 0xFFFFFFFF;
   }

   context->bp.numcodebreakpoints = 0;
}

//////////////////////////////////////////////////////////////////////////////

static u8 FASTCALL SH2MemoryBreakpointReadByte(SH2_struct *sh, u8* mem, u32 addr) {
   int i;
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (sh->bp.memorybreakpoint[i].addr == (addr & 0x0FFFFFFF))
      {
         if (sh->bp.BreakpointCallBack && sh->bp.inbreakpoint == 0)
         {
            sh->bp.inbreakpoint = 1;
         }

         return sh->bp.memorybreakpoint[i].oldreadbyte(sh, mem, addr);
      }
   }

   // Use the closest match if address doesn't match
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (((sh->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
         return sh->bp.memorybreakpoint[i].oldreadbyte(sh, mem, addr);
   }
   SH2_struct *otherSH = (sh == MSH2)?SSH2:MSH2;
   // the breakpoint might have been set for the other core.
   for (i = 0; i < otherSH->bp.nummemorybreakpoints; i++)
   {
      if (((otherSH->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
         return otherSH->bp.memorybreakpoint[i].oldreadbyte(sh, mem, addr);
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static u16 FASTCALL SH2MemoryBreakpointReadWord(SH2_struct *sh, u8* mem, u32 addr) {
   int i;
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (sh->bp.memorybreakpoint[i].addr == (addr & 0x0FFFFFFF))
      {
         if (sh->bp.BreakpointCallBack && sh->bp.inbreakpoint == 0)
         {
            sh->bp.inbreakpoint = 1;
         }
         return sh->bp.memorybreakpoint[i].oldreadword(sh, mem, addr);
      }
   }

   // Use the closest match if address doesn't match
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (((sh->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
         return sh->bp.memorybreakpoint[i].oldreadword(sh, mem, addr);
   }
   SH2_struct *otherSH = (sh == MSH2)?SSH2:MSH2;
   // the breakpoint might have been set for the other core.
   for (i = 0; i < otherSH->bp.nummemorybreakpoints; i++)
   {
      if (((otherSH->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
         return otherSH->bp.memorybreakpoint[i].oldreadword(sh, mem, addr);
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static u32 FASTCALL SH2MemoryBreakpointReadLong(SH2_struct *sh, u8* mem, u32 addr) {
   int i;
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (sh->bp.memorybreakpoint[i].addr == (addr & 0x0FFFFFFF))
      {
         if (sh->bp.BreakpointCallBack && sh->bp.inbreakpoint == 0)
         {
            sh->bp.inbreakpoint = 1;
         }
         return sh->bp.memorybreakpoint[i].oldreadlong(sh, mem, addr);
      }
   }

   // Use the closest match if address doesn't match
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (((sh->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
         return sh->bp.memorybreakpoint[i].oldreadlong(sh, mem, addr);
   }
   SH2_struct *otherSH = (sh == MSH2)?SSH2:MSH2;
   // the breakpoint might have been set for the other core.
   for (i = 0; i < otherSH->bp.nummemorybreakpoints; i++)
   {
      if (((otherSH->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
         return otherSH->bp.memorybreakpoint[i].oldreadlong(sh, mem, addr);
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static void FASTCALL SH2MemoryBreakpointWriteByte(SH2_struct *sh, u8* mem, u32 addr, u8 val) {
   int i;
   SH2WriteNotify(MSH2, addr, 1);
   SH2WriteNotify(SSH2, addr, 1);
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (sh->bp.memorybreakpoint[i].addr == (addr & 0x0FFFFFFF))
      {
         if (sh->bp.BreakpointCallBack && sh->bp.inbreakpoint == 0)
         {
            sh->bp.inbreakpoint = 1;
         }

         sh->bp.memorybreakpoint[i].oldwritebyte(sh, mem, addr, val);
         return;
      }
   }

   // Use the closest match if address doesn't match
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (((sh->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
      {
         sh->bp.memorybreakpoint[i].oldwritebyte(sh, mem, addr, val);
         return;
      }
   }
   SH2_struct *otherSH = (sh == MSH2)?SSH2:MSH2;
   // the breakpoint might have been set for the other core.
   for (i = 0; i < otherSH->bp.nummemorybreakpoints; i++)
   {
     if (((otherSH->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
     {
        otherSH->bp.memorybreakpoint[i].oldwritebyte(sh, mem, addr, val);
        return;
     }
   }
}

//////////////////////////////////////////////////////////////////////////////

static void FASTCALL SH2MemoryBreakpointWriteWord(SH2_struct *sh, u8* mem, u32 addr, u16 val) {
   int i;
    SH2WriteNotify(MSH2, addr, 2);
    SH2WriteNotify(SSH2, addr, 2);
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (sh->bp.memorybreakpoint[i].addr == (addr & 0x0FFFFFFF))
      {
         if (sh->bp.BreakpointCallBack && sh->bp.inbreakpoint == 0)
         {
            sh->bp.inbreakpoint = 1;
         }

         sh->bp.memorybreakpoint[i].oldwriteword(sh, mem, addr, val);
         return;
      }
   }

   // Use the closest match if address doesn't match
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (((sh->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
      {
         sh->bp.memorybreakpoint[i].oldwriteword(sh, mem, addr, val);
         return;
      }
   }
   SH2_struct *otherSH = (sh == MSH2)?SSH2:MSH2;
   // the breakpoint might have been set for the other core.
   for (i = 0; i < otherSH->bp.nummemorybreakpoints; i++)
   {
     if (((otherSH->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
     {
        otherSH->bp.memorybreakpoint[i].oldwriteword(sh, mem, addr, val);
        return;
     }
   }
}

//////////////////////////////////////////////////////////////////////////////

static void FASTCALL SH2MemoryBreakpointWriteLong(SH2_struct *sh, u8* mem, u32 addr, u32 val) {
   int i;
   SH2WriteNotify(MSH2, addr, 4);
   SH2WriteNotify(SSH2, addr, 4);
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (sh->bp.memorybreakpoint[i].addr == (addr & 0x0FFFFFFF))
      {
         if (sh->bp.BreakpointCallBack && sh->bp.inbreakpoint == 0)
         {
            sh->bp.inbreakpoint = 1;
         }

         sh->bp.memorybreakpoint[i].oldwritelong(sh, mem, addr, val);
         return;
      }
   }

   // Use the closest match if address doesn't match
   for (i = 0; i < sh->bp.nummemorybreakpoints; i++)
   {
      if (((sh->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
      {
         sh->bp.memorybreakpoint[i].oldwritelong(sh, mem, addr, val);
         return;
      }
   }
   SH2_struct *otherSH = (sh == MSH2)?SSH2:MSH2;
   // the breakpoint might have been set for the other core.
   for (i = 0; i < otherSH->bp.nummemorybreakpoints; i++)
   {
     if (((otherSH->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) == ((addr >> 16) & 0xFFF))
     {
        otherSH->bp.memorybreakpoint[i].oldwritelong(sh, mem, addr, val);
        return;
     }
   }
}

//////////////////////////////////////////////////////////////////////////////

static int CheckForMemoryBreakpointDupes(SH2_struct *context, u32 addr, u32 flag, int *which)
{
   int i;

   for (i = 0; i < context->bp.nummemorybreakpoints; i++)
   {
      if (((context->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) ==
          ((addr >> 16) & 0xFFF))
      {
         // See it actually was using the same operation flag
         if (context->bp.memorybreakpoint[i].flags & flag)
         {
            *which = i;
            return 1;
         }
      }
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

int SH2AddMemoryBreakpoint(SH2_struct *context, u32 addr, u32 flags) {
   int which;
   int i;

   if (flags == 0)
      return -1;

   if (context->bp.nummemorybreakpoints < MAX_BREAKPOINTS) {
      // Only regular addresses are supported at this point(Sorry, no onchip!)
      switch (addr >> 29) {
         case 0x0:
         case 0x1:
         case 0x5:
            break;
         default:
            return -1;
      }

      addr &= 0x0FFFFFFF;

      // Make sure it isn't already on the list
      for (i = 0; i < context->bp.nummemorybreakpoints; i++)
      {
         if (addr == context->bp.memorybreakpoint[i].addr)
            return -1;
      }

      context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].addr = addr;
      context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].flags = flags;

      context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldreadbyte = ReadByteList[(addr >> 16) & 0xFFF];
      context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldreadword = ReadWordList[(addr >> 16) & 0xFFF];
      context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldreadlong = ReadLongList[(addr >> 16) & 0xFFF];
      context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldwritebyte = WriteByteList[(addr >> 16) & 0xFFF];
      context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldwriteword = WriteWordList[(addr >> 16) & 0xFFF];
      context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldwritelong = WriteLongList[(addr >> 16) & 0xFFF];

      if (flags & BREAK_BYTEREAD)
      {
         // Make sure function isn't already being breakpointed by another breakpoint
         if (!CheckForMemoryBreakpointDupes(context, addr, BREAK_BYTEREAD, &which))
            ReadByteList[(addr >> 16) & 0xFFF] = CacheReadByteList[(addr >> 16) & 0xFFF] = &SH2MemoryBreakpointReadByte;
         else
            // fix old memory access function
            context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldreadbyte = context->bp.memorybreakpoint[which].oldreadbyte;
      }

      if (flags & BREAK_WORDREAD)
      {
         // Make sure function isn't already being breakpointed by another breakpoint
         if (!CheckForMemoryBreakpointDupes(context, addr, BREAK_WORDREAD, &which))
            ReadWordList[(addr >> 16) & 0xFFF] = CacheReadWordList[(addr >> 16) & 0xFFF] = &SH2MemoryBreakpointReadWord;
         else
            // fix old memory access function
            context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldreadword = context->bp.memorybreakpoint[which].oldreadword;
      }

      if (flags & BREAK_LONGREAD)
      {
         // Make sure function isn't already being breakpointed by another breakpoint
         if (!CheckForMemoryBreakpointDupes(context, addr, BREAK_LONGREAD, &which))
            ReadLongList[(addr >> 16) & 0xFFF] = CacheReadLongList[(addr >> 16) & 0xFFF] = &SH2MemoryBreakpointReadLong;
         else
            // fix old memory access function
            context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldreadword = context->bp.memorybreakpoint[which].oldreadword;
      }

      if (flags & BREAK_BYTEWRITE)
      {
         // Make sure function isn't already being breakpointed by another breakpoint
         if (!CheckForMemoryBreakpointDupes(context, addr, BREAK_BYTEWRITE, &which))
            WriteByteList[(addr >> 16) & 0xFFF] = CacheWriteByteList[(addr >> 16) & 0xFFF] = &SH2MemoryBreakpointWriteByte;
         else
            // fix old memory access function
            context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldwritebyte = context->bp.memorybreakpoint[which].oldwritebyte;
      }

      if (flags & BREAK_WORDWRITE)
      {
         // Make sure function isn't already being breakpointed by another breakpoint
         if (!CheckForMemoryBreakpointDupes(context, addr, BREAK_WORDWRITE, &which))
            WriteWordList[(addr >> 16) & 0xFFF] = CacheWriteWordList[(addr >> 16) & 0xFFF] = &SH2MemoryBreakpointWriteWord;
         else
            // fix old memory access function
            context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldwriteword = context->bp.memorybreakpoint[which].oldwriteword;
      }

      if (flags & BREAK_LONGWRITE)
      {
         // Make sure function isn't already being breakpointed by another breakpoint
         if (!CheckForMemoryBreakpointDupes(context, addr, BREAK_LONGWRITE, &which))
           WriteLongList[(addr >> 16) & 0xFFF] = CacheWriteLongList[(addr >> 16) & 0xFFF] = &SH2MemoryBreakpointWriteLong;
         else
           // fix old memory access function
           context->bp.memorybreakpoint[context->bp.nummemorybreakpoints].oldwritelong = context->bp.memorybreakpoint[which].oldwritelong;
      }

      context->bp.nummemorybreakpoints++;

      return 0;
   }

   return -1;
}

//////////////////////////////////////////////////////////////////////////////

static void SH2SortMemoryBreakpoints(SH2_struct *context) {
   int i, i2;
   memorybreakpoint_struct tmp;

   for (i = 0; i < (MAX_BREAKPOINTS-1); i++)
   {
      for (i2 = i+1; i2 < MAX_BREAKPOINTS; i2++)
      {
         if (context->bp.memorybreakpoint[i].addr == 0xFFFFFFFF &&
             context->bp.memorybreakpoint[i2].addr != 0xFFFFFFFF)
         {
            memcpy(&tmp, context->bp.memorybreakpoint+i, sizeof(memorybreakpoint_struct));
            memcpy(context->bp.memorybreakpoint+i, context->bp.memorybreakpoint+i2, sizeof(memorybreakpoint_struct));
            memcpy(context->bp.memorybreakpoint+i2, &tmp, sizeof(memorybreakpoint_struct));
         }
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

int SH2DelMemoryBreakpoint(SH2_struct *context, u32 addr) {
   int i, i2;

   if (context->bp.nummemorybreakpoints > 0) {
      for (i = 0; i < context->bp.nummemorybreakpoints; i++) {
         if (context->bp.memorybreakpoint[i].addr == addr)
         {
            // Remove memory access piggyback function to memory access function table

            // Make sure no other breakpoints need the breakpoint functions first
            for (i2 = 0; i2 < context->bp.nummemorybreakpoints; i2++)
            {
               if (((context->bp.memorybreakpoint[i].addr >> 16) & 0xFFF) ==
                   ((context->bp.memorybreakpoint[i2].addr >> 16) & 0xFFF) &&
                   i != i2)
               {
                  // Clear the flags
                  context->bp.memorybreakpoint[i].flags &= ~context->bp.memorybreakpoint[i2].flags;
               }
            }

            if (context->bp.memorybreakpoint[i].flags & BREAK_BYTEREAD)
               ReadByteList[(addr >> 16) & 0xFFF] = CacheReadByteList[(addr >> 16) & 0xFFF] = context->bp.memorybreakpoint[i].oldreadbyte;

            if (context->bp.memorybreakpoint[i].flags & BREAK_WORDREAD)
               ReadWordList[(addr >> 16) & 0xFFF] = CacheReadWordList[(addr >> 16) & 0xFFF] = context->bp.memorybreakpoint[i].oldreadword;

            if (context->bp.memorybreakpoint[i].flags & BREAK_LONGREAD)
               ReadLongList[(addr >> 16) & 0xFFF] = CacheReadLongList[(addr >> 16) & 0xFFF] = context->bp.memorybreakpoint[i].oldreadlong;

            if (context->bp.memorybreakpoint[i].flags & BREAK_BYTEWRITE)
               WriteByteList[(addr >> 16) & 0xFFF] = CacheWriteByteList[(addr >> 16) & 0xFFF] = context->bp.memorybreakpoint[i].oldwritebyte;

            if (context->bp.memorybreakpoint[i].flags & BREAK_WORDWRITE)
               WriteWordList[(addr >> 16) & 0xFFF] = CacheWriteWordList[(addr >> 16) & 0xFFF] = context->bp.memorybreakpoint[i].oldwriteword;

            if (context->bp.memorybreakpoint[i].flags & BREAK_LONGWRITE) {
              WriteLongList[(addr >> 16) & 0xFFF] = CacheWriteLongList[(addr >> 16) & 0xFFF] = context->bp.memorybreakpoint[i].oldwritelong;

            }

            context->bp.memorybreakpoint[i].addr = 0xFFFFFFFF;
            SH2SortMemoryBreakpoints(context);
            context->bp.nummemorybreakpoints--;
            return 0;
         }
      }
   }

   return -1;
}

//////////////////////////////////////////////////////////////////////////////

memorybreakpoint_struct *SH2GetMemoryBreakpointList(SH2_struct *context) {
   return context->bp.memorybreakpoint;
}

//////////////////////////////////////////////////////////////////////////////

void SH2ClearMemoryBreakpoints(SH2_struct *context) {
   int i;
   for (i = 0; i < MAX_BREAKPOINTS; i++)
   {
      context->bp.memorybreakpoint[i].addr = 0xFFFFFFFF;
      context->bp.memorybreakpoint[i].flags = 0;
      context->bp.memorybreakpoint[i].oldreadbyte = NULL;
      context->bp.memorybreakpoint[i].oldreadword = NULL;
      context->bp.memorybreakpoint[i].oldreadlong = NULL;
      context->bp.memorybreakpoint[i].oldwritebyte = NULL;
      context->bp.memorybreakpoint[i].oldwriteword = NULL;
      context->bp.memorybreakpoint[i].oldwritelong = NULL;
   }
   context->bp.nummemorybreakpoints = 0;
}

//////////////////////////////////////////////////////////////////////////////

void SH2HandleBackTrace(SH2_struct *context)
{
   u16 inst = context->instruction;
   if ((inst & 0xF000) == 0xB000 || // BSR
      (inst & 0xF0FF) == 0x0003 || // BSRF
      (inst & 0xF0FF) == 0x400B)   // JSR
   {
      if (context->bt.numbacktrace < sizeof(context->bt.addr)/sizeof(u32))
      {
         context->bt.addr[context->bt.numbacktrace] = context->regs.PC;
         context->bt.numbacktrace++;
      }
   }
   else if ((inst == 0x000B) || // RTS
            (inst == 0x002B)) //RTE
   {
      if (context->bt.numbacktrace > 0)
         context->bt.numbacktrace--;
   }
}

//////////////////////////////////////////////////////////////////////////////

u32 *SH2GetBacktraceList(SH2_struct *context, int *size)
{
   *size = context->bt.numbacktrace;
   return context->bt.addr;
}

