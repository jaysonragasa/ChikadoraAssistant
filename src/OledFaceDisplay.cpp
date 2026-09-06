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
// The whole face (eyes AND mouth) is offset by gazeX/gazeY so idle "wander"
// looks around cohesively instead of the mouth staying pinned at center.
void OledFaceDisplay::drawFace(Expression expr, bool blinkClosed) {
    display.clearDisplay();

    const bool eyesClosed = blinkClosed || expr == Expression::Thinking;
    const int ox = (int)lroundf(gazeX);
    const int oy = (int)lroundf(gazeY);

    // NOTE: draw the mouth FIRST, then the eyes on top. The smile is rendered
    // as full circles with the top half erased by a black rectangle; that mask
    // used to reach up into the eyes and bite black chunks out of their lower
    // inner corners. Drawing the eyes last means they repaint over any part of
    // the mask that overlaps them, while the smile arc (which sits low, y >= 52)
    // is never touched by the eyes.

    // Eye geometry (configurable). Eyes centered around x=64, at vertical
    // center eyeCY; each sits `eyeGap` px from center. With the mouth shown the
    // eyes sit high to leave room for it; with it hidden they center on the
    // full 64px screen height.
    const int eyeCY = showMouth ? 25 : 32;
    const int cxL = 64 - eyeGap;
    const int cxR = 64 + eyeGap;

    // ---------- Mouth (sits low; drawn FIRST so eyes render on top) ----------
    // Offset by ox/oy too so the mouth drifts with the eyes during idle wander.
    if (showMouth) {
        if (expr == Expression::Thinking) {
            // Small neutral line (concentrating).
            display.fillRoundRect(54 + ox, 50 + oy, 20, 3, 1, WHITE);
        } else {
            // Smile for everything else (normal, listening, happy/speaking).
            for (int i = 0; i < 4; i++) {
                display.drawCircle(64 + ox, 51 + oy, 9 + i, WHITE);
            }
            display.fillRect(48 + ox, 38 + oy, 32, 14, BLACK);   // mask top half of arcs
            display.fillCircle(53 + ox, 52 + oy, 2, WHITE);      // rounded smile corners
            display.fillCircle(75 + ox, 52 + oy, 2, WHITE);
        }
    }

    // ---------- Server-offline badge (top-right, BEHIND the eyes) ----------
    if (serverWarning) {
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(104, 1);
        display.print("!TTS");
    }

    // ---------- Status overlays (top-left, BEHIND the eyes) ----------
    if (showLoopTime || showFreeMem) {
        display.setTextSize(1);
        display.setTextColor(WHITE);
        int ty = 1;
        if (showLoopTime) {
            display.setCursor(0, ty);
            display.print((int)lroundf(loopHz));
            display.print("Hz");
            ty += 9;
        }
        if (showFreeMem) {
            display.setCursor(0, ty);
            display.print(freeHeap / 1024);
            display.print("K");
        }
    }

    // ---------- Eyes ----------
    if (eyesClosed) {
        // Thin bars = shut eyes (sending / blink).
        const int barH = 5;
        display.fillRoundRect(cxL - eyeWidth / 2 + ox, eyeCY - barH / 2 + oy, eyeWidth, barH, 2, WHITE);
        display.fillRoundRect(cxR - eyeWidth / 2 + ox, eyeCY - barH / 2 + oy, eyeWidth, barH, 2, WHITE);
    } else if (expr == Expression::Happy) {
        // Happy "u"-shaped (smiling) eyes: bottom arc of a circle.
        int r = eyeWidth / 3; if (r < 6) r = 6;
        for (int i = 0; i < 3; i++) {
            display.drawCircle(cxL + ox, eyeCY + oy, r + i, WHITE);
            display.drawCircle(cxR + ox, eyeCY + oy, r + i, WHITE);
        }
        const int mw = 2 * (r + 3);
        display.fillRect(cxL - (r + 3) + ox, eyeCY - (r + 3) + oy, mw, r + 3, BLACK); // erase top -> u
        display.fillRect(cxR - (r + 3) + ox, eyeCY - (r + 3) + oy, mw, r + 3, BLACK);
    } else {
        // Normal / listening: rounded rectangles from the configured geometry.
        int w = eyeWidth, h = eyeHeight, rr = eyeRoundness;
        if (expr == Expression::Listening) { w += 4; h += 6; if (rr < 10) rr = 10; }
        int maxr = (w < h ? w : h) / 2;   // Adafruit GFX needs r <= min(w,h)/2
        if (rr > maxr) rr = maxr;
        display.fillRoundRect(cxL - w / 2 + ox, eyeCY - h / 2 + oy, w, h, rr, WHITE);
        display.fillRoundRect(cxR - w / 2 + ox, eyeCY - h / 2 + oy, w, h, rr, WHITE);
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

void OledFaceDisplay::drawConnectingServer() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 4);
    display.print("Connecting to");
    display.setCursor(0, 17);
    display.print("TTS server...");
    display.setCursor(0, 30);
    display.print(bootHost);
    drawSpinner(64, 53, 7, spinnerFrame);
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

void OledFaceDisplay::showConnectingServer(const char* host) {
    screen = Screen::ConnectingServer;
    bootHost = host ? host : "";
    spinnerFrame = 0;
    nextSpinnerAt = millis();
    drawConnectingServer();
}

void OledFaceDisplay::showSleeping() {
    screen = Screen::Sleeping;
    spinnerFrame = 0;
    nextSpinnerAt = millis();
    drawSleeping();
}

static const char* categoryLabel(WeatherCategory c) {
    switch (c) {
        case WeatherCategory::Clear:  return "Clear";
        case WeatherCategory::Clouds: return "Cloudy";
        case WeatherCategory::Rain:   return "Rain";
        case WeatherCategory::Snow:   return "Snow";
        case WeatherCategory::Storm:  return "Storm";
        case WeatherCategory::Fog:    return "Fog";
    }
    return "";
}

// Draw a small animated weather icon centered at (cx, cy). `size` ~ icon
// diameter; `frame` advances the animation.
void OledFaceDisplay::drawWeatherIcon(int cx, int cy, int size, WeatherCategory cat, int frame) {
    int r = size / 2;
    switch (cat) {
        case WeatherCategory::Clear: {
            // Sun with slowly rotating rays.
            for (int k = 0; k < 8; k++) {
                float a = k * (PI / 4.0f) + frame * 0.15f;
                int x1 = cx + (int)(cosf(a) * (r + 2)), y1 = cy + (int)(sinf(a) * (r + 2));
                int x2 = cx + (int)(cosf(a) * (r + 5)), y2 = cy + (int)(sinf(a) * (r + 5));
                display.drawLine(x1, y1, x2, y2, WHITE);
            }
            display.fillCircle(cx, cy, r, WHITE);
            break;
        }
        case WeatherCategory::Clouds: {
            int dx = (frame % 6) - 3;   // gentle drift
            display.fillCircle(cx - 4 + dx, cy + 2, r - 1, WHITE);
            display.fillCircle(cx + 4 + dx, cy + 2, r - 2, WHITE);
            display.fillCircle(cx + dx,     cy - 2, r - 2, WHITE);
            break;
        }
        case WeatherCategory::Rain: {
            display.fillCircle(cx - 3, cy - 2, r - 2, WHITE);
            display.fillCircle(cx + 3, cy - 2, r - 3, WHITE);
            for (int k = 0; k < 3; k++) {
                int rx = cx - 4 + k * 4;
                int ry = cy + 2 + ((frame * 2 + k * 3) % 8);
                display.drawLine(rx, ry, rx, ry + 3, WHITE);
            }
            break;
        }
        case WeatherCategory::Snow: {
            display.fillCircle(cx - 3, cy - 2, r - 2, WHITE);
            display.fillCircle(cx + 3, cy - 2, r - 3, WHITE);
            for (int k = 0; k < 3; k++) {
                int sx = cx - 4 + k * 4;
                int sy = cy + 3 + ((frame + k * 3) % 8);
                display.drawPixel(sx, sy, WHITE);
                display.drawPixel(sx + 1, sy, WHITE);
            }
            break;
        }
        case WeatherCategory::Storm: {
            display.fillCircle(cx - 3, cy - 3, r - 2, WHITE);
            display.fillCircle(cx + 3, cy - 3, r - 3, WHITE);
            if ((frame / 2) % 2 == 0) {           // blinking bolt
                display.drawLine(cx,     cy,     cx - 3, cy + 4, WHITE);
                display.drawLine(cx - 3, cy + 4, cx + 1, cy + 4, WHITE);
                display.drawLine(cx + 1, cy + 4, cx - 2, cy + 9, WHITE);
            }
            break;
        }
        case WeatherCategory::Fog: {
            int dx = (frame % 6) - 3;
            for (int k = 0; k < 3; k++) {
                display.drawLine(cx - r + dx, cy - 2 + k * 3, cx + r + dx, cy - 2 + k * 3, WHITE);
            }
            break;
        }
    }
}

void OledFaceDisplay::drawWeather() {
    display.clearDisplay();
    display.setTextColor(WHITE);

    if (!weatherData.valid) {
        display.setTextSize(1);
        display.setCursor(0, 0);   display.print("Weather");
        display.setCursor(0, 28);  display.print("(loading...)");
        display.display();
        return;
    }

    // ---- Current conditions (top) ----
    WeatherCategory cur = weatherCategory(weatherData.currentCode);
    drawWeatherIcon(20, 18, 16, cur, weatherFrame);
    display.setTextSize(2);
    display.setCursor(46, 6);
    display.print((int)lroundf(weatherData.currentTemp));
    display.print("C");
    display.setTextSize(1);
    display.setCursor(46, 26);
    display.print(categoryLabel(cur));

    display.drawFastHLine(0, 37, 128, WHITE);

    // ---- Hourly strip (bottom): hour / tiny icon / temp per column ----
    int cols = weatherData.hourCount < 4 ? weatherData.hourCount : 4;
    for (int i = 0; i < cols; i++) {
        const HourlyForecast& h = weatherData.hours[i];
        int cx = i * 32 + 16;
        char buf[6];
        display.setTextSize(1);
        snprintf(buf, sizeof(buf), "%02d", h.hour);
        display.setCursor(cx - 6, 40);
        display.print(buf);
        drawWeatherIcon(cx, 51, 8, weatherCategory(h.code), weatherFrame);
        snprintf(buf, sizeof(buf), "%dc", (int)lroundf(h.temp));
        display.setCursor(cx - 8, 57);
        display.print(buf);
    }

    display.display();
}

void OledFaceDisplay::showWeather() {
    screen = Screen::Weather;
    weatherFrame = 0;
    nextWeatherFrameAt = millis();
    drawWeather();
}

void OledFaceDisplay::setWeather(const WeatherData& data) {
    weatherData = data;
    if (screen == Screen::Weather) drawWeather();
}

void OledFaceDisplay::setServerWarning(bool show) {
    if (serverWarning == show) return;
    serverWarning = show;
    if (screen == Screen::Face) drawFace(currentExpr, isBlinking);  // refresh badge now
}

void OledFaceDisplay::setEyeConfig(int width, int height, int gap, int roundness, bool mouth) {
    eyeWidth     = width;
    eyeHeight    = height;
    eyeGap       = gap;
    eyeRoundness = roundness;
    showMouth    = mouth;
    if (screen == Screen::Face) drawFace(currentExpr, isBlinking);  // live preview
}

void OledFaceDisplay::setOverlayFlags(bool loopTime, bool freeMem) {
    showLoopTime = loopTime;
    showFreeMem  = freeMem;
    if (screen == Screen::Face) drawFace(currentExpr, isBlinking);
}

void OledFaceDisplay::setMetrics(float hz, uint32_t freeHeapBytes) {
    // Only bother redrawing if a shown value actually changed (~1 Hz refresh).
    bool changed = false;
    if (showLoopTime && (int)lroundf(hz) != (int)lroundf(loopHz)) changed = true;
    if (showFreeMem && (freeHeapBytes / 1024) != (freeHeap / 1024)) changed = true;
    loopHz = hz;
    freeHeap = freeHeapBytes;
    if (changed && screen == Screen::Face && !isBlinking) drawFace(currentExpr, false);
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
    gazeX = gazeY = 0;
    targetGazeX = targetGazeY = 0;   // start centered; no leftover wander target
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
    if (screen == Screen::ConnectingServer) {
        if (now >= nextSpinnerAt) {
            spinnerFrame++;
            nextSpinnerAt = now + 100;
            drawConnectingServer();
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
    if (screen == Screen::Weather) {
        if (now >= nextWeatherFrameAt) {
            weatherFrame++;
            nextWeatherFrameAt = now + 150;
            drawWeather();
        }
        return;
    }

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

    // Gaze wander: occasionally pick a new target to glance toward (mostly
    // center-biased). The easing step below animates the move smoothly.
    if (now >= nextWanderAt) {
        switch (random(0, 5)) {
            case 0: targetGazeX = -8; targetGazeY =  0; break; // left
            case 1: targetGazeX =  8; targetGazeY =  0; break; // right
            case 2: targetGazeX =  0; targetGazeY = -4; break; // up
            default: targetGazeX = 0; targetGazeY =  0; break; // center (biased)
        }
        nextWanderAt = now + random(1500, 4000);
    }

    // Ease the gaze toward its target (exponential ease-out), redrawing at
    // ~30 fps only while it's actually moving so we don't hammer the I2C bus.
    if (now >= nextFrameAt) {
        float dx = targetGazeX - gazeX;
        float dy = targetGazeY - gazeY;
        if (fabsf(dx) > 0.1f || fabsf(dy) > 0.1f) {
            gazeX += dx * 0.25f;
            gazeY += dy * 0.25f;
            if (fabsf(targetGazeX - gazeX) < 0.1f) gazeX = targetGazeX;
            if (fabsf(targetGazeY - gazeY) < 0.1f) gazeY = targetGazeY;
            drawFace(currentExpr, false);
        }
        nextFrameAt = now + 33;
    }
}
