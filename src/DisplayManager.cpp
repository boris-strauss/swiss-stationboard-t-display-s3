#include "DisplayManager.h"
#include "pin_config.h"
#include "line_colors_data.h"
#include "vbz_font.h"
#include <Preferences.h>

#define LCD_MODULE_CMD_1
TFT_eSPI tft = TFT_eSPI();

// ==========================================
// BUS ICON (16x22 pixels)
// ==========================================
const uint8_t bus_bitmap[] PROGMEM = {
  0x3F, 0xFC, 0x3F, 0xFC, 0xC0, 0x03, 0xC0, 0x03, 0xFF, 0xFF, 0xFF, 0xFF,
  0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03,
  0xC0, 0x03, 0xC0, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xCF, 0xF3, 0xCF, 0xF3,
  0x30, 0x0C, 0x30, 0x0C, 0x00, 0x00, 0x00, 0x00 
};

void initDisplay() {
    pinMode(PIN_POWER_ON, OUTPUT); digitalWrite(PIN_POWER_ON, HIGH);
    pinMode(PIN_LCD_BL, OUTPUT); digitalWrite(PIN_LCD_BL, HIGH);
    tft.begin();

    // Load saved rotation from NVS (default: 1 = original orientation)
    Preferences prefs;
    prefs.begin("settings", true);
    uint8_t savedRotation = prefs.getUChar("rotation", 1);
    prefs.end();
    if (savedRotation != 1 && savedRotation != 3) savedRotation = 1;
    tft.setRotation(savedRotation);

    tft.fillScreen(TFT_BLACK);
}

void toggleDisplayRotation() {
    Preferences prefs;
    prefs.begin("settings", false);
    uint8_t currentRotation = prefs.getUChar("rotation", 1);
    uint8_t newRotation = (currentRotation == 1) ? 3 : 1;
    prefs.putUChar("rotation", newRotation);
    prefs.end();
    tft.setRotation(newRotation);
    tft.fillScreen(TFT_BLACK);
}

// ==========================================
// STRING FORMATTING & UMLAUT MAPPING
// ==========================================
String formatDestination(String name, bool keepPrefix) {
    if (!keepPrefix) {
        if (name.startsWith("Z\xC3\xBCrich, ")) name.remove(0, 9);
        else if (name.startsWith("Zürich, ")) name.remove(0, 8);
        else if (name.startsWith("Z\xC3\xBCrich ")) name.remove(0, 8);
        else if (name.startsWith("Zürich ")) name.remove(0, 7);
    }
    
    name.trim(); 
    
    // Map UTF-8 Umlauts
    name.replace("\xC3\xA4", String((char)228)); // ä
    name.replace("\xC3\xB6", String((char)246)); // ö
    name.replace("\xC3\xBC", String((char)252)); // ü
    name.replace("\xC3\x84", String((char)196)); // Ä
    name.replace("\xC3\x96", String((char)214)); // Ö
    name.replace("\xC3\x9C", String((char)220)); // Ü
    name.replace("\xC3\xA0", String((char)224)); // à
    name.replace("\xC3\xA8", String((char)232)); // è
    name.replace("\xC3\xA9", String((char)233)); // é
    name.replace("\xC3\xAC", String((char)236)); // ì
    name.replace("\xC3\xB2", String((char)242)); // ò
    name.replace("\xC3\xB9", String((char)249)); // ù

    while (name.length() > 0 && (name.endsWith(",") || name.endsWith(" ") || name.endsWith("."))) {
        name.remove(name.length() - 1);
    }
    return name;
}

// ==========================================
// CUSTOM FONT ENGINE
// ==========================================
int getVBZTextWidth(String text, int scale) {
    int width = 0;
    for (int i = 0; i < text.length(); i++) {
        uint8_t c = (uint8_t)text[i];
        if (c == ' ') {
            width += 4 * scale; 
        } else {
            const VBZChar* charData = getVBZChar(c);
            if (charData != nullptr) width += (charData->width + 1) * scale;
            else width += 5 * scale; 
        }
    }
    if (width > 0) width -= (1 * scale); 
    return width;
}

void drawVBZString(String text, int x, int y, uint16_t color, int scale) {
    int cursorX = x;

    for (int i = 0; i < text.length(); i++) {
        uint8_t c = (uint8_t)text[i];
        
        if (c == ' ') {
            cursorX += 4 * scale; 
            continue;
        }

        const VBZChar* charData = getVBZChar(c);
        if (charData != nullptr) {
            for (int row = 0; row < charData->height; row++) {
                uint16_t rowData = pgm_read_word(&(charData->data[row]));
                for (int col = 0; col < charData->width; col++) {
                    if (rowData & (1 << (15 - col))) {
                        DISP_FILL_RECT(cursorX + (col * scale), y + (row * scale), scale, scale, color);
                    }
                }
            }
            cursorX += (charData->width + 1) * scale; 
        } else {
            cursorX += 5 * scale; 
        }
    }
}

// ==========================================
// CSV COLOR LOOKUP
// ==========================================
void getLineColors(String opName, String lineNum, uint16_t &bg, uint16_t &fg) {
    bg = TFT_BLACK; fg = TFT_ORANGE; // Default fallback
    opName.toLowerCase();
    int spaceIdx = opName.indexOf(' ');
    String cleanApiOp = (spaceIdx != -1) ? opName.substring(0, spaceIdx) : opName;
    
    for (size_t i = 0; i < NUM_TRANSIT_COLORS; i++) {
        if (lineNum.equalsIgnoreCase(TRANSIT_COLORS[i].lineNum)) {
            String csvOp = String(TRANSIT_COLORS[i].opName);
            csvOp.toLowerCase();
            if (csvOp.indexOf(cleanApiOp) != -1) {
                bg = TRANSIT_COLORS[i].bgColor; 
                fg = TRANSIT_COLORS[i].fgColor;
                return;
            }
        }
    }
}

// ==========================================
// STATUS MESSAGE
// ==========================================
void showStatus(String message) {
    DISP_FILL_SCREEN(TFT_BLACK);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    int fontHeight = 26;
    int numLines = 1;
    for (int i = 0; i < message.length(); i++) if (message[i] == '\n') numLines++;
    int startY = (LCD_DISP_H / 2) - ((numLines - 1) * fontHeight) / 2;
    int startIndex = 0, endIndex = message.indexOf('\n'), currentY = startY;
    while (endIndex != -1) {
        tft.drawString(message.substring(startIndex, endIndex), LCD_DISP_W / 2, currentY, 4);
        currentY += fontHeight; startIndex = endIndex + 1; endIndex = message.indexOf('\n', startIndex);
    }
    tft.drawString(message.substring(startIndex), LCD_DISP_W / 2, currentY, 4);
}

// ==========================================
// SMART LINE NAME FORMATTER HELPER
// ==========================================
String getDisplayLineNumber(const Departure& dep, bool& isSBBorLongDistance) {
    String dispNum = dep.number;
    
    isSBBorLongDistance = (dep.operatorName.indexOf("SBB") != -1) ||
                          dep.category.equalsIgnoreCase("IR") ||
                          dep.category.equalsIgnoreCase("IC") ||
                          dep.category.equalsIgnoreCase("RE") ||
                          dep.category.equalsIgnoreCase("ICE") ||
                          dep.category.equalsIgnoreCase("EC");

    if (isSBBorLongDistance) {
        if (dep.number.length() > 2) {
            dispNum = dep.category; 
        } else {
            if (dep.number.startsWith(dep.category)) {
                dispNum = dep.number; 
            } else {
                dispNum = dep.category + dep.number; 
            }
        }
    }

    if (dep.category.equalsIgnoreCase("S")) {
        if (dep.number.startsWith("S") || dep.number.startsWith("s")) {
            dispNum = dep.number; 
        } else {
            dispNum = dep.category + dep.number;
        }
    }
    
    return dispNum;
}

// ==========================================
// DRAW THE TIMETABLE
// ==========================================
void drawTimetable(const std::vector<Departure>& departures, const String& opMode) {
    DISP_FILL_SCREEN(TFT_BLACK);
    time_t now; time(&now);
    int rowHeight = 28;
    int fontScale = 2;

    bool trainMode = (opMode == "train");

    // --- PRE-CALCULATE MAXIMUM BOX WIDTH ---
    int maxBoxWidth = 48; // Base minimum width
    for (int i = 0; i < departures.size(); i++) {
        bool dummyFlag;
        String dispNum = getDisplayLineNumber(departures[i], dummyFlag);
        int numWidth = getVBZTextWidth(dispNum, fontScale);
        if (numWidth + 8 > maxBoxWidth) {
            maxBoxWidth = numWidth + 8;
        }
    }

    // --- PRE-CALCULATE TRAIN MODE RIGHT COLUMN WIDTHS ---
    // Reserve a fixed block for [delay] [time] so all rows are aligned
    int timeColW   = getVBZTextWidth("00:00", fontScale);       // HH:MM width
    int delayColW  = getVBZTextWidth("+99'", fontScale) + 6;    // reserve for up to +99 min
    int trainRightW = delayColW + 6 + timeColW;                 // total right block

    // --- DRAWING LOOP ---
    for (int i = 0; i < departures.size(); i++) {
        int yPos = i * rowHeight + 2; 
        const Departure& dep = departures[i];
        
        // ==========================================
        // 1. SMART LINE NUMBER FORMATTING & COLORS
        // ==========================================
        bool isSBBorLongDistance = false;
        String dispNum = getDisplayLineNumber(dep, isSBBorLongDistance);
        
        uint16_t bgColor = TFT_BLACK;
        uint16_t fgColor = TFT_ORANGE;
        
        getLineColors(dep.operatorName, dep.number, bgColor, fgColor);
        bool colorFound = !(bgColor == TFT_BLACK && fgColor == TFT_ORANGE);

        if (dep.operatorName.indexOf("PAG") != -1 && !colorFound) {
            bgColor = DISP_COLOR565(255, 204, 0);
            fgColor = TFT_BLACK;
        }

        if (isSBBorLongDistance) {
            bgColor = DISP_COLOR565(235, 0, 0);
            fgColor = TFT_WHITE;
        }

        if (dep.category.equalsIgnoreCase("S")) {
            bgColor = DISP_COLOR565(0, 51, 153);
            fgColor = TFT_WHITE;
        }

        DISP_FILL_RECT(2, yPos, maxBoxWidth, 26, bgColor);
        int numWidth = getVBZTextWidth(dispNum, fontScale);
        int textX = 2 + maxBoxWidth - 4 - numWidth;
        drawVBZString(dispNum, textX, yPos + 1, fgColor, fontScale);
        
        // ==========================================
        // 2. DESTINATION (clipped to available space)
        // ==========================================
        String cleanDest = formatDestination(dep.destination, isSBBorLongDistance);
        int destStartX = 2 + maxBoxWidth + 8;

        if (trainMode) {
            // ==========================================
            // TRAIN MODE: [dest] [+Nmin] [HH:MM]
            // ==========================================
            int maxDestW = LCD_RIGHT_EDGE - destStartX - trainRightW - 4;

            if (getVBZTextWidth(cleanDest, fontScale) > maxDestW) {
                while (getVBZTextWidth(cleanDest + "...", fontScale) > maxDestW && cleanDest.length() > 0) {
                    cleanDest.remove(cleanDest.length() - 1);
                }
                cleanDest += "...";
            }
            drawVBZString(cleanDest, destStartX, yPos + 1, TFT_ORANGE, fontScale);

            // Scheduled departure time (HH:MM)
            time_t schedTime = (time_t)dep.scheduledTimestamp;
            struct tm* ti = localtime(&schedTime);
            char timeBuf[6];
            sprintf(timeBuf, "%02d:%02d", ti->tm_hour, ti->tm_min);
            int timeX = LCD_RIGHT_EDGE - timeColW;
            drawVBZString(String(timeBuf), timeX, yPos + 1, TFT_ORANGE, fontScale);

            // Delay column (right-aligned within its slot, shown in yellow)
            if (dep.delayMinutes > 0) {
                String delayStr = "+" + String(dep.delayMinutes) + "'";
                int dw = getVBZTextWidth(delayStr, fontScale);
                int delayX = timeX - 6 - dw;
                drawVBZString(delayStr, delayX, yPos + 1, TFT_YELLOW, fontScale);
            }

        } else {
            // ==========================================
            // TRAM MODE: [dest] [Nmin | icon | HH:MM]
            // ==========================================
            int diffSeconds = dep.actualTimestamp - now;
            int diffMinutes = diffSeconds / 60;

            int rightElementWidth = 0;
            String rightText = "";
            bool drawIcon = false;

            if (diffMinutes <= 0) {
                drawIcon = true;
                rightElementWidth = 16; 
            } else if (diffMinutes < 30) {
                rightText = String(diffMinutes) + "'"; 
                rightElementWidth = getVBZTextWidth(rightText, fontScale);
            } else {
                time_t depTime = (time_t)dep.actualTimestamp;
                struct tm *timeinfo = localtime(&depTime);
                char timeBuf[10];
                sprintf(timeBuf, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
                rightText = String(timeBuf);
                rightElementWidth = getVBZTextWidth(rightText, fontScale);
            }

            int maxTextWidth = LCD_RIGHT_EDGE - rightElementWidth - 8 - destStartX;

            if (getVBZTextWidth(cleanDest, fontScale) > maxTextWidth) {
                while (getVBZTextWidth(cleanDest + "...", fontScale) > maxTextWidth && cleanDest.length() > 0) {
                    cleanDest.remove(cleanDest.length() - 1);
                }
                cleanDest += "...";
            }
            drawVBZString(cleanDest, destStartX, yPos + 1, TFT_ORANGE, fontScale);

            if (drawIcon) {
                DISP_DRAW_BITMAP(LCD_RIGHT_EDGE - rightElementWidth, yPos + 2, bus_bitmap, 16, 22, TFT_ORANGE);
            } else {
                drawVBZString(rightText, LCD_RIGHT_EDGE - rightElementWidth, yPos + 1, TFT_ORANGE, fontScale);
            }
        }
    }
}
