"""
Speech-to-text for transcription (used by the Chikadora firmware's /api/transcribe).

Two selectable engines behind one interface (available() / transcribe()):

  faster-whisper (default) - CTranslate2 Whisper. Robust, multilingual.
  moonshine                - Moonshine (Moonshine AI). Optimized for short/medium
                             English speech; low latency on short clips. Runs via
                             Hugging Face Transformers.

Both download their model from Hugging Face on first use and are lazily loaded;
if the required package isn't installed the API returns a clear, actionable
error instead of crashing.

Environment variables:
  STT_ENGINE      "faster-whisper" (default) or "moonshine".
  STT_MODEL       faster-whisper model size/repo. Default "base"
                  (tiny, base, small, medium, large-v3, ...).
  MOONSHINE_MODEL Moonshine HF repo. Default "UsefulSensors/moonshine-base"
                  (or "UsefulSensors/moonshine-tiny").
  STT_DEVICE      "cpu" (default) or "cuda".
  STT_LANGUAGE    Default language when the request doesn't specify one. Default
                  "en". (faster-whisper only; Moonshine base/tiny are English.)
                  Set "auto" to let Whisper guess.
  STT_VAD         "1" to enable the voice-activity filter, "0" (default) to
                  disable it (faster-whisper only). The VAD often drops short
                  commands, showing up as "clear audio but no transcript".
"""

import importlib.util
import os
import threading

# Ensure downloads work behind corporate TLS inspection.
import certs  # noqa: F401

import numpy as np

STT_ENGINE = os.environ.get("STT_ENGINE", "faster-whisper").strip().lower()
STT_MODEL = os.environ.get("STT_MODEL", "base")
MOONSHINE_MODEL = os.environ.get("MOONSHINE_MODEL", "UsefulSensors/moonshine-base")
STT_DEVICE = os.environ.get("STT_DEVICE", "cpu")
STT_LANGUAGE = os.environ.get("STT_LANGUAGE", "en")
STT_VAD = os.environ.get("STT_VAD", "0") in ("1", "true", "True")

# Map language names / short codes to Whisper's 2-letter codes.
_LANG_TO_WHISPER = {
    "english": "en", "chinese": "zh", "japanese": "ja", "korean": "ko",
    "german": "de", "french": "fr", "russian": "ru", "portuguese": "pt",
    "spanish": "es", "italian": "it",
    # Kokoro short codes
    "a": "en", "b": "en",
}

_model = None          # cached engine handle (type depends on STT_ENGINE)
_lock = threading.Lock()


def available() -> bool:
    """True if the selected engine's package is importable."""
    if STT_ENGINE == "moonshine":
        return importlib.util.find_spec("transformers") is not None
    return importlib.util.find_spec("faster_whisper") is not None


def _missing_package_error() -> RuntimeError:
    if STT_ENGINE == "moonshine":
        return RuntimeError(
            "Moonshine STT needs 'transformers'. Install it with: "
            "venv\\Scripts\\python.exe -m pip install transformers"
        )
    return RuntimeError(
        "Transcription needs the 'faster-whisper' package. Install it with: "
        "venv\\Scripts\\python.exe -m pip install faster-whisper"
    )


def _to_16k_mono(wav: np.ndarray, sr: int) -> np.ndarray:
    """Both engines expect 16 kHz mono float32. Downmix + linear-resample."""
    wav = np.asarray(wav, dtype=np.float32)
    if wav.ndim > 1:
        wav = wav.mean(axis=1)
    if sr != 16000 and len(wav) > 1:
        duration = len(wav) / sr
        new_len = int(round(duration * 16000))
        if new_len > 0:
            xp = np.linspace(0.0, 1.0, num=len(wav), endpoint=False)
            xq = np.linspace(0.0, 1.0, num=new_len, endpoint=False)
            wav = np.interp(xq, xp, wav).astype(np.float32)
    return wav


# ---------------------------------------------------------------------------
# faster-whisper backend
# ---------------------------------------------------------------------------
def _load_faster_whisper():
    from faster_whisper import WhisperModel
    compute_type = "int8" if STT_DEVICE == "cpu" else "float16"
    return WhisperModel(STT_MODEL, device=STT_DEVICE, compute_type=compute_type)


def _transcribe_faster_whisper(audio: np.ndarray, language: str) -> dict:
    model = _load()

    lang_code = _LANG_TO_WHISPER.get((language or "").strip().lower())
    if lang_code is None:
        default = STT_LANGUAGE.strip().lower()
        lang_code = None if default in ("", "auto") else default

    segments, info = model.transcribe(
        audio,
        language=lang_code,
        beam_size=5,
        vad_filter=STT_VAD,
        condition_on_previous_text=False,
        no_speech_threshold=0.85,
        temperature=[0.0, 0.2, 0.4],
    )
    text = " ".join(seg.text.strip() for seg in segments).strip()
    return {"text": text, "language": getattr(info, "language", lang_code or "")}


# ---------------------------------------------------------------------------
# Moonshine backend (Hugging Face Transformers)
# ---------------------------------------------------------------------------
def _load_moonshine():
    from transformers import AutoProcessor, MoonshineForConditionalGeneration
    processor = AutoProcessor.from_pretrained(MOONSHINE_MODEL)
    model = MoonshineForConditionalGeneration.from_pretrained(MOONSHINE_MODEL)
    if STT_DEVICE != "cpu":
        model = model.to(STT_DEVICE)
    return (processor, model)


def _transcribe_moonshine(audio: np.ndarray, language: str) -> dict:
    import torch  # provided by transformers' backend

    processor, model = _load()
    inputs = processor(audio, sampling_rate=16000, return_tensors="pt")
    if STT_DEVICE != "cpu":
        inputs = {k: v.to(STT_DEVICE) for k, v in inputs.items()}

    # Moonshine recommends bounding output length by audio duration
    # (~6 tokens/sec of speech is plenty), which also avoids runaway decoding.
    seconds = max(1.0, len(audio) / 16000.0)
    max_new = min(512, int(seconds * 6) + 16)

    with torch.no_grad():
        generated = model.generate(**inputs, max_new_tokens=max_new)
    text = processor.batch_decode(generated, skip_special_tokens=True)[0].strip()
    return {"text": text, "language": "en"}  # Moonshine base/tiny are English


# ---------------------------------------------------------------------------
# Shared entry points
# ---------------------------------------------------------------------------
def _load():
    global _model
    if _model is not None:
        return _model
    with _lock:
        if _model is not None:
            return _model
        _model = _load_moonshine() if STT_ENGINE == "moonshine" else _load_faster_whisper()
        return _model


def transcribe(wav: np.ndarray, sr: int, language: str = "") -> dict:
    """Transcribe a waveform to text. Returns { "text": str, "language": str }."""
    if not available():
        raise _missing_package_error()

    audio = _to_16k_mono(wav, sr)
    if STT_ENGINE == "moonshine":
        return _transcribe_moonshine(audio, language)
    return _transcribe_faster_whisper(audio, language)
