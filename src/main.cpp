#include <Arduino.h>
#include <SPI.h>            // <-- FORCE PLATFORMIO TO LINK SPI
#include <TFT_eSPI.h>       // <-- FORCE PLATFORMIO TO LINK TFT_eSPI
#include <WiFi.h>
#include "config.h"
#include "NetworkManager.h"
#include "TransportAPI.h"
#include "DisplayManager.h"
#include "pin_config.h"
#include "time.h"

#define BOOT_BUTTON_PIN PIN_BUTTON_1 // GPIO 0
#define PAGE_BUTTON_PIN 14

unsigned long lastApiCheck = 0;
std::vector<Departure> currentDepartures; 

bool showingPageTwo = false;
unsigned long pageTwoStartTime = 0;
bool lastPageButtonState = HIGH;

// BOOT button tracking
unsigned long bootButtonPressStartTime = 0;
bool isBootButtonPressed = false;
bool bootLongPressTriggered = false;

void updateDisplay() {
    if (currentDepartures.empty()) {
        showStatus("No Departures Found");
        return;
    }
    
    String opMode = getOperationMode();

    std::vector<Departure> pageData;
    int startIdx = showingPageTwo ? 6 : 0;
    
    if (startIdx >= currentDepartures.size()) {
        startIdx = 0;
        showingPageTwo = false; 
    }
    
    for (int i = startIdx; i < currentDepartures.size() && i < startIdx + 6; i++) {
        pageData.push_back(currentDepartures[i]);
    }
    
    drawTimetable(pageData, opMode);
}

void setup() {
    Serial.begin(115200);

    pinMode(PAGE_BUTTON_PIN, INPUT_PULLUP);
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    initDisplay();
    
    // This will either connect to saved WiFi or halt and open the AP
    setupNetwork(); 

    if (WiFi.status() == WL_CONNECTED) {
        showStatus("Syncing Time...");
        // Sets the time to Zurich (CET/CEST) with automatic Daylight Saving Time switching!
        configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
        delay(2000); 
        
        showStatus("Fetching Schedule...");
        currentDepartures = fetchDepartures(getTargetStation().c_str(), getOperationMode());
        updateDisplay();
    }
}

void loop() {
    // 1. Process Captive portal requests (if active)
    handleNetworkLoop();

    // 2. Boot Button Logic
    //    - Short press (< 3s): toggle display rotation 180°
    //    - Long press (>= 3s): open setup portal
    bool currentBootState = digitalRead(BOOT_BUTTON_PIN);
    if (currentBootState == LOW) {
        if (!isBootButtonPressed) {
            isBootButtonPressed = true;
            bootButtonPressStartTime = millis();
            bootLongPressTriggered = false;
        } else if (!bootLongPressTriggered && millis() - bootButtonPressStartTime > 3000) {
            // Button held for > 3 seconds -> open setup portal
            bootLongPressTriggered = true;
            triggerCaptivePortal();
        }
    } else {
        // Button released
        if (isBootButtonPressed && !bootLongPressTriggered) {
            unsigned long pressDuration = millis() - bootButtonPressStartTime;
            // Debounce: ignore presses shorter than 50ms
            if (pressDuration > 50) {
                toggleDisplayRotation();
                updateDisplay();
            }
        }
        isBootButtonPressed = false;
    }

    // Only run the API and display logic if we are connected to WiFi
    if (WiFi.status() == WL_CONNECTED) {
        
        // 3. Check API periodically
        if (millis() - lastApiCheck >= API_REFRESH_INTERVAL) {
            lastApiCheck = millis(); 
            currentDepartures = fetchDepartures(getTargetStation().c_str(), getOperationMode());
            updateDisplay();
        }

        // 4. Page Toggle Button Logic
        bool currentPageButtonState = digitalRead(PAGE_BUTTON_PIN);
        if (currentPageButtonState == LOW && lastPageButtonState == HIGH) {
            showingPageTwo = !showingPageTwo; 
            
            if (showingPageTwo) {
                pageTwoStartTime = millis(); 
            }
            updateDisplay();
        }
        lastPageButtonState = currentPageButtonState;

        // 5. Auto-revert to Page 1
        if (showingPageTwo && (millis() - pageTwoStartTime > 10000)) {
            showingPageTwo = false;
            updateDisplay();
        }
    }
    
    delay(50); 
}
