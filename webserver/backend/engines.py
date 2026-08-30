"""
Kokoro TTS engine for the local web app.

This is a Kokoro-only build. The engine adapts the Kokoro-82M model behind a
small common interface so the server and frontend can treat it uniformly:

    caps()          -> capability dict (what inputs the UI should show)
    is_downloaded() -> bool (weights present in local cache)
    download(prog)  -> fetch weights, reporting progress via `prog`
    load()          -> load the model into memory (lazy, cached)
    options()       -> dynamic choices (voices)
    synthesize(...) -> (float32 mono waveform, sample_rate)

Everything runs on CPU by default which suits an Intel-only laptop.
"""

import os
import threading

# Route TLS through the OS trust store before any download (corporate proxies).
import certs  # noqa: F401

import numpy as np

DEVICE = os.environ.get("TTS_DEVICE", "cpu")

# Output sample rate for synthesized audio. Kokoro renders at 24 kHz, but the
# ESP32-C3 buffers the whole WAV in RAM to play it smoothly, so smaller is
# better: 16 kHz halves the byte size (vs 24 kHz) while staying clear speech.
# Lower to 12000 or 8000 if long replies still exceed the device's memory.
try:
    TTS_OUTPUT_RATE = int(os.environ.get("TTS_OUTPUT_RATE", "16000"))
except ValueError:
    TTS_OUTPUT_RATE = 16000


def _resample(wav: "np.ndarray", sr_in: int, sr_out: int) -> "np.ndarray":
    """Resample a mono float32 waveform. When downsampling, a short moving-average
    low-pass tames aliasing so speech stays clear. Dependency-free (numpy only)."""
    if sr_out == sr_in or len(wav) < 2:
        return np.asarray(wav, dtype=np.float32)
    x = np.asarray(wav, dtype=np.float32)
    if sr_out < sr_in:
        k = max(1, int(round(sr_in / sr_out)))
        if k > 1:
            kernel = np.ones(k, dtype=np.float32) / k
            x = np.convolve(x, kernel, mode="same").astype(np.float32)
    n_out = int(round(len(x) * sr_out / sr_in))
    if n_out < 1:
        return x
    xp = np.linspace(0.0, 1.0, num=len(x), endpoint=False)
    xq = np.linspace(0.0, 1.0, num=n_out, endpoint=False)
    return np.interp(xq, xp, x).astype(np.float32)

# Common Kokoro voices (a* = US English, b* = UK English).
KOKORO_VOICES = [
    "af_heart", "af_bella", "af_nicole", "af_sarah", "af_sky",
    "am_adam", "am_michael",
    "bf_emma", "bf_isabella", "bm_george", "bm_lewis",
]


def _hf_cached(repo_id: str, filename: str) -> bool:
    """True if `filename` from `repo_id` is present in the local HF cache."""
    try:
        from huggingface_hub import try_to_load_from_cache

        res = try_to_load_from_cache(repo_id, filename)
        return isinstance(res, str)
    except Exception:
        return False


def _hf_snapshot_download(repo_id: str, progress, allow_patterns=None):
    """Download an entire HF repo, reporting byte progress via `progress`."""
    from huggingface_hub import snapshot_download

    tqdm_cls = _make_tqdm_class(progress)
    snapshot_download(
        repo_id,
        tqdm_class=tqdm_cls,
        allow_patterns=allow_patterns,
    )


def _make_tqdm_class(progress):
    """Build a tqdm subclass that forwards byte counts to a DownloadProgress."""
    from tqdm.auto import tqdm as _base

    class _T(_base):
        def __init__(self, *a, **k):
            super().__init__(*a, **k)
            self._pkey = id(self)

        def update(self, n=1):
            r = super().update(n)
            try:
                progress.update_file(self._pkey, self.n, self.total)
            except Exception:
                pass
            return r

        def close(self):
            try:
                progress.update_file(self._pkey, self.n, self.total)
            except Exception:
                pass
            return super().close()

    return _T


class BaseEngine:
    #: one of "clone" | "speaker" | "voice" - drives which inputs the UI shows
    input_type = "voice"
    family = "Other"
    #: python import names this engine needs at runtime (checked for readiness)
    pip_imports = ()
    #: pip package names to install if the imports are missing
    pip_packages = ()

    def __init__(self, model_id: str, repo_id: str, label: str):
        self.model_id = model_id      # our catalog key, e.g. "kokoro-82m"
        self.repo_id = repo_id        # HF repo id or local path
        self.label = label
        self._model = None
        self._lock = threading.Lock()

    # ---- capability description consumed by the frontend ----
    def caps(self) -> dict:
        return {
            "id": self.model_id,
            "label": self.label,
            "family": self.family,
            "input_type": self.input_type,
            "languages": [],
            "has_instruct": False,
            "has_speed": False,
        }

    # ---- readiness ----
    def runtime_available(self) -> bool:
        """True if the python packages this engine needs are importable."""
        import importlib.util

        for mod in self.pip_imports:
            if importlib.util.find_spec(mod) is None:
                return False
        return True

    def needs(self) -> list:
        """What's missing before this model can generate: 'package', 'weights'."""
        missing = []
        if not self.runtime_available():
            missing.append("package")
        if not self.is_downloaded():
            missing.append("weights")
        return missing

    def ready(self) -> bool:
        return not self.needs()

    # ---- download / cache status ----
    def is_downloaded(self) -> bool:  # pragma: no cover - overridden
        raise NotImplementedError

    def download(self, progress):  # pragma: no cover - overridden
        raise NotImplementedError

    # ---- model loading ----
    def load(self):  # pragma: no cover - overridden
        raise NotImplementedError

    def options(self) -> dict:
        """Dynamic choices for the UI (voices). Default: none."""
        return {}

    def synthesize(self, params: dict):  # pragma: no cover - overridden
        raise NotImplementedError

    @staticmethod
    def _to_wav(wavs, sr):
        wav = np.asarray(wavs[0], dtype=np.float32)
        return wav, int(sr)


# --------------------------------------------------------------------------
# Kokoro engine
# --------------------------------------------------------------------------
class KokoroEngine(BaseEngine):
    input_type = "voice"
    family = "Kokoro"
    pip_imports = ("kokoro",)
    pip_packages = ("kokoro",)

    def caps(self) -> dict:
        c = super().caps()
        # Kokoro 'a' = US English, 'b' = UK English (match the voice prefix).
        c.update(languages=["a", "b"], has_speed=True)
        return c

    def is_downloaded(self) -> bool:
        # The kokoro package pulls weights from hexgrad/Kokoro-82M.
        return _hf_cached("hexgrad/Kokoro-82M", "config.json")

    def download(self, progress):
        _hf_snapshot_download(
            "hexgrad/Kokoro-82M",
            progress,
            allow_patterns=["*.json", "*.pth", "*.txt", "voices/*"],
        )

    def load(self):
        if self._model is not None:
            return self._model
        with self._lock:
            if self._model is not None:
                return self._model
            try:
                from kokoro import KPipeline  # noqa: F401
            except Exception as e:  # noqa: BLE001
                raise RuntimeError(
                    "The 'kokoro' package is not installed. Install it with "
                    "`pip install kokoro soundfile` (and espeak-ng for some "
                    "languages). See README."
                ) from e
            # Pipeline is created per language code at synth time; cache a dict.
            self._model = {}
            return self._model

    def options(self) -> dict:
        return {"voices": KOKORO_VOICES}

    def synthesize(self, params: dict):
        text = (params.get("text") or "").strip()
        if not text:
            raise ValueError("`text` is required.")
        voice = (params.get("voice") or "af_heart").strip()
        lang = (params.get("language") or "a").strip() or "a"
        # Guard against non-Kokoro language values (e.g. "Auto"): Kokoro expects
        # a single-letter code. Fall back to the voice prefix, then US English.
        if lang not in ("a", "b"):
            lang = voice[0] if voice and voice[0] in ("a", "b") else "a"
        try:
            speed = float(params.get("speed") or 1.0)
        except (TypeError, ValueError):
            speed = 1.0

        pipelines = self.load()
        from kokoro import KPipeline

        if lang not in pipelines:
            pipelines[lang] = KPipeline(lang_code=lang)
        pipeline = pipelines[lang]

        # Kokoro yields audio in chunks; concatenate into one waveform.
        chunks = []
        for _, _, audio in pipeline(text, voice=voice, speed=speed):
            arr = audio.detach().cpu().numpy() if hasattr(audio, "detach") else np.asarray(audio)
            chunks.append(np.asarray(arr, dtype=np.float32).reshape(-1))
        if not chunks:
            raise RuntimeError("Kokoro produced no audio.")
        wav = np.concatenate(chunks)
        # Downsample so the clip is small enough to fit in the device's RAM.
        if TTS_OUTPUT_RATE != 24000:
            wav = _resample(wav, 24000, TTS_OUTPUT_RATE)
        return wav, TTS_OUTPUT_RATE
