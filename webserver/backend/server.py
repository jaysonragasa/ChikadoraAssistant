"""
Kokoro TTS local web app backend (Kokoro-only build).

A small FastAPI service that exposes a single-model catalog (Kokoro-82M), lets
you download it on demand with progress, and synthesizes speech. It keeps the
exact same API surface as the multi-model Local TTS Studio so the Mochi
ESP32-C3 firmware works against it unchanged.

Endpoints:
  GET  /api/health                 liveness + device
  POST /api/transcribe             speech-to-text (used by the Mochi firmware)
  GET  /api/models                 catalog with per-model download status
  GET  /api/models/{id}/options    dynamic choices (voices)
  POST /api/models/{id}/download   start a background download
  GET  /api/models/{id}/download   poll download progress
  POST /api/synthesize             enqueue a synthesis job -> job record
  GET  /api/jobs                   list all jobs
  GET  /api/jobs/{id}              one job's status
  GET  /api/jobs/{id}/result       download a finished job's WAV
  POST /api/jobs/{id}/cancel       cancel a queued/running job
  DELETE /api/jobs/{id}            remove a finished job
  POST /api/jobs/clear             remove all finished jobs

Synthesis returns 24kHz mono WAV (audio/wav).

SECURITY: binds to 127.0.0.1 by default. Set TTS_HOST=0.0.0.0 (run.cmd does
this) to let the ESP32 reach it over your LAN. No auth (local single-user use).
"""

import io
import os

# Enable OS trust store for downloads (corporate SSL inspection) before net I/O.
import certs  # noqa: F401

import numpy as np
import soundfile as sf
from fastapi import FastAPI, Form, HTTPException, UploadFile, File
from fastapi.responses import Response, FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

from registry import registry
from jobs import JobQueue
import engines
import stt
import settings
import ollama
import re
import time

# Single background worker renders queued jobs one at a time.
job_queue = JobQueue(synth_fn=lambda mid, params: registry.synthesize(mid, params))

HERE = os.path.dirname(os.path.abspath(__file__))
FRONTEND_DIR = os.path.join(os.path.dirname(HERE), "frontend")

# Debug: keep the last few clips the device sent to /api/transcribe so you can
# listen to exactly what the mic captured (useful for tuning MIC_GAIN). Set the
# SAVE_RECORDINGS env var to 0 to disable. Files are bounded to MAX_RECORDINGS.
RECORDINGS_DIR = os.path.join(HERE, "recordings")
SAVE_RECORDINGS = os.environ.get("SAVE_RECORDINGS", "1") not in ("0", "false", "False")
MAX_RECORDINGS = 20
_REC_RE = re.compile(r"^rec-\d+\.wav$")

app = FastAPI(title="Kokoro TTS", version="1.0.0")


@app.middleware("http")
async def no_cache_static(request, call_next):
    """Tell browsers not to cache the frontend so UI updates show on refresh."""
    response = await call_next(request)
    path = request.url.path
    if path == "/" or path.endswith((".html", ".js", ".css")):
        response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
        response.headers["Pragma"] = "no-cache"
        response.headers["Expires"] = "0"
    return response


def _decode_wav_bytes(raw: bytes, filename: str = ""):
    try:
        wav, sr = sf.read(io.BytesIO(raw), dtype="float32", always_2d=False)
    except Exception as e:  # noqa: BLE001
        raise HTTPException(
            status_code=400,
            detail=f"Could not read audio '{filename}'. Upload a WAV. ({e})",
        )
    if getattr(wav, "ndim", 1) > 1:
        wav = wav.mean(axis=1).astype(np.float32)
    return wav, int(sr)


def _save_recording(raw: bytes) -> str:
    """Persist the raw uploaded WAV bytes for later playback. Returns filename."""
    if not SAVE_RECORDINGS:
        return ""
    try:
        os.makedirs(RECORDINGS_DIR, exist_ok=True)
        name = f"rec-{int(time.time() * 1000)}.wav"
        with open(os.path.join(RECORDINGS_DIR, name), "wb") as f:
            f.write(raw)
        # Keep only the newest MAX_RECORDINGS files.
        files = sorted(n for n in os.listdir(RECORDINGS_DIR) if _REC_RE.match(n))
        for old in files[:-MAX_RECORDINGS]:
            try:
                os.remove(os.path.join(RECORDINGS_DIR, old))
            except OSError:
                pass
        return name
    except Exception:  # noqa: BLE001 - saving is best-effort debug only
        return ""


@app.get("/api/health")
def health():
    return {
        "status": "ok",
        "device": engines.DEVICE,
        "stt_available": stt.available(),
        "ollama_reachable": ollama.reachable(),
    }


@app.get("/api/settings")
def get_settings():
    return {
        "ollama_host": settings.get("ollama_host"),
        "ollama_model": settings.get("ollama_model"),
        "ollama_system_prompt": settings.get("ollama_system_prompt"),
    }


@app.post("/api/settings")
def update_settings(
    ollama_host: str = Form(""),
    ollama_model: str = Form(""),
    ollama_system_prompt: str = Form(""),
):
    """Persist settings. Blank fields keep the existing value."""
    updates = {}
    if ollama_host.strip():
        updates["ollama_host"] = ollama_host.strip()
    if ollama_model.strip():
        updates["ollama_model"] = ollama_model.strip()
    if ollama_system_prompt.strip():
        updates["ollama_system_prompt"] = ollama_system_prompt.strip()
    settings.save(updates)
    return get_settings()


@app.post("/api/chat")
def chat(text: str = Form(...)):
    """Send transcribed text to Ollama and return the assistant's reply.

    Used by the Mochi firmware between transcription and TTS.
    """
    try:
        reply = ollama.chat(text)
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))
    except RuntimeError as e:
        # Ollama down / model missing / timeout -> 503 with detail.
        raise HTTPException(status_code=503, detail=str(e))
    except Exception as e:  # noqa: BLE001
        raise HTTPException(status_code=500, detail=f"Chat failed: {e}")
    return {"reply": reply}


@app.post("/api/chat/reset")
def chat_reset():
    """Clear the rolling conversation history."""
    ollama.reset_history()
    return {"reset": True}


@app.get("/api/ollama/models")
def ollama_models():
    """List models installed on the Ollama server."""
    try:
        return {"models": ollama.list_models()}
    except RuntimeError as e:
        raise HTTPException(status_code=503, detail=str(e))
    except Exception as e:  # noqa: BLE001
        raise HTTPException(status_code=500, detail=f"Could not list models: {e}")


@app.post("/api/transcribe")
def transcribe(
    audio: UploadFile = File(...),
    language: str = Form(""),
):
    """Transcribe an uploaded recording to text (used by the Mochi firmware)."""
    raw = audio.file.read()
    saved = _save_recording(raw)              # debug: keep a copy to listen to
    wav_in, sr_in = _decode_wav_bytes(raw, audio.filename)
    try:
        result = stt.transcribe(wav_in, sr_in, language=language)
    except RuntimeError as e:
        # Missing engine -> 503 with install guidance.
        raise HTTPException(status_code=503, detail=str(e))
    except Exception as e:  # noqa: BLE001
        raise HTTPException(status_code=500, detail=f"Transcription failed: {e}")
    if saved:
        result["recording"] = saved
    return result


@app.get("/api/recordings")
def list_recordings():
    """List the most recent clips received from the device (newest first)."""
    out = []
    if os.path.isdir(RECORDINGS_DIR):
        for name in os.listdir(RECORDINGS_DIR):
            if not _REC_RE.match(name):
                continue
            try:
                st = os.stat(os.path.join(RECORDINGS_DIR, name))
            except OSError:
                continue
            out.append({"name": name, "size": st.st_size, "mtime": st.st_mtime})
    out.sort(key=lambda d: d["mtime"], reverse=True)
    return {"recordings": out, "saving": SAVE_RECORDINGS}


@app.get("/api/recordings/{name}")
def get_recording(name: str):
    """Serve a saved recording WAV (name is validated to prevent traversal)."""
    if not _REC_RE.match(name):
        raise HTTPException(status_code=404, detail="No such recording.")
    path = os.path.join(RECORDINGS_DIR, name)
    if not os.path.isfile(path):
        raise HTTPException(status_code=404, detail="No such recording.")
    return FileResponse(path, media_type="audio/wav")


@app.get("/api/models")
def list_models():
    return {"models": registry.list_models()}


@app.get("/api/models/{model_id}/options")
def model_options(model_id: str):
    try:
        return registry.options(model_id)
    except KeyError as e:
        raise HTTPException(status_code=404, detail=str(e))
    except Exception as e:  # noqa: BLE001
        raise HTTPException(status_code=503, detail=f"Could not load options: {e}")


@app.post("/api/models/{model_id}/download")
def start_download(model_id: str):
    try:
        return registry.start_download(model_id)
    except KeyError as e:
        raise HTTPException(status_code=404, detail=str(e))


@app.get("/api/models/{model_id}/download")
def download_status(model_id: str):
    try:
        return registry.download_status(model_id)
    except KeyError as e:
        raise HTTPException(status_code=404, detail=str(e))


def _short(text: str, n: int = 40) -> str:
    text = " ".join((text or "").split())
    return text if len(text) <= n else text[: n - 1] + "\u2026"


@app.post("/api/synthesize")
def synthesize(
    model_id: str = Form("kokoro-82m"),
    text: str = Form(...),
    language: str = Form("a"),
    voice: str = Form(""),
    speed: float = Form(1.0),
):
    """Enqueue a synthesis job and return immediately with the job record."""
    if model_id not in {m["id"] for m in registry.list_models()}:
        raise HTTPException(status_code=404, detail=f"Unknown model: {model_id}")

    params = {
        "text": text,
        "language": language,
        "voice": voice,
        "speed": speed,
    }
    voice_desc = voice or ""
    label = f"{voice_desc}: {_short(text)}" if voice_desc else _short(text)
    return job_queue.submit(model_id, params, label)


@app.get("/api/jobs")
def list_jobs():
    return {"jobs": job_queue.list()}


@app.get("/api/jobs/{job_id}")
def get_job(job_id: str):
    job = job_queue.get(job_id)
    if not job:
        raise HTTPException(status_code=404, detail="No such job.")
    return job


@app.get("/api/jobs/{job_id}/result")
def job_result(job_id: str):
    data = job_queue.result_bytes(job_id)
    if data is None:
        raise HTTPException(status_code=404, detail="No result available for this job.")
    return Response(
        content=data,
        media_type="audio/wav",
        headers={"Content-Disposition": f'inline; filename="tts-{job_id}.wav"'},
    )


@app.post("/api/jobs/{job_id}/cancel")
def cancel_job(job_id: str):
    try:
        return job_queue.cancel(job_id)
    except KeyError:
        raise HTTPException(status_code=404, detail="No such job.")
    except ValueError as e:
        raise HTTPException(status_code=409, detail=str(e))


@app.delete("/api/jobs/{job_id}")
def remove_job(job_id: str):
    try:
        ok = job_queue.remove(job_id)
    except ValueError as e:
        raise HTTPException(status_code=409, detail=str(e))
    if not ok:
        raise HTTPException(status_code=404, detail="No such job.")
    return {"removed": job_id}


@app.post("/api/jobs/clear")
def clear_jobs():
    return {"cleared": job_queue.clear_finished()}


# Serve the static frontend at the root. Mounted last so /api/* wins.
if os.path.isdir(FRONTEND_DIR):
    @app.get("/")
    def index():
        return FileResponse(os.path.join(FRONTEND_DIR, "index.html"))

    app.mount("/", StaticFiles(directory=FRONTEND_DIR), name="frontend")
else:
    @app.get("/")
    def index_missing():
        return JSONResponse(
            {"detail": f"Frontend not found at {FRONTEND_DIR}"}, status_code=500
        )


if __name__ == "__main__":
    import uvicorn

    host = os.environ.get("TTS_HOST", "127.0.0.1")
    port = int(os.environ.get("TTS_PORT", "8000"))
    uvicorn.run(app, host=host, port=port)
