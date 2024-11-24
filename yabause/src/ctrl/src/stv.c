
#include <stdio.h>
#include <stdlib.h>

#ifndef WIN32
#include <sys/types.h>
#include <dirent.h>
#include <libgen.h>
#else
#include <windows.h>
#endif

#ifdef __LIBRETRO__
#include <file/file_path.h>
#endif


#include "stv.h"
#include "cs0.h"
#include "junzip.h"
#include "yabause.h"
#include "eeprom.h"
#include "decrypt.h"
#include "zlib.h"

#ifdef __cplusplus
extern "C" {
#endif


#define LOGSTV
//YuiMsg
#define ROTATED 1

#define NB_STV_GAMES 103

GameLink availableGames[NB_STV_GAMES];
BiosLink biosLink;
int loadGames(char* path);
int copyFile(JZFile *zip, void* data);
int copyBios(JZFile *zip, void* id);

const u8 NV_1P[0x80]={
    0x53,0x45,0x47,0x41,0xff,0xff,0xff,0xff,0x68,0x5c,0xff,0xff,0x00,0x00,0x00,0x02,
    0x01,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x08,0x08,0xfc,0x00,0xca,0x01,0x56,
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0x68,0x5c,0xff,0xff,0x00,0x00,0x00,0x02,0x01,0x00,0x01,0x01,
    0x00,0x00,0x00,0x00,0x00,0x08,0x08,0xfc,0x00,0xca,0x01,0x56,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};

const u8 NV_2P[0x80]={
    0x53,0x45,0x47,0x41,0xff,0xff,0xff,0xff,0xdf,0xf9,0xff,0xff,0x00,0x00,0x00,0x04,
    0x01,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x08,0x08,0xfd,0x01,0x01,0x01,0x02,
    0x02,0x02,0x02,0x01,0x02,0x02,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xdf,0xf9,0xff,0xff,0x00,0x00,0x00,0x04,0x01,0x00,0x01,0x01,
    0x00,0x00,0x00,0x00,0x00,0x08,0x08,0xfd,0x01,0x01,0x01,0x02,0x02,0x02,0x02,0x01,
    0x02,0x02,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};


const u8 ShienryuNV[0x80]={
    0x53,0x45,0x47,0x41,0x3b,0xe2,0x5e,0x09,0x5e,0x09,0x00,0x00,0x00,0x00,0x00,0x02,
    0x01,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x08,0x18,0xfd,0x18,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0x5e,0x09,0x00,0x00,0x00,0x00,0x00,0x02,0x01,0x00,0x01,0x01,
    0x00,0x00,0x00,0x00,0x00,0x08,0x18,0xfd,0x18,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};

#ifdef WIN32
char *get_basename(char* path) {
  char *ssc;
  int l = 0;
  ssc = strstr(path, "\\");
  while(ssc){
    l = strlen(ssc) + 1;
    path = &path[strlen(path)-l+2];
    ssc = strstr(path, "\\");
  }
  return path;
}
#else
char *get_basename(char* path) {
  return basename(path);
}
#endif

static u8 bitswap8(u8 in, const u8* vec)
{
  u8 ret = 0;
  for (int i = 0; i<8; i++) {
    ret |= ((in >> vec[i])&0x1)<<(7-i);
  }
  return ret;
}

void sanjeon_init(void) {
  const u8 vec1[8] = {6,0,5,7,  1,3,4,2};
  for (int x = 0; x < 0x3000000; x++)
  {
    T1WriteByte(CartridgeArea->rom, x, bitswap8(T1ReadByte(CartridgeArea->rom, x)^0xff,vec1));
  }
}


Bios BiosList =
{
    "stvbios",
    "STV Bios",
    {
        // For now, the emulator picks the first bios it finds so, worldwide, declaring english bioses first seems like a good idea.
        // Also, usa bioses are known for practicing censorship, so let's prioritize euro.
        // In the future it would be nice to have a way to choose this.
        BIOS_BLOB, STV_REGION_JP, "epr-23603.ic8",  0x000000, 0x080000, 0xf688ae60, // jp
        BIOS_BLOB, STV_REGION_JP, "epr-20091.ic8",  0x000000, 0x080000, 0x59ed40f4, // jp1
        BIOS_BLOB, STV_REGION_JP, "epr-19730.ic8",  0x000000, 0x080000, 0xd0e0889d, // jp2
        BIOS_BLOB, STV_REGION_JP, "epr-17951a.ic8", 0x000000, 0x080000, 0x2672f9d8, // jp3
        BIOS_BLOB, STV_REGION_JP, "epr-17740a.ic8", 0x000000, 0x080000, 0x3e23c81f, // jp4
        BIOS_BLOB, STV_REGION_JP, "epr-17740.ic8",  0x000000, 0x080000, 0x5c5aa63d, // jp5
        BIOS_BLOB, STV_REGION_EU, "epr-17954a.ic8", 0x000000, 0x080000, 0xf7722da3, // euro
        BIOS_BLOB, STV_REGION_US, "epr-17952a.ic8", 0x000000, 0x080000, 0xd1be2adf, // us
        BIOS_BLOB, STV_REGION_US, "epr-17741a.ic8", 0x000000, 0x080000, 0x4166c663, // us1
        BIOS_BLOB, STV_REGION_TW, "epr-19854.ic8",  0x000000, 0x080000, 0xe09d1f60, // tw
        BIOS_BLOB, STV_REGION_TW, "epr-17953a.ic8", 0x000000, 0x080000, 0xa4c47570, // tw1
        BIOS_BLOB, STV_REGION_TW, "epr-17742a.ic8", 0x000000, 0x080000, 0x02daf123, // tw2
        BIOS_BLOB, STV_DEBUG,     "stv110.bin",     0x000000, 0x080000, 0x3dfeda92, // debug
        BIOS_BLOB, STV_DEV,       "stv1061.bin",    0x000000, 0x080000, 0x728dbca3, // dev
        GAME_END
    },
};

Game GameList[NB_STV_GAMES]={
  {
    "astrass",
    NULL,
    "Astra SuperStars (J 980514 V1.002)",
    STV_REGION_JP,
    0x052e2901,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"epr20825.13",	0x0000001, 0x0100000, 0x94a9ad8f,
        GAME_BYTE_BLOB, "epr20825.13",	0x0200000, 0x0100000, 0x94a9ad8f,
        GAME_BYTE_BLOB, "epr20825.13",	0x0300000, 0x0100000, 0x94a9ad8f,
        GAME_WORD_BLOB, "mpr20827.2",	0x0400000, 0x0400000, 0x65cabbb3,
        GAME_WORD_BLOB, "mpr20828.3",	0x0800000, 0x0400000, 0x3934d44a,
        GAME_WORD_BLOB, "mpr20829.4",	0x0c00000, 0x0400000, 0x814308c3,
        GAME_WORD_BLOB, "mpr20830.5",	0x1000000, 0x0400000, 0xff97fd19,
        GAME_WORD_BLOB, "mpr20831.6",	0x1400000, 0x0400000, 0x4408e6fb,
        GAME_WORD_BLOB, "mpr20826.1",	0x1800000, 0x0400000, 0xbdc4b941,
        GAME_WORD_BLOB, "mpr20832.8",	0x1c00000, 0x0400000, 0xaf1b0985,
        GAME_WORD_BLOB, "mpr20833.9",	0x2000000, 0x0400000, 0xcb6af231,
        GAME_END, "", 0, 0, 0
    },
    STV6B,
  },
  {
    "bakubaku",
    NULL,
    "Baku Baku Animal (J 950407 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "fpr17969.13",	0x0000001, 0x0100000, 0xbee327e5,
        GAME_BYTE_BLOB, "fpr17969.13",	0x0200000, 0x0100000, 0xbee327e5,
        GAME_BYTE_BLOB, "fpr17969.13",	0x0300000, 0x0100000, 0xbee327e5,
        GAME_WORD_BLOB, "mpr17970.2",	0x0400000, 0x0400000, 0xbc4d6f91,
        GAME_WORD_BLOB, "mpr17971.3",	0x0800000, 0x0400000, 0xc780a3b3,
        GAME_WORD_BLOB, "mpr17972.4",	0x0c00000, 0x0400000, 0x8f29815a,
        GAME_WORD_BLOB, "mpr17973.5",	0x1000000, 0x0400000, 0x5f6e0e8b,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "batmanfr",
    NULL,
    "Batman Forever (JUE 960507 V1.000)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "350-mpa1.u19", 0x0000000, 0x0100000, 0x2a5a8c3a,
        HEADER_BLOB,    "350-mpa1.u19", 0x0200000, 0x0100000, 0x2a5a8c3a,
        HEADER_BLOB,    "350-mpa1.u16", 0x0000001, 0x0100000, 0x735e23ab,
        HEADER_BLOB,    "350-mpa1.u16", 0x0200001, 0x0100000, 0x735e23ab,
        GAME_WORD_BLOB, "gfx0.u1",      0x0400000, 0x0400000, 0xa82d0b7e,
        GAME_WORD_BLOB, "gfx1.u3",      0x0800000, 0x0400000, 0xa41e55d9,
        GAME_WORD_BLOB, "gfx2.u5",      0x0c00000, 0x0400000, 0x4c1ebeb7,
        GAME_WORD_BLOB, "gfx3.u8",      0x1000000, 0x0400000, 0xf679a3e7,
        GAME_WORD_BLOB, "gfx4.u12",     0x1400000, 0x0400000, 0x52d95242,
        GAME_WORD_BLOB, "gfx5.u15",     0x1800000, 0x0400000, 0xe201f830,
        GAME_WORD_BLOB, "gfx6.u18",     0x1c00000, 0x0400000, 0xc6b381a3,
        GAME_END, "", 0, 0, 0
    },
    BATMANFR,
  },
  {
    "choroqhr",
    NULL,
    "Choro Q Hyper Racing 5 (J 981230 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB,	"ic22.bin",		0x0200000, 0x200000, 0x22c58710,
        GAME_WORD_BLOB,	"ic24.bin",		0x0400000, 0x200000, 0x09b8a154,
        GAME_WORD_BLOB, "ic26.bin",		0x0600000, 0x200000, 0x136ca5e9,
        GAME_WORD_BLOB, "ic28.bin",		0x0800000, 0x200000, 0x3c949563,
        GAME_WORD_BLOB, "ic30.bin",		0x0a00000, 0x200000, 0x7e93078d,
        GAME_WORD_BLOB, "ic32.bin",		0x0c00000, 0x200000, 0x86cdcbd8,
        GAME_WORD_BLOB, "ic34.bin",		0x0e00000, 0x200000, 0xbe2ed0a0,
        GAME_WORD_BLOB, "ic36.bin",		0x1000000, 0x200000, 0x9a4109e5,
        EEPROM_BLOB,	"choroqhr.nv",	0x0000, 0x0080, 0x6e89815f,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "colmns97",
    NULL,
    "Columns '97 (JET 961209 V1.000)",
    STV_REGION_EU | STV_REGION_JP | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "fpr19553.13",  0x000001, 0x100000, 0xd4fb6a5e,
        GAME_BYTE_BLOB, "fpr19553.13",  0x200000, 0x100000, 0xd4fb6a5e,
        GAME_BYTE_BLOB, "fpr19553.13",  0x300000, 0x100000, 0xd4fb6a5e,
        GAME_WORD_BLOB, "mpr19554.2",   0x400000, 0x400000, 0x5a3ebcac,
        GAME_WORD_BLOB, "mpr19555.3",   0x800000, 0x400000, 0x74f6e6b8,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "cotton2",
    NULL,
    "Cotton 2 (JUET 970902 V1.000)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP | STV_REGION_TW,
    0x0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr20122.7",    0x0200000, 0x0200000, 0xd616f78a,
        GAME_WORD_BLOB, "mpr20117.2",    0x0400000, 0x0400000, 0x893656ea,
        GAME_WORD_BLOB, "mpr20118.3",    0x0800000, 0x0400000, 0x1b6a1d4c,
        GAME_WORD_BLOB, "mpr20119.4",    0x0c00000, 0x0400000, 0x5a76e72b,
        GAME_WORD_BLOB, "mpr20120.5",    0x1000000, 0x0400000, 0x7113dd7b,
        GAME_WORD_BLOB, "mpr20121.6",    0x1400000, 0x0400000, 0x8c8fd521,
        GAME_WORD_BLOB, "mpr20116.1",    0x1800000, 0x0400000, 0xd30b0175,
        GAME_WORD_BLOB, "mpr20123.8",    0x1c00000, 0x0400000, 0x35f1b89f,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "cottonbm",
    NULL,
    "Cotton Boomerang (JUET 980709 V1.000)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP | STV_REGION_TW,
    0x0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr21075.7",    0x0200000, 0x0200000, 0x200b58ba,
        GAME_WORD_BLOB, "mpr21070.2",    0x0400000, 0x0400000, 0x56c0bf1d,
        GAME_WORD_BLOB, "mpr21071.3",    0x0800000, 0x0400000, 0x2bb18df2,
        GAME_WORD_BLOB, "mpr21072.4",    0x0c00000, 0x0400000, 0x7c7cb977,
        GAME_WORD_BLOB, "mpr21073.5",    0x1000000, 0x0400000, 0xf2e5a5b7,
        GAME_WORD_BLOB, "mpr21074.6",    0x1400000, 0x0400000, 0x6a7e7a7b,
        GAME_WORD_BLOB, "mpr21069.1",    0x1800000, 0x0400000, 0x6a28e3c5,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "critcrsh",
    NULL,
    "Critter Crusher (EA 951204 V1.000)",
    STV_REGION_EU,
    0x0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"epr-18821.ic13",	0x0000001, 0x0080000, 0x9a6658e2,
        HEADER_BLOB,	"epr-18821.ic13",	0x0100001, 0x0080000, 0x9a6658e2,
        GAME_BYTE_BLOB,	"epr-18821.ic13",	0x0200000, 0x0080000, 0x9a6658e2,
        GAME_BYTE_BLOB,	"epr-18821.ic13",	0x0300000, 0x0080000, 0x9a6658e2,
        GAME_WORD_BLOB,	"mpr-18789.ic8",	0x1c00000, 0x0400000, 0xb388616f,
        GAME_WORD_BLOB,	"mpr-18788.ic9",	0x2000000, 0x0400000, 0xfeae5867,
        EEPROM_BLOB,	"critcrsh.nv",		0x0000000, 0x0000080, 0x3da9860e,
        GAME_END, "", 0, 0, 0
    },
    CRITCRSH,
  },
  {
    "sanjeon",
    "sasissu",
    "DaeJeon! SanJeon SuJeon (AJTUE 990412 V1.000)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP | STV_REGION_TW,
    0,
    0,
    sanjeon_init,
    NV_2P,
    {
        HEADER_BLOB,	"ic11",	0x0000001, 0x0200000, 0x9abae8d4,
        GAME_BYTE_BLOB,	"ic13", 0x0400000, 0x0200000, 0xf72c1d13,
        GAME_BYTE_BLOB,	"ic14", 0x0600000, 0x0200000, 0xbcd72105,
        GAME_BYTE_BLOB,	"ic15", 0x0800000, 0x0200000, 0x8c9c8352,
        GAME_BYTE_BLOB,	"ic16", 0x0a00000, 0x0200000, 0x07e11512,
        GAME_BYTE_BLOB,	"ic17", 0x0c00000, 0x0200000, 0x46b7b344,
        GAME_BYTE_BLOB,	"ic18", 0x0e00000, 0x0200000, 0xd48404e1,
        GAME_BYTE_BLOB,	"ic19", 0x1000000, 0x0200000, 0x33d23bb9,
        GAME_BYTE_BLOB,	"ic20", 0x1200000, 0x0200000, 0xf8cc1038,
        GAME_BYTE_BLOB,	"ic21", 0x1400000, 0x0200000, 0x74ceb649,
        GAME_BYTE_BLOB,	"ic22", 0x1600000, 0x0200000, 0x85f31277,
        GAME_BYTE_BLOB,	"ic12",	0x1800000, 0x0400000, 0xd5ebc84e,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "danchih",
    NULL,
    "Danchi de Hanafuda (J 990607 V1.400)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr21974.7",    0x0200000, 0x0200000, 0xe7472793,
        GAME_WORD_BLOB, "mpr21970.2",    0x0400000, 0x0400000, 0x34dd7f4d,
        GAME_WORD_BLOB, "mpr21971.3",    0x0800000, 0x0400000, 0x8995158c,
        GAME_WORD_BLOB, "mpr21972.4",    0x0c00000, 0x0400000, 0x68a39090,
        GAME_WORD_BLOB, "mpr21973.5",    0x1000000, 0x0400000, 0xb0f23f14,
        GAME_END, "", 0, 0, 0
    },
    STVMP,
  },
  {
    "danchiq",
    NULL,
    "Danchi de Quiz: Okusan Yontaku Desuyo! (J 001128 V1.200)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "ic22",    0x0200000, 0x0200000, 0xe216bfc8,
        GAME_WORD_BLOB, "ic24",    0x0400000, 0x0200000, 0xb95aa5ac,
        GAME_WORD_BLOB, "ic26",    0x0600000, 0x0200000, 0xdf6ebd48,
        GAME_WORD_BLOB, "ic28",    0x0800000, 0x0200000, 0xcf6a2b76,
        GAME_WORD_BLOB, "ic30",    0x0a00000, 0x0200000, 0x0b6a9901,
        GAME_WORD_BLOB, "ic32",    0x0c00000, 0x0200000, 0x0b4604f5,
        GAME_WORD_BLOB, "ic34",    0x0e00000, 0x0200000, 0x616e20fa,
        GAME_WORD_BLOB, "ic36",    0x1000000, 0x0200000, 0x43474e08,
        GAME_WORD_BLOB, "ic23",    0x1200000, 0x0200000, 0xd080eb71,
        GAME_WORD_BLOB, "ic25",    0x1400000, 0x0200000, 0x9a4109e5,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "dfeverg",
    NULL,
    "Dancing Fever Gold (J 000821 V2.001)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "13",  0x0000000, 0x0080000, 0xecd7ac4b,
        GAME_WORD_BLOB, "1",   0x0400000, 0x0400000, 0xd72f1640,
        GAME_WORD_BLOB, "2",   0x0800000, 0x0400000, 0xc2e8aee5,
        GAME_WORD_BLOB, "3",   0x0c00000, 0x0400000, 0xcb5b2744,
        GAME_WORD_BLOB, "4",   0x1000000, 0x0400000, 0x7eca59b2,
        GAME_WORD_BLOB, "5",   0x1400000, 0x0400000, 0xc3450f2b,
        GAME_WORD_BLOB, "6",   0x1800000, 0x0400000, 0x1ac57ed5,
        GAME_WORD_BLOB, "8",   0x1c00000, 0x0400000, 0xacc78f10,
        GAME_WORD_BLOB, "9",   0x2000000, 0x0400000, 0x4ffbba8d,
        GAME_WORD_BLOB, "10",  0x2400000, 0x0400000, 0x4b2a4397,
        GAME_WORD_BLOB, "11",  0x2800000, 0x0400000, 0x58877b19,
        // GAME_WORD_BLOB, "12",  0x2c00000, 0x0400000, 0x00000000, //NO_DUMP
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "decathlt",
    NULL,
    "Decathlete (JUET 960709 V1.001)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "epr18967a.13", 0x0000001, 0x0100000, 0xac59c186,
        GAME_BYTE_BLOB, "epr18967a.13", 0x0200000, 0x0100000, 0xac59c186,
        GAME_BYTE_BLOB, "epr18967a.13", 0x0300000, 0x0100000, 0xac59c186,
        GAME_WORD_BLOB, "mpr18968.2",   0x0400000, 0x0400000, 0x11a891de,
        GAME_WORD_BLOB, "mpr18969.3",   0x0800000, 0x0400000, 0x199cc47d,
        GAME_WORD_BLOB, "mpr18970.4",   0x0c00000, 0x0400000, 0x8b7a509e,
        GAME_WORD_BLOB, "mpr18971.5",   0x1000000, 0x0400000, 0xc87c443b,
        GAME_WORD_BLOB, "mpr18972.6",   0x1400000, 0x0400000, 0x45c64fca,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "decathlto",
    "decathlt",
    "Decathlete (JUET 960424 V1.000)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "epr18967.13", 0x0000001, 0x0100000, 0xc0446674,
        GAME_BYTE_BLOB, "epr18967.13", 0x0200000, 0x0100000, 0xc0446674,
        GAME_BYTE_BLOB, "epr18967.13", 0x0300000, 0x0100000, 0xc0446674,
        GAME_WORD_BLOB, "mpr18968.2",  0x0400000, 0x0400000, 0x11a891de,
        GAME_WORD_BLOB, "mpr18969.3",  0x0800000, 0x0400000, 0x199cc47d,
        GAME_WORD_BLOB, "mpr18970.4",  0x0c00000, 0x0400000, 0x8b7a509e,
        GAME_WORD_BLOB, "mpr18971.5",  0x1000000, 0x0400000, 0xc87c443b,
        GAME_WORD_BLOB, "mpr18972.6",  0x1400000, 0x0400000, 0x45c64fca,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "diehard",
    NULL,
    "Die Hard Arcade (UET 960515 V1.000)",
    STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"fpr19119.13",	0x0000001, 0x0100000, 0xde5c4f7c,
        GAME_BYTE_BLOB, "fpr19119.13",	0x0200000, 0x0100000, 0xde5c4f7c,
        GAME_BYTE_BLOB, "fpr19119.13",	0x0300000, 0x0100000, 0xde5c4f7c,
        GAME_WORD_BLOB, "mpr19115.2",	0x0400000, 0x0400000, 0x6fe06a30,
        GAME_WORD_BLOB, "mpr19116.3",	0x0800000, 0x0400000, 0xaf9e627b,
        GAME_WORD_BLOB, "mpr19117.4",	0x0c00000, 0x0400000, 0x74520ff1,
        GAME_WORD_BLOB, "mpr19118.5",	0x1000000, 0x0400000, 0x2c9702f0,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "dnmtdeka",
    "diehard",
    "Dynamite Deka (J 960515 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "fpr19114.13", 0x0000001, 0x0100000, 0x1fd22a5f,
        GAME_BYTE_BLOB, "fpr19114.13", 0x0200000, 0x0100000, 0x1fd22a5f,
        GAME_BYTE_BLOB, "fpr19114.13", 0x0300000, 0x0100000, 0x1fd22a5f,
        GAME_WORD_BLOB, "mpr19115.2",  0x0400000, 0x0400000, 0x6fe06a30,
        GAME_WORD_BLOB, "mpr19116.3",  0x0800000, 0x0400000, 0xaf9e627b,
        GAME_WORD_BLOB, "mpr19117.4",  0x0c00000, 0x0400000, 0x74520ff1,
        GAME_WORD_BLOB, "mpr19118.5",  0x1000000, 0x0400000, 0x2c9702f0,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "ejihon",
    NULL,
    "Ejihon Tantei Jimusyo (J 950613 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"epr18137.13",	0x0000001, 0x0080000, 0x151aa9bc,
        HEADER_BLOB,	"epr18137.13",	0x0100001, 0x0080000, 0x151aa9bc,
        GAME_BYTE_BLOB, "epr18137.13",	0x0200000, 0x0080000, 0x151aa9bc,
        GAME_BYTE_BLOB, "epr18137.13",	0x0300000, 0x0080000, 0x151aa9bc,
        GAME_WORD_BLOB, "mpr18138.2",	0x0400000, 0x0400000, 0xf5567049,
        GAME_WORD_BLOB, "mpr18139.3",	0x0800000, 0x0400000, 0xf36b4878,
        GAME_WORD_BLOB, "mpr18140.4",	0x0c00000, 0x0400000, 0x228850a0,
        GAME_WORD_BLOB, "mpr18141.5",	0x1000000, 0x0400000, 0xb51eef36,
        GAME_WORD_BLOB, "mpr18142.6",	0x1400000, 0x0400000, 0xcf259541,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "fantazonem",
    NULL,
    "Fantasy Zone (J 990202 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "epr-21440a.ic13",	0x0000000, 0x0080000, 0x28457d58,
        GAME_WORD_BLOB, "mpr-21441.ic2",	0x0400000, 0x0400000, 0xb69133a5,
        GAME_WORD_BLOB, "mpr-21445.ic1",	0x0800000, 0x0400000, 0xc02ffbd3,
        GAME_WORD_BLOB, "mpr-21442.ic4",	0x0c00000, 0x0400000, 0xd4d3575f,
        GAME_WORD_BLOB, "mpr-21446.ic3",	0x1000000, 0x0400000, 0x4831539e,
        GAME_WORD_BLOB, "mpr-21443.ic6",	0x1400000, 0x0400000, 0xcb1401c9,
        GAME_WORD_BLOB, "mpr-21447.ic5",	0x1800000, 0x0400000, 0x61e0d313,
        GAME_WORD_BLOB, "mpr-21444.ic8",	0x1c00000, 0x0400000, 0xa82ff33b,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "finlarch",
    "smleague",
    "Final Arch (J 950714 V1.001)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"finlarch.13",	0x0000001, 0x0100000, 0x4505fa9e,
        GAME_BYTE_BLOB,	"finlarch.13",	0x0200000, 0x0100000, 0x4505fa9e,
        GAME_BYTE_BLOB,	"finlarch.13",	0x0300000, 0x0100000, 0x4505fa9e,
        GAME_WORD_BLOB,	"mpr18257.2",	0x0400000, 0x0400000, 0x137fdf55,
        HEADER_BLOB,	"mpr18257.2",	0x1400000, 0x0400000, 0x137fdf55,
        GAME_WORD_BLOB,	"mpr18258.3",	0x0800000, 0x0400000, 0xf519c505,
        HEADER_BLOB,	"mpr18258.3",	0x1800000, 0x0400000, 0xf519c505,
        GAME_WORD_BLOB,	"mpr18259.4",	0x0c00000, 0x0400000, 0x5cabc775,
        HEADER_BLOB,	"mpr18259.4",	0x1c00000, 0x0400000, 0x5cabc775,
        GAME_WORD_BLOB,	"mpr18260.5",	0x1000000, 0x0400000, 0xf5b92082,
        HEADER_BLOB,	"mpr18260.5",	0x2000000, 0x0400000, 0xf5b92082,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "ffreveng",
    NULL,
    "Final Fight Revenge (JUET 990714 V1.000)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0x0524ac01,
    0,
    NULL,
    NULL,
    {
		HEADER_BLOB,	"ffr110.ic35",	0x0000001, 0x0100000, 0x3ffea541,
        GAME_WORD_BLOB, "opr21872.7",   0x0200000, 0x0200000, 0x32d36fee,
        GAME_WORD_BLOB, "mpr21873.2",   0x0400000, 0x0400000, 0xdac5bd98,
        GAME_WORD_BLOB, "mpr21874.3",   0x0800000, 0x0400000, 0x0a7be2f1,
        GAME_WORD_BLOB, "mpr21875.4",   0x0c00000, 0x0400000, 0xccb75029,
        GAME_WORD_BLOB, "mpr21876.5",   0x1000000, 0x0400000, 0xbb92a7fc,
        GAME_WORD_BLOB, "mpr21877.6",   0x1400000, 0x0400000, 0xc22a4a75,
        GAME_WORD_BLOB, "opr21878.1",   0x1800000, 0x0200000, 0x2ea4a64d,
        GAME_END, "", 0, 0, 0
    },
    STV6B,
  },
  {
    "ffrevng10",
    "ffreveng",
    "Final Fight Revenge (JUET 990930 V1.100)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0x0524ac01,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "opr21872.7",   0x0200000, 0x0200000, 0x32d36fee,
        GAME_WORD_BLOB, "mpr21873.2",   0x0400000, 0x0400000, 0xdac5bd98,
        GAME_WORD_BLOB, "mpr21874.3",   0x0800000, 0x0400000, 0x0a7be2f1,
        GAME_WORD_BLOB, "mpr21875.4",   0x0c00000, 0x0400000, 0xccb75029,
        GAME_WORD_BLOB, "mpr21876.5",   0x1000000, 0x0400000, 0xbb92a7fc,
        GAME_WORD_BLOB, "mpr21877.6",   0x1400000, 0x0400000, 0xc22a4a75,
        GAME_WORD_BLOB, "opr21878.1",   0x1800000, 0x0200000, 0x2ea4a64d,
        GAME_END, "", 0, 0, 0
    },
    STV6B,
  },
  {
    "fhboxers",
    NULL,
    "Funky Head Boxers (JUETBKAL 951218 V1.000)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP | STV_REGION_TW,
    0x0524ac01,
    0,
    NULL,
    NV_1P,
    {
        HEADER_BLOB,    "fr18541a.13",   0x0000001, 0x0100000, 0x8c61a17c,
        GAME_WORD_BLOB, "mpr18538.7",    0x0200000, 0x0200000, 0x7b5230c5,
        GAME_WORD_BLOB, "mpr18533.2",    0x0400000, 0x0400000, 0x7181fe51,
        GAME_WORD_BLOB, "mpr18534.3",    0x0800000, 0x0400000, 0xc87ef125,
        GAME_WORD_BLOB, "mpr18535.4",    0x0c00000, 0x0400000, 0x929a64cf,
        GAME_WORD_BLOB, "mpr18536.5",    0x1000000, 0x0400000, 0x51b9f64e,
        GAME_WORD_BLOB, "mpr18537.6",    0x1400000, 0x0400000, 0xc364f6a7,
        GAME_WORD_BLOB, "mpr18532.1",    0x1800000, 0x0400000, 0x39528643,
        GAME_WORD_BLOB, "mpr18539.8",    0x1c00000, 0x0400000, 0x62b3908c,
        GAME_WORD_BLOB, "mpr18540.9",    0x2000000, 0x0400000, 0x4c2b59a4,
        EEPROM_BLOB,    "fhboxers.nv",   0x0000, 0x0080, 0x590fd6da,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "gaxeduel",
    NULL,
    "Golden Axe - The Duel (JUETL 950117 V1.000)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,   "epr17766.13",    0x0000001, 0x0080000, 0xa83fcd62,
        HEADER_BLOB,   "epr17766.13",    0x0100001, 0x0080000, 0xa83fcd62,
        GAME_BYTE_BLOB,"epr17766.13",    0x0200000, 0x0080000, 0xa83fcd62,
        GAME_BYTE_BLOB,"epr17766.13",    0x0300000, 0x0080000, 0xa83fcd62,
        GAME_WORD_BLOB, "mpr17768.2",    0x0400000, 0x0400000, 0xd6808a7d,
        GAME_WORD_BLOB, "mpr17769.3",    0x0800000, 0x0400000, 0x3471dd35,
        GAME_WORD_BLOB, "mpr17770.4",    0x0c00000, 0x0400000, 0x06978a00,
        GAME_WORD_BLOB, "mpr17771.5",    0x1000000, 0x0400000, 0xaea2ea3b,
        GAME_WORD_BLOB, "mpr17772.6",    0x1400000, 0x0400000, 0xb3dc0e75,
        GAME_WORD_BLOB, "mpr17767.1",    0x1800000, 0x0400000, 0x9ba1e7b1,
        GAME_END, "", 0, 0, 0
    },
    STV6B,
  },
  {
    "groovef",
    NULL,
    "Groove on Fight - Gouketsuji Ichizoku 3 (J 970416 V1.001)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr19820.7",    0x0200000, 0x0100000, 0xe93c4513,
        GAME_WORD_BLOB, "mpr19815.2",    0x0400000, 0x0400000, 0x1b9b14e6,
        GAME_WORD_BLOB, "mpr19816.3",    0x0800000, 0x0400000, 0x83f5731c,
        GAME_WORD_BLOB, "mpr19817.4",    0x0c00000, 0x0400000, 0x525bd6c7,
        GAME_WORD_BLOB, "mpr19818.5",    0x1000000, 0x0400000, 0x66723ba8,
        GAME_WORD_BLOB, "mpr19819.6",    0x1400000, 0x0400000, 0xee8c55f4,
        GAME_WORD_BLOB, "mpr19814.1",    0x1800000, 0x0400000, 0x8f20e9f7,
        GAME_WORD_BLOB, "mpr19821.8",    0x1c00000, 0x0400000, 0xf69a76e6,
        GAME_WORD_BLOB, "mpr19822.9",    0x2000000, 0x0200000, 0x5e8c4b5f,
        GAME_END, "", 0, 0, 0
    },
    STV6B,
  },
  {
    "grdforce",
    NULL,
    "Guardian Force (JUET 980318 V0.105)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr20844.7",    0x0200000, 0x0200000, 0x283e7587,
        GAME_WORD_BLOB, "mpr20839.2",    0x0400000, 0x0400000, 0xfacd4dd8,
        GAME_WORD_BLOB, "mpr20840.3",    0x0800000, 0x0400000, 0xfe0158e6,
        GAME_WORD_BLOB, "mpr20841.4",    0x0c00000, 0x0400000, 0xd87ac873,
        GAME_WORD_BLOB, "mpr20842.5",    0x1000000, 0x0400000, 0xbaebc506,
        GAME_WORD_BLOB, "mpr20843.6",    0x1400000, 0x0400000, 0x263e49cc,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "patocar",
    NULL,
    "Hashire Patrol Car (J 990326 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "ic22.bin",		0x0200000, 0x200000, 0xb7e6d425,
        GAME_WORD_BLOB, "ic24.bin",		0x0400000, 0x200000, 0xcbbb687e,
        GAME_WORD_BLOB, "ic26.bin",		0x0600000, 0x200000, 0x91db9dbe,
        GAME_WORD_BLOB, "ic28.bin",		0x0800000, 0x200000, 0xbff0cd9c,
        GAME_WORD_BLOB, "ic30.bin",		0x0a00000, 0x200000, 0x9a4109e5,
        EEPROM_BLOB,	"patocar.nv",	0x0000, 0x0080, 0xd9873ee8,
        GAME_END, "", 0, 0, 0
    },
    PATOCAR,
  },
  {
    "introdon",
    NULL,
    "Karaoke Quiz Intro Don Don! (J 960213 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "epr18937.13", 0x0000001, 0x0080000, 0x1f40d766,
        HEADER_BLOB,    "epr18937.13", 0x0100001, 0x0080000, 0x1f40d766,
        GAME_WORD_BLOB, "mpr18944.7",  0x0200000, 0x0100000, 0xf7f75ce5,
        GAME_WORD_BLOB, "mpr18944.7",  0x0300000, 0x0100000, 0xf7f75ce5,
        GAME_WORD_BLOB, "mpr18939.2",  0x0400000, 0x0400000, 0xef95a6e6,
        GAME_WORD_BLOB, "mpr18940.3",  0x0800000, 0x0400000, 0xcabab4cd,
        GAME_WORD_BLOB, "mpr18941.4",  0x0c00000, 0x0400000, 0xf4a33a20,
        GAME_WORD_BLOB, "mpr18942.5",  0x1000000, 0x0400000, 0x8dd0a446,
        GAME_WORD_BLOB, "mpr18943.6",  0x1400000, 0x0400000, 0xd8702a9e,
        GAME_WORD_BLOB, "mpr18938.1",  0x1800000, 0x0400000, 0x580ecb83,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "magzun",
    NULL,
    "Magical Zunou Power (J 961031 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "flash.ic13",    0x0000001, 0x0100000, 0xe6f0aca0,
        GAME_BYTE_BLOB, "flash.ic13",    0x0200000, 0x0100000, 0xe6f0aca0,
        GAME_BYTE_BLOB, "flash.ic13",    0x0300000, 0x0100000, 0xe6f0aca0,
        GAME_WORD_BLOB, "mpr-19354.ic2", 0x0400000, 0x0400000, 0xa23822e7,
        GAME_WORD_BLOB, "mpr-19355.ic3", 0x0800000, 0x0400000, 0xd70e5ebc,
        GAME_WORD_BLOB, "mpr-19356.ic4", 0x0c00000, 0x0400000, 0x3bc43fe9,
        GAME_WORD_BLOB, "mpr-19357.ic5", 0x1000000, 0x0400000, 0xaa749370,
        GAME_WORD_BLOB, "mpr-19358.ic6", 0x1400000, 0x0400000, 0x0969f1ec,
        GAME_WORD_BLOB, "mpr-19359.ic1", 0x1800000, 0x0400000, 0xb0d06f9c,
        EEPROM_BLOB,    "magzun.nv",     0x0000, 0x0080, 0x42700321,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "maruchan",
    NULL,
    "Maru-Chan de Goo! (J 971216 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "epr20416.13", 0x0000001, 0x0100000, 0x8bf0176d,
        GAME_BYTE_BLOB, "epr20416.13", 0x0200000, 0x0100000, 0x8bf0176d,
        GAME_BYTE_BLOB, "epr20416.13", 0x0300000, 0x0100000, 0x8bf0176d,
        GAME_WORD_BLOB, "mpr20417.2",  0x0400000, 0x0400000, 0x636c2a08,
        GAME_WORD_BLOB, "mpr20418.3",  0x0800000, 0x0400000, 0x3f0d9e34,
        GAME_WORD_BLOB, "mpr20419.4",  0x0c00000, 0x0400000, 0xec969815,
        GAME_WORD_BLOB, "mpr20420.5",  0x1000000, 0x0400000, 0xf2902c88,
        GAME_WORD_BLOB, "mpr20421.6",  0x1400000, 0x0400000, 0xcd0b477c,
        GAME_WORD_BLOB, "mpr20422.1",  0x1800000, 0x0400000, 0x66335049,
        GAME_WORD_BLOB, "mpr20423.8",  0x1c00000, 0x0400000, 0x2bd55832,
        GAME_WORD_BLOB, "mpr20443.9",  0x2000000, 0x0400000, 0x8ac288f5,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "mausuke",
    NULL,
    "Mausuke no Ojama the World (J 960314 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "ic13.bin", 0x0000001, 0x0100000, 0xb456f4cd,
        GAME_BYTE_BLOB, "ic13.bin", 0x0200000, 0x0100000, 0xb456f4cd,
        GAME_BYTE_BLOB, "ic13.bin", 0x0300000, 0x0100000, 0xb456f4cd,
        GAME_WORD_BLOB, "mcj-00.2", 0x0400000, 0x0200000, 0x4eeacd6f,
        GAME_WORD_BLOB, "mcj-01.3", 0x0800000, 0x0200000, 0x365a494b,
        GAME_WORD_BLOB, "mcj-02.4", 0x0c00000, 0x0200000, 0x8b8e4931,
        GAME_WORD_BLOB, "mcj-03.5", 0x1000000, 0x0200000, 0x9015a0e7,
        GAME_WORD_BLOB, "mcj-04.6", 0x1400000, 0x0200000, 0x9d1beaee,
        GAME_WORD_BLOB, "mcj-05.1", 0x1800000, 0x0200000, 0xa7626a82,
        GAME_WORD_BLOB, "mcj-06.8", 0x1c00000, 0x0200000, 0x1ab8e90e,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "micrombc",
    NULL,
    "Microman Battle Charge (J 990326 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22",			0x0200000, 0x200000, 0x8385bc45,
        GAME_WORD_BLOB, "ic24",			0x0400000, 0x200000, 0x84ecb42f,
        GAME_WORD_BLOB, "ic26",			0x0600000, 0x200000, 0x869bc19c,
        GAME_WORD_BLOB, "ic28",			0x0800000, 0x200000, 0x0c3db354,
        GAME_WORD_BLOB, "ic30",			0x0a00000, 0x200000, 0x03b9eacf,
        GAME_WORD_BLOB, "ic32",			0x0c00000, 0x200000, 0x62c10626,
        GAME_WORD_BLOB, "ic34",			0x1000000, 0x200000, 0x8d89877e,
        GAME_WORD_BLOB, "ic36",			0x1200000, 0x200000, 0x8d89877e,
        EEPROM_BLOB,	"micrombc.nv",	0x0000, 0x0080, 0x6e89815f,
        GAME_END, "", 0, 0, 0
    },
    MICROMBC,
  },
  {
    "nameclub",
    NULL,
    "Name Club (J 960315 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "ic22",			0x0200000, 0x0200000, 0xac23c648,
        GAME_WORD_BLOB, "ic24",			0x0600000, 0x0200000, 0xa16902e3,
        GAME_WORD_BLOB, "ic26",			0x0a00000, 0x0200000, 0xa5eab3f3,
        GAME_WORD_BLOB, "ic28",			0x0e00000, 0x0200000, 0x34ed677a,
        EEPROM_BLOB,	"nameclub.nv",	0x0000, 0x0080, 0x680a64bc,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "nclubv2",
    NULL,
    "Name Club Ver.2 (J 960315 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "nclubv2.ic22",	0x0200000, 0x0200000, 0x7e81676d,
        GAME_WORD_BLOB, "nclubv2.ic24",	0x0600000, 0x0200000, 0x1b7637de,
        GAME_WORD_BLOB, "nclubv2.ic26",	0x0a00000, 0x0200000, 0x630be99d,
        GAME_WORD_BLOB, "nclubv2.ic28",	0x0e00000, 0x0200000, 0x1a3ca5e2,
        EEPROM_BLOB,	"nclubv2.nv",	0x0000, 0x0080, 0x96d55fa9,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "nclubv3",
    NULL,
    "Name Club Ver.3 (J 970723 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "ic22",		0x0200000, 0x0200000, 0xb4008ed0,
        GAME_WORD_BLOB, "ic24",		0x0400000, 0x0200000, 0x4e894850,
        GAME_WORD_BLOB, "ic26",		0x0600000, 0x0200000, 0x5b6b023f,
        GAME_WORD_BLOB, "ic28",		0x0800000, 0x0200000, 0xb7beab03,
        GAME_WORD_BLOB, "ic30",		0x0a00000, 0x0200000, 0xa9f81069,
        GAME_WORD_BLOB, "ic32",		0x0c00000, 0x0200000, 0x02708d66,
        GAME_WORD_BLOB, "ic34",		0x0e00000, 0x0200000, 0xc79d0537,
        GAME_WORD_BLOB, "ic36",		0x1000000, 0x0200000, 0x0c9df896,
        GAME_WORD_BLOB, "ic23",		0x1200000, 0x0200000, 0xbd922829,
        GAME_WORD_BLOB, "ic25",		0x1400000, 0x0200000, 0xf77f9e24,
        EEPROM_BLOB, "nclubv3.nv",  0x0000, 0x0080, 0x9122a9e9,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "nclubdis",
    NULL,
    "Name Club Disney (J 980614 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22",			0x0200000, 0x0200000, 0xcfd5eb54,
        GAME_WORD_BLOB, "ic24",			0x0400000, 0x0200000, 0x943682eb,
        GAME_WORD_BLOB, "ic26",			0x0600000, 0x0200000, 0xa17bd53a,
        GAME_WORD_BLOB, "ic28",			0x0800000, 0x0200000, 0x9a597bff,
        GAME_WORD_BLOB, "ic30",			0x0a00000, 0x0200000, 0x3eb020f6,
        GAME_WORD_BLOB, "ic32",			0x0c00000, 0x0200000, 0xac26d375,
        GAME_WORD_BLOB, "ic34",			0x0e00000, 0x0200000, 0xe157cd99,
        GAME_WORD_BLOB, "ic36",			0x1000000, 0x0200000, 0x9a4109e5,
        GAME_WORD_BLOB, "ic23",			0x1200000, 0x0200000, 0x9a4109e5,
        GAME_WORD_BLOB, "ic25",			0x1400000, 0x0200000, 0x9a4109e5,
        EEPROM_BLOB,	"nclubdis.nv",  0x0000, 0x0080, 0x7efc9c6a,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "supgoal",
    NULL,
    "Nerae! Super Goal (J 981218 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "ic22.bin",		0x0200000, 0x200000, 0xa686f7a2,
        GAME_WORD_BLOB, "ic24.bin",		0x0400000, 0x200000, 0x56fbbeea,
        GAME_WORD_BLOB, "ic26.bin",		0x0600000, 0x200000, 0x64701c2b,
        GAME_WORD_BLOB, "ic28.bin",		0x0800000, 0x200000, 0xd9aebe8c,
        GAME_WORD_BLOB, "ic30.bin",		0x0a00000, 0x200000, 0x26d4ade5,
        EEPROM_BLOB, 	"supgoal.nv",	0x0000, 0x0080, 0x63806aae,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "othellos",
    NULL,
    "Othello Shiyouyo (J 980423 V1.002)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_2P,
    {
        GAME_WORD_BLOB, "mpr20967.7",    0x0200000, 0x0200000, 0xefc05b97,
        GAME_WORD_BLOB, "mpr20963.2",    0x0400000, 0x0400000, 0x2cc4f141,
        GAME_WORD_BLOB, "mpr20964.3",    0x0800000, 0x0400000, 0x5f5cda94,
        GAME_WORD_BLOB, "mpr20965.4",    0x0c00000, 0x0400000, 0x37044f3e,
        GAME_WORD_BLOB, "mpr20966.5",    0x1000000, 0x0400000, 0xb94b83de,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pblbeach",
    NULL,
    "Pebble Beach - The Great Shot (JUE 950913 V0.990)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "epr18852.13", 0x0000001, 0x0080000, 0xd12414ec,
        HEADER_BLOB,    "epr18852.13", 0x0100001, 0x0080000, 0xd12414ec,
        GAME_BYTE_BLOB, "epr18852.13", 0x0200000, 0x0080000, 0xd12414ec,
        GAME_BYTE_BLOB, "epr18852.13", 0x0300000, 0x0080000, 0xd12414ec,
        GAME_WORD_BLOB, "mpr18853.2",  0x0400000, 0x0400000, 0xb9268c97,
        GAME_WORD_BLOB, "mpr18854.3",  0x0800000, 0x0400000, 0x3113c8bc,
        GAME_WORD_BLOB, "mpr18855.4",  0x0c00000, 0x0400000, 0xdaf6ad0c,
        GAME_WORD_BLOB, "mpr18856.5",  0x1000000, 0x0400000, 0x214cef24,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "prikura",
    NULL,
    "Princess Clara Daisakusen (J 960910 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr19337.7",	0x0200000, 0x0200000, 0x76f69ff3,
        GAME_WORD_BLOB, "mpr19333.2",	0x0400000, 0x0400000, 0xeb57a6a6,
        GAME_WORD_BLOB, "mpr19334.3",	0x0800000, 0x0400000, 0xc9979981,
        GAME_WORD_BLOB, "mpr19335.4",	0x0c00000, 0x0400000, 0x9e000140,
        GAME_WORD_BLOB, "mpr19336.5",	0x1000000, 0x0400000, 0x2363fa4b,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "prc298sp",
    NULL,
    "Print Club 2 '98 Spring Ver (J 971017 V1.100)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "prc298sp_ic22",	0x0200000, 0x0200000, 0xcb0ec98a,
        GAME_WORD_BLOB, "prc298sp_ic24",	0x0400000, 0x0200000, 0x645e7e24,
        GAME_WORD_BLOB, "prc298sp_ic26",	0x0600000, 0x0200000, 0x9d3ad85d,
        GAME_WORD_BLOB, "prc298sp_ic28",	0x0800000, 0x0200000, 0x877e73cc,
        GAME_WORD_BLOB, "prc298sp_ic30",	0x0a00000, 0x0200000, 0x03b9eacf,
        GAME_WORD_BLOB, "prc298sp_ic32",	0x0c00000, 0x0200000, 0x62c10626,
        GAME_WORD_BLOB, "prc298sp_ic34",	0x0e00000, 0x0200000, 0x8d89877e,
        GAME_WORD_BLOB, "prc298sp_ic36",	0x1000000, 0x0200000, 0x8d89877e,
        EEPROM_BLOB,    "prc298sp.nv",		0x0000, 0x0080, 0xa23dd0f2,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "prc298au",
    NULL,
    "Print Club 2 '98 Autumn Ver (J 980827 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "prc298au_ic22",	0x0200000, 0x0200000, 0x21a995ce,
        GAME_WORD_BLOB, "prc298au_ic24",	0x0400000, 0x0200000, 0x94540f39,
        GAME_WORD_BLOB, "prc298au_ic26",	0x0600000, 0x0200000, 0x8b22c41f,
        GAME_WORD_BLOB, "prc298au_ic28",	0x0800000, 0x0200000, 0xbf68cec0,
        GAME_WORD_BLOB, "prc298au_ic30",	0x0a00000, 0x0200000, 0xae276c06,
        GAME_WORD_BLOB, "prc298au_ic32",	0x0c00000, 0x0200000, 0xa3fb81f5,
        GAME_WORD_BLOB, "prc298au_ic34",	0x0e00000, 0x0200000, 0x04200dc9,
        GAME_WORD_BLOB, "prc298au_ic36",	0x1000000, 0x0200000, 0x9a4109e5,
        EEPROM_BLOB,    "prc298au.nv",		0x0000, 0x0080, 0xb4440ff0,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "prc298su",
    NULL,
    "Print Club 2 '98 Summer Ver (J 980603 V1.100)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclb298s_ic22",	0x0200000, 0x0200000, 0x9720fe7a,
        GAME_WORD_BLOB, "pclb298s_ic24",	0x0400000, 0x0200000, 0x380496dc,
        GAME_WORD_BLOB, "pclb298s_ic26",	0x0600000, 0x0200000, 0x42622126,
        GAME_WORD_BLOB, "pclb298s_ic28",	0x0800000, 0x0200000, 0xc03e861a,
        GAME_WORD_BLOB, "pclb298s_ic30",	0x0a00000, 0x0200000, 0x01844b12,
        EEPROM_BLOB,    "prc298su.nv",		0x0000, 0x0080, 0x6b81636a,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "prc29au",
    NULL,
    "Print Club 2 Vol. 9 Autumn (J V1.100)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22.bin",		0x0200000, 0x0200000, 0xb7a9bfa4,
        GAME_WORD_BLOB, "ic24.bin",		0x0400000, 0x0200000, 0x91f36785,
        GAME_WORD_BLOB, "ic26.bin",		0x0600000, 0x0200000, 0x4c1b8823,
        GAME_WORD_BLOB, "ic28.bin",		0x0800000, 0x0200000, 0x594d200f,
        GAME_WORD_BLOB, "ic30.bin",		0x0a00000, 0x0200000, 0x03b9eacf,
        GAME_WORD_BLOB, "ic32.bin",		0x0c00000, 0x0200000, 0x76f6efaa,
        GAME_WORD_BLOB, "ic34.bin",		0x0e00000, 0x0200000, 0x894c63f9,
        GAME_WORD_BLOB, "ic36.bin",		0x1000000, 0x0200000, 0x524a1c4e,
        EEPROM_BLOB,	"eeprom",		0x0000, 0x0080, 0x447bb3bd,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclub27s",
    NULL,
    "Print Club 2 Vol. 7 Spring (J 970313 V1.100)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclub2v7.ic22",	0x0200000, 0x0200000, 0x44c8ab27,
        GAME_WORD_BLOB, "pclub2v7.ic24",	0x0400000, 0x0200000, 0x24818437,
        GAME_WORD_BLOB, "pclub2v7.ic26",	0x0600000, 0x0200000, 0x076c1d44,
        GAME_WORD_BLOB, "pclub2v7.ic28",	0x0800000, 0x0200000, 0xff9643ca,
        GAME_WORD_BLOB, "pclub2v7.ic30",	0x0a00000, 0x0200000, 0x03b9eacf,
        EEPROM_BLOB,	"pclub27s.nv",		0x0000, 0x0080, 0xe58c7167,
        // EEPROM_BLOB,	"315-6055.ic12",	0x0000, 0x0000, 0x00000000,
        // EEPROM_BLOB,	"315-6056.ic13",	0x0000, 0x0200, 0x01170000,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "prc28su",
    NULL,
    "Print Club 2 Vol. 8 '97 Summer (J 970616 V1.100))",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "u22.bin",	0x0200000, 0x0200000, 0xb78cf122,
        GAME_WORD_BLOB, "u24.bin",	0x0400000, 0x0200000, 0xaca05d29,
        GAME_WORD_BLOB, "u26.bin",	0x0600000, 0x0200000, 0x5591f6e2,
        GAME_WORD_BLOB, "u28.bin",	0x0800000, 0x0200000, 0x0899889b,
        GAME_WORD_BLOB, "u30.bin",	0x0a00000, 0x0200000, 0x03b9eacf,
        GAME_WORD_BLOB, "u32.bin",	0x0c00000, 0x0200000, 0x5dc1f4d7,
        GAME_WORD_BLOB, "u34.bin",	0x0e00000, 0x0200000, 0x0222734a,
        GAME_WORD_BLOB, "u36.bin",	0x1000000, 0x0200000, 0x57c30e0f,
        EEPROM_BLOB,	"eeprom",	0x0000, 0x0080, 0x447bb3bd,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclub2",
    NULL,
    "Print Club 2 (U 970921 V1.000)",
    STV_REGION_US,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclub2_ic22",	0x0200000, 0x0200000, 0xd2ceade7,
        GAME_WORD_BLOB, "pclub2_ic24",	0x0400000, 0x0200000, 0x0e968c2d,
        GAME_WORD_BLOB, "pclub2_ic26",	0x0600000, 0x0200000, 0xab51da70,
        GAME_WORD_BLOB, "pclub2_ic28",	0x0800000, 0x0200000, 0x3a654b2a,
        GAME_WORD_BLOB, "pclub2_ic30",	0x0a00000, 0x0200000, 0x8d89877e,
        EEPROM_BLOB,	"pclub2.nv",	0x0000, 0x0080, 0x00d0f04e,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclub26w",
    NULL,
    "Print Club 2 Vol. 6 Winter (J 961210 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclbvol6w_ic22",	0x0200000, 0x0200000, 0x72aa320c,
        GAME_WORD_BLOB, "pclbvol6w_ic24",	0x0400000, 0x0200000, 0xd98371e2,
        GAME_WORD_BLOB, "pclbvol6w_ic26",	0x0600000, 0x0200000, 0xe6bbe3a5,
        GAME_WORD_BLOB, "pclbvol6w_ic28",	0x0800000, 0x0200000, 0x3c330c9b,
        GAME_WORD_BLOB, "pclbvol6w_ic30",	0x0a00000, 0x0200000, 0x67646090,
        EEPROM_BLOB,	"pclub26w.nv",		0x0000, 0x0080, 0x448f770d,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclub26wa",
    "pclub26w",
    "Print Club 2 Vol. 6 Winter (J 970121 V1.200)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22.bin",		0x0200000, 0x0200000, 0xa88e117d,
        GAME_WORD_BLOB, "ic24.bin",		0x0600000, 0x0200000, 0xd98371e2,
        GAME_WORD_BLOB, "ic26.bin",		0x0600000, 0x0200000, 0xd0412c8d,
        GAME_WORD_BLOB, "ic28.bin",		0x0600000, 0x0200000, 0x3c330c9b,
        GAME_WORD_BLOB, "ic30.bin",		0x0a00000, 0x0200000, 0x00a0c702,
        EEPROM_BLOB,	"pclub26w.nv",	0x0000, 0x0080, 0x448f770d,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "prc297wi",
    NULL,
    "Print Club 2 '97 Winter Ver (J 971017 V1.100, set 1)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "prc297wi_ic22",	0x0200000, 0x0200000, 0x589f6705,
        GAME_WORD_BLOB, "prc297wi_ic24",	0x0400000, 0x0200000, 0x4bd706d1,
        GAME_WORD_BLOB, "prc297wi_ic26",	0x0600000, 0x0200000, 0x417e182a,
        GAME_WORD_BLOB, "prc297wi_ic28",	0x0800000, 0x0200000, 0x73da594e,
        GAME_WORD_BLOB, "prc297wi_ic30",	0x0a00000, 0x0200000, 0x03b9eacf,
        GAME_WORD_BLOB, "prc297wi_ic32",	0x0c00000, 0x0200000, 0x20437e93,
        GAME_WORD_BLOB, "prc297wi_ic34",	0x0e00000, 0x0200000, 0x9639b003,
        GAME_WORD_BLOB, "prc297wi_ic36",	0x1000000, 0x0200000, 0xdd1b57b6,
        GAME_WORD_BLOB, "prc297wi_ic23",	0x1200000, 0x0200000, 0xe3d9d12b,
        GAME_WORD_BLOB, "prc297wi_ic25",	0x1400000, 0x0200000, 0x71238374,
        GAME_WORD_BLOB, "prc297wi_ic27",	0x1600000, 0x0200000, 0x7485a9a2,
        EEPROM_BLOB,	"eeprom",			0x0000, 0x0080, 0x9ba58358,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "prc297wia",
    "prc297wi",
    "Print Club 2 '97 Winter Ver (J 971017 V1.100, set 2)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclb297w_ic22_alt",	0x0200000, 0x0200000, 0x1feb3bfe,
        GAME_WORD_BLOB, "prc297wi_ic24",		0x0400000, 0x0200000, 0x4bd706d1,
        GAME_WORD_BLOB, "prc297wi_ic26",		0x0600000, 0x0200000, 0x417e182a,
        GAME_WORD_BLOB, "prc297wi_ic28",		0x0800000, 0x0200000, 0x73da594e,
        GAME_WORD_BLOB, "prc297wi_ic30",		0x0a00000, 0x0200000, 0x03b9eacf,
        GAME_WORD_BLOB, "prc297wi_ic32",		0x0c00000, 0x0200000, 0x20437e93,
        GAME_WORD_BLOB, "prc297wi_ic34",		0x0e00000, 0x0200000, 0x9639b003,
        GAME_WORD_BLOB, "prc297wi_ic36",		0x1000000, 0x0200000, 0xdd1b57b6,
        GAME_WORD_BLOB, "prc297wi_ic23",		0x1200000, 0x0200000, 0xe3d9d12b,
        GAME_WORD_BLOB, "prc297wi_ic25",		0x1400000, 0x0200000, 0x71238374,
        GAME_WORD_BLOB, "prc297wi_ic27",		0x1600000, 0x0200000, 0x7485a9a2,
        EEPROM_BLOB,	"eeprom",				0x0000, 0x0080, 0x9ba58358,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "prc2ksu",
    NULL,
    "Print Club 2 2000 Summer (J 000509 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22.bin",		0x0200000, 0x0200000, 0x4b3de7df,
        GAME_WORD_BLOB, "ic24.bin",		0x0400000, 0x0200000, 0x02da49b7,
        GAME_WORD_BLOB, "ic26.bin",		0x0600000, 0x0200000, 0xa431d614,
        GAME_WORD_BLOB, "ic28.bin",		0x0800000, 0x0200000, 0xc0fba1a5,
        GAME_WORD_BLOB, "ic30.bin",		0x0a00000, 0x0200000, 0x0811d0e4,
        EEPROM_BLOB,	"prc2ksu.nv",	0x0000, 0x0080, 0xee7ffdc5,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclb2elk",
    NULL,
    "Print Club 2 Earth Limited Kobe (Print Club Custom) (J 970808 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22.bin",		0x0200000, 0x0200000, 0x2faed82a,
        GAME_WORD_BLOB, "ic24.bin",		0x0400000, 0x0200000, 0x9cacfb7b,
        GAME_WORD_BLOB, "ic26.bin",		0x0600000, 0x0200000, 0x533a189e,
        GAME_WORD_BLOB, "ic28.bin",		0x0800000, 0x0200000, 0x1f0c9113,
        GAME_WORD_BLOB, "ic30.bin",		0x0a00000, 0x0200000, 0x0e188b8c,
        EEPROM_BLOB,	"pclb2elk.nv",	0x0000, 0x0080, 0x54c7564f,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclub2fc",
    NULL,
    "Print Club 2 Felix The Cat (Rev. A) (J 970415 V1.100)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclub2fc_ic22",	0x0200000, 0x0200000, 0x109c61a5,
        GAME_WORD_BLOB, "pclub2fc_ic24",	0x0400000, 0x0200000, 0x0ccc5538,
        GAME_WORD_BLOB, "pclub2fc_ic26",	0x0600000, 0x0200000, 0x8d89877e,
        GAME_WORD_BLOB, "pclub2fc_ic28",	0x0800000, 0x0200000, 0xff9643ca,
        GAME_WORD_BLOB, "pclub2fc_ic30",	0x0a00000, 0x0200000, 0x03b9eacf,
        EEPROM_BLOB,	"pclub2fc.nv",		0x0000, 0x0080, 0xc8082326,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclubor",
    NULL,
    "Print Club Goukakenran (J 991104 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclubor_ic22",	0x0200000, 0x0200000, 0xb25072f7,
        GAME_WORD_BLOB, "pclubor_ic24",	0x0400000, 0x0200000, 0xa4863a0e,
        GAME_WORD_BLOB, "pclubor_ic26",	0x0600000, 0x0200000, 0x7f55baf7,
        GAME_WORD_BLOB, "pclubor_ic28",	0x0800000, 0x0200000, 0xcafd2a7d,
        GAME_WORD_BLOB, "pclubor_ic30",	0x0a00000, 0x0200000, 0xccf6f885,
        GAME_WORD_BLOB, "pclubor_ic32",	0x0c00000, 0x0200000, 0x62e6d1e1,
        GAME_WORD_BLOB, "pclubor_ic34",	0x0e00000, 0x0200000, 0x19cdd167,
        GAME_WORD_BLOB, "pclubor_ic36",	0x1000000, 0x0200000, 0x9a4109e5,
        EEPROM_BLOB,	"pclubor.nv",	0x0000, 0x0080, 0x3ad918c0,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pckobe99",
    NULL,
    "Print Club 2 Kobe Luminaire '99 (Print Club Custom 3) (J 991203 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22.bin",	0x0200000, 0x0200000, 0x670296eb,
        GAME_WORD_BLOB, "ic24.bin",	0x0400000, 0x0200000, 0xc2139f62,
        GAME_WORD_BLOB, "ic26.bin",	0x0600000, 0x0200000, 0x17e3efd0,
        GAME_WORD_BLOB, "ic28.bin",	0x0800000, 0x0200000, 0xa52f99f6,
        GAME_WORD_BLOB, "ic30.bin",	0x0a00000, 0x0200000, 0xf1b7e3d5,
        GAME_WORD_BLOB, "ic32.bin",	0x0c00000, 0x0200000, 0x76f6efaa,
        GAME_WORD_BLOB, "ic34.bin",	0x0e00000, 0x0200000, 0x894c63f9,
        GAME_WORD_BLOB, "ic36.bin",	0x1000000, 0x0200000, 0x078694c3,
        EEPROM_BLOB,	"eeprom",	0x0000, 0x0080, 0xdbe305a9,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclub2kc",
    NULL,
    "Print Club Kome Kome Club (J 970203 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclub2kc_ic22",	0x0200000, 0x0200000, 0x9eb4cfd7,
        GAME_WORD_BLOB, "pclub2kc_ic24",	0x0400000, 0x0200000, 0xcf3b4080,
        GAME_WORD_BLOB, "pclub2kc_ic26",	0x0600000, 0x0200000, 0xdbdcb1d7,
        GAME_WORD_BLOB, "pclub2kc_ic28",	0x0800000, 0x0200000, 0x3c330c9b,
        GAME_WORD_BLOB, "pclub2kc_ic30",	0x0a00000, 0x0200000, 0x00a0c702,
		EEPROM_BLOB,	"pclub2kc.nv",		0x0000, 0x0080, 0x064366fe,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclove",
    NULL,
    "Print Club LoveLove (J 970421 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclblove.ic22",	0x0200000, 0x0200000, 0x8cd25a0f,
        GAME_WORD_BLOB, "pclblove.ic24",	0x0400000, 0x0200000, 0x85583e2c,
        GAME_WORD_BLOB, "pclblove.ic26",	0x0600000, 0x0200000, 0x7efcabcc,
        GAME_WORD_BLOB, "pclblove.ic28",	0x0800000, 0x0200000, 0xa1336da7,
        GAME_WORD_BLOB, "pclblove.ic30",	0x0a00000, 0x0200000, 0xec5b5e28,
        GAME_WORD_BLOB, "pclblove.ic32",	0x0c00000, 0x0200000, 0x9a4109e5,
        EEPROM_BLOB,	"pclove.nv",		0x0000, 0x0080, 0x3c78e3bd,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclove2",
    NULL,
    "Print Club LoveLove Ver 2 (J 970825 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22",			0x0200000, 0x0200000, 0xd7d968d6,
        GAME_WORD_BLOB, "ic24",			0x0400000, 0x0200000, 0x9c9b7e57,
        GAME_WORD_BLOB, "ic26",			0x0600000, 0x0200000, 0x55eb859f,
        GAME_WORD_BLOB, "ic28",			0x0800000, 0x0200000, 0x463604a6,
        GAME_WORD_BLOB, "ic30",			0x0a00000, 0x0200000, 0xec5b5e28,
        GAME_WORD_BLOB, "ic32",			0x0c00000, 0x0200000, 0x9a4109e5,
        EEPROM_BLOB,	"pclove2.nv",	0x0000, 0x0080, 0x93b30600,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclubol",
    NULL,
    "Print Club Olive (J 980717 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclubol_ic22",	0x0200000, 0x0200000, 0x668b1049,
        GAME_WORD_BLOB, "pclubol_ic24",	0x0400000, 0x0200000, 0x35721f04,
        GAME_WORD_BLOB, "pclubol_ic26",	0x0600000, 0x0200000, 0x17b3cc4b,
        GAME_WORD_BLOB, "pclubol_ic28",	0x0800000, 0x0200000, 0x7cfaa530,
        GAME_WORD_BLOB, "pclubol_ic30",	0x0a00000, 0x0200000, 0xe1dd7854,
        GAME_WORD_BLOB, "pclubol_ic32",	0x0c00000, 0x0200000, 0xf0a3ded7,
        GAME_WORD_BLOB, "pclubol_ic34",	0x0e00000, 0x0200000, 0x53aa9821,
        GAME_WORD_BLOB, "pclubol_ic36",	0x1000000, 0x0200000, 0x9a4109e5,
        EEPROM_BLOB,	"eeprom",		0x0000, 0x0080, 0xa744ca03,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclub2pe",
    NULL,
    "Print Club 2 Pepsiman (J 970618 V1.100)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclb2psi_ic22",	0x0200000, 0x0200000, 0xcaadc660,
        GAME_WORD_BLOB, "pclb2psi_ic24",	0x0400000, 0x0200000, 0xece82698,
        GAME_WORD_BLOB, "pclb2psi_ic26",	0x0600000, 0x0200000, 0xc8a1e335,
        GAME_WORD_BLOB, "pclb2psi_ic28",	0x0800000, 0x0200000, 0x52f09627,
        GAME_WORD_BLOB, "pclb2psi_ic30",	0x0a00000, 0x0200000, 0x03b9eacf,
        EEPROM_BLOB,	"pclub2pe.nv",		0x0000, 0x0080, 0x447bb3bd,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclubpok",
    NULL,
    "Print Club Pokemon B (U 991126 V1.000)",
    STV_REGION_US,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclubpok_ic22",	0x0200000, 0x0200000, 0x48ab8371,
        GAME_WORD_BLOB, "pclubpok_ic22",	0x0400000, 0x0200000, 0x9915faea,
        GAME_WORD_BLOB, "pclubpok_ic26",	0x0600000, 0x0200000, 0x054ad120,
        GAME_WORD_BLOB, "pclubpok_ic28",	0x0800000, 0x0200000, 0x3a654b2a,
        GAME_WORD_BLOB, "pclubpok_ic30",	0x0a00000, 0x0200000, 0x98747bef,
        EEPROM_BLOB,	"pclubpok.nv",		0x0000, 0x0080, 0x4ba3f21a,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclub2pf",
    NULL,
    "Print Club 2 Puffy (J V1.100)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclb2puf.ic22",    0x0200000, 0x0200000, 0xa14282f2,
        GAME_WORD_BLOB, "pclb2puf.ic24",    0x0400000, 0x0200000, 0x4fb4dc74,
        GAME_WORD_BLOB, "pclb2puf.ic26",    0x0600000, 0x0200000, 0xd20bbfb5,
        GAME_WORD_BLOB, "pclb2puf.ic28",    0x0800000, 0x0200000, 0xda658ae9,
        GAME_WORD_BLOB, "pclb2puf.ic30",    0x0a00000, 0x0200000, 0xcafc0e6b,
        EEPROM_BLOB,    "pclub2pf.nv",      0x0000, 0x0080, 0x447bb3bd,
        GAME_END, "", 0, 0, 0
    },
  },
  {
    "pclub2v3",
    NULL,
    "Print Club 2 Vol. 3 (U 990310 V1.000)",
    STV_REGION_US,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclub2v3_ic22",    0x0200000, 0x0200000, 0xf88347aa,
        GAME_WORD_BLOB, "pclub2v3_ic24",    0x0400000, 0x0200000, 0xb5871198,
        GAME_WORD_BLOB, "pclub2v3_ic26",    0x0600000, 0x0200000, 0xd97034ed,
        GAME_WORD_BLOB, "pclub2v3_ic28",    0x0800000, 0x0200000, 0xf1421506,
        GAME_WORD_BLOB, "pclub2v3_ic30",    0x0a00000, 0x0200000, 0x8d89877e,
        EEPROM_BLOB,    "pclub2v3.nv",     0x0000, 0x0080, 0xa8a2d30c,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclub2wb",
    NULL,
    "Print Club 2 Warner Bros (J 970228 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclb2wb_ic22",    0x0200000, 0x0200000, 0x12245be7,
        GAME_WORD_BLOB, "pclb2wb_ic24",    0x0400000, 0x0200000, 0xe5d6e11e,
        GAME_WORD_BLOB, "pclb2wb_ic26",    0x0600000, 0x0200000, 0x7ee066f0,
        GAME_WORD_BLOB, "pclb2wb_ic28",    0x0800000, 0x0200000, 0x9ed59513,
        GAME_WORD_BLOB, "pclb2wb_ic30",    0x0a00000, 0x0200000, 0x00a0c702,
        EEPROM_BLOB,    "pclub2wb.nv",     0x0000, 0x0080, 0x0d442eec,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pcpooh2",
    NULL,
    "Print Club Winnie-the-Pooh Vol. 2 (J 971218 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22.bin",    0x0200000, 0x0200000, 0x2cb33332,
        GAME_WORD_BLOB, "ic24.bin",    0x0400000, 0x0200000, 0x3c6fc10f,
        GAME_WORD_BLOB, "ic26.bin",    0x0600000, 0x0200000, 0xb891c7ab,
        GAME_WORD_BLOB, "ic28.bin",    0x0800000, 0x0200000, 0x1a1c74cb,
        GAME_WORD_BLOB, "ic30.bin",    0x0a00000, 0x0200000, 0xb7b6fc61,
		EEPROM_BLOB,     "eeprom",     0x0000, 0x0080, 0x5aee29d0,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pcpooh3",
    NULL,
    "Print Club Winnie-the-Pooh Vol. 3 (J 980406 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22.bin",	0x0200000, 0x0200000, 0xefc83c4f,
        GAME_WORD_BLOB, "ic24.bin",	0x0400000, 0x0200000, 0xe8753d0d,
        GAME_WORD_BLOB, "ic26.bin",	0x0600000, 0x0200000, 0xdaf1a0d4,
        GAME_WORD_BLOB, "ic28.bin",	0x0800000, 0x0200000, 0x3c0f040f,
        GAME_WORD_BLOB, "ic30.bin",	0x0a00000, 0x0200000, 0x6006d785,
		EEPROM_BLOB,	"eeprom",	0x0000, 0x0080, 0xe41d541b,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
    {
    "pclubyo",
    NULL,
    "Print Club Yoshimoto V1 (J 970208 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclbyov1.ic22",    0x0200000, 0x0200000, 0x769468d1,
        GAME_WORD_BLOB, "pclbyov2.ic24",    0x0400000, 0x0200000, 0xb25885bf,
        GAME_WORD_BLOB, "pclbyov2.ic26",    0x0600000, 0x0200000, 0xcefc697f,
        GAME_WORD_BLOB, "pclbyov2.ic28",    0x0800000, 0x0200000, 0x3c330c9b,
        GAME_WORD_BLOB, "pclbyov2.ic30",    0x0a00000, 0x0200000, 0x00a0c702,
        EEPROM_BLOB,	"pclubyo2.nv",		0x0000, 0x0080, 0x2021e0e5,
        // EEPROM_BLOB,	"315-6055.ic12",	0x0000, 0x0000, 0x00000000, // PALCE16V8H-10JC on the front side of the cart
        // EEPROM_BLOB,	"315-6056.ic13",	0x0000, 0x0200, 0x01170000, // PALCE16V8H-10JC on the back side of the cart
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "pclubyo2",
    NULL,
    "Print Club Yoshimoto V2 (J 970422 V1.100)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "pclbyov2.ic22",    0x0200000, 0x0200000, 0x719a4d27,
        GAME_WORD_BLOB, "pclbyov2.ic24",    0x0400000, 0x0200000, 0x790dc7b5,
        GAME_WORD_BLOB, "pclbyov2.ic26",    0x0600000, 0x0200000, 0x12ae1606,
        GAME_WORD_BLOB, "pclbyov2.ic28",    0x0800000, 0x0200000, 0xff9643ca,
        GAME_WORD_BLOB, "pclbyov2.ic30",    0x0a00000, 0x0200000, 0x03b9eacf,
        EEPROM_BLOB,	"pclubyo2.nv",		0x0000, 0x0080, 0x2b26a8f7,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "kiwames",
    NULL,
    "Pro Mahjong Kiwame S (J 951020 V1.208)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        HEADER_BLOB,    "epr18737.13",	0x0000001, 0x0080000, 0xcfad6c49,
        GAME_BYTE_BLOB, "epr18737.13",	0x0100000, 0x0080000, 0xcfad6c49,
        GAME_WORD_BLOB, "mpr18738.2",	0x0400000, 0x0400000, 0x4b3c175a,
        GAME_WORD_BLOB, "mpr18739.3",	0x0800000, 0x0400000, 0xeb41fa67,
        GAME_WORD_BLOB, "mpr18740.4",	0x0c00000, 0x0200000, 0x9ca7962f,
        EEPROM_BLOB,	"kiwames.nv",	0x0000, 0x0080, 0xc7002732,
        GAME_END, "", 0, 0, 0
    },
    STVMP,
  },
  {
    "puyosun",
    NULL,
    "Puyo Puyo Sun (J 961115 V0.001)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"epr19531.13",	0x0000001, 0x0080000, 0xac81024f,
        HEADER_BLOB,	"epr19531.13",	0x0100001, 0x0080000, 0xac81024f,
        GAME_BYTE_BLOB, "epr19531.13",	0x0200000, 0x0080000, 0xac81024f,
        GAME_BYTE_BLOB, "epr19531.13",	0x0300000, 0x0080000, 0xac81024f,
        GAME_WORD_BLOB, "mpr19533.2",	0x0400000, 0x0400000, 0x17ec54ba,
        GAME_WORD_BLOB, "mpr19534.3",	0x0800000, 0x0400000, 0x820e4781,
        GAME_WORD_BLOB, "mpr19535.4",	0x0c00000, 0x0400000, 0x94fadfa4,
        GAME_WORD_BLOB, "mpr19536.5",	0x1000000, 0x0400000, 0x5765bc9c,
        GAME_WORD_BLOB, "mpr19537.6",	0x1400000, 0x0400000, 0x8b736686,
        GAME_WORD_BLOB, "mpr19532.1",	0x1800000, 0x0400000, 0x985f0c9d,
        GAME_WORD_BLOB, "mpr19538.8",	0x1c00000, 0x0400000, 0x915a723e,
        GAME_WORD_BLOB, "mpr19539.9",	0x2000000, 0x0400000, 0x72a297e5,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "thuntk",
    "sandor",
    "Puzzle & Action: BoMulEul Chajara (JUET 970125 V2.00K)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "2.ic13_2",      0x0200000, 0x0080000, 0x6cae2926,
        HEADER_BLOB,    "1.ic13_1",      0x0200001, 0x0080000, 0x460727c8,
        GAME_BYTE_BLOB, "bom210-10.ic2", 0x1c00000, 0x0400000, 0xf59d0827,
        GAME_BYTE_BLOB, "bom210-10.ic2", 0x0400000, 0x0400000, 0xf59d0827,
        GAME_BYTE_BLOB, "bom210-11.ic3", 0x2000000, 0x0400000, 0x44e5a13e,
        GAME_BYTE_BLOB, "bom210-11.ic3", 0x0800000, 0x0400000, 0x44e5a13e,
        GAME_BYTE_BLOB, "bom210-12.ic4", 0x2400000, 0x0400000, 0xdeabc701,
        GAME_BYTE_BLOB, "bom210-12.ic4", 0x0c00000, 0x0400000, 0xdeabc701,
        GAME_BYTE_BLOB, "bom210-13.ic5", 0x2800000, 0x0400000, 0x5ece1d5c,
        GAME_BYTE_BLOB, "bom210-13.ic5", 0x1000000, 0x0400000, 0x5ece1d5c,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "sandor",
    NULL,
    "Puzzle & Action: Sando-R (J 951114 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "sando-r.13",  0x0000001, 0x0100000, 0xfe63a239,
        GAME_BYTE_BLOB, "sando-r.13",  0x0200000, 0x0100000, 0xfe63a239,
        GAME_BYTE_BLOB, "sando-r.13",  0x0300000, 0x0100000, 0xfe63a239,
        GAME_WORD_BLOB, "mpr18635.8",  0x1c00000, 0x0400000, 0x441e1368,
        GAME_WORD_BLOB, "mpr18635.8",  0x0400000, 0x0400000, 0x441e1368,
        GAME_WORD_BLOB, "mpr18636.9",  0x2000000, 0x0400000, 0xfff1dd80,
        GAME_WORD_BLOB, "mpr18636.9",  0x0800000, 0x0400000, 0xfff1dd80,
        GAME_WORD_BLOB, "mpr18637.10", 0x2400000, 0x0400000, 0x83aced0f,
        GAME_WORD_BLOB, "mpr18637.10", 0x0c00000, 0x0400000, 0x83aced0f,
        GAME_WORD_BLOB, "mpr18638.11", 0x2800000, 0x0400000, 0xcaab531b,
        GAME_WORD_BLOB, "mpr18638.11", 0x1000000, 0x0400000, 0xcaab531b,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "thunt",
    "sandor",
    "Puzzle & Action: Treasure Hunt (JUET 970901 V2.00E)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "th-ic7_2.stv", 0x0200000, 0x0080000, 0xc4e993de,
        HEADER_BLOB,    "th-ic7_1.stv", 0x0200001, 0x0080000, 0x1355cc18,
        GAME_WORD_BLOB, "th-e-2.ic2",   0x0400000, 0x0400000, 0x47315694,
        GAME_WORD_BLOB, "th-e-3.ic3",   0x0800000, 0x0400000, 0xc9290b44,
        GAME_WORD_BLOB, "th-e-4.ic4",   0x0c00000, 0x0400000, 0xc672e40b,
        GAME_WORD_BLOB, "th-e-5.ic5",   0x1000000, 0x0400000, 0x3914b805,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "rsgun",
    NULL,
    "Radiant Silvergun (JUET 980523 V1.000)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP | STV_REGION_TW,
    0x05272d01,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr20958.7",   0x0200000, 0x0200000, 0xcbe5a449,
        GAME_WORD_BLOB, "mpr20959.2",   0x0400000, 0x0400000, 0xa953330b,
        GAME_WORD_BLOB, "mpr20960.3",   0x0800000, 0x0400000, 0xb5ab9053,
        GAME_WORD_BLOB, "mpr20961.4",   0x0c00000, 0x0400000, 0x0e06295c,
        GAME_WORD_BLOB, "mpr20962.5",   0x1000000, 0x0400000, 0xf1e6c7fc,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "hanagumi",
    NULL,
    "Sakura Taisen - Hanagumi Taisen Columns (J 971007 V1.010)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr20143.7",    0x0200000, 0x0100000, 0x7bfc38d0,
        GAME_WORD_BLOB, "mpr20138.2",    0x0400000, 0x0400000, 0xfdcf1046,
        GAME_WORD_BLOB, "mpr20139.3",    0x0800000, 0x0400000, 0x7f0140e5,
        GAME_WORD_BLOB, "mpr20140.4",    0x0c00000, 0x0400000, 0x2fa03852,
        GAME_WORD_BLOB, "mpr20141.5",    0x1000000, 0x0400000, 0x45d6d21b,
        GAME_WORD_BLOB, "mpr20142.6",    0x1400000, 0x0400000, 0xe38561ec,
        GAME_WORD_BLOB, "mpr20137.1",    0x1800000, 0x0400000, 0x181d2688,
        GAME_WORD_BLOB, "mpr20144.8",    0x1c00000, 0x0400000, 0x235b43f6,
        GAME_WORD_BLOB, "mpr20145.9",    0x2000000, 0x0400000, 0xaeaac7a1,
        GAME_WORD_BLOB, "mpr20146.10",   0x2400000, 0x0400000, 0x39bab9a2,
        GAME_WORD_BLOB, "mpr20147.11",   0x2800000, 0x0400000, 0x294ab997,
        GAME_WORD_BLOB, "mpr20148.12",   0x2c00000, 0x0400000, 0x5337ccb0,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "seabass",
    NULL,
    "Sea Bass Fishing (JUET 971110 V0.001)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"seabassf.13",	0x0000001, 0x0100000, 0x6d7c39cc,
        GAME_BYTE_BLOB, "seabassf.13",	0x0200000, 0x0100000, 0x6d7c39cc,
        GAME_BYTE_BLOB, "seabassf.13",	0x0300000, 0x0100000, 0x6d7c39cc,
        GAME_WORD_BLOB, "mpr20551.2",	0x0400000, 0x0400000, 0x9a0c6dd8,
        GAME_WORD_BLOB, "mpr20552.3",	0x0800000, 0x0400000, 0x5f46b0aa,
        GAME_WORD_BLOB, "mpr20553.4",	0x0c00000, 0x0400000, 0xc0f8a6b6,
        GAME_WORD_BLOB, "mpr20554.5",	0x1000000, 0x0400000, 0x215fc1f9,
        GAME_WORD_BLOB, "mpr20555.6",	0x1400000, 0x0400000, 0x3f5186a9,
        GAME_WORD_BLOB, "mpr20550.1",	0x1800000, 0x0400000, 0x083e1ca8,
        GAME_WORD_BLOB, "mpr20556.8",	0x1c00000, 0x0400000, 0x1fd70c6c,
        GAME_WORD_BLOB, "mpr20557.9",	0x2000000, 0x0400000, 0x3c9ba442,
        EEPROM_BLOB,    "seabass.nv",	0x0000, 0x0080, 0x4e7c0944,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "shanhigw",
    NULL,
    "Shanghai - The Great Wall / Shanghai Triple Threat (JUE 950623 V1.005)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr18341.7",    0x0200000, 0x0200000, 0xcc5e8646,
        GAME_WORD_BLOB, "mpr18340.2",    0x0400000, 0x0200000, 0x8db23212,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "shienryu",
    NULL,
    "Shienryu (JUET 961226 V1.000)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP | STV_REGION_TW,
    0,
    ROTATED,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr19631.7",			0x0200000, 0x0200000, 0x3a4b1abc,
        GAME_WORD_BLOB, "mpr19632.2",			0x0400000, 0x0400000, 0x985fae46,
        GAME_WORD_BLOB, "mpr19633.3",			0x0800000, 0x0400000, 0xe2f0b037,
        EEPROM_BLOB,	"eeprom-shienryu.bin",	0x0000, 0x0080, 0x98db6925,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "skychal",
    NULL,
    "Sky Challenger (J 000406 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22.bin",   0x0200000, 0x200000, 0xa12ccf64,
        GAME_WORD_BLOB, "ic24.bin",   0x0400000, 0x200000, 0x9a929dcf,
        GAME_WORD_BLOB, "ic26.bin",   0x0600000, 0x200000, 0xed2183d3,
        GAME_WORD_BLOB, "ic28.bin",   0x0800000, 0x200000, 0xe7401d68,
        GAME_WORD_BLOB, "ic30.bin",   0x0a00000, 0x200000, 0x950f7a2f,
        GAME_WORD_BLOB, "ic32.bin",   0x0c00000, 0x200000, 0xa656212b,
        EEPROM_BLOB,    "skychal.nv", 0x0000, 0x0080, 0xa6515237,
        GAME_END, "", 0, 0, 0
    },
    PATOCAR,
  },
  {
    "sackids",
    NULL,
    "Soreyuke Anpanman Crayon Kids (J 001026 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB, "epr-20091.ic8", 0x0000001, 0x008000, 0x59ed40f4,
        GAME_WORD_BLOB, "ic22.bin",   0x0200000, 0x200000, 0x4d9d1870,
        GAME_WORD_BLOB, "ic24.bin",   0x0400000, 0x200000, 0x39fca3e5,
        GAME_WORD_BLOB, "ic26.bin",   0x0600000, 0x200000, 0xf38c79b6,
        GAME_WORD_BLOB, "ic28.bin",   0x0800000, 0x200000, 0x63d09f3c,
        GAME_WORD_BLOB, "ic30.bin",   0x0a00000, 0x200000, 0xf89811ba,
        GAME_WORD_BLOB, "ic32.bin",   0x0c00000, 0x200000, 0x1db6c26b,
        GAME_WORD_BLOB, "ic34.bin",   0x0e00000, 0x200000, 0x0f3622c8,
        GAME_WORD_BLOB, "ic36.bin",   0x1000000, 0x200000, 0x9a4109e5,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "sokyugrt",
    NULL,
    "Soukyugurentai / Terra Diver (JUET 960821 V1.000)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "fpr19188.13", 0x0000001, 0x0100000, 0x45a27e32,
        GAME_BYTE_BLOB, "fpr19188.13", 0x0200000, 0x0100000, 0x45a27e32,
        GAME_BYTE_BLOB, "fpr19188.13", 0x0300000, 0x0100000, 0x45a27e32,
        GAME_WORD_BLOB, "mpr19189.2",  0x0400000, 0x0400000, 0x0b202a3e,
        GAME_WORD_BLOB, "mpr19190.3",  0x0800000, 0x0400000, 0x1777ded8,
        GAME_WORD_BLOB, "mpr19191.4",  0x0c00000, 0x0400000, 0xec6eb07b,
        GAME_WORD_BLOB, "mpr19192.5",  0x1000000, 0x0200000, 0xcb544a1e,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "sfish2",
    NULL,
    "Sport Fishing 2 (UET 951106 V1.10e)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        BIOS_BLOB, "epr18343.bin",        0x0000000, 0x0080000, 0x48e2eecf,
        HEADER_BLOB, "epr-18427.ic13",    0x0000001, 0x0100000, 0x3f25bec8,
        GAME_BYTE_BLOB, "epr-18427.ic13", 0x0200000, 0x0100000, 0x3f25bec8,
        GAME_BYTE_BLOB, "epr-18427.ic13", 0x0300000, 0x0100000, 0x3f25bec8,
        GAME_WORD_BLOB, "mpr-18273.ic2",  0x0400000, 0x0400000, 0x6fec0193,
        GAME_WORD_BLOB, "mpr-18274.ic3",  0x0800000, 0x0400000, 0xa6d76d23,
        GAME_WORD_BLOB, "mpr-18275.ic4",  0x0c00000, 0x0200000, 0x7691deca,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "sfish2j",
    "sfish2",
    "Sport Fishing 2 (J 951201 V1.100)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        BIOS_BLOB, "epr18343.bin",       0x0000000, 0x0080000, 0x48e2eecf,
        HEADER_BLOB, "epr18344.a",       0x0000001, 0x0100000, 0x5a7de018,
        GAME_BYTE_BLOB, "epr18344.a",    0x0200000, 0x0100000, 0x5a7de018,
        GAME_BYTE_BLOB, "epr18344.a",    0x0300000, 0x0100000, 0x5a7de018,
        GAME_WORD_BLOB, "mpr-18273.ic2", 0x0400000, 0x0400000, 0x6fec0193,
        GAME_WORD_BLOB, "mpr-18274.ic3", 0x0800000, 0x0400000, 0xa6d76d23,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "sss",
    NULL,
    "Steep Slope Sliders (JUET 981110 V1.000)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0x052b6901,
    0,
    NULL,
    NV_1P,
    {
        HEADER_BLOB,	"epr21488.13",	0x0000001, 0x0080000, 0x71c9def1,
        HEADER_BLOB,	"epr21488.13",	0x0100001, 0x0080000, 0x71c9def1,
        GAME_BYTE_BLOB,	"epr21488.13",	0x0200000, 0x0080000, 0x71c9def1,
        GAME_BYTE_BLOB,	"epr21488.13",	0x0300000, 0x0080000, 0x71c9def1,
        GAME_WORD_BLOB,	"mpr21489.2",	0x0400000, 0x0400000, 0x4c85152b,
        GAME_WORD_BLOB,	"mpr21490.3",	0x0800000, 0x0400000, 0x03da67f8,
        GAME_WORD_BLOB,	"mpr21491.4",	0x0c00000, 0x0400000, 0xcf7ee784,
        GAME_WORD_BLOB,	"mpr21492.5",	0x1000000, 0x0400000, 0x57753894,
        GAME_WORD_BLOB,	"mpr21493.6",	0x1400000, 0x0400000, 0xefb2d271,
        EEPROM_BLOB,	"sss.nv",		0x0000, 0x0080, 0x052b6901,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "stress",
    NULL,
    "Stress Busters (J 981020 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"epr-21300a.ic13",	0x0000001, 0x0100000, 0x899d829e,
        GAME_BYTE_BLOB,	"epr-21300a.ic13",	0x0200000, 0x0100000, 0x899d829e,
        GAME_BYTE_BLOB,	"epr-21300a.ic13",	0x0300000, 0x0100000, 0x899d829e,
        GAME_WORD_BLOB,	"mpr-21290.ic2",	0x0400000, 0x0400000, 0xa49d29f3,
        GAME_WORD_BLOB,	"mpr-21291.ic3",	0x0800000, 0x0400000, 0x9452ba20,
        GAME_WORD_BLOB,	"mpr-21292.ic4",	0x0c00000, 0x0400000, 0xf60268e2,
        GAME_WORD_BLOB,	"mpr-21293.ic5",	0x1000000, 0x0400000, 0x794946e0,
        GAME_WORD_BLOB,	"mpr-21294.ic6",	0x1400000, 0x0400000, 0x550843bb,
        GAME_WORD_BLOB,	"mpr-21289.ic1",	0x1800000, 0x0400000, 0xc2ee8bea,
        GAME_WORD_BLOB,	"mpr-21296.ic8",	0x1c00000, 0x0400000, 0xb825c42a,
        GAME_WORD_BLOB,	"mpr-21297.ic9",	0x2000000, 0x0400000, 0x4bff7469,
        GAME_WORD_BLOB,	"mpr-21298.ic10",	0x2400000, 0x0400000, 0x68d07144,
        GAME_WORD_BLOB,	"mpr-21299.ic11",	0x2800000, 0x0400000, 0xecc521c6,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "suikoenb",
    NULL,
    "Suiko Enbu / Outlaws of the Lost Dynasty (JUETL 950314 V2.001)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"fpr17834.13",	0x0000001, 0x0100000, 0x746ef686,
        GAME_BYTE_BLOB,	"fpr17834.13",	0x0200000, 0x0100000, 0x746ef686,
        GAME_BYTE_BLOB,	"fpr17834.13",	0x0300000, 0x0100000, 0x746ef686,
        GAME_WORD_BLOB,	"mpr17836.2",	0x0400000, 0x0400000, 0x55e9642d,
        GAME_WORD_BLOB,	"mpr17837.3",	0x0800000, 0x0400000, 0x13d1e667,
        GAME_WORD_BLOB,	"mpr17838.4",	0x0c00000, 0x0400000, 0xf9e70032,
        GAME_WORD_BLOB,	"mpr17839.5",	0x1000000, 0x0400000, 0x1b2762c5,
        GAME_WORD_BLOB,	"mpr17840.6",	0x1400000, 0x0400000, 0x0fd4c857,
        GAME_WORD_BLOB,	"mpr17835.1",	0x1800000, 0x0400000, 0x77f5cb43,
        GAME_WORD_BLOB,	"mpr17841.8",	0x1c00000, 0x0400000, 0xf48beffc,
        GAME_WORD_BLOB,	"mpr17842.9",	0x2000000, 0x0400000, 0x0ac8deed7,
        GAME_END, "", 0, 0, 0
    },
    STV6B,
  },
  {
    "smleague",
    NULL,
    "Super Major League (U 960108 V1.000)",
    STV_REGION_US,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"epr18777.13",	0x0000001, 0x0080000, 0x8d180866,
        HEADER_BLOB,	"epr18777.13",	0x0100001, 0x0080000, 0x8d180866,
        GAME_BYTE_BLOB,	"epr18777.13",	0x0200000, 0x0080000, 0x8d180866,
        GAME_BYTE_BLOB,	"epr18777.13",	0x0300000, 0x0080000, 0x8d180866,
        GAME_WORD_BLOB,	"mpr18778.8",	0x1c00000, 0x0400000, 0x25e1300e,
        GAME_WORD_BLOB,	"mpr18779.9",	0x2000000, 0x0400000, 0x51e2fabd,
        GAME_WORD_BLOB,	"mpr18780.10",	0x2400000, 0x0400000, 0x8cd4dd74,
        GAME_WORD_BLOB,	"mpr18781.11",	0x2800000, 0x0400000, 0x13ee41ae,
        GAME_WORD_BLOB,	"mpr18782.12",	0x2c00000, 0x0200000, 0x9be2270a,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "sasissu",
    NULL,
    "Taisen Tanto-R Sashissu!! (J 980216 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"epr20542.13",	0x0000001, 0x0100000, 0x0e632db5,
        GAME_BYTE_BLOB,	"epr20542.13",	0x0200000, 0x0100000, 0x0e632db5,
        GAME_BYTE_BLOB,	"epr20542.13",	0x0300000, 0x0100000, 0x0e632db5,
        GAME_WORD_BLOB,	"mpr20544.2",	0x0400000, 0x0400000, 0x661fff5e,
        GAME_WORD_BLOB,	"mpr20545.3",	0x0800000, 0x0400000, 0x8e3a37be,
        GAME_WORD_BLOB,	"mpr20546.4",	0x0c00000, 0x0400000, 0x72020886,
        GAME_WORD_BLOB,	"mpr20547.5",	0x1000000, 0x0400000, 0x8362e397,
        GAME_WORD_BLOB,	"mpr20548.6",	0x1400000, 0x0400000, 0xe37534d9,
        GAME_WORD_BLOB,	"mpr20543.1",	0x1800000, 0x0400000, 0x1f688cdf,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "tatacot",
    "critcrsh",
    "Tatacot (JA 951128 V1.000)",
    STV_REGION_JP,
    0x0,
    0,
    sanjeon_init,
    NV_1P,
    {
        HEADER_BLOB,	"epr-18790.ic13",	0x0000001, 0x0080000, 0xd95155dc,
        HEADER_BLOB,	"epr-18790.ic13",	0x0100001, 0x0080000, 0xd95155dc,
        GAME_BYTE_BLOB,	"epr-18790.ic13",	0x0200000, 0x0080000, 0xd95155dc,
        GAME_BYTE_BLOB,	"epr-18790.ic13",	0x0300000, 0x0080000, 0xd95155dc,
        GAME_WORD_BLOB,	"mpr-18789.ic8",	0x1c00000, 0x0400000, 0xb388616f,
        GAME_WORD_BLOB,	"mpr-18788.ic9",	0x2000000, 0x0400000, 0xfeae5867,
        EEPROM_BLOB,    "critcrsh.nv",      0x0000, 0x0080, 0x3da9860e,
        GAME_END, "", 0, 0, 0
    },
    CRITCRSH,
  },
  {
    "techbowl",
    NULL,
    "Technical Bowling (J 971212 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22",   0x0200000, 0x0200000, 0x5058db21,
        GAME_WORD_BLOB, "ic24",   0x0400000, 0x0200000, 0x34090f6d,
        GAME_WORD_BLOB, "ic26",   0x0600000, 0x0200000, 0xfb073352,
        GAME_WORD_BLOB, "ic28",   0x0800000, 0x0200000, 0x530e0ceb,
        GAME_WORD_BLOB, "ic30",   0x0a00000, 0x0200000, 0x8d89877e,
        EEPROM_BLOB,    "techbowl.nv",        0x0000, 0x0080, 0x5bebc2b7,
        GAME_END, "", 0, 0, 0
    },
    PATOCAR,
  },
  {
    "twcup98",
    NULL,
    "Tecmo World Cup '98 (JUET 980410 V1.000)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP | STV_REGION_TW,
    0x05200913,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"epr20819.24",	0x0000001, 0x0100000, 0xd930dfc8,
        GAME_BYTE_BLOB, "epr20819.24",	0x0200000, 0x0100000, 0xd930dfc8,
        GAME_BYTE_BLOB, "epr20819.24",	0x0300000, 0x0100000, 0xd930dfc8,
        GAME_WORD_BLOB, "mpr20821.12",	0x0400000, 0x0400000, 0x2d930d23,
        GAME_WORD_BLOB, "mpr20822.13",	0x0800000, 0x0400000, 0x8b33a5e2,
        GAME_WORD_BLOB, "mpr20823.14",	0x0c00000, 0x0400000, 0x6e6d4e95,
        GAME_WORD_BLOB, "mpr20824.15",	0x1000000, 0x0400000, 0x4cf18a25,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "twsoc98",
    "twcup98",
    "Tecmo World Soccer '98 (JUET 980410 V1.000)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0x05200913,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "epr-20820.ic24",	0x0000001, 0x0100000, 0xb10451f9,
        GAME_BYTE_BLOB, "epr-20820.ic24",	0x0200000, 0x0100000, 0xb10451f9,
        GAME_BYTE_BLOB, "epr-20820.ic24",	0x0300000, 0x0100000, 0xb10451f9,
        GAME_WORD_BLOB, "mpr20821.12",		0x0400000, 0x0400000, 0x2d930d23,
        GAME_WORD_BLOB, "mpr20822.13",		0x0800000, 0x0400000, 0x8b33a5e2,
        GAME_WORD_BLOB, "mpr20823.14",		0x0c00000, 0x0400000, 0x6e6d4e95,
        GAME_WORD_BLOB, "mpr20824.15",		0x1000000, 0x0400000, 0x4cf18a25,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "elandore",
    NULL,
    "Touryuu Densetsu Elan-Doree / Elan Doree - Legend of Dragoon (JUET 980922 V1.006)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0x05226d41,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "mpr21307.7", 0x0200000, 0x0200000, 0x966ad472,
        GAME_WORD_BLOB, "mpr21301.2", 0x0400000, 0x0400000, 0x1a23b0a0,
        GAME_WORD_BLOB, "mpr21302.3", 0x0800000, 0x0400000, 0x1c91ca33,
        GAME_WORD_BLOB, "mpr21303.4", 0x0c00000, 0x0400000, 0x07b2350e,
        GAME_WORD_BLOB, "mpr21304.5", 0x1000000, 0x0400000, 0xcfea52ae,
        GAME_WORD_BLOB, "mpr21305.6", 0x1400000, 0x0400000, 0x46cfc2a2,
        GAME_WORD_BLOB, "mpr21306.1", 0x1800000, 0x0400000, 0x87a5929c,
        GAME_WORD_BLOB, "mpr21308.8", 0x1c00000, 0x0400000, 0x336ec1a4,
        GAME_END, "", 0, 0, 0
    },
    STV6B,
  },
  {
    "vfkids",
    NULL,
    "Virtua Fighter Kids (JUET 960319 V0.000)",
    STV_REGION_EU | STV_REGION_US | STV_REGION_JP | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "fpr18914.13",	0x0000001, 0x0100000, 0xcd35730a,
        GAME_BYTE_BLOB, "fpr18914.13",	0x0200000, 0x0100000, 0xcd35730a,
        GAME_BYTE_BLOB, "fpr18914.13",	0x0300000, 0x0100000, 0xcd35730a,
        GAME_WORD_BLOB, "mpr18916.4",	0x0c00000, 0x0400000, 0x4aae3ddb,
        GAME_WORD_BLOB, "mpr18917.5",	0x1000000, 0x0400000, 0xedf6edc3,
        GAME_WORD_BLOB, "mpr18918.6",	0x1400000, 0x0400000, 0xd3a95036,
        GAME_WORD_BLOB, "mpr18915.1",	0x1800000, 0x0400000, 0x09cc38e5,
        GAME_WORD_BLOB, "mpr18919.8",	0x1c00000, 0x0400000, 0x4ac700de,
        GAME_WORD_BLOB, "mpr18920.9",	0x2000000, 0x0400000, 0x0106e36c,
        GAME_WORD_BLOB, "mpr18921.10",	0x2400000, 0x0400000, 0xc23d51ad,
        GAME_WORD_BLOB, "mpr18922.11",	0x2800000, 0x0400000, 0x99d0ab90,
        GAME_WORD_BLOB, "mpr18923.12",	0x2c00000, 0x0400000, 0x30a41ae9,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "vfremix",
    NULL,
    "Virtua Fighter Remix (JUETBKAL 950428 V1.000)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,	"epr17944.13",	0x0000001, 0x0080000, 0x3304c175,
        HEADER_BLOB,	"epr17944.13",	0x0100001, 0x0080000, 0x3304c175,
        GAME_BYTE_BLOB, "epr17944.13",	0x0200000, 0x0080000, 0x3304c175,
        GAME_BYTE_BLOB, "epr17944.13",	0x0300000, 0x0080000, 0x3304c175,
        GAME_WORD_BLOB, "mpr17946.2",	0x0400000, 0x0400000, 0x4cb245f7,
        GAME_WORD_BLOB, "mpr17947.3",	0x0800000, 0x0400000, 0xfef4a9fb,
        GAME_WORD_BLOB, "mpr17948.4",	0x0c00000, 0x0400000, 0x3e2b251a,
        GAME_WORD_BLOB, "mpr17949.5",	0x1000000, 0x0400000, 0xb2ecea25,
        GAME_WORD_BLOB, "mpr17950.6",	0x1400000, 0x0400000, 0x5b1f981d,
        GAME_WORD_BLOB, "mpr17945.1",	0x1800000, 0x0200000, 0x03ede188,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "vmahjong",
    NULL,
    "Virtual Mahjong (J 961214 V1.000)",
    STV_REGION_JP,
    0x0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "mpr19620.7",	0x0200000, 0x0200000, 0xc98de7e5,
        GAME_WORD_BLOB, "mpr19615.2",	0x0400000, 0x0400000, 0xc62896da,
        GAME_WORD_BLOB, "mpr19616.3",	0x0800000, 0x0400000, 0xf62207c7,
        GAME_WORD_BLOB, "mpr19617.4",	0x0c00000, 0x0400000, 0xab667e19,
        GAME_WORD_BLOB, "mpr19618.5",	0x1000000, 0x0400000, 0x9782ceee,
        GAME_WORD_BLOB, "mpr19619.6",	0x1400000, 0x0400000, 0x0b76866c,
        GAME_WORD_BLOB, "mpr19614.1",	0x1800000, 0x0400000, 0xb83b3f03,
        GAME_WORD_BLOB, "mpr19621.8",	0x1c00000, 0x0400000, 0xf92616b3,
        EEPROM_BLOB,	"vmahjong.nv",	0x0000, 0x0080, 0x4e6487f4,
        GAME_END, "", 0, 0, 0
    },
    VMAHJONG,
  },
  {
    "myfairld",
    NULL,
    "Virtual Mahjong 2 - My Fair Lady (J 980608 V1.000)",
    STV_REGION_JP,
    0x0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "mpr21000.7",	0x0200000, 0x0200000, 0x2581c560,
        GAME_WORD_BLOB, "mpr20995.2",	0x0400000, 0x0400000, 0x1bb73f24,
        GAME_WORD_BLOB, "mpr20996.3",	0x0800000, 0x0400000, 0x993c3859,
        GAME_WORD_BLOB, "mpr20997.4",	0x0c00000, 0x0400000, 0xf0bf64a4,
        GAME_WORD_BLOB, "mpr20998.5",	0x1000000, 0x0400000, 0xd3b19786,
        GAME_WORD_BLOB, "mpr20999.6",	0x1400000, 0x0400000, 0x82e31f25,
        GAME_WORD_BLOB, "mpr20994.1",	0x1800000, 0x0400000, 0xa69243a0,
        GAME_WORD_BLOB, "mpr21001.8",	0x1c00000, 0x0400000, 0x95fbe549,
        EEPROM_BLOB,	"myfairld.nv",	0x0000, 0x0080, 0xc7cf3a5a,
        GAME_END, "", 0, 0, 0
    },
    MYFAIRLD,
  },
  {
    "wasafari",
    NULL,
    "Wanpaku Safari (J 981109 V1.000)",
    STV_REGION_JP,
    0x0,
    0,
    NULL,
    NV_1P,
    {
        GAME_WORD_BLOB, "ic22.bin",	0x0200000, 0x0200000, 0xd8bb2e2c,
        GAME_WORD_BLOB, "ic24.bin",	0x0400000, 0x0200000, 0xc1b0e173,
        GAME_WORD_BLOB, "ic26.bin",	0x0600000, 0x0200000, 0xa5c0577c,
        GAME_WORD_BLOB, "ic28.bin",	0x0800000, 0x0200000, 0x2a8cfa97,
        GAME_WORD_BLOB, "ic30.bin",	0x0a00000, 0x0200000, 0x9a4109e5,
        EEPROM_BLOB,	"wasafari.nv",	0x0000, 0x0080, 0x50861c5a,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "winterht",
    NULL,
    "Winter Heat (JUET 971012 V1.000)",
    STV_REGION_JP | STV_REGION_US | STV_REGION_EU | STV_REGION_TW,
    0x0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "fpr20108.13",	0x0000001, 0x0100000, 0x1ef9ced0,
        GAME_BYTE_BLOB, "fpr20108.13",	0x0200000, 0x0100000, 0x1ef9ced0,
        GAME_BYTE_BLOB, "fpr20108.13",	0x0300000, 0x0100000, 0x1ef9ced0,
        GAME_WORD_BLOB, "mpr20110.2",	0x0400000, 0x0400000, 0x238ef832,
        GAME_WORD_BLOB, "mpr20111.3",	0x0800000, 0x0400000, 0xb0a86f69,
        GAME_WORD_BLOB, "mpr20112.4",	0x0c00000, 0x0400000, 0x3ba2b49b,
        GAME_WORD_BLOB, "mpr20113.5",	0x1000000, 0x0400000, 0x8c858b41,
        GAME_WORD_BLOB, "mpr20114.6",	0x1400000, 0x0400000, 0xb723862c,
        GAME_WORD_BLOB, "mpr20109.1",	0x1800000, 0x0400000, 0xc1a713b8,
        GAME_WORD_BLOB, "mpr20115.8",	0x1c00000, 0x0400000, 0xdd01f2ad,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "yattrmnp",
    NULL,
    "Yatterman Plus (J 981006 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        GAME_WORD_BLOB, "epr-21122.ic13",	0x0000000, 0x0080000, 0x49f56e32,
        GAME_WORD_BLOB, "mpr-21125.ic02",	0x0400000, 0x0400000, 0x40f5f119,
        GAME_WORD_BLOB, "mpr-21130.ic03",	0x0800000, 0x0400000, 0x84cb4e9c,
        GAME_WORD_BLOB, "mpr-21126.ic04",	0x0c00000, 0x0400000, 0x36095649,
        GAME_WORD_BLOB, "mpr-21131.ic05",	0x1000000, 0x0400000, 0xb1c860d8,
        GAME_WORD_BLOB, "mpr-21127.ic06",	0x1400000, 0x0400000, 0xf55901d7,
        GAME_WORD_BLOB, "mpr-21132.ic07",	0x1800000, 0x0400000, 0x809b1b0e,
        GAME_WORD_BLOB, "mpr-21128.ic08",	0x1c00000, 0x0400000, 0xc2e2f72e,
        GAME_WORD_BLOB, "mpr-21133.ic09",	0x2000000, 0x0400000, 0x2dc21956,
        GAME_WORD_BLOB, "mpr-21129.ic10",	0x2400000, 0x0400000, 0x997d67bd,
        GAME_WORD_BLOB, "mpr-21124.ic11",	0x2800000, 0x0400000, 0x10f073ac,
        GAME_WORD_BLOB, "mpr-21123.ic12",	0x2c00000, 0x0400000, 0x491b9166,
        EEPROM_BLOB,    "epr-21121.bin", 	0x0000, 0x2000, 0xd615bce0,
        // EEPROM_BLOB,    "315-5930.ic19", 	0x0000, 0x0117, 0xd1201563,		
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "znpwfv",
    NULL,
    "Zen Nippon Pro-Wrestling Featuring Virtua (J 971123 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "epr20398.13", 0x0000001, 0x0100000, 0x3fb56a0b,
        GAME_BYTE_BLOB, "epr20398.13", 0x0200000, 0x0100000, 0x3fb56a0b,
        GAME_BYTE_BLOB, "epr20398.13", 0x0300000, 0x0100000, 0x3fb56a0b,
        GAME_WORD_BLOB, "mpr20400.2",  0x0400000, 0x0400000, 0x1edfbe05,
        GAME_WORD_BLOB, "mpr20401.3",  0x0800000, 0x0400000, 0x99e98937,
        GAME_WORD_BLOB, "mpr20402.4",  0x0c00000, 0x0400000, 0x4572aa60,
        GAME_WORD_BLOB, "mpr20403.5",  0x1000000, 0x0400000, 0x26a8e13e,
        GAME_WORD_BLOB, "mpr20404.6",  0x1400000, 0x0400000, 0x0b70275d,
        GAME_WORD_BLOB, "mpr20399.1",  0x1800000, 0x0400000, 0xc178a96e,
        GAME_WORD_BLOB, "mpr20405.8",  0x1c00000, 0x0400000, 0xf53337b7,
        GAME_WORD_BLOB, "mpr20406.9",  0x2000000, 0x0400000, 0xb677c175,
        GAME_WORD_BLOB, "mpr20407.10", 0x2400000, 0x0400000, 0x58356050,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "znpwfvt",
    "znpwfv",
    "Zen Nippon Pro-Wrestling Featuring Virtua (T 971123 V1.000)",
    STV_REGION_TW,
    0,
    0,
    NULL,
    NULL,
    {
        HEADER_BLOB,    "epr20408.13", 0x0000001, 0x0100000, 0x1d62fcf6,
        GAME_BYTE_BLOB, "epr20408.13", 0x0200000, 0x0100000, 0x1d62fcf6,
        GAME_BYTE_BLOB, "epr20408.13", 0x0300000, 0x0100000, 0x1d62fcf6,
        GAME_WORD_BLOB, "mpr20400.2",  0x0400000, 0x0400000, 0x1edfbe05,
        GAME_WORD_BLOB, "mpr20401.3",  0x0800000, 0x0400000, 0x99e98937,
        GAME_WORD_BLOB, "mpr20402.4",  0x0c00000, 0x0400000, 0x4572aa60,
        GAME_WORD_BLOB, "mpr20403.5",  0x1000000, 0x0400000, 0x26a8e13e,
        GAME_WORD_BLOB, "mpr20404.6",  0x1400000, 0x0400000, 0x0b70275d,
        GAME_WORD_BLOB, "mpr20399.1",  0x1800000, 0x0400000, 0xc178a96e,
        GAME_WORD_BLOB, "mpr20405.8",  0x1c00000, 0x0400000, 0xf53337b7,
        GAME_WORD_BLOB, "mpr20406.9",  0x2000000, 0x0400000, 0xb677c175,
        GAME_WORD_BLOB, "mpr20407.10", 0x2400000, 0x0400000, 0x58356050,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
  {
    "findlove",
    NULL,
    "Zenkoku Seifuku Bishoujo Grand Prix Find Love (J 971212 V1.000)",
    STV_REGION_JP,
    0,
    0,
    NULL,
    NV_1P,
    {
        HEADER_BLOB,   "epr20424.13",  0x0000001, 0x0100000, 0x4e61fa46,
        GAME_WORD_BLOB, "mpr20431.7",  0x0200000, 0x0200000, 0xea656ced,
        GAME_WORD_BLOB, "mpr20426.2",  0x0400000, 0x0400000, 0x897d1747,
        GAME_WORD_BLOB, "mpr20427.3",  0x0800000, 0x0400000, 0xa488a694,
        GAME_WORD_BLOB, "mpr20428.4",  0x0c00000, 0x0400000, 0x4353b3b6,
        GAME_WORD_BLOB, "mpr20429.5",  0x1000000, 0x0400000, 0x4f566486,
        GAME_WORD_BLOB, "mpr20430.6",  0x1400000, 0x0400000, 0xd1e11979,
        GAME_WORD_BLOB, "mpr20425.1",  0x1800000, 0x0400000, 0x67f104c4,
        GAME_WORD_BLOB, "mpr20432.8",  0x1c00000, 0x0400000, 0x79fcdecd,
        GAME_WORD_BLOB, "mpr20433.9",  0x2000000, 0x0400000, 0x82289f29,
        GAME_WORD_BLOB, "mpr20434.10", 0x2400000, 0x0400000, 0x85c94afc,
        GAME_WORD_BLOB, "mpr20435.11", 0x2800000, 0x0400000, 0x263a2e48,
        GAME_WORD_BLOB, "mpr20436.12", 0x2c00000, 0x0400000, 0xe3823f49,
        EEPROM_BLOB,    "findlove.nv", 0x0000, 0x0080, 0xdf2fa9f6,
        GAME_END, "", 0, 0, 0
    },
    STV,
  },
};

u8 hasBios = 0;
u8 hasEeprom = 0;
u8 fileFound[NB_STV_GAMES][MAX_GAME_FILES];
u8 biosFound[MAX_GAME_FILES];
static int stv_favorite_region = STV_REGION_EU;

typedef struct {
    const char* filename;
    int gameId;
    int bios;
} rominfo;

int processBios(JZFile *zip,void *input) {
    JZFileHeader header;
    char filename[MAX_LENGTH_FILENAME];
    int j;
    rominfo* info = (rominfo*) input;

   if ((info != NULL) && (info->gameId != -1)) return copyBios(zip, input);


    if(jzReadLocalFileHeader(zip, &header, filename, sizeof(filename))) {
        LOGSTV("Couldn't read local file header!\n");
        return -1;
    }

    //LOGSTV("%s, %d / %d bytes at offset %08X\n", filename, header.compressedSize, header.uncompressedSize, header.offset);
    j=0;
    while(BiosList.blobs[j].type != GAME_END) {
      if (header.crc32 == BiosList.blobs[j].crc32) {
        // file is the same but has a different filename, let's replace it
        strncpy(BiosList.blobs[j].filename, filename, MAX_LENGTH_FILENAME);
        //Compatible file found
        biosFound[j] = 1;
      }
      j++;
    }

    return 0;
}

int processFile(JZFile *zip,void *input) {
    JZFileHeader header;
    char *filename;
    char long_filename[MAX_LENGTH_FILENAME];
    int i,j;
    rominfo* info = (rominfo*) input;

    if (info != NULL)
        if (info->gameId != -1)  return copyFile(zip, input);

    if(jzReadLocalFileHeader(zip, &header, long_filename, sizeof(long_filename))) {
        LOGSTV("Couldn't read local file header!\n");
        return -1;
    }
    filename = get_basename(long_filename);
    LOGSTV("CRC32 0x%x %s\n", header.crc32, filename);
    //LOGSTV("%s, %d / %d bytes at offset %08X\n", filename, header.compressedSize, header.uncompressedSize, header.offset);
    for (i=0; i<NB_STV_GAMES; i++) {
      j=0;
      while(GameList[i].blobs[j].type != GAME_END) {
        if (header.crc32 == GameList[i].blobs[j].crc32) {
          // file is the same but has a different filename, let's replace it
          strncpy(GameList[i].blobs[j].filename, filename, MAX_LENGTH_FILENAME);
          //Compatible file found
          fileFound[i][j] = 1;
          LOGSTV("File %s from %s Goes to romset %s\n",filename, info->filename,GameList[i].romset);
        }
        j++;
      }
    }

    return 0;
}
int biosloaded = 0xFF;

int copyBios(JZFile *zip, void* id) {
    JZFileHeader header;
    char filename[MAX_LENGTH_FILENAME];
    char* biosname = NULL;
    u8* data;
    unsigned int i,j, dataAvailable;
    rominfo* info = (rominfo*)id;
    int gameId = -1;
    if (info != NULL) gameId = info->gameId;

    if(jzReadLocalFileHeader(zip, &header, filename, sizeof(filename))) {
        LOGSTV("Couldn't read local file header!\n");
        return -1;
    }

    if((data = (u8*)malloc(header.uncompressedSize)) == NULL) {
        LOGSTV("Couldn't allocate memory!\n");
        return -1;
    }

    LOGSTV("copyBios %s\n", filename);

    i=0;
    while(availableGames[gameId].entry->blobs[i].type != GAME_END) {
      if (availableGames[gameId].entry->blobs[i].type == BIOS_BLOB) {
        // We need a specific bios
        biosname = (char*)malloc(strlen(availableGames[gameId].entry->blobs[i].filename) + 1);
        strcpy(biosname, availableGames[gameId].entry->blobs[i].filename);
      }
      i++;
    }

    i=0;
    dataAvailable = 0;
    while(biosLink.entry->blobs[i].type != GAME_END) {
      if (strncmp(biosLink.entry->blobs[i].filename, filename, MAX_LENGTH_FILENAME) == 0) {
        if (dataAvailable == 0) {
          dataAvailable = (jzReadData(zip, &header, data) == Z_OK);
        }
        if(dataAvailable != 0) {
          if (info->bios == 1) {
            biosFound[i] = 1;
          } else {
            fileFound[gameId][i] = 1;
          }
          switch (biosLink.entry->blobs[i].type) {
            case BIOS_BLOB:
              if(biosname != NULL) {
                // Load the special bios (sfish2 & sfish2j have a special bios, part of the romset, which allow optical media reading)
                if (strcmp(biosname,filename) == 0) {
                  LOGSTV("Load bios %s\n", filename);
                  for (j=0; j<biosLink.entry->blobs[i].length;j++) {
                    T1WriteByte(BiosRom, biosLink.entry->blobs[i].offset+j, data[j]);
                  }
                }
              } else {
                // Load a bios from stvbios, restricted by allowed regions for the game, further restricted by favorite region if allowed
                if (biosloaded > i && (biosLink.entry->blobs[i].region & availableGames[gameId].entry->regions) == biosLink.entry->blobs[i].region
                && (stv_favorite_region == biosLink.entry->blobs[i].region || (availableGames[gameId].entry->regions & stv_favorite_region) != stv_favorite_region)) {
                  LOGSTV("Load bios %s\n", filename);
                  for (j=0; j<biosLink.entry->blobs[i].length;j++) {
                    T1WriteByte(BiosRom, biosLink.entry->blobs[i].offset+j, data[j]);
                  }
                  biosloaded = i;
                }
              }
              break;
          }
        }
      }
      i++;
    }

    free(data);

    return 0;
}

int copyFile(JZFile *zip, void* id) {
    JZFileHeader header;
    char long_filename[MAX_LENGTH_FILENAME];
    char* filename;
    u8* data;
    int i,j, dataAvailable;
    rominfo* info = (rominfo*)id;
    int gameId = -1;
    if (info != NULL) gameId = info->gameId;

    if(jzReadLocalFileHeader(zip, &header, long_filename, sizeof(long_filename))) {
        LOGSTV("Couldn't read local file header!\n");
        return -1;
    }
    filename = get_basename(long_filename);

    if((data = (u8*)malloc(header.uncompressedSize)) == NULL) {
        LOGSTV("Couldn't allocate memory!\n");
        return -1;
    }

    i=0;
    dataAvailable = 0;
    while(availableGames[gameId].entry->blobs[i].type != GAME_END) {
      if (strncmp(availableGames[gameId].entry->blobs[i].filename, filename, MAX_LENGTH_FILENAME) == 0) {
        LOGSTV("copyFile %s\n", filename);
        if (dataAvailable == 0) {
          dataAvailable = (jzReadData(zip, &header, data) == Z_OK);
        }
        if(dataAvailable != 0) {
          fileFound[gameId][i] = 1;
          switch (availableGames[gameId].entry->blobs[i].type) {
            case BIOS_BLOB:
              if (biosloaded > i) {
                LOGSTV("Load bios %s\n", filename);
                for (j=0; j<availableGames[gameId].entry->blobs[i].length;j++) {
                  T1WriteByte(BiosRom, availableGames[gameId].entry->blobs[i].offset+j, data[j]);
                }
                biosloaded = i;
              }
              break;
            case GAME_WORD_BLOB:
              for (j=0; j<availableGames[gameId].entry->blobs[i].length;j++) {
                T2WriteByte(CartridgeArea->rom, availableGames[gameId].entry->blobs[i].offset+j, data[j]);
              }
              break;
            case HEADER_BLOB:
              for (j=0; j<availableGames[gameId].entry->blobs[i].length;j++) {
                T1WriteByte(CartridgeArea->rom, availableGames[gameId].entry->blobs[i].offset+(2*j), data[j]);
                // is zeroing odd bytes required ? i couldn't find a game requiring it, and it's breaking interlaced roms
                //T1WriteByte(CartridgeArea->rom, availableGames[gameId].entry->blobs[i].offset+(2*j+1), 0);
              }
              break;
            case GAME_BYTE_BLOB:
              for (j=0; j<availableGames[gameId].entry->blobs[i].length;j++) {
                T1WriteByte(CartridgeArea->rom, availableGames[gameId].entry->blobs[i].offset+j, data[j]);
              }
              break;
            case EEPROM_BLOB:
              eeprom_start(data);
              hasEeprom = 1;
              break;
          }
        } else LOGSTV("Error : No data read from %s\n", filename);
      }
      i++;
    }

    free(data);

    return 0;
}

int recordCallback(JZFile *zip, int idx, JZFileHeader *header, char *filename, void *user_data) {
    long offset;
    rominfo* info = (rominfo*)user_data;
    offset = zip->tell(zip); // store current position

    if(zip->seek(zip, header->offset, SEEK_SET)) {
        LOGSTV("Cannot seek in zip file!\n");
        return 0; // abort
    }

    LOGSTV("%s\n", filename);
#ifdef __LIBRETRO__
    char *last = (char *)strrchr(info->filename, PATH_DEFAULT_SLASH_C());
#else
    char *last = (char *)strrchr(info->filename, '/');
#endif
    if (last != NULL) {
      if (strcmp(last+1, "stvbios.zip") == 0) {
        processBios(zip, user_data); // alters file offset
      } else {
        processFile(zip, user_data); // alters file offset
      }
    } else {
      processFile(zip, user_data); // alters file offset
    }

    zip->seek(zip, offset, SEEK_SET); // return to position

    return 1; // continue
}

char * updateGameList(const char* file, int *nbGames){
  FILE *fp;
  JZEndRecord endRecord;
  JZFile *zip;
  char *romset = NULL;
  int i, j;
  u8 isASTVGame, isBiosFound, isBlobFound;
  rominfo info;
  info.filename = file;
  info.gameId = -1;
  info.bios = 0;

  LOGSTV("updateGameList %s\n", file);

  memset(fileFound, 0x0, NB_STV_GAMES*MAX_GAME_FILES);

  if(!(fp = fopen(file, "rb"))) {
        LOGSTV("Couldn't open \"%s\"!\n", file);
        return NULL;
  }

  zip = jzfile_from_stdio_file(fp);

  if(jzReadEndRecord(zip, &endRecord)) {
    LOGSTV("Couldn't read ZIP file end record.\n");
    goto endClose;
  }

  if(jzReadCentralDirectory(zip, &endRecord, recordCallback, &info)) {
    LOGSTV("Couldn't read ZIP file central record.\n");
    goto endClose;
  }

  j=0;
  if (!hasBios) {
    while(BiosList.blobs[j].type != GAME_END) {
      // Any available bios will do, let's pick the first one
      if (BiosList.blobs[j].type == BIOS_BLOB && biosFound[j] == 1) {
        hasBios |= biosFound[j];
        biosLink.entry = &BiosList;
        strncpy(biosLink.path, file, MAX_LENGTH_FILEPATH);
        break;
      }
      j++;
    }
  }
  for (i=0; i<NB_STV_GAMES; i++) {
    isASTVGame = 1;
    isBiosFound = 0;
    isBlobFound = 1;
    j=0;
    while(GameList[i].blobs[j].type != GAME_END) {
      if (GameList[i].blobs[j].type == BIOS_BLOB) {
        isBiosFound |= fileFound[i][j];
      } else {
        if (GameList[i].blobs[j].type != EEPROM_BLOB)
          isBlobFound &= fileFound[i][j];
      }
      j++;
    }
    isASTVGame = isBlobFound & (isBiosFound | hasBios);
    if (isASTVGame == 1) {
      //Add the filename as a Game
      int found = 0;
      for (j=0; j<NB_STV_GAMES; j++) {
        if (availableGames[j].entry == &GameList[i]) {
          found = 1;
          break;
        }
      }
      if (found == 0) {
        availableGames[*nbGames].entry = &GameList[i];
        strncpy(availableGames[*nbGames].path, file, MAX_LENGTH_FILEPATH);
        romset = strdup(GameList[i].romset);
        (*nbGames)++;
        //break;
      }
    }
  }

endClose:
    zip->close(zip);
    return romset;
}

int loadBios(int id){
  FILE *fp;
  JZEndRecord endRecord;
  JZFile *zip;
  int i = 0;
  u8 isBiosFound = 0;
  rominfo info;
  info.filename = biosLink.path;
  info.gameId = id;
  info.bios = 1;
  memset(fileFound, 0x0, NB_STV_GAMES*MAX_GAME_FILES);
  if(!(fp = fopen(biosLink.path, "rb"))) {
        LOGSTV("Couldn't open bios\"%s\"!\n", biosLink.path);
        return 0;
  }

  zip = jzfile_from_stdio_file(fp);

  if(jzReadEndRecord(zip, &endRecord)) {
    LOGSTV("Couldn't read ZIP file end record.\n");
  } else {
    if(jzReadCentralDirectory(zip, &endRecord, recordCallback, &info)) {
      LOGSTV("Couldn't read ZIP file central record.\n");
    }
  }
  zip->close(zip);
  while(biosLink.entry->blobs[i].type != GAME_END) {
    if (biosLink.entry->blobs[i].type == BIOS_BLOB) {
      isBiosFound |= biosFound[i];
    }
    i++;
  }
  return isBiosFound;
}

int loadGame(int gameId){
  FILE *fp;
  JZEndRecord endRecord;
  JZFile *zip;
  int i = 0;
  u8 isBiosFound = 0;
  u8 isBlobFound = 1;
  u8 hasBios = 0;
  rominfo info;
  LOGSTV("loadGame %d path = %s\n", gameId, availableGames[gameId].path);

  info.filename = availableGames[gameId].path;
  info.gameId = gameId;
  info.bios = 0;
  hasBios = loadBios(gameId);
  biosloaded = 0xFF;

  LOGSTV("Loading game[%d] %s from %s\n", gameId, availableGames[gameId].entry->name, availableGames[gameId].path);
  memset(fileFound, 0x0, NB_STV_GAMES*MAX_GAME_FILES);

  if(!(fp = fopen(availableGames[gameId].path, "rb"))) {
        LOGSTV("Couldn't open \"%s\"!\n", availableGames[gameId].path);
        return -1;
  }

  zip = jzfile_from_stdio_file(fp);

  if(jzReadEndRecord(zip, &endRecord)) {
    LOGSTV("Couldn't read ZIP file end record.\n");
  } else {
    if(jzReadCentralDirectory(zip, &endRecord, recordCallback, &info)) {
      LOGSTV("Couldn't read ZIP file central record.\n");
    }
  }
  zip->close(zip);

  while(availableGames[gameId].entry->blobs[i].type != GAME_END) {
    if (availableGames[gameId].entry->blobs[i].type == BIOS_BLOB) {
      isBiosFound |= fileFound[gameId][i];
    } else {
      if (availableGames[gameId].entry->blobs[i].type != EEPROM_BLOB)
        isBlobFound &= fileFound[gameId][i];
    }
    i++;
  }
  if (isBlobFound & (isBiosFound|hasBios)) {
    LOGSTV("%s has been sucessfully loaded\n", availableGames[gameId].entry->name);
    if (availableGames[gameId].entry->eeprom != NULL || hasEeprom == 0)
      eeprom_start(availableGames[gameId].entry->eeprom);
    cryptoSetKey(availableGames[gameId].entry->key);
    yabsys.isRotated = availableGames[gameId].entry->rotated;
    yabsys.stvInputType = availableGames[gameId].entry->input;
    if (availableGames[gameId].entry->init != NULL) availableGames[gameId].entry->init();
    return 0;
  }
  return -1;
}

#ifndef WIN32
int STVGetRomList(const char* path, int force){
//List number of files in directory
  DIR *d;
  FILE *fp;
  int i, nbGames = 0;
  char savefile[MAX_LENGTH_FILEPATH];
  memset(availableGames, 0x0, sizeof(GameLink)*NB_STV_GAMES);
  snprintf(savefile, MAX_LENGTH_FILEPATH, "%s/gamelistv2.save", path);
  if (force == 0) {
    nbGames = loadGames(savefile);
    if (nbGames != 0) return nbGames;
  }
  struct dirent *dir;
  d = opendir(path);
  if (d) {
    //Force a detection of the bios first
    unsigned int len = strlen(path)+strlen("/")+strlen("stvbios.zip")+1;
    unsigned char *file = (unsigned char *)malloc(len);
    snprintf((char*)file, len, "%s/stvbios.zip",path);
    updateGameList((const char *)file, &nbGames);
    free(file);
    while ((dir = readdir(d)) != NULL) {
      if (dir->d_type == DT_REG)
      {
        char *dot = strrchr(dir->d_name, '.');
        if (dot && !strcmp(dot, ".zip")) {
          len = strlen(path)+strlen("/")+strlen(dir->d_name)+1;
          file = (unsigned char *)malloc(len);
          snprintf((char *)file, len, "%s/%s",path, dir->d_name);
          updateGameList((const char *)file, &nbGames);
          free(file);
        }
      }
    }
    closedir(d);
    fp = fopen(savefile, "w");
    if (biosLink.entry != NULL) {
      fprintf(fp, "%s,%s\n", biosLink.entry->name, biosLink.path);
    }
    for (i=0; i<nbGames; i++) {
      fprintf(fp, "%s,%s\n", availableGames[i].entry->name, availableGames[i].path);
    }
    fclose(fp);
  }
  return nbGames;
}
#else
int STVGetRomList(const char* path, int force){
//List number of files in directory
  FILE *fp;
  int i, nbGames = 0;
  char savefile[MAX_LENGTH_FILEPATH];
  char pathfile[MAX_LENGTH_FILEPATH];
  memset(availableGames, 0x0, sizeof(GameLink)*NB_STV_GAMES);
  snprintf(savefile, MAX_LENGTH_FILEPATH, "%s/gamelistv2.save", path);
  snprintf(pathfile, MAX_LENGTH_FILEPATH, "%s/*.zip", path);
  if (force == 0) {
    nbGames = loadGames(savefile);
    if (nbGames != 0) return nbGames;
  }
  HANDLE hFind;
  WIN32_FIND_DATAA FindFileData;
  //Force a detection of the bios first
  unsigned int len = strlen(path) + strlen("/") + strlen("stvbios.zip") + 1;
  unsigned char *file = malloc(len);
  snprintf(file, len, "%s/stvbios.zip", path);
  updateGameList(file, &nbGames);
  free(file);
  if((hFind = FindFirstFileA(pathfile, &FindFileData)) != INVALID_HANDLE_VALUE){
    do{
      unsigned int len = strlen(path)+strlen("/")+strlen(FindFileData.cFileName)+1;
      unsigned char *file = malloc(len);
      snprintf(file, len, "%s/%s",path, FindFileData.cFileName);
      LOGSTV(file);
      updateGameList(file, &nbGames);
      free(file);
    }while(FindNextFileA(hFind, &FindFileData));
    FindClose(hFind);
    fp = fopen(savefile, "w");
    if (biosLink.entry != NULL) {
        fprintf(fp, "%s,%s\n", biosLink.entry->name, biosLink.path);
    }
    for (i=0; i<nbGames; i++) {
      fprintf(fp, "%s,%s\n", availableGames[i].entry->name, availableGames[i].path);
    }
    fclose(fp);
  }
  return nbGames;
}
#endif

char* getSTVGameName(int id) {
  if ((id>=0) && (id<NB_STV_GAMES)) return availableGames[id].entry->name;
  return NULL;
}
char* getSTVRomset(int id) {
  if ((id>=0) && (id<NB_STV_GAMES)) return availableGames[id].entry->romset;
  return NULL;
}

int loadGames(char* path) {
  int i, nbGames = 0;
  int character;
  char* field;
  char gameName[1024];
  char gamePath[MAX_LENGTH_FILEPATH];
  FILE *fp;
  fp = fopen(path, "r");
  if (fp == NULL) return 0;
  for(;;) {
    i=0;
    field = &gameName[0];
    while((character = fgetc(fp)) != EOF) {
      field[i] = character;
      if (field[i] == '\n') {
        field[i] = '\0';
        break;
      }
      else {
        if (field[i] == ',') {
          field[i] = '\0';
          field = &gamePath[0];
          i = 0;
        } else {
          i++;
        }
      }
    }
    if (character == EOF) break;
    LOGSTV("Scan new game %s %s!!!\n", gameName, gamePath);
    if (strncmp(gameName,BiosList.name,1024)==0) {
      biosLink.entry = &BiosList;
      strncpy(biosLink.path, gamePath, MAX_LENGTH_FILEPATH);
    } else {
      for (i=0; i<NB_STV_GAMES; i++) {
        if (strncmp(gameName,GameList[i].name,1024)==0) {
          availableGames[nbGames].entry = &GameList[i];
          strncpy(availableGames[nbGames].path, gamePath, MAX_LENGTH_FILEPATH);
          LOGSTV("Rebuild %s from %s\n", gameName, gamePath);
          nbGames++;
          break;
        }
      }
    }
  }
  fclose(fp);
  return nbGames;
}

int STVGetSingle(const char *pathfile, const char *biospath, char **romset){
  int i, nbGames = 0;
  memset(availableGames, 0x0, sizeof(GameLink)*NB_STV_GAMES);
  struct dirent *dir;
  //Force a detection of the bios first
  updateGameList(biospath, &nbGames);
  *romset = updateGameList(pathfile, &nbGames);
  return nbGames;
}

int STVSingleInit(const char *gamepath, const char *biospath, const char *eepromdir, int favorite_region) {
  int nbGame = 0;
  char *romset = NULL;
  yabsys.isSTV = 0;
  if (favorite_region != 0) stv_favorite_region = favorite_region;
  if ((gamepath == NULL) || (biospath == NULL)) return -1;
  nbGame = STVGetSingle(gamepath, biospath, &romset);
  if (loadGame(0) == 0) {
    char eeprom_path[4096];
    snprintf(eeprom_path, sizeof(eeprom_path), "%s/%s.nv", eepromdir, romset);
    eeprom_init(eeprom_path);
    yabsys.isSTV = 1;
    return 0;
  }
  return -1;
}

int STVInit(const char* romset, const char *path, const char *eepromdir, int favorite_region){
  yabsys.isSTV = 0;
  if (romset == NULL) return -1;
  if (favorite_region != 0) stv_favorite_region = favorite_region;
  cryptoReset();
  if (CartridgeArea->carttype != CART_ROMSTV) return 0;
  int id = 0;
  for (id = 0; id < NB_STV_GAMES; id++) {
    if (availableGames[id].entry == NULL) break;
    if (strcmp(availableGames[id].entry->romset, romset) == 0) break;
  }
#ifndef __LIBRETRO__
  int nbGames = STVGetRomList(path, 0);
  if (nbGames == 0) return -1;
  if (nbGames <= id) return -1;
#endif
  if (loadGame(id) == 0) {
    char eeprom_path[4096];
    snprintf(eeprom_path, sizeof(eeprom_path), "%s/%s.nv", eepromdir, romset);
    eeprom_init(eeprom_path);
    yabsys.isSTV = 1;
    return 0;
  }
  return -1;
}

int STVDeInit(){
  if (CartridgeArea == NULL || CartridgeArea->carttype != CART_ROMSTV) return 0;
  eeprom_deinit();
  yabsys.isSTV = 0;
  return 0;
}

#ifdef __cplusplus
}
#endif

