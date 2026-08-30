"""
Speech-to-text for transcription (used by the Chikadora firmware's /api/transcribe).

Uses faster-whisper (CTranslate2) which runs well on CPU and downloads a small
model from Hugging Face on first use. Kept optional and lazily loaded: if the
package isn't installed, the API returns a clear, actionable error instead of
crashing.

Environment variables:
  STT_MODEL     faster-whisper model size or repo. Default: "base"
                (options: tiny, base, small, medium, large-v3, ...)
  STT_DEVICE    "cpu" (default) or "cuda".
  STT_LANGUAGE  Default language when the request doesn't specify one. Default
                "en". Forcing a language is far more reliable than auto-detect
                on short clips. Set "auto" to let Whisper guess.
  STT_VAD       "1" to enable the voice-activity filter, "0" (default) to
                disable it. The VAD often drops short/quiet commands entirely,
                which shows up as "clear audio but no transcript".
"""

import os
import threading

# Ensure downloads work behind corporate TLS inspection.
import certs  # noqa: F401

import numpy as np

STT_MODEL = os.environ.get("STT_MODEL", "base")
STT_DEVICE = os.environ.get("STT_DEVICE", "cpu")
STT_LANGUAGE = os.environ.get("STT_LANGUAGE", "en")
STT_VAD = os.environ.get("STT_VAD", "0") in ("1", "true", "True")

# Map language names / short codes to Whisper's 2-letter codes.
# "Auto" / unknown -> None lets Whisper auto-detect.
_LANG_TO_WHISPER = {
    "english": "en", "chinese": "zh", "japanese": "ja", "korean": "ko",
    "german": "de", "french": "fr", "russian": "ru", "portuguese": "pt",
    "spanish": "es", "italian": "it",
    # Kokoro short codes
    "a": "en", "b": "en",
}

_model = None
_lock = threading.Lock()


def available() -> bool:
    import importlib.util
    return importlib.util.find_spec("faster_whisper") is not None


def _load():
    global _model
    if _model is not None:
        return _model
    with _lock:
        if _model is not None:
            return _model
        from faster_whisper import WhisperModel

        # int8 keeps it light and fast on CPU.
        compute_type = "int8" if STT_DEVICE == "cpu" else "float16"
        _model = WhisperModel(STT_MODEL, device=STT_DEVICE, compute_type=compute_type)
        return _model


def _to_16k_mono(wav: np.ndarray, sr: int) -> np.ndarray:
    """Whisper expects 16 kHz mono float32. Downmix + linear-resample."""
    wav = np.asarray(wav, dtype=np.float32)
    if wav.ndim > 1:
        wav = wav.mean(axis=1)
    if sr != 16000 and len(wav) > 1:
        # Simple linear resample; adequate for ASR.
        duration = len(wav) / sr
        new_len = int(round(duration * 16000))
        if new_len > 0:
            xp = np.linspace(0.0, 1.0, num=len(wav), endpoint=False)
            xq = np.linspace(0.0, 1.0, num=new_len, endpoint=False)
            wav = np.interp(xq, xp, wav).astype(np.float32)
    return wav


def transcribe(wav: np.ndarray, sr: int, language: str = "") -> dict:
    """
    Transcribe a waveform to text.

    Returns { "text": str, "language": str }.
    Raises RuntimeError with install instructions if the engine is missing.
    """
    if not available():
        raise RuntimeError(
            "Transcription needs the 'faster-whisper' package. Install it "
            "with: venv\\Scripts\\python.exe -m pip install faster-whisper"
        )

    audio = _to_16k_mono(wav, sr)
    model = _load()

    # Resolve the language: request value -> STT_LANGUAGE default -> auto.
    lang_code = _LANG_TO_WHISPER.get((language or "").strip().lower())
    if lang_code is None:
        default = STT_LANGUAGE.strip().lower()
        lang_code = None if default in ("", "auto") else default

    segments, info = model.transcribe(
        audio,
        language=lang_code,             # forced language is reliable on short clips
        beam_size=5,
        vad_filter=STT_VAD,             # VAD off by default: it drops short commands
        condition_on_previous_text=False,  # each command is independent
        # Make Whisper less eager to discard a short/quiet clip as "silence".
        no_speech_threshold=0.85,
        temperature=[0.0, 0.2, 0.4],    # fallbacks if the greedy decode is unsure
    )
    text = " ".join(seg.text.strip() for seg in segments).strip()
    return {"text": text, "language": getattr(info, "language", lang_code or "")}
