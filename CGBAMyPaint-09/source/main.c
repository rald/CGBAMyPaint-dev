#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "palette.h"
#include "font.h"

#define FALSE 0
#define TRUE 1

#define GBA_SCREEN_W            240
#define GBA_SCREEN_H            160
#define GBA_SCREEN_S            (GBA_SCREEN_W*GBA_SCREEN_H)
#define GBA_SCREEN              ((uint16_t *)0x06000000)

#define REG_DISPCNT             *((volatile uint16_t *)0x04000000)

#define DISPCNT_BG_MODE_MASK    (0x7)
#define DISPCNT_BG_MODE(n)      ((n) & DISPCNT_BG_MODE_MASK) // 0 to 5

#define DISPCNT_BG2_ENABLE      (1 << 10)

#define MEM_VRAM_MODE3_FB       ((uint16_t *)0x06000000)

#define SRAM_BASE ((volatile uint8_t*) 0x0E000000)

volatile unsigned short* buttons = (volatile unsigned short*) 0x04000130;

#define BUTTON_A (1 << 0)
#define BUTTON_B (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START (1 << 3)
#define BUTTON_RIGHT (1 << 4)
#define BUTTON_LEFT (1 << 5)
#define BUTTON_UP (1 << 6)
#define BUTTON_DOWN (1 << 7)
#define BUTTON_R (1 << 8)
#define BUTTON_L (1 << 9)

#define BITMAP_PIXEL_SIZE 4
#define BITMAP_W (GBA_SCREEN_W/BITMAP_PIXEL_SIZE)
#define BITMAP_H (GBA_SCREEN_H/BITMAP_PIXEL_SIZE)
#define BITMAP_S (BITMAP_W*BITMAP_H)

// Force the SRAM backup ID into the rodata section with explicit alignment or positioning
const char sram_backup_id[] __attribute__((section(".rodata"))) = "SRAM_V113";

struct Bitmap {
	uint8_t pixelSize;
	uint8_t width,height;
	uint8_t pixels[BITMAP_W*BITMAP_H];
};

struct Cursor {
	int x,y;
	int size;
	int dx,dy;
	int blinkDelay;
	int blinkCounter;
	int isVisible;
	int hold;
};

void save(struct Bitmap *bitmap,uint8_t slot) {
	volatile uint8_t* dest = SRAM_BASE + (slot * sizeof(struct Bitmap));
	dest[0]=bitmap->pixelSize;
	dest[1]=bitmap->width;
	dest[2]=bitmap->height;
	for(int i=0;i<bitmap->width*bitmap->height;i++) {
		dest[i+3]=bitmap->pixels[i];
	}
}

void load(struct Bitmap *bitmap,uint8_t slot) {
	volatile uint8_t* dest = SRAM_BASE + (slot * sizeof(struct Bitmap));
	bitmap->pixelSize=dest[0];
	bitmap->width=dest[1];
	bitmap->height=dest[2];
	for(int i=0;i<bitmap->width*bitmap->height;i++) {
		bitmap->pixels[i]=dest[i+3];
	}
}

unsigned char button_pressed(unsigned short button) {
  unsigned short pressed = *buttons & button;
  if (pressed == 0) {
    return TRUE;
  } else {
    return FALSE;
  }
}

#define REG_VCOUNT (* (volatile uint16_t*) 0x04000006)
void vsync(void) {
  while (REG_VCOUNT >= 160);
  while (REG_VCOUNT < 160);
}

static inline uint16_t RGB15(uint16_t r, uint16_t g, uint16_t b) {
    return (r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10);
}

void pset(volatile uint16_t* srf, uint8_t x, uint8_t y, uint16_t c) {
  srf[y*GBA_SCREEN_W+x]=c;
}

uint16_t pget(volatile uint16_t* srf,uint8_t x,uint8_t y) {
  return srf[y*GBA_SCREEN_W+x];
}

void frect(volatile uint16_t* srf,uint8_t x,uint8_t y,uint8_t w,uint8_t h,uint16_t c) {
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
          pset(srf,x+i,y+j,c);
        }
    }
}

void drect(volatile uint16_t* srf,uint8_t x,uint8_t y,uint8_t w,uint8_t h,uint16_t c) {
  for(int i=x;i<x+w;i++) {
    pset(srf,i,y,c);
    pset(srf,i,y+h-1,c);
  }
  for(int j=y;j<y+h;j++) {
    pset(srf,x,j,c);
    pset(srf,x+h-1,j,c);
  }
}

void dbmp(volatile uint16_t* srf,uint8_t* bmp,uint8_t w,uint8_t h,uint8_t x,uint8_t y,uint8_t t,uint8_t f) {
  for(int j=0;j<h;j++) {
    for(int i=0;i<w;i++) {
      int k=bmp[i+j*w+f*w*h];
      if(k!=t) {
        pset(srf,x+i,y+j,sweetie[k]);    
      }
    }    
  }
}

void dchr(volatile uint16_t* srf,uint8_t* fnt,uint8_t w,uint8_t h,uint8_t f,uint8_t x,uint8_t y,uint8_t s,uint16_t c) {
  for(int j=0;j<h;j++) {
    for(int i=0;i<w;i++) {
      int k=fnt[i+j*w+f*w*h];
      if(k!=0) {
        frect(srf,x+i*s,y+j*s,s,s,c);    
      }
    }    
  }
}

int indexOf(char* l,uint8_t c) {
  int j=-1;
  for(int i=0;l[i];i++) {
    if(l[i]==c) {
      j=i;
      break;
    }
  }
  return j;
}

void dtxt(volatile uint16_t* srf,uint8_t* fnt,uint8_t w,uint8_t h,char* txt,uint8_t x,uint8_t y,uint8_t s,uint16_t c) {
  char* l="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int xc=x,yc=y;
  for(int i=0;txt[i];i++) {
    int j=indexOf(l,txt[i]);
    if(j!=-1) {
      dchr(srf,fnt,w,h,j,xc,yc,s,c);
    }
    xc+=w*s;
    if(xc+w*s>GBA_SCREEN_W) {
      xc=0;
      yc+=h*s;
      if(yc+h*s>GBA_SCREEN_H) break;
    }
  }
}

void bget(volatile uint16_t *srf,uint8_t x,uint8_t y,uint8_t w,uint8_t h) {
  for(int j=0;j<h;j++) {
    for(int i=0;i<w;i++) {
      srf[i+j*w]=pget(GBA_SCREEN,x+i,y+j);
    }
  }  
}

void bput(volatile uint16_t* srf,uint8_t x,uint8_t y,uint8_t w,uint8_t h) {
  for(int j=0;j<h;j++) {
    for(int i=0;i<w;i++) {
      pset(GBA_SCREEN,x+i,y+j,srf[i+j*w]);
    }
  }
}

int sgn(int x) {
  return x<0?-1:x>0?1:0;
}

int main(void) {
    REG_DISPCNT = DISPCNT_BG_MODE(3) | DISPCNT_BG2_ENABLE;

	uint16_t seed=0;
	
	struct Bitmap bitmap;

	bitmap.pixelSize=BITMAP_PIXEL_SIZE;
	bitmap.width=BITMAP_W;
	bitmap.height=BITMAP_H;

	for(int i=0;i<BITMAP_S;i++) {
		bitmap.pixels[i]=0;
	}

	struct Cursor cursor = {
		bitmap.width/2,
		bitmap.height/2,
		bitmap.pixelSize,
		0,0,10000,0,TRUE,FALSE
	};

	int isButtonAPressed = FALSE;
	int isButtonBPressed = FALSE;
	int isButtonLPressed = FALSE;
	int isButtonRPressed = FALSE;

	int isButtonUpPressed = FALSE;
	int isButtonDownPressed = FALSE;
	int isButtonLeftPressed = FALSE;
	int isButtonRightPressed = FALSE;

	int currentSweetieIndex = 12;

	int hold=FALSE;
	int draw = FALSE;
		
	frect(GBA_SCREEN,0,0,GBA_SCREEN_W,GBA_SCREEN_H,sweetie[0]);

	int py=0;

	dtxt(GBA_SCREEN,font_pixels,font_width,font_height,"HOW TO USE",0,py,1,sweetie[12]);

	py+=font_height*2;

	dtxt(GBA_SCREEN,font_pixels,font_width,font_height,"ARROW KEYS MOVE CURSOR",0,py,1,sweetie[12]);

	py+=font_height;

	dtxt(GBA_SCREEN,font_pixels,font_width,font_height,"A BUTTON CHANGE COLOR",0,py,1,sweetie[12]);

	py+=font_height;

	dtxt(GBA_SCREEN,font_pixels,font_width,font_height,"B BUTTON TOGGLE DRAW",0,py,1,sweetie[12]);

	py+=font_height;

	dtxt(GBA_SCREEN,font_pixels,font_width,font_height,"L BUTTON LOAD",0,py,1,sweetie[12]);

	py+=font_height;

	dtxt(GBA_SCREEN,font_pixels,font_width,font_height,"R BUTTON SAVE",0,py,1,sweetie[12]);

	py+=font_height*2;

	dtxt(GBA_SCREEN,font_pixels,font_width,font_height,"PRESS START BUTTON TO CONTINUE",0,py,1,sweetie[12]);

	while(!button_pressed(BUTTON_START)) seed++;
	srand(seed);

	frect(GBA_SCREEN,0,0,GBA_SCREEN_W,GBA_SCREEN_H,sweetie[0]);

    while(1) {

   		isButtonAPressed     = button_pressed(BUTTON_A);
		isButtonBPressed     = button_pressed(BUTTON_B);
   		isButtonLPressed     = button_pressed(BUTTON_L);
		isButtonRPressed     = button_pressed(BUTTON_R);
		isButtonUpPressed    = button_pressed(BUTTON_UP);
		isButtonDownPressed  = button_pressed(BUTTON_DOWN);
		isButtonLeftPressed  = button_pressed(BUTTON_LEFT);
		isButtonRightPressed = button_pressed(BUTTON_RIGHT);

		if(cursor.blinkCounter>=cursor.blinkDelay) {
			cursor.blinkCounter=0;
			cursor.isVisible=!cursor.isVisible;
			if(cursor.isVisible) {
				frect(GBA_SCREEN,cursor.x*cursor.size,cursor.y*cursor.size,cursor.size,cursor.size,sweetie[currentSweetieIndex]);
			} else {
				frect(GBA_SCREEN,cursor.x*cursor.size,cursor.y*cursor.size,cursor.size,cursor.size,sweetie[0]);
				drect(GBA_SCREEN,cursor.x*cursor.size,cursor.y*cursor.size,cursor.size,cursor.size,sweetie[12]);
			}
		}
		cursor.blinkCounter++;

		if(!hold) {

			if(isButtonAPressed) {
				hold=TRUE;
				currentSweetieIndex++;
				if(currentSweetieIndex>=16) {
					currentSweetieIndex=0;
				}
				cursor.blinkCounter=0;
				cursor.isVisible=FALSE;
				if(draw) {
					bitmap.pixels[cursor.y*bitmap.width+cursor.x]=currentSweetieIndex;
					frect(GBA_SCREEN,cursor.x*cursor.size,cursor.y*cursor.size,cursor.size,cursor.size,sweetie[currentSweetieIndex]);
				}
			}

			if(isButtonBPressed) {
				hold=TRUE;
				draw=!draw;
				if(draw) {
					bitmap.pixels[cursor.y*bitmap.width+cursor.x]=currentSweetieIndex;
					frect(GBA_SCREEN,cursor.x*cursor.size,cursor.y*cursor.size,cursor.size,cursor.size,sweetie[currentSweetieIndex]);
				}
			}

			if(isButtonLPressed) {
				hold=TRUE;
				load(&bitmap,0);

				frect(GBA_SCREEN,0,0,GBA_SCREEN_W,GBA_SCREEN_H,sweetie[0]);		
				for(int i=0;i<bitmap.width*bitmap.height;i++) {
					frect(GBA_SCREEN,(i%bitmap.width)*bitmap.pixelSize,(i/bitmap.width)*bitmap.pixelSize,bitmap.pixelSize,bitmap.pixelSize,sweetie[bitmap.pixels[i]]);
				}
			}

			if(isButtonRPressed) {
				hold=TRUE;
				save(&bitmap,0);
			}

		} else if(!(isButtonAPressed || isButtonBPressed || isButtonLPressed || isButtonRPressed)) {
			hold=FALSE;
		}

		if(!cursor.hold && (isButtonUpPressed || isButtonDownPressed || isButtonLeftPressed || isButtonRightPressed)) {

			frect(GBA_SCREEN,cursor.x*cursor.size,cursor.y*cursor.size,cursor.size,cursor.size,sweetie[bitmap.pixels[cursor.y*bitmap.width+cursor.x]]);

			cursor.dx=0;
			cursor.dy=0;

			if(isButtonUpPressed) {
				cursor.hold=TRUE;
				cursor.dy=-1; 
			}

			if(isButtonDownPressed) {
				cursor.hold=TRUE;
				cursor.dy=1; 
			}

			if(isButtonLeftPressed) {
				cursor.hold=TRUE;
				cursor.dx=-1;
			}

			if(isButtonRightPressed) {
				cursor.hold=TRUE;
				cursor.dx=1;
			}

			cursor.x+=cursor.dx;
			cursor.y+=cursor.dy;

			if(cursor.x<0) cursor.x=bitmap.width-1;
			if(cursor.x>=bitmap.width) cursor.x=0;

			if(cursor.y<0) cursor.y=bitmap.height-1;
			if(cursor.y>=bitmap.height) cursor.y=0;

			if(draw) {
				bitmap.pixels[cursor.y*bitmap.width+cursor.x]=currentSweetieIndex;
				frect(GBA_SCREEN,cursor.x*cursor.size,cursor.y*cursor.size,cursor.size,cursor.size,sweetie[currentSweetieIndex]);
			}

			
		} else if(!(isButtonUpPressed || isButtonDownPressed || isButtonLeftPressed || isButtonRightPressed)) {
			cursor.hold=FALSE;
		}

    }

    return 0;
}
