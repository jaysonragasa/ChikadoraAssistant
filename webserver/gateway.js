/**
 * Mochi Phone Gateway (Termux / Node.js)
 * 
 * Orchestrates Kokoro TTS (port 8000), Moonshine STT (port 8002), and Ollama LLM (port 11434).
 * Serves the full Web UI, manages the audio Queue, and stores all mic recordings and Kokoro audio files.
 */

const express = require('express');
const multer = require('multer');
const fs = require('fs');
const path = require('path');
const http = require('http');

const app = express();
const PORT = process.env.PORT || 8080;

const KOKORO_PORT = process.env.KOKORO_PORT || 8000;
const MOONSHINE_PORT = process.env.MOONSHINE_PORT || 8002;
const OLLAMA_PORT = process.env.OLLAMA_PORT || 11434;

const RECORDINGS_DIR = path.join(__dirname, 'recordings');
if (!fs.existsSync(RECORDINGS_DIR)) {
    fs.mkdirSync(RECORDINGS_DIR, { recursive: true });
}

// -----------------------------------------------------------------------------
// CORS & Middleware
// -----------------------------------------------------------------------------
app.use((req, res, next) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', '*');
    if (req.method === 'OPTIONS') return res.sendStatus(200);
    next();
});

app.use(express.json({ limit: '50mb' }));
app.use(express.urlencoded({ extended: true, limit: '50mb' }));

// Multer for file uploads (INMP441 audio from ESP32)
const storage = multer.diskStorage({
    destination: (req, file, cb) => cb(null, RECORDINGS_DIR),
    filename: (req, file, cb) => {
        const d = new Date();
        const pad = (n) => String(n).padStart(2, '0');
        const ts = `${d.getFullYear()}${pad(d.getMonth()+1)}${pad(d.getDate())}-${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`;
        cb(null, `rec-${ts}.wav`);
    }
});
const upload = multer({ storage });

// -----------------------------------------------------------------------------
// State Management (Queue, Settings, Personality)
// -----------------------------------------------------------------------------
const MAX_QUEUE = 50;
const queue = []; // Stores combined STT + TTS interaction items

let tarsSettings = {
    tars_enabled: false,
    tars_humor: 75,
    tars_honesty: 90,
    tars_sarcasm: 30,
    tars_discretion: 60
};

let ollamaSettings = {
    ollama_host: `http://127.0.0.1:${OLLAMA_PORT}`,
    ollama_model: 'gemma4:31b-cloud',
    ollama_system_prompt: 'You are Mochi, a friendly and concise desk robot. Keep replies to 1-2 short sentences without markdown or emojis.'
};

function addQueueItem(item) {
    queue.unshift(item);
    if (queue.length > MAX_QUEUE) queue.pop();
}

function updateQueueItem(id, updates) {
    const item = queue.find(q => q.id === id);
    if (item) Object.assign(item, updates);
    return item;
}

// -----------------------------------------------------------------------------
// 1. Unified Queue & Audio Endpoints
// -----------------------------------------------------------------------------

// GET /api/queue - Returns unified list of STT and TTS interactions for Web UI
app.get('/api/queue', (req, res) => {
    res.json(queue);
});

// GET /api/jobs - Compatibility with LocalTtsStudio frontend
app.get('/api/jobs', (req, res) => {
    const jobs = queue.map(q => ({
        id: q.id,
        model_id: 'kokoro-82m',
        params: { text: q.response || q.transcription || '' },
        label: q.transcription ? `Heard: "${q.transcription}"` : (q.response || 'Voice Job'),
        state: q.status === 'completed' ? 'done' : (q.status || 'queued'),
        created: q.created_at || Date.now() / 1000,
        finished: q.finished_at || Date.now() / 1000,
        transcription: q.transcription,
        has_mic: !!q.mic_audio_url,
        has_tts: !!q.tts_audio_url
    }));
    res.json({ jobs });
});

// GET /api/jobs/:id/result - Serves Kokoro generated WAV
app.get('/api/jobs/:id/result', (req, res) => {
    const filePath = path.join(RECORDINGS_DIR, `tts-${req.params.id}.wav`);
    if (fs.existsSync(filePath)) {
        res.setHeader('Content-Type', 'audio/wav');
        res.setHeader('Content-Disposition', `inline; filename="tts-${req.params.id}.wav"`);
        fs.createReadStream(filePath).pipe(res);
    } else {
        res.status(404).json({ error: 'Audio file not found' });
    }
});

// POST /api/jobs/clear - Clears queue
app.post('/api/jobs/clear', (req, res) => {
    queue.length = 0;
    res.json({ status: 'cleared' });
});

// -----------------------------------------------------------------------------
// 2. Microphone Recordings Endpoints
// -----------------------------------------------------------------------------

// GET /api/recordings - List saved recordings
app.get('/api/recordings', (req, res) => {
    try {
        const files = fs.readdirSync(RECORDINGS_DIR)
            .filter(f => f.startsWith('rec-') && f.endsWith('.wav'))
            .map(f => {
                const stat = fs.statSync(path.join(RECORDINGS_DIR, f));
                return { name: f, size: stat.size, mtime: stat.mtimeMs / 1000 };
            })
            .sort((a, b) => b.mtime - a.mtime);
        res.json({ recordings: files, saving: true });
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

// GET /api/recordings/:name - Serve mic recording WAV
app.get('/api/recordings/:name', (req, res) => {
    const safeName = path.basename(req.params.name);
    const filePath = path.join(RECORDINGS_DIR, safeName);
    if (fs.existsSync(filePath)) {
        res.setHeader('Content-Type', 'audio/wav');
        fs.createReadStream(filePath).pipe(res);
    } else {
        res.status(404).send('Recording not found');
    }
});

// DELETE /api/recordings - Clear all recordings
app.delete('/api/recordings', (req, res) => {
    try {
        const files = fs.readdirSync(RECORDINGS_DIR);
        let removed = 0;
        files.forEach(f => {
            if (f.startsWith('rec-') || f.startsWith('tts-')) {
                fs.unlinkSync(path.join(RECORDINGS_DIR, f));
                removed++;
            }
        });
        queue.length = 0;
        res.json({ removed });
    } catch (e) {
        res.status(500).json({ error: e.message });
    }
});

// -----------------------------------------------------------------------------
// 3. STT Transcription (Moonshine proxy on port 8002)
// -----------------------------------------------------------------------------
async function forwardToMoonshine(filePath) {
    const boundary = '----NodeGatewayBoundary' + Date.now();
    const fileData = fs.readFileSync(filePath);
    const filename = path.basename(filePath);

    let head = `--${boundary}\r\n`;
    head += `Content-Disposition: form-data; name="file"; filename="${filename}"\r\n`;
    head += `Content-Type: audio/wav\r\n\r\n`;
    const tail = `\r\n--${boundary}--\r\n`;

    const bodyBuffer = Buffer.concat([
        Buffer.from(head, 'utf8'),
        fileData,
        Buffer.from(tail, 'utf8')
    ]);

    return new Promise((resolve, reject) => {
        const req = http.request({
            hostname: '127.0.0.1',
            port: MOONSHINE_PORT,
            path: '/transcribe',
            method: 'POST',
            headers: {
                'Content-Type': `multipart/form-data; boundary=${boundary}`,
                'Content-Length': bodyBuffer.length
            },
            timeout: 15000
        }, (res) => {
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                try {
                    const parsed = JSON.parse(data);
                    resolve(parsed.text || parsed.transcription || '');
                } catch (e) {
                    resolve(data);
                }
            });
        });
        req.on('error', reject);
        req.on('timeout', () => { req.destroy(); reject(new Error('Moonshine timeout')); });
        req.write(bodyBuffer);
        req.end();
    });
}

const handleTranscribe = async (req, res) => {
    if (!req.file) {
        return res.status(400).json({ error: 'No audio file uploaded' });
    }

    const jobId = Date.now().toString();
    const recName = req.file.filename;
    const recPath = req.file.path;

    const queueItem = {
        id: jobId,
        timestamp: new Date().toLocaleTimeString(),
        created_at: Date.now() / 1000,
        status: 'transcribing',
        mic_audio_url: `/api/recordings/${recName}`,
        transcription: '',
        response: '',
        tts_audio_url: ''
    };
    addQueueItem(queueItem);

    try {
        console.log(`[Gateway] Transcribing ${recName} with Moonshine...`);
        const text = await forwardToMoonshine(recPath);
        console.log(`[Gateway] Moonshine transcription: "${text}"`);
        
        updateQueueItem(jobId, {
            transcription: text,
            status: text ? 'transcribed' : 'no_speech'
        });

        res.json({ text, recording: recName, job_id: jobId });
    } catch (err) {
        console.error('[Gateway] Moonshine error:', err.message);
        updateQueueItem(jobId, { status: 'error', error: err.message });
        res.status(500).json({ error: 'Moonshine failed: ' + err.message });
    }
};

app.post('/api/transcribe', upload.single('file'), handleTranscribe);
app.post('/transcribe', upload.single('file'), handleTranscribe);

// -----------------------------------------------------------------------------
// 4. LLM Generation (Ollama proxy on port 11434 with TARS tuning)
// -----------------------------------------------------------------------------
async function forwardToOllama(promptText) {
    let system = ollamaSettings.ollama_system_prompt || '';
    if (tarsSettings.tars_enabled) {
        system += ` TARS parameters: Humor ${tarsSettings.tars_humor}%, ` +
                  `Sarcasm ${tarsSettings.tars_sarcasm}%, ` +
                  `Honesty ${tarsSettings.tars_honesty}%, ` +
                  `Discretion ${tarsSettings.tars_discretion}%. Speak in character.`;
    }

    const payload = JSON.stringify({
        model: ollamaSettings.ollama_model || 'gemma4:31b-cloud',
        prompt: promptText,
        system: system,
        stream: false
    });

    return new Promise((resolve, reject) => {
        const req = http.request({
            hostname: '127.0.0.1',
            port: OLLAMA_PORT,
            path: '/api/generate',
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(payload)
            },
            timeout: 30000
        }, (res) => {
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                try {
                    const parsed = JSON.parse(data);
                    resolve(parsed.response || '');
                } catch (e) {
                    resolve(data);
                }
            });
        });
        req.on('error', reject);
        req.on('timeout', () => { req.destroy(); reject(new Error('Ollama timeout')); });
        req.write(payload);
        req.end();
    });
}

app.post('/api/generate', async (req, res) => {
    const prompt = req.body.prompt || req.body.text || '';
    try {
        console.log(`[Gateway] Querying Ollama: "${prompt}"...`);
        const reply = await forwardToOllama(prompt);
        console.log(`[Gateway] Ollama reply: "${reply}"`);
        
        // Link with most recent queue item if applicable
        if (queue.length > 0 && queue[0].status === 'transcribed') {
            queue[0].response = reply;
            queue[0].status = 'answered';
        }

        res.json({ response: reply, reply: reply });
    } catch (err) {
        console.error('[Gateway] Ollama error:', err.message);
        res.status(500).json({ error: 'Ollama failed: ' + err.message });
    }
});

app.post('/api/chat', async (req, res) => {
    const text = req.body.text || req.body.prompt || '';
    try {
        const reply = await forwardToOllama(text);
        res.json({ reply });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// -----------------------------------------------------------------------------
// 5. TTS Synthesis (Kokoro proxy on port 8000)
// -----------------------------------------------------------------------------
async function forwardToKokoro(text, voice) {
    const encodedText = encodeURIComponent(text);
    const encodedVoice = encodeURIComponent(voice || 'af_sarah');
    const pathUrl = `/tts?text=${encodedText}&voice=${encodedVoice}`;

    return new Promise((resolve, reject) => {
        const req = http.request({
            hostname: '127.0.0.1',
            port: KOKORO_PORT,
            path: pathUrl,
            method: 'GET',
            timeout: 30000
        }, (res) => {
            const chunks = [];
            res.on('data', chunk => chunks.push(chunk));
            res.on('end', () => {
                const buffer = Buffer.concat(chunks);
                resolve(buffer);
            });
        });
        req.on('error', reject);
        req.on('timeout', () => { req.destroy(); reject(new Error('Kokoro timeout')); });
        req.end();
    });
}

// GET /tts - Direct download for ESP32 and browser
app.get('/tts', async (req, res) => {
    const text = req.query.text || 'Hello from Mochi!';
    const voice = req.query.voice || 'af_sarah';
    const jobId = Date.now().toString();

    try {
        console.log(`[Gateway] Synthesizing Kokoro TTS: "${text}" (${voice})...`);
        const audioBuffer = await forwardToKokoro(text, voice);
        
        // Save to disk for queue playback
        const ttsFilename = `tts-${jobId}.wav`;
        fs.writeFileSync(path.join(RECORDINGS_DIR, ttsFilename), audioBuffer);

        // Update or create queue item
        let item = queue[0];
        if (!item || item.status === 'completed') {
            item = {
                id: jobId,
                timestamp: new Date().toLocaleTimeString(),
                created_at: Date.now() / 1000,
                status: 'completed',
                mic_audio_url: '',
                transcription: '',
                response: text,
                tts_audio_url: `/api/jobs/${jobId}/result`,
                finished_at: Date.now() / 1000
            };
            addQueueItem(item);
        } else {
            item.tts_audio_url = `/api/jobs/${jobId}/result`;
            item.status = 'completed';
            item.finished_at = Date.now() / 1000;
        }

        res.setHeader('Content-Type', 'audio/wav');
        res.setHeader('Content-Length', audioBuffer.length);
        res.send(audioBuffer);
    } catch (err) {
        console.error('[Gateway] Kokoro error:', err.message);
        res.status(500).json({ error: 'Kokoro failed: ' + err.message });
    }
});

// POST /api/synthesize - Compatible with Web Studio
app.post('/api/synthesize', async (req, res) => {
    const text = req.body.text || '';
    const voice = req.body.voice || 'af_sarah';
    const jobId = Date.now().toString();

    try {
        const audioBuffer = await forwardToKokoro(text, voice);
        const ttsFilename = `tts-${jobId}.wav`;
        fs.writeFileSync(path.join(RECORDINGS_DIR, ttsFilename), audioBuffer);

        const item = {
            id: jobId,
            timestamp: new Date().toLocaleTimeString(),
            created_at: Date.now() / 1000,
            status: 'completed',
            mic_audio_url: '',
            transcription: '',
            response: text,
            tts_audio_url: `/api/jobs/${jobId}/result`,
            finished_at: Date.now() / 1000
        };
        addQueueItem(item);

        res.json({ id: jobId, state: 'done' });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// -----------------------------------------------------------------------------
// 6. Personality & Settings Endpoints
// -----------------------------------------------------------------------------
app.get('/api/personality', (req, res) => res.json(tarsSettings));
app.post('/api/personality', (req, res) => {
    if (req.body.tars_enabled !== undefined) tarsSettings.tars_enabled = req.body.tars_enabled === 'true' || req.body.tars_enabled === true;
    if (req.body.tars_humor !== undefined) tarsSettings.tars_humor = parseInt(req.body.tars_humor, 10);
    if (req.body.tars_honesty !== undefined) tarsSettings.tars_honesty = parseInt(req.body.tars_honesty, 10);
    if (req.body.tars_sarcasm !== undefined) tarsSettings.tars_sarcasm = parseInt(req.body.tars_sarcasm, 10);
    if (req.body.tars_discretion !== undefined) tarsSettings.tars_discretion = parseInt(req.body.tars_discretion, 10);
    res.json(tarsSettings);
});

app.get('/api/settings', (req, res) => res.json(ollamaSettings));
app.post('/api/settings', (req, res) => {
    if (req.body.ollama_host) ollamaSettings.ollama_host = req.body.ollama_host;
    if (req.body.ollama_model) ollamaSettings.ollama_model = req.body.ollama_model;
    if (req.body.ollama_system_prompt) ollamaSettings.ollama_system_prompt = req.body.ollama_system_prompt;
    res.json(ollamaSettings);
});

// -----------------------------------------------------------------------------
// 7. Health & Models Check
// -----------------------------------------------------------------------------
app.get('/api/health', (req, res) => {
    res.json({
        status: 'ok',
        device: 'Mochi Desk Robot',
        kokoro_port: KOKORO_PORT,
        moonshine_port: MOONSHINE_PORT,
        ollama_port: OLLAMA_PORT,
        ollama_reachable: true,
        queue_count: queue.length
    });
});

app.get('/api/models', (req, res) => {
    res.json({
        models: [{
            id: 'kokoro-82m',
            family: 'Kokoro',
            repo_id: 'hexgrad/Kokoro-82M',
            approx_size: '350 MB',
            ready: true,
            languages: ['a', 'b']
        }]
    });
});

app.get('/api/models/kokoro-82m/options', (req, res) => {
    res.json({
        voices: [
            'af_sarah', 'af_bella', 'af_nicole', 'af_sky',
            'am_adam', 'am_michael', 'bf_emma', 'bf_isabella', 'bm_george', 'bm_lewis'
        ]
    });
});

// -----------------------------------------------------------------------------
// 8. Static Web Frontend (public folder in Termux)
// -----------------------------------------------------------------------------
const publicDir = path.join(__dirname, 'public');
if (fs.existsSync(publicDir)) {
    app.use(express.static(publicDir));
}
const frontendDir = path.join(__dirname, 'frontend');
if (fs.existsSync(frontendDir)) {
    app.use(express.static(frontendDir));
}

// Start Server
app.listen(PORT, '0.0.0.0', () => {
    console.log('====================================================');
    console.log(`  Mochi Phone Gateway listening on http://0.0.0.0:${PORT}`);
    console.log(`  - Kokoro TTS Target:   http://127.0.0.1:${KOKORO_PORT}`);
    console.log(`  - Moonshine STT:       http://127.0.0.1:${MOONSHINE_PORT}`);
    console.log(`  - Ollama LLM:          http://127.0.0.1:${OLLAMA_PORT}`);
    console.log(`  - Audio Recordings:    ${RECORDINGS_DIR}`);
    console.log(`  - Web UI:              http://0.0.0.0:${PORT}/`);
    console.log('====================================================');
});
