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

void dchr(volatile uint16_t* srf,uint8_t* fnt,uint8_t w,uint8_t h,uint8_t x,uint8_t y,uint8_t s,uint16_t c,uint8_t f) {
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

void dtxt(volatile uint16_t* srf,uint8_t* fnt,uint8_t w,uint8_t h,uint8_t x,uint8_t y,uint8_t s,uint16_t c,char* txt) {
  char* l="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int xc=x,yc=y;
  for(int i=0;txt[i];i++) {
    int j=indexOf(l,txt[i]);
    if(j!=-1) {
      dchr(srf,fnt,w,h,xc,yc,s,c,j);
    }
    xc+=w*s;
    if(xc+w*s>=GBA_SCREEN_W) {
      yc+=h*s;
      if(yc+h*s>=GBA_SCREEN_H) break;
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

	int s=4,w=GBA_SCREEN_W/s,h=GBA_SCREEN_H/s;

	int hold=FALSE;

	struct Cursor {
		int x,y;
		int size;
		int dx,dy;
		int blinkDelay;
		int blinkCounter;
		int isVisible;
		int hold;
	};

	struct Cursor cursor = {w/2,h/2,s,0,0,5000,0,TRUE,FALSE};

	int isButtonAPressed = FALSE;
	int isButtonBPressed = FALSE;

	int isButtonUpPressed = FALSE;
	int isButtonDownPressed = FALSE;
	int isButtonLeftPressed = FALSE;
	int isButtonRightPressed = FALSE;

	int currentSweetieIndex = 12;
	
	frect(GBA_SCREEN,0,0,GBA_SCREEN_W,GBA_SCREEN_H,sweetie[0]);
	dtxt(GBA_SCREEN,font_pixels,font_width,font_height,0,0,1,sweetie[12],"PRESS START BUTTON");

	while(!button_pressed(BUTTON_START)) seed++;
	srand(seed);

	frect(GBA_SCREEN,0,0,GBA_SCREEN_W,GBA_SCREEN_H,sweetie[0]);

	frect(GBA_SCREEN,cursor.x*cursor.size,cursor.y*cursor.size,cursor.size,cursor.size,sweetie[currentSweetieIndex]);

    while(1) {

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

   		isButtonAPressed     = button_pressed(BUTTON_A);
		isButtonBPressed     = button_pressed(BUTTON_B);
		isButtonUpPressed    = button_pressed(BUTTON_UP);
		isButtonDownPressed  = button_pressed(BUTTON_DOWN);
		isButtonLeftPressed  = button_pressed(BUTTON_LEFT);
		isButtonRightPressed = button_pressed(BUTTON_RIGHT);

		if(!hold) {
			if(isButtonAPressed) {
				hold=TRUE;
				currentSweetieIndex++;
				if(currentSweetieIndex>=16) {
					currentSweetieIndex=0;
				}
				cursor.blinkCounter=0;
				cursor.isVisible=TRUE;
			}

			if(isButtonBPressed) {
				hold=TRUE;
				currentSweetieIndex=0;
				cursor.blinkCounter=0;
				cursor.isVisible=TRUE;
			}

		} else if(!(isButtonAPressed || isButtonBPressed)) {
			hold=FALSE;
		}

		if(!cursor.hold && (isButtonAPressed || isButtonBPressed || isButtonUpPressed || isButtonDownPressed || isButtonLeftPressed || isButtonRightPressed)) {

			frect(GBA_SCREEN,cursor.x*cursor.size,cursor.y*cursor.size,cursor.size,cursor.size,sweetie[currentSweetieIndex]);

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

			if(cursor.x<0) cursor.x=w-1;
			if(cursor.x>=w) cursor.x=0;

			if(cursor.y<0) cursor.y=h-1;
			if(cursor.y>=h) cursor.y=0;

			frect(GBA_SCREEN,cursor.x*cursor.size,cursor.y*cursor.size,cursor.size,cursor.size,sweetie[currentSweetieIndex]);
			
		} else if(!(isButtonAPressed || isButtonUpPressed || isButtonDownPressed || isButtonLeftPressed || isButtonRightPressed)) {
			cursor.hold=FALSE;
		}

    }

    return 0;
}
