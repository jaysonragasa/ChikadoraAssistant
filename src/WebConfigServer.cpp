#include "WebConfigServer.h"
#include "DeviceConfig.h"
#include "Config.h"
#include <WiFi.h>

// Shared page chrome with sleek modern dark theme
static String page(const String& body) {
    String h =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Mochi Robot Setup</title><style>"
        "body{font-family:system-ui,-apple-system,sans-serif;background:#0b0d12;color:#e2e8f0;"
        "margin:0;padding:20px;display:flex;justify-content:center}"
        ".card{background:#12151c;border:1px solid #ffffff1a;border-radius:18px;"
        "padding:24px;max-width:480px;width:100%;box-shadow:0 10px 30px rgba(0,0,0,0.5)}"
        "h1{font-size:20px;font-weight:700;margin:0 0 4px;color:#fff;display:flex;align-items:center;gap:10px}"
        "h2{font-size:12px;font-weight:700;text-transform:uppercase;"
        "letter-spacing:.08em;color:#818cf8;margin:24px 0 8px;border-top:1px solid #ffffff14;padding-top:16px}"
        "p{color:#94a3b8;font-size:13px;margin:0 0 12px;line-height:1.4}"
        "label{display:block;font-size:13px;font-weight:500;margin:12px 0 6px;color:#cbd5e1}"
        "input,select,textarea{width:100%;box-sizing:border-box;padding:10px 12px;border-radius:8px;"
        "border:1px solid #ffffff26;background:#1a1e28;color:#f8fafc;font-size:14px;outline:none}"
        "input:focus,select:focus,textarea:focus{border-color:#6366f1}"
        "input[type='range']{accent-color:#6366f1;cursor:pointer;padding:0;background:transparent;border:0}"
        ".row{display:flex;gap:10px}.row>div{flex:1}"
        ".chk{display:flex;align-items:center;gap:10px;margin-top:12px;font-size:14px;cursor:pointer}"
        ".chk input{width:auto;accent-color:#6366f1;cursor:pointer}"
        "button{margin-top:24px;width:100%;padding:13px;border:0;border-radius:10px;"
        "background:linear-gradient(135deg,#6366f1,#4f46e5);color:#fff;font-size:15px;font-weight:600;cursor:pointer;"
        "transition:opacity 0.2s}button:hover{opacity:0.9}"
        ".ok{color:#34d399}.val{color:#818cf8;font-weight:600}.hint{font-size:12px;color:#64748b;margin-top:4px}"
        ".badge{background:#1e1b4b;color:#a5b4fc;border:1px solid #312e81;padding:2px 8px;border-radius:12px;font-size:11px}"
        "</style></head><body><div class='card'>";
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
    RuntimeSettings s = DeviceConfig::settings();
    bool stream = s.playbackMode == Config::PlaybackMode::Stream;

    String body =
        "<h1>Mochi Setup <span class='badge'>On-Chip</span></h1>"
        "<p>Settings are stored in ESP32 flash memory. Click Save to apply.</p>"
        "<form method='POST' action='/save'>"

        // ---------------- AI Services (Phone / Termux) ----------------
        "<h2>AI Brain Endpoints (Termux)</h2>"
        "<label>Phone IP Address</label>"
        "<input name='ip' value='" + ip + "' placeholder='192.168.1.34'>"
        "<div class='row' style='margin-top:6px;'>"
        "<div><label>Kokoro TTS</label>"
        "<input name='kokoro_port' type='number' min='1' max='65535' value='" + String(s.kokoroPort) + "'></div>"
        "<div><label>Moonshine STT</label>"
        "<input name='moon_port' type='number' min='1' max='65535' value='" + String(s.moonshinePort) + "'></div>"
        "<div><label>Ollama LLM</label>"
        "<input name='ollama_port' type='number' min='1' max='65535' value='" + String(s.ollamaPort) + "'></div>"
        "</div>"

        // ---------------- Ollama LLM ----------------
        "<h2>Ollama Model & System Prompt</h2>"
        "<label>Model</label>"
        "<input name='o_model' value='" + s.ollamaModel + "' placeholder='gemma4:31b-cloud'>"
        "<label>System Prompt</label>"
        "<textarea name='o_prompt' rows='3'>" + s.ollamaSystemPrompt + "</textarea>"

        // ---------------- Personality (TARS) ----------------
        "<h2>Personality (TARS)</h2>"
        "<label class='chk'><input type='checkbox' id='tars_en' name='tars_en'" + String(s.tarsEnabled ? " checked" : "") + "> Enable TARS personality</label>"
        "<label>Humor: <span id='tv_humor' class='val'>" + String(s.tarsHumor) + "</span>%</label>"
        "<input id='t_humor' name='t_humor' type='range' min='0' max='100' step='5' value='" + String(s.tarsHumor) + "'>"
        "<label>Honesty: <span id='tv_honest' class='val'>" + String(s.tarsHonesty) + "</span>%</label>"
        "<input id='t_honest' name='t_honest' type='range' min='0' max='100' step='5' value='" + String(s.tarsHonesty) + "'>"
        "<label>Sarcasm: <span id='tv_sarc' class='val'>" + String(s.tarsSarcasm) + "</span>%</label>"
        "<input id='t_sarc' name='t_sarc' type='range' min='0' max='100' step='5' value='" + String(s.tarsSarcasm) + "'>"
        "<label>Discretion: <span id='tv_disc' class='val'>" + String(s.tarsDiscretion) + "</span>%</label>"
        "<input id='t_disc' name='t_disc' type='range' min='0' max='100' step='5' value='" + String(s.tarsDiscretion) + "'>"

        // ---------------- Audio & Voice ----------------
        "<h2>Audio & Voice</h2>"
        "<label>Agent Name</label>"
        "<input name='agent_name' value='" + s.agentName + "' placeholder='Mochi'>"
        "<label>Kokoro Voice ID</label>"
        "<input name='voice' value='" + s.voiceId + "' placeholder='af_sarah'>"
        "<div class='hint'>e.g. af_sarah, af_bella, am_adam, bm_george</div>"
        "<div class='row'>"
        "<div><label>Mic gain</label>"
        "<input name='mic_gain' type='number' step='0.1' min='0' value='" + String(s.micGain, 1) + "'></div>"
        "<div><label>Speaker volume</label>"
        "<input name='spk_vol' type='number' step='0.1' min='0' value='" + String(s.speakerVolume, 1) + "'></div>"
        "</div>"
        "<label class='chk'><input type='checkbox' name='use_llm'" + String(s.useLlm ? " checked" : "") + "> Send speech to Ollama</label>"
        "<label class='chk'><input type='checkbox' name='no_speech'" + String(s.speakOnNoSpeech ? " checked" : "") + "> Speak retry when nothing heard</label>"

        // ---------------- Playback ----------------
        "<h2>Playback</h2>"
        "<label>Mode</label>"
        "<select name='pb_mode'>"
        "<option value='stream'" + String(stream ? " selected" : "") + ">Stream (Direct live audio)</option>"
        "<option value='chunk'" + String(!stream ? " selected" : "") + ">Chunk (Split into clips)</option>"
        "</select>"
        "<label>Words per chunk</label>"
        "<input name='wpc' type='number' step='1' min='1' value='" + String(s.wordsPerChunk) + "'>"

        // ---------------- Face / Eyes ----------------
        "<h2>Face / Eyes (Live Preview)</h2>"
        "<p class='hint'>Sliders update Mochi's OLED display in real-time!</p>"
        "<label>Width <span id='wv' class='val'>" + String(s.eyeWidth) + "</span>px</label>"
        "<input id='eye_w' name='eye_w' type='range' min='10' max='60' value='" + String(s.eyeWidth) + "'>"
        "<label>Distance <span id='gv' class='val'>" + String(s.eyeGap) + "</span>px</label>"
        "<input id='eye_gap' name='eye_gap' type='range' min='10' max='50' value='" + String(s.eyeGap) + "'>"
        "<label>Height <span id='hv' class='val'>" + String(s.eyeHeight) + "</span>px</label>"
        "<input id='eye_h' name='eye_h' type='range' min='10' max='50' value='" + String(s.eyeHeight) + "'>"
        "<label>Roundness <span id='rv' class='val'>" + String(s.eyeRoundness) + "</span>px</label>"
        "<input id='eye_r' name='eye_r' type='range' min='0' max='20' value='" + String(s.eyeRoundness) + "'>"
        "<label class='chk'><input id='show_mouth' type='checkbox' name='show_mouth'" + String(s.showMouth ? " checked" : "") + "> Show mouth</label>"

        // ---------------- Weather ----------------
        "<h2>Weather (Open-Meteo)</h2>"
        "<label class='chk'><input type='checkbox' name='wx_en'" + String(s.weatherEnabled ? " checked" : "") + "> Enable weather page</label>"
        "<div class='row'>"
        "<div><label>Latitude</label>"
        "<input name='wx_lat' type='number' step='0.0001' value='" + String(s.weatherLat, 4) + "'></div>"
        "<div><label>Longitude</label>"
        "<input name='wx_lon' type='number' step='0.0001' value='" + String(s.weatherLon, 4) + "'></div>"
        "</div>"
        "<label>Refresh interval (minutes)</label>"
        "<input name='wx_min' type='number' min='1' value='" + String(s.weatherPollMinutes) + "'>"

        // ---------------- Touch & Overlays ----------------
        "<h2>Touch & Overlays</h2>"
        "<label>Hold duration to switch page (seconds)</label>"
        "<input name='hold_s' type='number' step='0.5' min='0.5' max='5' value='" + String(s.holdMs / 1000.0, 1) + "'>"
        "<label class='chk'><input type='checkbox' name='ov_loop'" + String(s.showLoopTime ? " checked" : "") + "> Show loop time (Hz)</label>"
        "<label class='chk'><input type='checkbox' name='ov_mem'" + String(s.showFreeMem ? " checked" : "") + "> Show free memory (KB)</label>"

        // ---------------- Agent Testing ----------------
        "<h2>Agent Testing</h2>"
        "<div style='display:flex;gap:8px;margin-bottom:10px;'>"
        "<button type='button' style='margin-top:0' onclick='fetch(\"/agent_start\",{method:\"POST\"})'>Start</button>"
        "<button type='button' style='margin-top:0' onclick='fetch(\"/agent_thinking\",{method:\"POST\"})'>Think</button>"
        "<button type='button' style='margin-top:0' onclick='fetch(\"/agent_end\",{method:\"POST\"})'>End</button>"
        "</div>"
        "<div style='display:flex;gap:8px;'>"
        "<input type='text' id='speak_text' placeholder='Text to speak' value='Hello, I am Mochi!'>"
        "<button type='button' style='margin-top:0;width:auto;' onclick='fetch(\"/agent_speak?text=\"+encodeURIComponent(document.getElementById(\"speak_text\").value),{method:\"POST\"})'>Speak</button>"
        "</div>"

        "<button type='submit'>Save & Apply Settings</button>"
        "</form>"

        // Live preview & slider labels script
        "<script>"
        "function q(i){return document.getElementById(i)}"
        "function live(){var p=new URLSearchParams();"
        "p.set('eye_w',q('eye_w').value);p.set('eye_gap',q('eye_gap').value);"
        "p.set('eye_h',q('eye_h').value);p.set('eye_r',q('eye_r').value);"
        "p.set('show_mouth',q('show_mouth').checked?'1':'0');"
        "fetch('/eye/live',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});}"
        "[['eye_w','wv'],['eye_gap','gv'],['eye_h','hv'],['eye_r','rv']].forEach("
        "function(a){var e=q(a[0]),l=q(a[1]);e.addEventListener('input',function(){l.textContent=e.value;live();});});"
        "q('show_mouth').addEventListener('change',live);"
        "[['t_humor','tv_humor'],['t_honest','tv_honest'],['t_sarc','tv_sarc'],['t_disc','tv_disc']].forEach("
        "function(a){var e=q(a[0]),l=q(a[1]);e.addEventListener('input',function(){l.textContent=e.value;});});"
        "</script>";

    server.send(200, "text/html", page(body));
}

void WebConfigServer::handleSave() {
    String ip = server.arg("ip");
    ip.trim();
    if (ip.length() == 0) {
        server.send(400, "text/html", page("<h1>Invalid input</h1><p>Phone IP cannot be blank.</p><a href='/'>Back</a>"));
        return;
    }

    RuntimeSettings s = DeviceConfig::settings();

    // AI Endpoints
    if (server.hasArg("kokoro_port"))  s.kokoroPort    = server.arg("kokoro_port").toInt();
    if (server.hasArg("moon_port"))    s.moonshinePort = server.arg("moon_port").toInt();
    if (server.hasArg("ollama_port"))  s.ollamaPort    = server.arg("ollama_port").toInt();

    // Ollama & Personality
    if (server.hasArg("o_model"))  s.ollamaModel        = server.arg("o_model");
    if (server.hasArg("o_prompt")) s.ollamaSystemPrompt = server.arg("o_prompt");

    s.tarsEnabled = server.hasArg("tars_en");
    if (server.hasArg("t_humor"))  s.tarsHumor          = server.arg("t_humor").toInt();
    if (server.hasArg("t_honest")) s.tarsHonesty        = server.arg("t_honest").toInt();
    if (server.hasArg("t_sarc"))   s.tarsSarcasm        = server.arg("t_sarc").toInt();
    if (server.hasArg("t_disc"))   s.tarsDiscretion     = server.arg("t_disc").toInt();

    // Audio & Agent
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
    s.useLlm          = server.hasArg("use_llm");
    s.speakOnNoSpeech = server.hasArg("no_speech");

    // Playback
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

    // Touch
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

    DeviceConfig::setServer(ip, s.kokoroPort);
    DeviceConfig::setSettings(s);

    if (display) {
        display->setEyeConfig(s.eyeWidth, s.eyeHeight, s.eyeGap, s.eyeRoundness, s.showMouth);
        display->setOverlayFlags(s.showLoopTime, s.showFreeMem);
    }

    String body =
        "<h1 class='ok'>Settings Saved!</h1>"
        "<p>Face & eye settings applied immediately. Network & audio settings will activate on restart.</p>"
        "<form method='POST' action='/restart'><button type='submit'>Restart Mochi Now</button></form>"
        "<p style='margin-top:16px;'><a href='/' style='color:#818cf8;text-decoration:none;'>&larr; Back to settings</a></p>";
    server.send(200, "text/html", page(body));
}

void WebConfigServer::handleRestart() {
    String body =
        "<h1>Restarting...</h1>"
        "<p>Mochi is rebooting to apply new settings. This page will reconnect shortly.</p>"
        "<script>setTimeout(function(){location.href='/';},6000);</script>";
    server.send(200, "text/html", page(body));
    delay(500);
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
    String text = server.arg("text");
    if (onAgentSpeak) onAgentSpeak(text);
    server.send(200, "text/plain", "ok");
}
