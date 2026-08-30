"""
Model catalog + download-state tracking (Kokoro-only build).

Defines the single model the app knows about (Kokoro-82M), maps it to its
engine adapter, and manages background downloads with pollable progress. The
catalog shape is kept identical to the multi-model original so the same API
endpoints and frontend logic work unchanged.
"""

import subprocess
import sys
import threading
import time

from engines import KokoroEngine


def _pip_install(packages):
    """Install packages into the current interpreter's environment."""
    cmd = [sys.executable, "-m", "pip", "install", *packages]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(
            "pip install failed for %s:\n%s"
            % (" ".join(packages), (proc.stderr or proc.stdout)[-2000:])
        )


# Catalog entries: (id, engine_class, repo_id, label, approx_size)
_CATALOG_DEFS = [
    ("kokoro-82m", KokoroEngine, "hexgrad/Kokoro-82M",
     "Kokoro 82M - Fast preset voices", "~350 MB"),
]


class DownloadProgress:
    """Thread-safe byte-progress aggregator for a single model download."""

    def __init__(self):
        self._lock = threading.Lock()
        self.state = "idle"           # idle | downloading | done | error
        self.error = None
        self.stage = None             # human-readable current step
        self.started = None
        self._files = {}              # key -> (downloaded, total)

    def start(self):
        with self._lock:
            self.state = "downloading"
            self.started = time.time()
            self._files = {}
            self.error = None
            self.stage = "Starting..."

    def set_stage(self, stage):
        with self._lock:
            self.stage = stage

    def update_file(self, key, downloaded, total):
        with self._lock:
            self._files[key] = (int(downloaded or 0), int(total or 0))

    def finish(self):
        with self._lock:
            self.state = "done"

    def fail(self, msg):
        with self._lock:
            self.state = "error"
            self.error = str(msg)

    def snapshot(self) -> dict:
        with self._lock:
            downloaded = sum(d for d, _ in self._files.values())
            total = sum(t for _, t in self._files.values())
            pct = (downloaded / total * 100.0) if total else 0.0
            return {
                "state": self.state,
                "error": self.error,
                "stage": self.stage,
                "downloaded_bytes": downloaded,
                "total_bytes": total,
                "percent": round(pct, 1),
            }


class Registry:
    def __init__(self):
        self._engines = {}
        self._defs = {}
        self._progress = {}
        self._threads = {}
        self._lock = threading.Lock()
        for mid, cls, repo, label, size in _CATALOG_DEFS:
            self._defs[mid] = (cls, repo, label, size)

    def _engine(self, model_id: str):
        if model_id not in self._defs:
            raise KeyError(f"Unknown model id: {model_id}")
        with self._lock:
            eng = self._engines.get(model_id)
            if eng is None:
                cls, repo, label, _size = self._defs[model_id]
                eng = cls(model_id, repo, label)
                self._engines[model_id] = eng
            return eng

    def list_models(self) -> list:
        out = []
        for mid, (cls, repo, label, size) in self._defs.items():
            eng = self._engine(mid)
            prog = self._progress.get(mid)
            needs = eng.needs()
            out.append({
                **eng.caps(),
                "repo_id": repo,
                "approx_size": size,
                "downloaded": eng.is_downloaded(),
                "runtime_available": eng.runtime_available(),
                "needs": needs,           # subset of ["package", "weights"]
                "ready": not needs,
                "pip_packages": list(eng.pip_packages),
                "download": prog.snapshot() if prog else {"state": "idle"},
            })
        return out

    def options(self, model_id: str) -> dict:
        return self._engine(model_id).options()

    def start_download(self, model_id: str) -> dict:
        eng = self._engine(model_id)
        with self._lock:
            existing = self._progress.get(model_id)
            if existing and existing.state == "downloading":
                return existing.snapshot()
            prog = DownloadProgress()
            self._progress[model_id] = prog

        prog.start()

        def _run():
            try:
                # 1) Install the runtime package if it's missing.
                if not eng.runtime_available() and eng.pip_packages:
                    prog.set_stage("Installing package...")
                    _pip_install(eng.pip_packages)
                    if not eng.runtime_available():
                        raise RuntimeError(
                            "Package install completed but the module is still "
                            "not importable. See the server log."
                        )
                # 2) Download the weights.
                if not eng.is_downloaded():
                    prog.set_stage("Downloading weights...")
                    eng.download(prog)
                prog.finish()
            except Exception as e:  # noqa: BLE001
                prog.fail(e)

        t = threading.Thread(target=_run, daemon=True)
        self._threads[model_id] = t
        t.start()
        return prog.snapshot()

    def synthesize(self, model_id: str, params: dict):
        eng = self._engine(model_id)
        needs = eng.needs()
        if needs:
            what = " and ".join(needs)
            raise RuntimeError(
                f"Model '{model_id}' is not ready (missing: {what}). "
                f"Click Set up / Download first."
            )
        return eng.synthesize(params)

    def download_status(self, model_id: str) -> dict:
        prog = self._progress.get(model_id)
        eng = self._engine(model_id)
        snap = prog.snapshot() if prog else {"state": "idle"}
        snap["downloaded"] = eng.is_downloaded()
        snap["ready"] = eng.ready()
        snap["needs"] = eng.needs()
        return snap

    def default_model_for(self, input_type: str) -> str:
        for mid, (cls, repo, label, size) in self._defs.items():
            if self._engine(mid).input_type == input_type:
                return mid
        return next(iter(self._defs))


registry = Registry()
