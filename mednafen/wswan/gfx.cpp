/******************************************************************************/
/* Mednafen WonderSwan Emulation Module(based on Cygne)                       */
/******************************************************************************/
/* gfx.cpp:
**  Copyright (C) 2002 Dox dox@space.pl
**  Copyright (C) 2007-2017 Mednafen Team
**  Copyright (C) 2016 Alex 'trap15' Marshall - http://daifukkat.su/
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License version 2.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "wswan.h"
#include "gfx.h"
#include "memory-wswan.h"
#include "v30mz.h"
#include "rtc.h"
#include "comm.h"
#include "mednafen/video.h"

static uint32 wsMonoPal[16][4];
static uint32 wsColors[8];
static uint32 wsCols[16][16];

static uint32 ColorMapG[16];
static uint32 ColorMap[4096];
static uint32 LayerEnabled;

static uint8 wsLine;                 /*current scanline*/
static uint8 weppy;

static uint8 SpriteTable[2][0x80][4];
static uint32 SpriteCountCache[2];
static bool FrameWhichActive;
static uint8 DispControl;
static uint8 BGColor;
static uint8 LineCompare;
static uint8 SPRBase;
static uint8 SpriteStart, SpriteCount;
static uint8 FGBGLoc;
static uint8 FGx0, FGy0, FGx1, FGy1;
static uint8 SPRx0, SPRy0, SPRx1, SPRy1;

static uint8 BGXScroll, BGYScroll;
static uint8 FGXScroll, FGYScroll;
static uint8 LCDControl, LCDIcons;
static uint8 LCDVtotal;

static uint8 BTimerControl;
static uint16 HBTimerPeriod;
static uint16 VBTimerPeriod;

static uint16 HBCounter, VBCounter;
static uint8 VideoMode;

void WSwan_GfxInit(void)
{
   LayerEnabled = 7; // BG, FG, sprites
}

void WSwan_GfxWSCPaletteRAMWrite(uint32 ws_offset, uint8 data)
{
   ws_offset=(ws_offset&0xfffe)-0xfe00;
   wsCols[(ws_offset>>1)>>4][(ws_offset>>1)&15] = wsRAM[ws_offset+0xfe00] | ((wsRAM[ws_offset+0xfe01]&0x0f) << 8);
}

void WSwan_GfxWrite(uint32 A, uint8 V)
{
   if(A >= 0x1C && A <= 0x1F)
   {
      wsColors[(A - 0x1C) * 2 + 0] = 0xF - (V & 0xf);
      wsColors[(A - 0x1C) * 2 + 1] = 0xF - (V >> 4);
   }
   else if(A >= 0x20 && A <= 0x3F)
   {
      wsMonoPal[(A - 0x20) >> 1][((A & 0x1) << 1) + 0] = V&7;
      wsMonoPal[(A - 0x20) >> 1][((A & 0x1) << 1) | 1] = (V>>4)&7;
   }
   else switch(A)
   {
      case 0x00: DispControl = V; break;
      case 0x01: BGColor = V; break;
      case 0x03: LineCompare = V; break;
      case 0x04: SPRBase = V & 0x3F; break;
      case 0x05: SpriteStart = V; break;
      case 0x06: SpriteCount = V; break;
      case 0x07: FGBGLoc = V; break;
      case 0x08: FGx0 = V; break;
      case 0x09: FGy0 = V; break;
      case 0x0A: FGx1 = V; break;
      case 0x0B: FGy1 = V; break;
      case 0x0C: SPRx0 = V; break;
      case 0x0D: SPRy0 = V; break;
      case 0x0E: SPRx1 = V; break;
      case 0x0F: SPRy1 = V; break;
      case 0x10: BGXScroll = V; break;
      case 0x11: BGYScroll = V; break;
      case 0x12: FGXScroll = V; break;
      case 0x13: FGYScroll = V; break;

      case 0x14: LCDControl = V; break; // if((!(wsIO[0x14]&1))&&(data&1)) { wsLine=0; }break; /* LCD off ??*/
      case 0x15: LCDIcons = V; break;
      case 0x16: LCDVtotal = V; break;

      case 0x60: VideoMode = V;
                 wsSetVideo(V>>5, false);
                 //printf("VideoMode: %02x, %02x\n", V, V >> 5);
                 break;

      case 0xa2: BTimerControl = V; 
                 //printf("%04x:%02x\n", A, V);
                 break;
      case 0xa4: HBTimerPeriod &= 0xFF00; HBTimerPeriod |= (V << 0); /*printf("%04x:%02x, %d\n", A, V, wsLine);*/ break;
      case 0xa5: HBTimerPeriod &= 0x00FF; HBTimerPeriod |= (V << 8); HBCounter = HBTimerPeriod; /*printf("%04x:%02x, %d\n", A, V, wsLine);*/ break;
      case 0xa6: VBTimerPeriod &= 0xFF00; VBTimerPeriod |= (V << 0); /*printf("%04x:%02x, %d\n", A, V, wsLine);*/ break;
      case 0xa7: VBTimerPeriod &= 0x00FF; VBTimerPeriod |= (V << 8); VBCounter = VBTimerPeriod; /*printf("%04x:%02x, %d\n", A, V, wsLine);*/ break;
      //default: printf("%04x:%02x\n", A, V); break;
   }
}

uint8 WSwan_GfxRead(uint32 A)
{
   if(A >= 0x1C && A <= 0x1F)
   {
      uint8 ret = 0;

      ret |= 0xF - wsColors[(A - 0x1C) * 2 + 0];
      ret |= (0xF - wsColors[(A - 0x1C) * 2 + 1]) << 4;

      return(ret);
   }
   else if(A >= 0x20 && A <= 0x3F)
   {
      uint8 ret = wsMonoPal[(A - 0x20) >> 1][((A & 0x1) << 1) + 0] | (wsMonoPal[(A - 0x20) >> 1][((A & 0x1) << 1) | 1] << 4);

      return(ret);
   }
   else switch(A)
   {
      case 0x00: return(DispControl);
      case 0x01: return(BGColor);
      case 0x02: return(wsLine);
      case 0x03: return(LineCompare);
      case 0x04: return(SPRBase);
      case 0x05: return(SpriteStart);
      case 0x06: return(SpriteCount);
      case 0x07: return(FGBGLoc);
      case 0x08: return(FGx0); break;
      case 0x09: return(FGy0); break;
      case 0x0A: return(FGx1); break;
      case 0x0B: return(FGy1); break;
      case 0x0C: return(SPRx0); break;
      case 0x0D: return(SPRy0); break;
      case 0x0E: return(SPRx1); break;
      case 0x0F: return(SPRy1); break;
      case 0x10: return(BGXScroll);
      case 0x11: return(BGYScroll);
      case 0x12: return(FGXScroll);
      case 0x13: return(FGYScroll);
      case 0x14: return(LCDControl);
      case 0x15: return(LCDIcons);
      case 0x16: return(LCDVtotal);
      case 0x60: return(VideoMode);
      case 0xa0: return(wsc ? 0x87 : 0x86);
      case 0xa2: return(BTimerControl);
      case 0xa4: return((HBTimerPeriod >> 0) & 0xFF);
      case 0xa5: return((HBTimerPeriod >> 8) & 0xFF);
      case 0xa6: return((VBTimerPeriod >> 0) & 0xFF);
      case 0xa7: return((VBTimerPeriod >> 8) & 0xFF);
      case 0xa8: /*printf("%04x\n", A);*/ return((HBCounter >> 0) & 0xFF);
      case 0xa9: /*printf("%04x\n", A);*/ return((HBCounter >> 8) & 0xFF);
      case 0xaa: /*printf("%04x\n", A);*/ return((VBCounter >> 0) & 0xFF);
      case 0xab: /*printf("%04x\n", A);*/ return((VBCounter >> 8) & 0xFF);
      default: return(0);
      //default: printf("GfxRead:  %04x\n", A); return(0);
   }
}

bool wsExecuteLine(MDFN_Surface *surface, bool skip)
{
   bool ret;

   weppy = 0;

   ret = false;

   if(wsLine < 144)
   {
      if(!skip)
      {
         if(surface->pix_bytes == 2)
            wsScanline(surface->pixels16 + wsLine * surface->pitch, surface->pix_bytes);
         else if(surface->pix_bytes == 4)
            wsScanline(surface->pixels + wsLine * surface->pitch, surface->pix_bytes);
      }
   }

   Comm_Process();
   WSwan_CheckSoundDMA();

   // Update sprite data table
   // Note: it's at 142 actually but it doesn't "update" until next frame
   if(wsLine == 142)
   {
      SpriteCountCache[!FrameWhichActive] = MIN(0x80, SpriteCount);
      memcpy(SpriteTable[!FrameWhichActive], &wsRAM[(SPRBase << 9) + (SpriteStart << 2)], SpriteCountCache[!FrameWhichActive] << 2);
   }

   if(wsLine == 144)
   {
      FrameWhichActive = !FrameWhichActive;
      ret = true;
      WSwan_Interrupt(WSINT_VBLANK);
      //printf("VBlank: %d\n", wsLine);
      if(VBCounter && (BTimerControl & 0x04))
      {
         VBCounter--;
         if(!VBCounter)
         {
            if(BTimerControl & 0x08) // loop
               VBCounter = VBTimerPeriod;
            WSwan_Interrupt(WSINT_VBLANK_TIMER);
         }
      }
   }


   if(HBCounter && (BTimerControl & 0x01))
   {
      HBCounter--;
      if(!HBCounter)
      {
         if(BTimerControl & 0x02) // loop
            HBCounter = HBTimerPeriod;
         WSwan_Interrupt(WSINT_HBLANK_TIMER);
      }
   }

   weppy = 1;
   v30mz_execute(128);
   //

   WSwan_CheckSoundDMA();

   //
   weppy = 2;
   v30mz_execute(96);

   wsLine = (wsLine + 1) % (MAX(144, LCDVtotal) + 1);
   if(wsLine == LineCompare)
   {
      WSwan_Interrupt(WSINT_LINE_HIT);
      //printf("Line hit: %d\n", wsLine);
   }

   weppy = 3;
   v30mz_execute(32);

   RTC_Clock(256);

   weppy = 0;
   return(ret);
}

void WSwan_SetLayerEnableMask(uint64 mask)
{
   LayerEnabled = mask;
}

void WSwan_SetPixelFormat(int depth)
{
   for(int r = 0; r < 16; r++)
      for(int g = 0; g < 16; g++)
         for(int b = 0; b < 16; b++)
         {
            uint32 neo_r, neo_g, neo_b;

            neo_r = r * 17;
            neo_g = g * 17;
            neo_b = b * 17;

            switch(depth)
            {
            case 15: ColorMap[(r << 8) | (g << 4) | (b << 0)] = MAKECOLOR_15(neo_r, neo_g, neo_b, 0); break;
            case 16: ColorMap[(r << 8) | (g << 4) | (b << 0)] = MAKECOLOR_16(neo_r, neo_g, neo_b, 0); break;
            case 24: ColorMap[(r << 8) | (g << 4) | (b << 0)] = MAKECOLOR_24(neo_r, neo_g, neo_b, 0); break;
            }
         }

   for(int i = 0; i < 16; i++)
   {
      uint32 neo_r, neo_g, neo_b;

      neo_r = (i) * 17;
      neo_g = (i) * 17;
      neo_b = (i) * 17;

      switch(depth)
      {
      case 15: ColorMapG[i] = MAKECOLOR_15(neo_r, neo_g, neo_b, 0); break;
      case 16: ColorMapG[i] = MAKECOLOR_16(neo_r, neo_g, neo_b, 0); break;
      case 24: ColorMapG[i] = MAKECOLOR_24(neo_r, neo_g, neo_b, 0); break;
      }
   }
}

void wsScanline(void *target, int pix_bytes)
{
   uint32		start_tile_n,map_a,startindex,adrbuf,b1,b2,j,t,l;
   uint8		b_bg[256];
   uint8		b_bg_pal[256];

   if(!wsVMode)
      memset(b_bg, wsColors[BGColor&0xF]&0xF, 256);
   else
   {
      memset(&b_bg[0], BGColor & 0xF, 256);
      memset(&b_bg_pal[0], (BGColor>>4)  & 0xF, 256);
   }
   start_tile_n=(wsLine+BGYScroll)&0xff;/*First line*/
   map_a=(((uint32)(FGBGLoc&0xF))<<11)+((start_tile_n&0xfff8)<<3);
   startindex = BGXScroll >> 3; /*First tile in row*/
   adrbuf = 7-(BGXScroll&7); /*Pixel in tile*/

   if((DispControl & 0x01) && (LayerEnabled & 0x01)) /*BG layer*/
   {
      for(t=0;t<29;t++)
      {
         b1=wsRAM[map_a+(startindex<<1)];
         b2=wsRAM[map_a+(startindex<<1)+1];
         uint32 palette=(b2>>1)&15;
         b2=(b2<<8)|b1;
         wsGetTile(b2&0x1ff,start_tile_n&7,b2&0x8000,b2&0x4000,b2&0x2000);

         if(wsVMode)
         {
            if(wsVMode & 0x2)
            {
               for(int x = 0; x < 8; x++)
                  if(wsTileRow[x])
                  {
                     b_bg[adrbuf + x] = wsTileRow[x];
                     b_bg_pal[adrbuf + x] = palette;
                  }
            }
            else
            {
               for(int x = 0; x < 8; x++)
                  if(wsTileRow[x] || !(palette & 0x4))
                  {
                     b_bg[adrbuf + x] = wsTileRow[x];
                     b_bg_pal[adrbuf + x] = palette;
                  }
            }
         }
         else
         {
            for(int x = 0; x < 8; x++)
               if(wsTileRow[x] || !(palette & 4))
               {
                  b_bg[adrbuf + x] = wsColors[wsMonoPal[palette][wsTileRow[x]]];
               }
         }
         adrbuf += 8;
         startindex=(startindex + 1)&31;
      } // end for(t = 0 ...
   } // End BG layer drawing

   if((DispControl & 0x02) && (LayerEnabled & 0x02))/*FG layer*/
   {
      uint8 windowtype = DispControl&0x30;
      bool in_window[256 + 8*2];

      if(windowtype)
      {
         memset(in_window, 0, sizeof(in_window));

         if(windowtype == 0x20) // Display FG only inside window
         {
            if((wsLine >= FGy0) && (wsLine <= FGy1))
               for(j = FGx0; j <= FGx1 && j < 224; j++)
                  in_window[7 + j] = 1;
         }
         else if(windowtype == 0x30) // Display FG only outside window
         {
            for(j = 0; j < 224; j++)
            {
               if(!(j >= FGx0 && j <= FGx1) || !((wsLine >= FGy0) && (wsLine <= FGy1)))
                  in_window[7 + j] = 1;
            }
         }
         else
         {
            //puts("Who knows!");
         }
      }
      else
         memset(in_window, 1, sizeof(in_window));

      start_tile_n=(wsLine+FGYScroll)&0xff;
      map_a=(((uint32)((FGBGLoc>>4)&0xF))<<11)+((start_tile_n>>3)<<6);
      startindex = FGXScroll >> 3;
      adrbuf = 7-(FGXScroll&7);

      for(t=0; t<29; t++)
      {
         b1=wsRAM[map_a+(startindex<<1)];
         b2=wsRAM[map_a+(startindex<<1)+1];
         uint32 palette=(b2>>1)&15;
         b2=(b2<<8)|b1;
         wsGetTile(b2&0x1ff,start_tile_n&7,b2&0x8000,b2&0x4000,b2&0x2000);

         if(wsVMode)
         {
            if(wsVMode & 0x2)
               for(int x = 0; x < 8; x++)
               {
                  if(wsTileRow[x] && in_window[adrbuf + x])
                  {
                     b_bg[adrbuf + x] = wsTileRow[x] | 0x10;
                     b_bg_pal[adrbuf + x] = palette;
                  }
               }
            else
               for(int x = 0; x < 8; x++)
               {
                  if((wsTileRow[x] || !(palette & 0x4)) && in_window[adrbuf + x])
                  {
                     b_bg[adrbuf + x] = wsTileRow[x] | 0x10;
                     b_bg_pal[adrbuf + x] = palette;
                  }
               }
         }
         else
         {
            for(int x = 0; x < 8; x++)
               if((wsTileRow[x] || !(palette & 4)) && in_window[adrbuf + x])
               {
                  b_bg[adrbuf + x] = wsColors[wsMonoPal[palette][wsTileRow[x]]] | 0x10;
               }
         }
         adrbuf += 8;
         startindex=(startindex + 1)&31;
      } // end for(t = 0 ...

   } // end FG drawing

   if((DispControl & 0x04) && SpriteCountCache[FrameWhichActive] && (LayerEnabled & 0x04))/*Sprites*/
   {
      bool in_window[256 + 8*2];

      if(DispControl & 0x08)
      {
         memset(in_window, 0, sizeof(in_window));
         if((wsLine >= SPRy0) && (wsLine <= SPRy1))
            for(j = SPRx0; j <= SPRx1 && j < 256; j++)
               in_window[7 + j] = 1;
      }
      else
         memset(in_window, 1, sizeof(in_window));

      for(int h = SpriteCountCache[FrameWhichActive] - 1; h >= 0; h--)
      {
         const uint8* stab = SpriteTable[FrameWhichActive][h];
         int ts = stab[0];
         int as = stab[1];
         int ysx = stab[2];
         int xs = stab[3];
         int ys;

         if(xs >= 249) xs -= 256;

         if(ysx > 150) 
            ys = (int8)ysx;
         else 
            ys = ysx;

         ys = wsLine - ys;

         if(ys >= 0 && ys < 8 && xs < 224)
         {
            uint32 palette = ((as >> 1) & 0x7);

            ts |= (as&1) << 8;
            wsGetTile(ts, ys, as & 0x80, as & 0x40, 0);

            if(wsVMode)
            {
               if(wsVMode & 0x2)
               {
                  for(int x = 0; x < 8; x++)
                     if(wsTileRow[x])
                     {
                        if((as & 0x20) || !(b_bg[xs + x + 7] & 0x10))
                        {
                           bool drawthis = 0;

                           if(!(DispControl & 0x08)) 
                              drawthis = true;
                           else if((as & 0x10) && !in_window[7 + xs + x])
                              drawthis = true;
                           else if(!(as & 0x10) && in_window[7 + xs + x])
                              drawthis = true;

                           if(drawthis)
                           {
                              b_bg[xs + x + 7] = wsTileRow[x] | (b_bg[xs + x + 7] & 0x10);
                              b_bg_pal[xs + x + 7] = 8 + palette;
                           }
                        }
                     }
               }
               else
               {
                  for(int x = 0; x < 8; x++)
                     if(wsTileRow[x] || !(palette & 0x4))
                     {
                        if((as & 0x20) || !(b_bg[xs + x + 7] & 0x10))
                        {
                           bool drawthis = 0;

                           if(!(DispControl & 0x08))
                              drawthis = true;
                           else if((as & 0x10) && !in_window[7 + xs + x])
                              drawthis = true;
                           else if(!(as & 0x10) && in_window[7 + xs + x])
                              drawthis = true;

                           if(drawthis)
                           {
                              b_bg[xs + x + 7] = wsTileRow[x] | (b_bg[xs + x + 7] & 0x10);
                              b_bg_pal[xs + x + 7] = 8 + palette;
                           }
                        }
                     }

               }

            }
            else
            {
               for(int x = 0; x < 8; x++)
                  if(wsTileRow[x] || !(palette & 4))
                  {
                     if((as & 0x20) || !(b_bg[xs + x + 7] & 0x10))
                     {
                        bool drawthis = 0;

                        if(!(DispControl & 0x08))
                           drawthis = true;
                        else if((as & 0x10) && !in_window[7 + xs + x])
                           drawthis = true;
                        else if(!(as & 0x10) && in_window[7 + xs + x])
                           drawthis = true;

                        if(drawthis)
                           //if((as & 0x10) || in_window[7 + xs + x])
                        {
                           b_bg[xs + x + 7] = wsColors[wsMonoPal[8 + palette][wsTileRow[x]]] | (b_bg[xs + x + 7] & 0x10);
                        }
                     }
                  }

            }
         }
      }

   }	// End sprite drawing

   if(wsVMode)
   {
      if(pix_bytes == 2)
      {
         for(l=0;l<224;l++)
            ((uint16 *)target)[l] = ColorMap[wsCols[b_bg_pal[l+7]][b_bg[(l+7)]&0xf]];
      }
      else if(pix_bytes == 4)
      {
         for(l=0;l<224;l++)
            ((uint32 *)target)[l] = ColorMap[wsCols[b_bg_pal[l+7]][b_bg[(l+7)]&0xf]];
      }
   }
   else
   {
      if(pix_bytes == 2)
      {
         for(l=0;l<224;l++)
            ((uint16 *)target)[l] = ColorMapG[(b_bg[l+7])&15];
      }
      else if(pix_bytes == 4)
      {
         for(l=0;l<224;l++)
            ((uint32 *)target)[l] = ColorMapG[(b_bg[l+7])&15];
      }
   }
}


void WSwan_GfxReset(void)
{
   weppy = 0;
   wsLine=0;
   wsSetVideo(0,true);

   memset(SpriteTable, 0, sizeof(SpriteTable));
   SpriteCountCache[0] = SpriteCountCache[1] = 0;
   FrameWhichActive = false;
   DispControl = 0;
   BGColor = 0;
   LineCompare = 0xBB;
   SPRBase = 0;

   SpriteStart = 0;
   SpriteCount = 0;
   FGBGLoc = 0;

   FGx0 = 0;
   FGy0 = 0;
   FGx1 = 0;
   FGy1 = 0;
   SPRx0 = 0;
   SPRy0 = 0;
   SPRx1 = 0;
   SPRy1 = 0;

   BGXScroll = BGYScroll = 0;
   FGXScroll = FGYScroll = 0;
   LCDControl = 0;
   LCDIcons = 0;
   LCDVtotal = 158;

   BTimerControl = 0;
   HBTimerPeriod = 0;
   VBTimerPeriod = 0;

   HBCounter = 0;
   VBCounter = 0;


   for(int u0=0;u0<16;u0++)
      for(int u1=0;u1<16;u1++)
         wsCols[u0][u1]=0;

}

int WSwan_GfxStateAction(StateMem *sm, const unsigned load, const bool data_only)
{
   SFORMAT StateRegs[] =
   {
      SFVARN(wsMonoPal, "wsMonoPal"),
      SFVAR(wsColors),

      SFVAR(wsLine),

      SFVARN(SpriteTable[0], "SpriteTable"),
      SFVARN(SpriteTable[1], "SpriteTable1"),
      SFVARN(SpriteCountCache[0], "SpriteCountCache"),
      SFVARN(SpriteCountCache[1], "SpriteCountCache1"),
      SFVAR(FrameWhichActive),
      SFVAR(DispControl),
      SFVAR(BGColor),
      SFVAR(LineCompare),
      SFVAR(SPRBase),
      SFVAR(SpriteStart),
      SFVAR(SpriteCount),
      SFVAR(FGBGLoc),
      SFVAR(FGx0),
      SFVAR(FGy0),
      SFVAR(FGx1),
      SFVAR(FGy1),

      SFVAR(SPRx0),
      SFVAR(SPRy0),

      SFVAR(SPRx1),
      SFVAR(SPRy1),

      SFVAR(BGXScroll),
      SFVAR(BGYScroll),
      SFVAR(FGXScroll),
      SFVAR(FGYScroll),
      SFVAR(LCDControl),
      SFVAR(LCDIcons),
      SFVAR(LCDVtotal),

      SFVAR(BTimerControl),
      SFVAR(HBTimerPeriod),
      SFVAR(VBTimerPeriod),

      SFVAR(HBCounter),
      SFVAR(VBCounter),

      SFVAR(VideoMode),

      SFVAR(weppy),
      SFEND
   };

   if(!MDFNSS_StateAction(sm, load, data_only, StateRegs, "GFX", false))
      return(0);

   if(load)
   {
      if(load < 0x94100)
      {
         FrameWhichActive = 0;
         SpriteCountCache[1] = SpriteCountCache[0];
         memcpy(SpriteTable[1], SpriteTable[0], 0x80 * 4);

         if(weppy == 2)
            weppy = 3;
      }
      //
      //
      weppy %= 4;

      for(unsigned i = 0; i < 2; i++)
      {
         if(SpriteCountCache[i] > 0x80)
            SpriteCountCache[i] = 0x80;
      }

      for(unsigned i = 0; i < 16; i++)
         for(unsigned j = 0; j < 4; j++)
            wsMonoPal[i][j] &= 0x7;

      wsSetVideo(VideoMode >> 5, true);
   }

   return(1);
}
