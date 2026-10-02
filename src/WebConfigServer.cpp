#include "WebConfigServer.h"
#include "DeviceConfig.h"
#include "Config.h"
#include <WiFi.h>

// Shared page chrome so the pages look consistent and readable on a phone.
static String page(const String& body) {
    String h =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Chikadora Setup</title><style>"
        "body{font-family:system-ui,sans-serif;background:#0b0d12;color:#e2e8f0;"
        "margin:0;padding:24px;display:flex;justify-content:center}"
        ".card{background:#12151c;border:1px solid #ffffff1a;border-radius:16px;"
        "padding:24px;max-width:460px;width:100%}"
        "h1{font-size:18px;margin:0 0 4px}h2{font-size:13px;text-transform:uppercase;"
        "letter-spacing:.05em;color:#94a3b8;margin:22px 0 4px;border-top:1px solid #ffffff14;padding-top:16px}"
        "p{color:#94a3b8;font-size:13px;margin:0 0 8px}"
        "label{display:block;font-size:13px;margin:12px 0 6px}"
        "input,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;"
        "border:1px solid #ffffff26;background:#1a1e28;color:#e2e8f0;font-size:15px}"
        ".row{display:flex;gap:12px}.row>div{flex:1}"
        ".chk{display:flex;align-items:center;gap:8px;margin-top:14px;font-size:14px}"
        ".chk input{width:auto}"
        "button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:8px;"
        "background:#5757e6;color:#fff;font-size:15px;font-weight:600;cursor:pointer}"
        ".ok{color:#34d399}.hint{font-size:12px;color:#64748b;margin-top:4px}</style>"
        "</head><body><div class='card'>";
    return h + body + "</div></body></html>";
}

void WebConfigServer::begin(IDisplay& disp) {
    display = &disp;
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/save", HTTP_POST, [this]() { handleSave(); });
    server.on("/restart", HTTP_POST, [this]() { handleRestart(); });
    server.on("/eye/live", HTTP_POST, [this]() { handleEyeLive(); });
    
    // Agent endpoints
    server.on("/agent_start", HTTP_POST, [this]() { handleAgentStart(); });
    server.on("/agent_thinking", HTTP_POST, [this]() { handleAgentThinking(); });
    server.on("/agent_end", HTTP_POST, [this]() { handleAgentEnd(); });
    server.on("/agent_speak", HTTP_POST, [this]() { handleAgentSpeak(); });
    
    server.begin();
    Serial.println("[WebConfig] Config page at http://" + WiFi.localIP().toString() + "/");
}

// Apply eye geometry to the display immediately (live preview). Not persisted;
// the Save button writes to NVS. Starts from the stored settings so unspecified
// fields keep their value.
void WebConfigServer::handleEyeLive() {
    RuntimeSettings s = DeviceConfig::settings();
    if (server.hasArg("eye_w"))   s.eyeWidth     = constrain((int)server.arg("eye_w").toInt(), 10, 60);
    if (server.hasArg("eye_h"))   s.eyeHeight    = constrain((int)server.arg("eye_h").toInt(), 10, 50);
    if (server.hasArg("eye_gap")) s.eyeGap       = constrain((int)server.arg("eye_gap").toInt(), 10, 50);
    if (server.hasArg("eye_r"))   s.eyeRoundness = constrain((int)server.arg("eye_r").toInt(), 0, 20);
    if (server.hasArg("show_mouth")) s.showMouth = server.arg("show_mouth") == "1";

    if (display) {
        display->setEyeConfig(s.eyeWidth, s.eyeHeight, s.eyeGap, s.eyeRoundness, s.showMouth);
    }
    server.send(200, "text/plain", "ok");
}

void WebConfigServer::handle() {
    server.handleClient();
}

void WebConfigServer::handleRoot() {
    String ip   = DeviceConfig::serverIp();
    String port = String(DeviceConfig::serverPort());
    RuntimeSettings s = DeviceConfig::settings();
    bool stream = s.playbackMode == Config::PlaybackMode::Stream;

    String body =
        "<h1>Chikadora Setup</h1>"
        "<p>Everything here is stored on the device. Save, then restart to apply.</p>"
        "<form method='POST' action='/save'>"

        "<h2>TTS / LLM server</h2>"
        "<div class='row'>"
        "<div><label>Server IP</label>"
        "<input name='ip' value='" + ip + "' placeholder='192.168.1.60'></div>"
        "<div><label>Port</label>"
        "<input name='port' type='number' min='1' max='65535' value='" + port + "'></div>"
        "</div>"
        "<label>Agent Name</label>"
        "<input name='agent_name' value='" + s.agentName + "' placeholder='Mochi'>"

        "<h2>Audio</h2>"
        "<label>Voice</label>"
        "<input name='voice' value='" + s.voiceId + "' placeholder='af_sarah'>"
        "<div class='hint'>Any Kokoro voice id (e.g. af_sarah, am_adam).</div>"
        "<div class='row'>"
        "<div><label>Mic gain</label>"
        "<input name='mic_gain' type='number' step='0.1' min='0' value='" + String(s.micGain, 1) + "'></div>"
        "<div><label>Speaker volume</label>"
        "<input name='spk_vol' type='number' step='0.1' min='0' value='" + String(s.speakerVolume, 1) + "'></div>"
        "</div>"
        "<label class='chk'><input type='checkbox' name='use_llm'" + String(s.useLlm ? " checked" : "") + ">"
        " Send speech to the LLM (uncheck to just echo)</label>"
        "<label class='chk'><input type='checkbox' name='no_speech'" + String(s.speakOnNoSpeech ? " checked" : "") + ">"
        " Speak a retry prompt when nothing is heard</label>"

        "<h2>Playback</h2>"
        "<label>Mode</label>"
        "<select name='pb_mode'>"
        "<option value='stream'" + String(stream ? " selected" : "") + ">Stream (whole reply)</option>"
        "<option value='chunk'" + String(!stream ? " selected" : "") + ">Chunk (split into clips)</option>"
        "</select>"
        "<label>Words per chunk</label>"
        "<input name='wpc' type='number' step='1' min='1' value='" + String(s.wordsPerChunk) + "'>"

        "<h2>Face / Eyes</h2>"
        "<p class='hint'>Drag to preview live on the device screen. Save to keep.</p>"
        "<label>Width <span id='wv'>" + String(s.eyeWidth) + "</span></label>"
        "<input id='eye_w' name='eye_w' type='range' min='10' max='60' value='" + String(s.eyeWidth) + "'>"
        "<label>Distance <span id='gv'>" + String(s.eyeGap) + "</span></label>"
        "<input id='eye_gap' name='eye_gap' type='range' min='10' max='50' value='" + String(s.eyeGap) + "'>"
        "<label>Height <span id='hv'>" + String(s.eyeHeight) + "</span></label>"
        "<input id='eye_h' name='eye_h' type='range' min='10' max='50' value='" + String(s.eyeHeight) + "'>"
        "<label>Roundness <span id='rv'>" + String(s.eyeRoundness) + "</span></label>"
        "<input id='eye_r' name='eye_r' type='range' min='0' max='20' value='" + String(s.eyeRoundness) + "'>"
        "<label class='chk'><input id='show_mouth' type='checkbox' name='show_mouth'" + String(s.showMouth ? " checked" : "") + "> Show mouth</label>"

        "<h2>Touch</h2>"
        "<p class='hint'>Tap = speak. Hold the pad this long to switch pages.</p>"
        "<label>Hold to switch page (seconds)</label>"
        "<input name='hold_s' type='number' step='0.5' min='0.5' max='5' value='" +
        String(s.holdMs / 1000.0, 1) + "'>"

        "<h2>Overlays (top-left)</h2>"
        "<label class='chk'><input type='checkbox' name='ov_loop'" + String(s.showLoopTime ? " checked" : "") + "> Show loop time (Hz)</label>"
        "<label class='chk'><input type='checkbox' name='ov_mem'" + String(s.showFreeMem ? " checked" : "") + "> Show free memory (KB)</label>"

        "<h2>Weather</h2>"
        "<label class='chk'><input type='checkbox' name='wx_en'" + String(s.weatherEnabled ? " checked" : "") + "> Enable weather page</label>"
        "<div class='row'>"
        "<div><label>Latitude</label>"
        "<input name='wx_lat' type='number' step='0.0001' value='" + String(s.weatherLat, 4) + "'></div>"
        "<div><label>Longitude</label>"
        "<input name='wx_lon' type='number' step='0.0001' value='" + String(s.weatherLon, 4) + "'></div>"
        "</div>"
        "<label>Refresh every (minutes)</label>"
        "<input name='wx_min' type='number' min='1' value='" + String(s.weatherPollMinutes) + "'>"
        "<p class='hint'>Weather powered by "
        "<a href='https://open-meteo.com/' style='color:#8b8bff'>https://open-meteo.com/</a></p>"
        
        "<h2>Agent Extensions</h2>"
        "<p class='hint'>Test the new agent endpoints directly from here.</p>"
        "<div style='display:flex;gap:8px;margin-bottom:12px;'>"
        "<button type='button' style='margin-top:0' onclick='fetch(\"/agent_start\",{method:\"POST\"})'>Start</button>"
        "<button type='button' style='margin-top:0' onclick='fetch(\"/agent_thinking\",{method:\"POST\"})'>Thinking</button>"
        "<button type='button' style='margin-top:0' onclick='fetch(\"/agent_end\",{method:\"POST\"})'>End</button>"
        "</div>"
        "<div style='display:flex;gap:8px;'>"
        "<input type='text' id='speak_text' placeholder='Text to speak' value='Hello!'>"
        "<button type='button' style='margin-top:0;width:auto;' onclick='fetch(\"/agent_speak?text=\"+encodeURIComponent(document.getElementById(\"speak_text\").value),{method:\"POST\"})'>Speak</button>"
        "</div>"

        "<button type='submit'>Save</button>"
        "</form>"

        // Live-preview: push eye geometry to the device as the sliders move.
        "<script>"
        "function q(i){return document.getElementById(i)}"
        "function live(){var p=new URLSearchParams();"
        "p.set('eye_w',q('eye_w').value);p.set('eye_gap',q('eye_gap').value);"
        "p.set('eye_h',q('eye_h').value);p.set('eye_r',q('eye_r').value);"
        "p.set('show_mouth',q('show_mouth').checked?'1':'0');"
        "fetch('/eye/live',{method:'POST',headers:{'Content-Type':"
        "'application/x-www-form-urlencoded'},body:p.toString()});}"
        "[['eye_w','wv'],['eye_gap','gv'],['eye_h','hv'],['eye_r','rv']].forEach("
        "function(a){var e=q(a[0]),l=q(a[1]);e.addEventListener('input',function(){"
        "l.textContent=e.value;live();});});"
        "q('show_mouth').addEventListener('change',live);"
        "</script>";
    server.send(200, "text/html", page(body));
}

void WebConfigServer::handleSave() {
    String ip = server.arg("ip");
    ip.trim();
    long port = server.arg("port").toInt();
    if (ip.length() == 0 || port < 1 || port > 65535) {
        String body =
            "<h1>Invalid input</h1>"
            "<p>Enter a non-empty IP/host and a port between 1 and 65535.</p>"
            "<form method='GET' action='/'><button type='submit'>Back</button></form>";
        server.send(400, "text/html", page(body));
        return;
    }

    // Start from current settings so unspecified fields keep their value.
    RuntimeSettings s = DeviceConfig::settings();

    String voice = server.arg("voice"); voice.trim();
    if (voice.length() > 0) s.voiceId = voice;
    
    String agentName = server.arg("agent_name"); agentName.trim();
    if (agentName.length() > 0) s.agentName = agentName;

    if (server.hasArg("mic_gain")) {
        float g = server.arg("mic_gain").toFloat();
        if (g >= 0.0f) s.micGain = g;
    }
    if (server.hasArg("spk_vol")) {
        float v = server.arg("spk_vol").toFloat();
        if (v >= 0.0f) s.speakerVolume = v;
    }
    // Unchecked checkboxes are simply absent from the POST, so presence == on.
    s.useLlm         = server.hasArg("use_llm");
    s.speakOnNoSpeech = server.hasArg("no_speech");

    if (server.hasArg("pb_mode")) {
        s.playbackMode = (server.arg("pb_mode") == "chunk")
                             ? Config::PlaybackMode::Chunk
                             : Config::PlaybackMode::Stream;
    }
    if (server.hasArg("wpc")) {
        long w = server.arg("wpc").toInt();
        if (w >= 1) s.wordsPerChunk = (int)w;
    }

    // Eyes
    if (server.hasArg("eye_w"))   s.eyeWidth     = constrain((int)server.arg("eye_w").toInt(), 10, 60);
    if (server.hasArg("eye_h"))   s.eyeHeight    = constrain((int)server.arg("eye_h").toInt(), 10, 50);
    if (server.hasArg("eye_gap")) s.eyeGap       = constrain((int)server.arg("eye_gap").toInt(), 10, 50);
    if (server.hasArg("eye_r"))   s.eyeRoundness = constrain((int)server.arg("eye_r").toInt(), 0, 20);
    s.showMouth = server.hasArg("show_mouth");

    // Overlays
    s.showLoopTime = server.hasArg("ov_loop");
    s.showFreeMem  = server.hasArg("ov_mem");

    // Touch hold duration (entered in seconds, stored as ms).
    if (server.hasArg("hold_s")) {
        float sec = server.arg("hold_s").toFloat();
        s.holdMs = constrain((int)(sec * 1000.0f), 300, 5000);
    }

    // Weather
    s.weatherEnabled = server.hasArg("wx_en");
    if (server.hasArg("wx_lat")) s.weatherLat = server.arg("wx_lat").toFloat();
    if (server.hasArg("wx_lon")) s.weatherLon = server.arg("wx_lon").toFloat();
    if (server.hasArg("wx_min")) {
        long m = server.arg("wx_min").toInt();
        if (m >= 1) s.weatherPollMinutes = (int)m;
    }

    DeviceConfig::setServer(ip, (uint16_t)port);
    DeviceConfig::setSettings(s);

    // Apply the display-only settings immediately (no restart needed). The rest
    // (server address, audio, playback, weather) is read at boot by other
    // components, so those still need a restart.
    if (display) {
        display->setEyeConfig(s.eyeWidth, s.eyeHeight, s.eyeGap, s.eyeRoundness, s.showMouth);
        display->setOverlayFlags(s.showLoopTime, s.showFreeMem);
    }

    String body =
        "<h1 class='ok'>Saved</h1>"
        "<p>Eyes, mouth and overlays apply now. Server, audio and weather "
        "settings take effect after a restart.</p>"
        "<form method='POST' action='/restart'><button type='submit'>Restart now</button></form>"
        "<form method='GET' action='/'><button type='submit' "
        "style='background:#232838;margin-top:10px'>Back</button></form>";
    server.send(200, "text/html", page(body));
}

void WebConfigServer::handleRestart() {
    server.send(200, "text/html",
                page("<h1>Restarting...</h1><p>The device is rebooting now.</p>"));
    delay(500);       // let the response flush before we reset
    ESP.restart();
}

void WebConfigServer::handleAgentStart() {
    if (onAgentStart) onAgentStart();
    server.send(200, "text/plain", "ok");
}

void WebConfigServer::handleAgentThinking() {
    if (onAgentThinking) onAgentThinking();
    server.send(200, "text/plain", "ok");
}

void WebConfigServer::handleAgentEnd() {
    if (onAgentEnd) onAgentEnd();
    server.send(200, "text/plain", "ok");
}

void WebConfigServer::handleAgentSpeak() {
    String text = server.hasArg("text") ? server.arg("text") : "";
    if (onAgentSpeak) onAgentSpeak(text);
    server.send(200, "text/plain", "ok");
}
