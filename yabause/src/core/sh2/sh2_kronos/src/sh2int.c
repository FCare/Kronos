/*  Copyright 2003-2005 Guillaume Duhamel
    Copyright 2004-2007, 2013 Theo Berkau
    Copyright 2005 Fabien Coulon

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


/*! \file sh2int.c
    \brief SH2 interpreter interface
*/

#include "sh2core.h"
#include "cs0.h"
#include "debug.h"
#include "error.h"
#include "memory.h"
#include "bios.h"
#include "yabause.h"
#include "sh2int_kronos.h"

#include "cs2.h"

#include <yui.h>

#define LOCK(A)
#define UNLOCK(A)

extern void SH2undecoded(SH2_struct * sh);
extern void SH2InfiniteLoop(SH2_struct * sh);

static void SH2KronosNotifyInterrupt(SH2_struct *context);
static void insertInterruptReturnHandling(SH2_struct *context);

static void BUPDetectInit(SH2_struct *context);

extern void BiosBUPSelectPartition(SH2_struct * context);
extern void BiosBUPFormat(SH2_struct * context);
extern void BiosBUPStatus(SH2_struct * context);
extern void BiosBUPWrite(SH2_struct * context);
extern void BiosBUPRead(SH2_struct * context);
extern void BiosBUPDelete(SH2_struct * context);
extern void BiosBUPDirectory(SH2_struct * context);
extern void BiosBUPVerify(SH2_struct * context);
extern void BiosBUPGetDate(SH2_struct * context);
extern void BiosBUPSetDate(SH2_struct * context);

//SH2 hardware manual says that interrupt takes 13 cycles at least before execution due to decoding process
#define INTERRUPT_HANDLING_DELAY 13

void decode(SH2_struct *context);

//////////////////////////////////////////////////////////////////////////////

void SH2ExecCb(SH2_struct *context) {
  if (SH2Core->id == SH2CORE_KRONOS_DEBUG_INTERPRETER) {
    SH2HandleBreakpoints(context);
    SH2HandleBackTrace(context);
    SH2HandleStepOverOut(context);
    SH2HandleTrackInfLoop(context);
  }
}

static void showCPUState(SH2_struct *context)
{
  int i;

  YuiMsg("=================== %s ===================\n", (context == MSH2)?"MSH2":"SSH2");
  YuiMsg("PC = 0x%x\n", context->regs.PC);
  YuiMsg("PR = 0x%x\n", context->regs.PR);
  YuiMsg("SR = 0x%x\n", context->regs.SR);
  YuiMsg("GBR = 0x%x\n", context->regs.GBR);
  YuiMsg("VBR = 0x%x\n", context->regs.VBR);
  YuiMsg("MACH = 0x%x\n", context->regs.MACH);
  YuiMsg("MACL = 0x%x\n", context->regs.MACL);
  for (int i = 0; i<16; i++)
    YuiMsg("R[%d] = 0x%x\n", i, context->regs.R[i]);
}

void SH2KronosIOnFrame(SH2_struct *context) {
}

//////////////////////////////////////////////////////////////////////////////

void SH2HandleInterrupts(SH2_struct *context)
{
  LOCK(context);
  if (context->intPriority != 0x0)
  {
    u32 oldpc = context->regs.PC;
    u32 persr = context->regs.SR.part.I;
    context->regs.R[15] -= 4;
    SH2MappedMemoryWriteLong(context, context->regs.R[15], context->regs.SR.all);
    context->regs.R[15] -= 4;
    SH2MappedMemoryWriteLong(context, context->regs.R[15], context->regs.PC);
    context->regs.SR.part.I = context->intPriority;

    context->intPriority = 0; //Flag for next IT
    context->branchDepth = 0;
    insertInterruptReturnHandling(context); //Insert a new interrupt handling once this one will have been executed
    // force the next PC (or PC+2?) to be decodeWithInterrupt so that next interrupt is evaluated when back from IT
    context->regs.PC = SH2MappedMemoryReadLong(context,context->regs.VBR + (context->intVector << 2));
    if (SH2Core->id == SH2CORE_KRONOS_DEBUG_INTERPRETER) {
      //Show the interrupt as a JSR
      context->instruction = 0x400B;
      SH2HandleBackTrace(context);
    }
    context->isSleeping = 0;
  }
  UNLOCK(context);
}
fetchfunc krfetchlist[0x1000];
static u8 cacheId[0x1000];
//static opcode_func kropcodes[0x10000];

//////////////////////////////////////////////////////////////////////////////

static u16 FASTCALL FetchBios(SH2_struct *context, u32 addr)
{
   return SH2MappedMemoryReadWord(context,addr);
}

//////////////////////////////////////////////////////////////////////////////

static u16 FASTCALL FetchLWram(SH2_struct *context, u32 addr)
{
	return SH2MappedMemoryReadWord(context,addr);
}

//////////////////////////////////////////////////////////////////////////////

static u16 FASTCALL FetchHWram(SH2_struct *context, u32 addr)
{
	return SH2MappedMemoryReadWord(context,addr);
}

static u16 FASTCALL FetchVram(SH2_struct *context, u32 addr)
{
  return SH2MappedMemoryReadWord(context, addr);
}

static const int cacheMask[9] = {
  0x3FFFF, //Bios
  0x7FFFF, //LowWram
  0x1FFFFF, //CS0
  0x7FFFF, //SoundRam
  0x3FFFF, //VDP1Ram
  0x3FFFF, //VDP2Ram
  0x7FFFF, //HighWRam
  0x7FF, //Data Array
  0x7FFFF //Undecoded
};

static const int cacheSize[9] = {
  0x40000, //Bios
  0x80000, //LowWram
  0x200000, //CS0
  0x80000, //SoundRam
  0x40000, //VDP1Ram
  0x40000, //VDP2Ram
  0x80000, //HighWRam
  0x800, //Data Array
  0x80000 //Undecoded
};

static opcode_func cache_master_bios[0x40000];
static opcode_func cache_slave_bios[0x40000];
static opcode_func cache_master_lowram[0x80000];
static opcode_func cache_slave_lowram[0x80000];
static opcode_func cache_master_cs0[0x200000];
static opcode_func cache_slave_cs0[0x200000];
static opcode_func cache_master_sound[0x80000];
static opcode_func cache_slave_sound[0x80000];
static opcode_func cache_master_vdp1[0x40000];
static opcode_func cache_master_vdp2[0x40000];
static opcode_func cache_slave_vdp1[0x40000];
static opcode_func cache_slave_vdp2[0x40000];
static opcode_func cache_master_hiram[0x80000];
static opcode_func cache_slave_hiram[0x80000];
static opcode_func cache_master_array[0x800];
static opcode_func cache_slave_array[0x800];
static opcode_func cache_master_undecoded[0x80000];
static opcode_func cache_slave_undecoded[0x80000];

static opcode_func* cacheCode[2][9] = {
  {
    cache_master_bios,
    cache_master_lowram,
    cache_master_cs0,
    cache_master_sound,
    cache_master_vdp1,
    cache_master_vdp2,
    cache_master_hiram,
    cache_master_array,
    cache_master_undecoded
  },
  {
    cache_slave_bios,
    cache_slave_lowram,
    cache_slave_cs0,
    cache_slave_sound,
    cache_slave_vdp1,
    cache_slave_vdp2,
    cache_slave_hiram,
    cache_slave_array,
    cache_slave_undecoded
  }
};

opcode_func BUPEntries[10] = {
  BiosBUPSelectPartition,
  BiosBUPFormat,
  BiosBUPStatus,
  BiosBUPWrite,
  BiosBUPRead,
  BiosBUPDelete,
  BiosBUPDirectory,
  BiosBUPVerify,
  BiosBUPGetDate,
  BiosBUPSetDate
};
//////////////////////////////////////////////////////////////////////////////

static u16 FASTCALL FetchInvalid(SH2_struct *context, UNUSED u32 addr)
{
   return 0xFFFF;
}

static void BUPInstallHooks(SH2_struct *context) {

  LOG_BUP("BUPInstallHooks\n");
  u32 startTableAdress = context->BUPTableAddr+0x4;
  for (int i =0; i<10; i++) {
    u32 addr = startTableAdress+i*0x4;
    u32 vector = DMAMappedMemoryReadLong(addr);
    LOG_BUP("BUPInstallHooks replace vector @0x%x pointed @0x%x\n", vector, addr);
    int id = (vector >> 20) & 0xFFF;
    cacheCode[context->isslave][cacheId[id]][(vector>>1) & cacheMask[cacheId[id]]] = BUPEntries[i];
  }

  //Free the hook
  int id = (context->regs.PC >> 20) & 0xFFF;
  u16 opcode = krfetchlist[id](context, context->regs.PC);
  cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] = opcodeTable[opcode];

  //And finally execute the code
  context->instruction = opcode;
  if (SH2Core->id == SH2CORE_KRONOS_DEBUG_INTERPRETER) {
    SH2HandleBackTrace(context);
    SH2HandleStepOverOut(context);
    SH2HandleTrackInfLoop(context);
  }
  opcodeTable[opcode](context);
}

static void BUPDetectInit(SH2_struct *context)
{
  //Execute a dedicated task on BackupMemory Init

   LOG_BUP("BiosBUPInit. arg1 = %08X, arg2 = %08X, arg3 = %08X PR=0x%x\n", context->regs.R[4], context->regs.R[5], context->regs.R[6], context->regs.PR);
   //Store the Backup function adress table
   context->BUPTableAddr = context->regs.R[5];

   //Wait for the function to return to setup all the hooks
   int id = (context->regs.PR >> 20) & 0xFFF;
   int addr = context->regs.PR >> 1;
   cacheCode[context->isslave][cacheId[id]][addr & cacheMask[cacheId[id]]] = BUPInstallHooks;

   //And finally execute the code
   context->instruction = krfetchlist[id](context, context->regs.PC);
   if (SH2Core->id == SH2CORE_KRONOS_DEBUG_INTERPRETER) {
     SH2HandleBackTrace(context);
     SH2HandleStepOverOut(context);
     SH2HandleTrackInfLoop(context);
   }
   opcodeTable[context->instruction](context);
}

//////////////////////////////////////////////////////////////////////////////
void decode(SH2_struct *context) {
  int id = (context->regs.PC >> 20) & 0xFFF;
  u16 opcode = krfetchlist[id](context, context->regs.PC);

// if (cacheId[id] == 6) YabErrorMsg("Decode intstructions from Data array\n");
if (cacheId[id] == 7) YabErrorMsg("Decode intstructions from unxpected area @0x%x\n", context->regs.PC);
  cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] = opcodeTable[opcode];
  context->instruction = opcode;
  if (SH2Core->id == SH2CORE_KRONOS_DEBUG_INTERPRETER) {
    SH2HandleBackTrace(context);
    SH2HandleStepOverOut(context);
    SH2HandleTrackInfLoop(context);
  }
  opcodeTable[opcode](context);
}

static void executeLastPC(SH2_struct *context) {
  int id = (context->regs.PC >> 20) & 0xFFF;
  u16 opcode = krfetchlist[id](context, context->regs.PC);
  u32 oldPC = context->regs.PC;
  cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] = opcodeTable[opcode];
  if (context->regs.PC != oldPC) {
    //There was an interrupt to execute
    //Update the command to execute
    id = (context->regs.PC >> 20) & 0xFFF;
    opcode = krfetchlist[id](context, context->regs.PC);
    cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] = opcodeTable[opcode];
  }
  context->instruction = opcode;
  if (SH2Core->id == SH2CORE_KRONOS_DEBUG_INTERPRETER) {
    SH2HandleBackTrace(context);
    SH2HandleStepOverOut(context);
    SH2HandleTrackInfLoop(context);
  }
  opcodeTable[opcode](context);
}

static void decodeInt(SH2_struct *context) {
  executeLastPC(context);
  if (context->itTriggerCycles >= context->cycles) SH2KronosNotifyInterrupt(context);
  else SH2HandleInterrupts(context);
}

static void outOfInt(SH2_struct *context) {
  context->interruptReturnAddress = 0;
  executeLastPC(context);
}

int SH2KronosInterpreterInit(void)
{

   int i,j;
   SH2SetExecSet(0);
   if (SH2Core->id == SH2CORE_KRONOS_INTERPRETER) {
     //Optimize while(1) loop
     opcodeTable[0xAFFE] = SH2InfiniteLoop;
  }

   for (i = 0; i < 8; i++) {
       for (j = 0; j < cacheSize[i]; j++) {
           cacheCode[0][i][j] = decode;
           cacheCode[1][i][j] = decode;
       }
   }

   for (j = 0; j < cacheSize[8]; j++) {
       cacheCode[0][8][j] = SH2undecoded;
       cacheCode[1][8][j] = SH2undecoded;
   }

   for (i = 0; i < 0x1000; i++)
   {
      krfetchlist[i] = FetchInvalid;
      cacheId[i] = 8;
      if (((i>>8) == 0x0) || ((i>>8) == 0x2)) {
        switch (i&0xFF)
        {
          case 0x000: // Bios
            krfetchlist[i] = FetchBios;
            cacheId[i] = 0;
            break;
          case 0x002: // Low Work Ram
            krfetchlist[i] = SH2MappedMemoryReadWord;
            cacheId[i] = 1;
            break;
          case 0x020: // CS0
            krfetchlist[i] = SH2MappedMemoryReadWord;
            cacheId[i] = 2;
            break;
          case 0x05a: // SoundRam
            krfetchlist[i] = SH2MappedMemoryReadWord;
            cacheId[i] = 3;
            break;
          case 0x05c: // Fighting Viper
            krfetchlist[i] = SH2MappedMemoryReadWord;
            cacheId[i] = 4;
            break;
          case 0x05e: // PGA Tour 97
            krfetchlist[i] = SH2MappedMemoryReadWord;
            cacheId[i] = 5;
            break;
          case 0x060: // High Work Ram
          case 0x061:
          case 0x062:
          case 0x063:
          case 0x064:
          case 0x065:
          case 0x066:
          case 0x067:
          case 0x068:
          case 0x069:
          case 0x06A:
          case 0x06B:
          case 0x06C:
          case 0x06D:
          case 0x06E:
          case 0x06F:
            krfetchlist[i] = SH2MappedMemoryReadWord;
            cacheId[i] = 6;
            break;
          default:
            krfetchlist[i] = FetchInvalid;
            cacheId[i] = 8;
            break;
        }
     }
     if ((i>>8) == 0xC) {
       krfetchlist[i] = SH2MappedMemoryReadWord;
       cacheId[i] = 7;
     }
   }
   cacheCode[0][0][0x7d600>>1] = BUPDetectInit;
   cacheCode[1][0][0x7d600>>1] = BUPDetectInit;
   SH2ClearCodeBreakpoints(MSH2);
   SH2ClearCodeBreakpoints(SSH2);
   SH2ClearMemoryBreakpoints(MSH2);
   SH2ClearMemoryBreakpoints(SSH2);
   MSH2->breakpointEnabled = 0;
   SSH2->breakpointEnabled = 0;
   MSH2->backtraceEnabled = 0;
   SSH2->backtraceEnabled = 0;
   MSH2->stepOverOut.enabled = 0;
   SSH2->stepOverOut.enabled = 0;
   MSH2->isDelayed = SSH2->isDelayed = 0;

   return 0;
}

int SH2KronosInterpreterDebugInit(void)
{
  int ret = SH2KronosInterpreterInit();
  SH2SetExecSet(1);
  return ret;
}
//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterDeInit(void)
{
}

//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterReset(UNUSED SH2_struct *context)
{
}

//////////////////////////////////////////////////////////////////////////////

static INLINE void SH2UBCInterrupt(SH2_struct *context, u32 flag)
{
   if (15 > context->regs.SR.part.I) // Since UBC's interrupt are always level 15
   {
      context->regs.R[15] -= 4;
      SH2MappedMemoryWriteLong(context, context->regs.R[15], context->regs.SR.all);
      context->regs.R[15] -= 4;
      SH2MappedMemoryWriteLong(context, context->regs.R[15], context->regs.PC);
      context->regs.SR.part.I = 15;
      context->regs.PC = SH2MappedMemoryReadLong(context, context->regs.VBR + (12 << 2));
      LOG("interrupt successfully handled\n");
   }
   context->onchip.BRCR |= flag;
}

FASTCALL void SH2KronosInterpreterExec(SH2_struct *context, u32 cycles)
{
  context->target_cycles = context->cycles + cycles;
    SH2HandleInterrupts(context);
  while ((context->cycles < context->target_cycles) || (context->doNotInterrupt != 0)) {
    context->doNotInterrupt = 0;
    //NOTE: it can happen that next cachecode is generating a SH2HandleInterrupts which is normally forbidden when context->doNotInterrupt is not 0
    //Not sure it has a functionnal effect anyway
    // if (context == SSH2) YuiMsg("%x\n", context->regs.PC);
    u32 id = cacheId[(context->regs.PC >> 20) & 0xFFF];
    cacheCode[context->isslave][id][(context->regs.PC >> 1) & cacheMask[id]](context);
  }
  context->target_cycles = 0;
}

FASTCALL void SH2KronosInterpreterExecSave(SH2_struct *context, u32 cycles, sh2regs_struct *oldRegs)
{
  context->target_cycles = context->cycles + cycles;
  SH2HandleInterrupts(context);
  while ((context->cycles < context->target_cycles) || (context->doNotInterrupt != 0)) {
    context->doNotInterrupt = 0;
    //NOTE: it can happen that next cachecode is generating a SH2HandleInterrupts which is normally forbidden when context->doNotInterrupt is not 0
    //Not sure it has a functionnal effect anyway
    memcpy(oldRegs, &context->regs, sizeof(sh2regs_struct));
    int id = (context->regs.PC >> 20) & 0xFFF;
    // if (context == SSH2) YuiMsg("%x\n", context->regs.PC);
    u16 opcode = krfetchlist[id](context, context->regs.PC);
    if(context->isBlocked == 0) opcodeTable[opcode](context);
    if(context->isBlocked != 0) {
      context->cycles = context->target_cycles;
      memcpy(&context->regs, oldRegs, sizeof(sh2regs_struct));
      context->target_cycles = 0;
      return;
    }
  }
  context->target_cycles = 0;
}

static int enableTrace = 0;

FASTCALL void SH2KronosDebugInterpreterExecSave(SH2_struct *context, u32 cycles, sh2regs_struct *oldRegs) {
  context->target_cycles = context->cycles + cycles;

    SH2HandleInterrupts(context);

   while ((context->cycles < context->target_cycles) || (context->doNotInterrupt != 0))
   {
     context->doNotInterrupt = 0;
     //NOTE: it can happen that next cachecode is generating a SH2HandleInterrupts which is normally forbidden when context->doNotInterrupt is not 0
     //Not sure it has a functionnal effect anyway
#ifdef SH2_UBC
      int ubcinterrupt=0, ubcflag=0;
#endif

      if (SH2HandleBreakpoints(context)) {
        return;
      }

#ifdef SH2_UBC
      if (context->onchip.BBRA & (BBR_CPA_CPU | BBR_IDA_INST | BBR_RWA_READ)) // Break on cpu, instruction, read cycles
      {
         if (context->onchip.BARA.all == (context->regs.PC & (~context->onchip.BAMRA.all)))
         {
            LOG("Trigger UBC A interrupt: PC = %08X\n", context->regs.PC);
            if (!(context->onchip.BRCR & BRCR_PCBA))
            {
               // Break before instruction fetch
             SH2UBCInterrupt(context, BRCR_CMFCA);
            }
            else
            {
              // Break after instruction fetch
               ubcinterrupt=1;
               ubcflag = BRCR_CMFCA;
            }
         }
      }
      else if(context->onchip.BBRB & (BBR_CPA_CPU | BBR_IDA_INST | BBR_RWA_READ)) // Break on cpu, instruction, read cycles
      {
         if (context->onchip.BARB.all == (context->regs.PC & (~context->onchip.BAMRB.all)))
         {
            LOG("Trigger UBC B interrupt: PC = %08X\n", context->regs.PC);
            if (!(context->onchip.BRCR & BRCR_PCBB))
            {
               // Break before instruction fetch
               SH2UBCInterrupt(context, BRCR_CMFCB);
            }
            else
            {
               // Break after instruction fetch
               ubcinterrupt=1;
               ubcflag = BRCR_CMFCB;
            }
         }
      }
#endif

      memcpy(oldRegs, &context->regs, sizeof(sh2regs_struct));

      // Fetch Instruction
      int id = (context->regs.PC >> 20) & 0xFFF;
      uint8_t shallExecute = 1;
      if (cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] == BUPDetectInit) {
        shallExecute = 0;
        BUPDetectInit(context);
      }
      if (cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] == BUPInstallHooks) {
        shallExecute = 0;
        BUPInstallHooks(context);
      }
      for (int i = 0; i<10; i++) {
        if (cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] == BUPEntries[i]) {
          shallExecute = 0;
          BUPEntries[i](context);
          break;
        }
      }
      if (cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] == outOfInt) {
        //OutOfInt
        context->interruptReturnAddress = 0;
      }

#ifdef DMPHISTORY
    context->pchistory_index++;
    context->pchistory[context->pchistory_index & (MAX_DMPHISTORY - 1)] = context->regs.PC;
    context->regshistory[context->pchistory_index & (MAX_DMPHISTORY - 1)] = context->regs;
#endif


      // Execute it
      if (shallExecute != 0) {
        context->instruction = krfetchlist[id](context, context->regs.PC);
        cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] = opcodeTable[context->instruction];
        if(context->isBlocked == 0) {
          SH2HandleBackTrace(context);
          SH2HandleStepOverOut(context);
          SH2HandleTrackInfLoop(context);
          opcodeTable[context->instruction](context);
        }
      }
      if(context->isBlocked != 0) {
        context->cycles = context->target_cycles;
        memcpy(&context->regs, oldRegs, sizeof(sh2regs_struct));
        return;
      }
      if (context->bp.inbreakpoint) {
        return;
      }
#ifdef SH2_UBC
    if (ubcinterrupt)
       SH2UBCInterrupt(context, ubcflag);
#endif
   }
}


FASTCALL void SH2KronosDebugInterpreterExec(SH2_struct *context, u32 cycles)
{
  context->target_cycles = context->cycles + cycles;

    SH2HandleInterrupts(context);
   while ((context->cycles < context->target_cycles) || (context->doNotInterrupt != 0))

   {
     context->doNotInterrupt = 0;
     if (SH2HandleBreakpoints(context)) {
       return;
     }
#ifdef SH2_UBC
      int ubcinterrupt=0, ubcflag=0;
#endif

#ifdef SH2_UBC
      if (context->onchip.BBRA & (BBR_CPA_CPU | BBR_IDA_INST | BBR_RWA_READ)) // Break on cpu, instruction, read cycles
      {
         if (context->onchip.BARA.all == (context->regs.PC & (~context->onchip.BAMRA.all)))
         {
            LOG("Trigger UBC A interrupt: PC = %08X\n", context->regs.PC);
            if (!(context->onchip.BRCR & BRCR_PCBA))
            {
               // Break before instruction fetch
	           SH2UBCInterrupt(context, BRCR_CMFCA);
            }
            else
            {
            	// Break after instruction fetch
               ubcinterrupt=1;
               ubcflag = BRCR_CMFCA;
            }
         }
      }
      else if(context->onchip.BBRB & (BBR_CPA_CPU | BBR_IDA_INST | BBR_RWA_READ)) // Break on cpu, instruction, read cycles
      {
         if (context->onchip.BARB.all == (context->regs.PC & (~context->onchip.BAMRB.all)))
         {
            LOG("Trigger UBC B interrupt: PC = %08X\n", context->regs.PC);
            if (!(context->onchip.BRCR & BRCR_PCBB))
            {
          	   // Break before instruction fetch
       	       SH2UBCInterrupt(context, BRCR_CMFCB);
            }
            else
            {
               // Break after instruction fetch
               ubcinterrupt=1;
               ubcflag = BRCR_CMFCB;
            }
         }
      }
#endif

      // Fetch Instruction
      int id = (context->regs.PC >> 20) & 0xFFF;

#ifdef DMPHISTORY
	  context->pchistory_index++;
	  context->pchistory[context->pchistory_index & (MAX_DMPHISTORY - 1)] = context->regs.PC;
	  context->regshistory[context->pchistory_index & (MAX_DMPHISTORY - 1)] = context->regs;
#endif



      uint8_t shallExecute = 1;
      if (cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] == BUPDetectInit) {
        shallExecute = 0;
        BUPDetectInit(context);
      }
      if (cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] == BUPInstallHooks) {
        shallExecute = 0;
        BUPInstallHooks(context);
      }
      for (int i = 0; i<10; i++) {
        if (cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] == BUPEntries[i]) {
          shallExecute = 0;
          BUPEntries[i](context);
          break;
        }
      }
      if (cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] == outOfInt) {
        //OutOfInt
        context->interruptReturnAddress = 0;
      }
      // Execute it
      if (shallExecute != 0) {
        context->instruction = krfetchlist[id](context, context->regs.PC);
        SH2HandleBackTrace(context);
        SH2HandleStepOverOut(context);
        SH2HandleTrackInfLoop(context);
        cacheCode[context->isslave][cacheId[id]][(context->regs.PC >> 1) & cacheMask[cacheId[id]]] = opcodeTable[context->instruction];
        opcodeTable[context->instruction](context);
        if (context->bp.inbreakpoint) {
          return;
        }
      }

#ifdef SH2_UBC
	  if (ubcinterrupt)
	     SH2UBCInterrupt(context, ubcflag);
#endif
   }

}

FASTCALL void SH2KronosInterpreterTestExec(SH2_struct *context, u32 cycles)
{
  context->target_cycles = context->cycles + cycles;
  u32 id = cacheId[(context->regs.PC >> 20) & 0xFFF];
  cacheCode[context->isslave][id][(context->regs.PC >> 1) & cacheMask[id]](context);
  context->target_cycles = 0;
}


FASTCALL void SH2KronosInterpreterAddCycles(SH2_struct *context, u32 value)
{
  context->cycles += value;
}
//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterGetRegisters(SH2_struct *context, sh2regs_struct *regs)
{
   memcpy(regs, &context->regs, sizeof(sh2regs_struct));
}

//////////////////////////////////////////////////////////////////////////////

u32 SH2KronosInterpreterGetGPR(SH2_struct *context, int num)
{
    return context->regs.R[num];
}

//////////////////////////////////////////////////////////////////////////////

u32 SH2KronosInterpreterGetSR(SH2_struct *context)
{
    return context->regs.SR.all;
}

//////////////////////////////////////////////////////////////////////////////

u32 SH2KronosInterpreterGetGBR(SH2_struct *context)
{
    return context->regs.GBR;
}

//////////////////////////////////////////////////////////////////////////////

u32 SH2KronosInterpreterGetVBR(SH2_struct *context)
{
    return context->regs.VBR;
}

//////////////////////////////////////////////////////////////////////////////

u32 SH2KronosInterpreterGetMACH(SH2_struct *context)
{
    return context->regs.MACH;
}

//////////////////////////////////////////////////////////////////////////////

u32 SH2KronosInterpreterGetMACL(SH2_struct *context)
{
    return context->regs.MACL;
}

//////////////////////////////////////////////////////////////////////////////

u32 SH2KronosInterpreterGetPR(SH2_struct *context)
{
    return context->regs.PR;
}

//////////////////////////////////////////////////////////////////////////////

u32 SH2KronosInterpreterGetPC(SH2_struct *context)
{
    return context->regs.PC;
}

//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterSetRegisters(SH2_struct *context, const sh2regs_struct *regs)
{
   memcpy(&context->regs, regs, sizeof(sh2regs_struct));
   SH2HandleInterrupts(context);
}

//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterSetGPR(SH2_struct *context, int num, u32 value)
{
    context->regs.R[num] = value;
}

//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterSetSR(SH2_struct *context, u32 value)
{
    context->regs.SR.all = value;
}

//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterSetGBR(SH2_struct *context, u32 value)
{
    context->regs.GBR = value;
}

//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterSetVBR(SH2_struct *context, u32 value)
{
    context->regs.VBR = value;
}

//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterSetMACH(SH2_struct *context, u32 value)
{
    context->regs.MACH = value;
}

//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterSetMACL(SH2_struct *context, u32 value)
{
    context->regs.MACL = value;
}

//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterSetPR(SH2_struct *context, u32 value)
{
    context->regs.PR = value;
}

//////////////////////////////////////////////////////////////////////////////

void SH2KronosInterpreterSetPC(SH2_struct *context, u32 value)
{
    context->regs.PC = value;
}

void SH2KronosUpdateInterruptReturnHandling(SH2_struct *context) {
  //Clear previous hook
  int addr = context->interruptReturnAddress>>1;
  int id = (addr >> 19) & 0xFFF;
  cacheCode[context->isslave][cacheId[id]][addr & cacheMask[cacheId[id]]] = decode;
  //update hook reference to new PC
  context->interruptReturnAddress = context->regs.PC;
  addr = (context->regs.PC)>>1;
  id = (addr >> 19) & 0xFFF;
  cacheCode[context->isslave][cacheId[id]][addr & cacheMask[cacheId[id]]] = outOfInt;
}
void SH2KronosUpdateInterruptDebugReturnHandling(SH2_struct *context) {
  // while (context->branchDepth < 0) {
  //   //negative if coming from RTS
  //   //0 means we need to remove the trace of interrupt from RTS or RTE
  //   context->instruction = 0x000B; //Simulate rts to remove extra backtrace
  //   SH2HandleBackTrace(context);
  //   context->branchDepth++;
  // }
  SH2KronosUpdateInterruptReturnHandling(context);
}

//////////////////////////////////////////////////////////////////////////////

static void SH2KronosNotifyInterrupt(SH2_struct *context) {
  if (SH2MappedMemoryReadWord(context, context->regs.PC) == 0x1B) {
    //SH2 on a sleep command, wake it up
    context->regs.PC+=2;
  }

  if (context->interruptReturnAddress == 0) {
    int addr = (context->regs.PC + 2)>>1;
    if (context->target_cycles == 0)
      addr = context->regs.PC>>1;

    int id = (addr >> 19) & 0xFFF;
  context->itTriggerCycles = context->cycles + INTERRUPT_HANDLING_DELAY;
    cacheCode[context->isslave][cacheId[id]][addr & cacheMask[cacheId[id]]] = decodeInt;
  }
}

static void insertInterruptReturnHandling(SH2_struct *context) {
  if (context->interruptReturnAddress == 0) {
    int addr = (context->regs.PC)>>1;
    int id = (addr >> 19) & 0xFFF;
    context->interruptReturnAddress = context->regs.PC;
    cacheCode[context->isslave][cacheId[id]][addr & cacheMask[cacheId[id]]] = outOfInt;
  }
}

static void notify(SH2_struct *context, u32 start, u32 length) {
  int i;
  for (i=0; i<length; i+=2) {
    int id = ((start + i) >> 20) & 0xFFF;
    int addr = (start + i) >> 1;
    if (cacheCode[context->isslave][cacheId[id]][addr & cacheMask[cacheId[id]]] != outOfInt)
      cacheCode[context->isslave][cacheId[id]][addr & cacheMask[cacheId[id]]] = decode;
  }
}

void SH2KronosWriteNotify(SH2_struct *context, u32 start, u32 length){
  int id = start>>29;
 if((id == 0x0) || (id == 0x1)) {
   //in case of standard access
   notify(context, start & 0x1FFFFFFF, length);
   notify(context, (start & 0x1FFFFFFF)|0x20000000, length);
   //If the other core does not have the cache on, then it needs to see the modification
   if ((context->isslave != 0) && (MSH2->cacheOn == 0)) {
     notify(MSH2, start & 0x1FFFFFFF, length);
     notify(MSH2, (start & 0x1FFFFFFF)|0x20000000, length);
   }
   if ((context->isslave == 0) && (SSH2->cacheOn == 0)) {
     notify(SSH2, start & 0x1FFFFFFF, length);
     notify(SSH2, (start & 0x1FFFFFFF)|0x20000000, length);
   }
}
else {
  if (id == 0x6) {
    //Data Array access
    notify(context, start, length);
  }
}
//Need to add verification of cacheId in case non cacheable area is updated
//Maybe need to fix accessing equivalent non cacheable area in any case
}

//////////////////////////////////////////////////////////////////////////////
SH2Interface_struct SH2KronosInterpreter = {
   SH2CORE_KRONOS_INTERPRETER,
   "SH2 Performance",

   SH2KronosInterpreterInit,
   SH2KronosInterpreterDeInit,
   SH2KronosInterpreterReset,
   SH2KronosInterpreterExec,
   SH2KronosInterpreterExecSave,
   SH2KronosInterpreterTestExec,

   SH2KronosInterpreterGetRegisters,
   SH2KronosInterpreterGetGPR,
   SH2KronosInterpreterGetSR,
   SH2KronosInterpreterGetGBR,
   SH2KronosInterpreterGetVBR,
   SH2KronosInterpreterGetMACH,
   SH2KronosInterpreterGetMACL,
   SH2KronosInterpreterGetPR,
   SH2KronosInterpreterGetPC,

   SH2KronosInterpreterSetRegisters,
   SH2KronosInterpreterSetGPR,
   SH2KronosInterpreterSetSR,
   SH2KronosInterpreterSetGBR,
   SH2KronosInterpreterSetVBR,
   SH2KronosInterpreterSetMACH,
   SH2KronosInterpreterSetMACL,
   SH2KronosInterpreterSetPR,
   SH2KronosInterpreterSetPC,
   SH2KronosIOnFrame,

   SH2KronosNotifyInterrupt,

   SH2KronosWriteNotify,
   SH2KronosInterpreterAddCycles,
   SH2KronosUpdateInterruptReturnHandling
};

//////////////////////////////////////////////////////////////////////////////
SH2Interface_struct SH2KronosDebugInterpreter = {
   SH2CORE_KRONOS_DEBUG_INTERPRETER,
   "SH2 Debug",

   SH2KronosInterpreterDebugInit,
   SH2KronosInterpreterDeInit,
   SH2KronosInterpreterReset,
   SH2KronosDebugInterpreterExec,
   SH2KronosDebugInterpreterExecSave,
   SH2KronosInterpreterTestExec,

   SH2KronosInterpreterGetRegisters,
   SH2KronosInterpreterGetGPR,
   SH2KronosInterpreterGetSR,
   SH2KronosInterpreterGetGBR,
   SH2KronosInterpreterGetVBR,
   SH2KronosInterpreterGetMACH,
   SH2KronosInterpreterGetMACL,
   SH2KronosInterpreterGetPR,
   SH2KronosInterpreterGetPC,

   SH2KronosInterpreterSetRegisters,
   SH2KronosInterpreterSetGPR,
   SH2KronosInterpreterSetSR,
   SH2KronosInterpreterSetGBR,
   SH2KronosInterpreterSetVBR,
   SH2KronosInterpreterSetMACH,
   SH2KronosInterpreterSetMACL,
   SH2KronosInterpreterSetPR,
   SH2KronosInterpreterSetPC,
   SH2KronosIOnFrame,

   SH2KronosNotifyInterrupt,

   SH2KronosWriteNotify,
   SH2KronosInterpreterAddCycles,
   SH2KronosUpdateInterruptDebugReturnHandling
};
