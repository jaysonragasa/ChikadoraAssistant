# Kokoro TTS (local web app for Mochi)

A local, single-user text-to-speech web app built around **Kokoro-82M** — a
lightweight, fast preset-voice model. It's a trimmed, Kokoro-only version of
Local TTS Studio and keeps the **exact same HTTP API**, so the Mochi ESP32-C3
firmware works against it unchanged.

Pick a voice, set a speed, type text, and generate. Everything runs on your
machine; the server binds locally (or to your LAN so the ESP32 can reach it).

## Render queue

Synthesis runs as background **jobs** so the UI never blocks on a slow CPU
render:

- Clicking **Generate** enqueues a job and returns immediately.
- A single worker renders jobs one at a time (FIFO). The **Queue** panel shows
  each job's state: `queued #N`, `rendering...`, `done`, `error`, or `canceled`.
- **Cancel** works on both **queued** and **running** jobs (a running render is
  killed by terminating the worker process; a fresh worker is spawned for the
  next job).
- The running job shows a live elapsed timer plus a throughput-based estimated
  progress bar (the queue learns seconds-per-character from completed renders).
- Finished jobs show an inline player + **Download**.

## Your hardware

CPU-only works fine, but synthesis is **not real-time** — expect a few seconds
per generation. Kokoro is small (~350 MB) and quick relative to larger models.

## One-time setup

Run these from this folder (the one containing this `README.md`).

### 1. Create a virtual environment

```cmd
python -m venv venv
venv\Scripts\python.exe -m pip install --upgrade pip
```

### 2. Install a CPU build of PyTorch

Kokoro depends on torch. Install the CPU wheel first so pip doesn't pull a huge
CUDA build:

```cmd
venv\Scripts\python.exe -m pip install torch --index-url https://download.pytorch.org/whl/cpu
```

### 3. Install the rest of the dependencies

```cmd
venv\Scripts\python.exe -m pip install -r backend\requirements.txt
```

> `soundfile` bundles `libsndfile`, so **ffmpeg is not required**.

> **Corporate network / SSL note:** `requirements.txt` includes `truststore`,
> which makes Python verify TLS against the **Windows certificate store**. This
> fixes `SSL: CERTIFICATE_VERIFY_FAILED` errors caused by SSL-inspection proxies
> (Zscaler/Netskope). The app enables it automatically on startup via
> `backend/certs.py`. Do **not** install `hf_xet` — it bypasses the trust store.

### 4. (Optional) Pre-download the Kokoro weights

The weights download automatically on first use (or via the in-app **Download**
button), but you can fetch them ahead of time:

```cmd
venv\Scripts\python.exe -m pip install -U "huggingface_hub[cli]"
venv\Scripts\huggingface-cli download hexgrad/Kokoro-82M
```

## Run it

```cmd
run.cmd
```

`run.cmd` binds to `0.0.0.0:8000` so the ESP32 can reach it over your LAN. Open
<http://127.0.0.1:8000> in your browser on this machine.

- First generation is slower because the model loads into RAM.
- If Kokoro isn't installed/downloaded yet, the app shows a **Set up / Download**
  banner with a progress bar; the Generate button stays disabled until ready.

## Point the Mochi firmware here

The firmware posts to this server's IP on port 8000. In `src/main.cpp` set
`SERVER_IP` to this machine's LAN address (the same value it used for Local TTS
Studio, e.g. `192.168.1.60`). The firmware already sends `model_id=kokoro-82m`,
`voice`, `text`, and `language=a`, which this server accepts.

## Configuration

| Variable | Default | Purpose |
|---|---|---|
| `TTS_DEVICE` | `cpu` | Set to `cuda:0` on a GPU machine |
| `TTS_HOST` | `127.0.0.1` | Bind address (`run.cmd` sets `0.0.0.0`) |
| `TTS_PORT` | `8000` | Port |
| `STT_MODEL` | `base` | faster-whisper model for `/api/transcribe` |
| `STT_DEVICE` | `cpu` | `cpu` or `cuda` for transcription |

### Voices

US English (`a*`) and UK English (`b*`) voices work out of the box. Some other
languages need the `espeak-ng` system package installed separately.

Built-in voices: `af_heart`, `af_bella`, `af_nicole`, `af_sarah`, `af_sky`,
`am_adam`, `am_michael`, `bf_emma`, `bf_isabella`, `bm_george`, `bm_lewis`.

## API

- `GET  /api/health` — status + device
- `POST /api/transcribe` — speech-to-text (multipart `audio`); used by the firmware
- `GET  /api/models` — catalog (just Kokoro) with download status
- `GET  /api/models/{id}/options` — available voices
- `POST /api/models/{id}/download` — start a background download
- `GET  /api/models/{id}/download` — poll download progress
- `POST /api/synthesize` — enqueue a job. Form fields: `model_id`, `text`,
  `language` (`a`/`b`), `voice`, `speed`. Returns a job record (JSON).
- `GET  /api/jobs` — list all jobs
- `GET  /api/jobs/{id}` — one job's status (`state`, `has_result`, ...)
- `GET  /api/jobs/{id}/result` — download a finished job's WAV (24 kHz mono)
- `POST /api/jobs/{id}/cancel` — cancel a queued/running job
- `DELETE /api/jobs/{id}` — remove a finished job
- `POST /api/jobs/clear` — remove all finished jobs

## Notes on responsible use

Generated audio from this tool is your responsibility.
