#include "OledFaceDisplay.h"
#include <Arduino.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

OledFaceDisplay::OledFaceDisplay(int sda, int scl) 
    : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET),
      sdaPin(sda), sclPin(scl), lastUpdate(0), isBlinking(false), blinkStartTime(0) {
}

void OledFaceDisplay::initialize() {
    Wire.begin(sdaPin, sclPin);

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed. Continuing without display."));
        return; // Don't halt
    }

    display.clearDisplay();
    display.display();
}

void OledFaceDisplay::drawFace(bool eyesClosed) {
    display.clearDisplay();

    if (eyesClosed) {
        // Closed eyes
        display.fillRoundRect(22, 32, 30, 4, 2, WHITE);
        display.fillRoundRect(76, 32, 30, 4, 2, WHITE);
    } else {
        // Open eyes
        display.fillRoundRect(22, 16, 30, 36, 12, WHITE);
        display.fillRoundRect(76, 16, 30, 36, 12, WHITE);
    }

    // Draw Smile
    for(int i = 0; i < 4; i++) {
        display.drawCircle(64, 40, 10 + i, WHITE);
    }
    
    // Mask out the top half of the circles
    display.fillRect(50, 20, 28, 21, BLACK); 
    
    // Rounded edges of the smile
    display.fillCircle(53, 41, 1.5, WHITE);
    display.fillCircle(75, 41, 1.5, WHITE);

    display.display();
}

void OledFaceDisplay::showIdle() {
    drawFace(false);
}

void OledFaceDisplay::showListening() {
    // To be implemented: Maybe wider eyes or a different expression
    drawFace(false); 
}

void OledFaceDisplay::showSpeaking() {
    // To be implemented: Maybe animating the mouth
    drawFace(false);
}

void OledFaceDisplay::triggerBlink() {
    if (!isBlinking) {
        isBlinking = true;
        blinkStartTime = millis();
        drawFace(true); // Close eyes
    }
}

void OledFaceDisplay::update() {
    if (isBlinking) {
        unsigned long currentMillis = millis();
        // Check if blink is finished
        if (currentMillis - blinkStartTime >= blinkDuration) {
            isBlinking = false;
            drawFace(false); // Open eyes
        }
    }
}
