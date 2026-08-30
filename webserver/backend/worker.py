"""
Out-of-process synthesis worker.

Rendering runs in a separate OS process so a running job can be truly canceled
by terminating that process (the model's generate() call is blocking C/Python
and can't be interrupted inside a thread).

Design:
  - One long-lived worker process handles tasks sequentially, keeping models
    loaded in memory between jobs (so the normal path stays fast/warm).
  - To cancel a running job, the parent terminates the process; the parent then
    lazily respawns a fresh worker for subsequent jobs (models reload on next
    use). This only pays the reload cost when you actually cancel.

Communication uses two multiprocessing Queues:
  task_q   parent -> worker : (job_id, model_id, params) or None to stop
  result_q worker -> parent : (job_id, "done", wav_bytes, sample_rate)
                              (job_id, "error", message, None)
"""

import io
import multiprocessing as mp
import queue as pyqueue
import threading
import time


def _worker_loop(task_q, result_q):
    """Child-process entry point. Loads models on demand and renders tasks."""
    # Imported inside the child so the heavy deps load in this process only.
    import numpy as np
    import soundfile as sf
    from registry import registry

    while True:
        task = task_q.get()
        if task is None:
            break
        job_id, model_id, params = task
        try:
            wav, sr = registry.synthesize(model_id, params)
            buf = io.BytesIO()
            sf.write(buf, np.asarray(wav, dtype=np.float32), int(sr),
                     format="WAV", subtype="PCM_16")
            result_q.put((job_id, "done", buf.getvalue(), int(sr)))
        except Exception as e:  # noqa: BLE001
            result_q.put((job_id, "error", str(e), None))


class Canceled(Exception):
    """Raised when a running render was canceled by terminating the worker."""


class ProcessRenderer:
    """Owns the worker process and renders one job at a time, cancelably."""

    def __init__(self):
        # 'spawn' is the only start method on Windows and is safe cross-platform.
        self._ctx = mp.get_context("spawn")
        self._proc = None
        self._task_q = None
        self._result_q = None
        self._lock = threading.Lock()
        self._cancel_job = None   # job id requested for cancellation

    def _ensure_process(self):
        if self._proc is not None and self._proc.is_alive():
            return
        self._task_q = self._ctx.Queue()
        self._result_q = self._ctx.Queue()
        self._proc = self._ctx.Process(
            target=_worker_loop, args=(self._task_q, self._result_q), daemon=True
        )
        self._proc.start()

    def request_cancel(self, job_id: str):
        """Signal that the given running job should be canceled."""
        with self._lock:
            self._cancel_job = job_id

    def _kill(self):
        if self._proc is not None:
            try:
                self._proc.terminate()
                self._proc.join(timeout=5)
            except Exception:
                pass
        self._proc = None
        self._task_q = None
        self._result_q = None

    def render(self, job_id: str, model_id: str, params: dict):
        """Render a job in the worker. Raises Canceled if canceled mid-flight."""
        with self._lock:
            self._cancel_job = None
        self._ensure_process()
        self._task_q.put((job_id, model_id, params))

        while True:
            # If cancellation was requested for this job, kill the worker now.
            with self._lock:
                if self._cancel_job == job_id:
                    self._cancel_job = None
                    self._kill()
                    raise Canceled()

            try:
                rid, kind, payload, sr = self._result_q.get(timeout=0.25)
            except pyqueue.Empty:
                # Worker died unexpectedly (e.g. terminated) -> treat as cancel.
                if self._proc is None or not self._proc.is_alive():
                    raise Canceled()
                continue

            if rid != job_id:
                continue  # stale result from a killed job; ignore
            if kind == "done":
                return payload, sr
            raise RuntimeError(payload or "Synthesis failed.")

    def shutdown(self):
        with self._lock:
            if self._task_q is not None:
                try:
                    self._task_q.put(None)
                except Exception:
                    pass
        self._kill()
