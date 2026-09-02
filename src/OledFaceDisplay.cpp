#include "OledFaceDisplay.h"
#include <Arduino.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Eye centers (screen is 128 wide, so eyes sit symmetrically around x=64).
#define EYE_L_CX 37
#define EYE_R_CX 91

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

    randomSeed(micros());   // vary the idle blink/wander timing
    display.clearDisplay();
    display.display();
}

// Renders the whole face for the given expression. `blinkClosed` forces the
// eyes shut for the blink animation (overrides the expression's eyes).
// The eyes are offset by gazeX/gazeY so idle "wander" can look around.
void OledFaceDisplay::drawFace(Expression expr, bool blinkClosed) {
    display.clearDisplay();

    const bool eyesClosed = blinkClosed || expr == Expression::Thinking;
    const int ox = gazeX;
    const int oy = gazeY;

    // NOTE: draw the mouth FIRST, then the eyes on top. The smile is rendered
    // as full circles with the top half erased by a black rectangle; that mask
    // used to reach up into the eyes and bite black chunks out of their lower
    // inner corners. Drawing the eyes last means they repaint over any part of
    // the mask that overlaps them, while the smile arc (which sits low, y >= 52)
    // is never touched by the eyes.

    // ---------- Mouth (sits low, well clear of the eyes) ----------
    if (expr == Expression::Thinking) {
        // Small neutral line (concentrating).
        display.fillRoundRect(54, 50, 20, 3, 1, WHITE);
    } else {
        // Smile for everything else (normal, listening, happy/speaking).
        for (int i = 0; i < 4; i++) {
            display.drawCircle(64, 51, 9 + i, WHITE);
        }
        display.fillRect(48, 38, 32, 14, BLACK);       // mask top half of arcs
        display.fillCircle(53, 52, 2, WHITE);          // rounded smile corners
        display.fillCircle(75, 52, 2, WHITE);
    }

    // ---------- Eyes (kept in the upper half so the mouth has room) ----------
    if (eyesClosed) {
        // Thin bars = shut eyes (sending / blink).
        display.fillRoundRect(EYE_L_CX - 15 + ox, 22 + oy, 30, 5, 2, WHITE);
        display.fillRoundRect(EYE_R_CX - 15 + ox, 22 + oy, 30, 5, 2, WHITE);
    } else if (expr == Expression::Happy) {
        // Happy "u"-shaped (smiling) eyes: bottom arc of a circle.
        for (int i = 0; i < 3; i++) {
            display.drawCircle(EYE_L_CX + ox, 24 + oy, 10 + i, WHITE);
            display.drawCircle(EYE_R_CX + ox, 24 + oy, 10 + i, WHITE);
        }
        display.fillRect(EYE_L_CX - 14 + ox, 10 + oy, 28, 14, BLACK); // erase top -> u
        display.fillRect(EYE_R_CX - 14 + ox, 10 + oy, 28, 14, BLACK);
    } else if (expr == Expression::Listening) {
        // Wide-open eyes (attentive) - rounded rectangle, taller than normal.
        display.fillRoundRect(EYE_L_CX - 17 + ox, 6 + oy, 34, 40, 10, WHITE);
        display.fillRoundRect(EYE_R_CX - 17 + ox, 6 + oy, 34, 40, 10, WHITE);
    } else {
        // Normal open eyes - rounded rectangle (small corner radius so it reads
        // as a rectangle, not a circle).
        display.fillRoundRect(EYE_L_CX - 15 + ox, 8 + oy, 30, 34, 8, WHITE);
        display.fillRoundRect(EYE_R_CX - 15 + ox, 8 + oy, 30, 34, 8, WHITE);
    }

    display.display();
}

// ---------------------------------------------------------------------------
// Boot / connectivity screens
// ---------------------------------------------------------------------------

// A small ring of dots with one bright "head" that rotates with `frame`,
// giving a classic loading-spinner look on the mono OLED.
void OledFaceDisplay::drawSpinner(int cx, int cy, int r, int frame) {
    const int N = 8;
    for (int i = 0; i < N; i++) {
        float a = (float)i / N * 2.0f * PI;
        int x = cx + (int)(cosf(a) * r);
        int y = cy + (int)(sinf(a) * r);
        if (i == (frame % N)) {
            display.fillCircle(x, y, 2, WHITE);   // bright head
        } else {
            display.drawPixel(x, y, WHITE);       // faint trail dots
        }
    }
}

void OledFaceDisplay::drawConnecting() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 6);
    display.print("Connecting to:");
    display.setCursor(0, 20);
    display.print(bootSsid);
    drawSpinner(64, 50, 8, spinnerFrame);
    display.display();
}

void OledFaceDisplay::drawConnected() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 10);
    display.print("Connected");
    display.setCursor(0, 32);
    display.print("IP Address:");
    display.setCursor(0, 46);
    display.print(bootIp);
    display.display();
}

void OledFaceDisplay::drawSleeping() {
    // Closed eyes gently bob up and down (a slow ~0.6 Hz sine) while a spinner
    // in the top-right corner shows we're still actively trying to connect.
    int oy = (int)(3.0f * sinf(millis() / 300.0f));

    display.clearDisplay();
    // Thin bars = closed/sleeping eyes.
    display.fillRoundRect(EYE_L_CX - 15, 30 + oy, 30, 5, 2, WHITE);
    display.fillRoundRect(EYE_R_CX - 15, 30 + oy, 30, 5, 2, WHITE);
    // "zZ" sleep cue above the right eye.
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(EYE_R_CX + 14, 18 + oy);
    display.print("z");
    // Corner spinner (still working on it).
    drawSpinner(118, 10, 5, spinnerFrame);
    display.display();
}

void OledFaceDisplay::showConnecting(const char* ssid) {
    screen = Screen::Connecting;
    bootSsid = ssid ? ssid : "";
    spinnerFrame = 0;
    nextSpinnerAt = millis();
    drawConnecting();
}

void OledFaceDisplay::showConnected(const char* ip) {
    screen = Screen::Connected;
    bootIp = ip ? ip : "";
    drawConnected();
}

void OledFaceDisplay::showSleeping() {
    screen = Screen::Sleeping;
    spinnerFrame = 0;
    nextSpinnerAt = millis();
    drawSleeping();
}

// Arm the next idle blink and gaze-shift at randomized intervals.
void OledFaceDisplay::scheduleIdleAnim() {
    unsigned long now = millis();
    nextBlinkAt  = now + random(2500, 6000);
    nextWanderAt = now + random(1500, 4000);
}

void OledFaceDisplay::showIdle() {
    screen = Screen::Face;
    currentExpr = Expression::Normal;
    gazeX = 0;
    gazeY = 0;
    scheduleIdleAnim();
    drawFace(currentExpr, false);
}

void OledFaceDisplay::showListening() {
    screen = Screen::Face;
    currentExpr = Expression::Listening;
    gazeX = 0;  // eyes centered while engaged
    gazeY = 0;
    drawFace(currentExpr, false);
}

void OledFaceDisplay::showThinking() {
    screen = Screen::Face;
    currentExpr = Expression::Thinking;   // eyes stay shut while sending
    gazeX = 0;
    gazeY = 0;
    drawFace(currentExpr, false);
}

void OledFaceDisplay::showSpeaking() {
    screen = Screen::Face;
    currentExpr = Expression::Happy;
    gazeX = 0;
    gazeY = 0;
    drawFace(currentExpr, false);
}

void OledFaceDisplay::triggerBlink() {
    if (!isBlinking) {
        isBlinking = true;
        blinkStartTime = millis();
        drawFace(currentExpr, true); // blink the current expression's eyes shut
    }
}

void OledFaceDisplay::shakeEyes() {
    // Quick horizontal shake (~0.5s) of the normal face to signal "huh?".
    // Blocking, like playDingDong - it's a one-shot gesture before going idle.
    currentExpr = Expression::Normal;
    const int amp = 6;
    for (int i = 0; i < 4; i++) {
        gazeX = -amp; drawFace(currentExpr, false); delay(60);
        gazeX =  amp; drawFace(currentExpr, false); delay(60);
    }
    gazeX = 0;
    drawFace(currentExpr, false);
}

void OledFaceDisplay::update() {
    unsigned long now = millis();

    // Boot screens animate independently of the face.
    if (screen == Screen::Connecting) {
        if (now >= nextSpinnerAt) {
            spinnerFrame++;
            nextSpinnerAt = now + 100;
            drawConnecting();
        }
        return;
    }
    if (screen == Screen::Sleeping) {
        // Redraw every ~50 ms so the eyes bob smoothly; advance spinner slower.
        if (now >= nextSpinnerAt) { spinnerFrame++; nextSpinnerAt = now + 100; }
        drawSleeping();
        return;
    }
    if (screen == Screen::Connected) return;   // static screen

    // Finish an in-progress blink (any expression).
    if (isBlinking) {
        if (now - blinkStartTime >= blinkDuration) {
            isBlinking = false;
            drawFace(currentExpr, false); // restore current expression + gaze
        }
        return;
    }

    // Ambient animation only when idle.
    if (currentExpr != Expression::Normal) return;

    // Auto-blink.
    if (now >= nextBlinkAt) {
        isBlinking = true;
        blinkStartTime = now;
        drawFace(currentExpr, true);
        nextBlinkAt = now + random(2500, 6000);
        return;
    }

    // Gaze wander: mostly center, occasionally glance around.
    if (now >= nextWanderAt) {
        switch (random(0, 5)) {
            case 0: gazeX = -8; gazeY = 0;  break; // left
            case 1: gazeX =  8; gazeY = 0;  break; // right
            case 2: gazeX =  0; gazeY = -4; break; // up
            default: gazeX = 0; gazeY = 0;  break; // center (biased)
        }
        drawFace(currentExpr, false);
        nextWanderAt = now + random(1500, 4000);
    }
}
