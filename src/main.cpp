#include <Arduino.h>

#include "Config.h"
#include "OledFaceDisplay.h"
#include "Ttp223bSensor.h"
#include "I2SMicrophone.h"
#include "I2SAmplifier.h"
#include "LocalTtsClient.h"
#include "TouchTrigger.h"
#include "VoiceTrigger.h"
#include "VoiceAssistant.h"

// Composition root: build the concrete devices and inject them into the app.
// (These are constructed before setup(); their constructors only store config
// and don't touch hardware, so this is safe.)
static OledFaceDisplay display(Config::Pins::OLED_SDA, Config::Pins::OLED_SCL);
static Ttp223bSensor   touch(Config::Pins::TOUCH);
static I2SMicrophone   mic(Config::Pins::I2S_BCLK, Config::Pins::I2S_LRC, Config::Pins::MIC_DOUT);
static I2SAmplifier    speaker(Config::Pins::I2S_BCLK, Config::Pins::I2S_LRC, Config::Pins::AMP_DIN);
static LocalTtsClient  api(Config::Server::IP, Config::Server::PORT, Config::Server::VOICE_ID);

// Both triggers are cheap to construct; we bind the reference to the one chosen
// by Config::Trigger::MODE (constexpr, so the compiler folds the choice).
static TouchTrigger    touchTrigger(touch);
static VoiceTrigger    voiceTrigger(mic, touch,
                                    Config::Trigger::START_THRESHOLD,
                                    Config::Trigger::START_MIN_MS,
                                    Config::Trigger::DEBUG_LEVEL,
                                    Config::Trigger::TOUCH_FALLBACK);
static ITrigger&       trigger = (Config::Trigger::MODE == Config::TriggerMode::Voice)
                                     ? static_cast<ITrigger&>(voiceTrigger)
                                     : static_cast<ITrigger&>(touchTrigger);

static VoiceAssistant  assistant(display, trigger, touch, mic, speaker, api);

void setup() {
    assistant.begin();
}

void loop() {
    assistant.loop();
}
