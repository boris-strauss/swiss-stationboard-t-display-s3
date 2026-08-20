#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "TransportAPI.h"

// ---- T-Display-S3: TFT_eSPI + ST7789 8-bit parallel ----
#include <TFT_eSPI.h>

#define LCD_DISP_W  320
#define LCD_DISP_H  170

extern TFT_eSPI tft;

#define DISP_FILL_SCREEN(color)           tft.fillScreen(color)
#define DISP_FILL_RECT(x,y,w,h,color)    tft.fillRect(x,y,w,h,color)
#define DISP_DRAW_BITMAP(x,y,bmp,w,h,c)  tft.drawBitmap(x,y,bmp,w,h,c)
#define DISP_COLOR565(r,g,b)             tft.color565(r,g,b)
#define DISP_SET_TEXT_COLOR(fg,bg)        tft.setTextColor(fg,bg)
#define DISP_SET_TEXT_DATUM(d)            tft.setTextDatum(d)
#define DISP_DRAW_STRING(s,x,y,f)        tft.drawString(s,x,y,f)

// Right edge used by the drawing code (2px margin)
#define LCD_RIGHT_EDGE  (LCD_DISP_W - 2)

void initDisplay();
void showStatus(String message);
void drawTimetable(const std::vector<Departure>& departures, const String& opMode);
void toggleDisplayRotation();

#endif // DISPLAY_MANAGER_H
