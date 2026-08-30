"""
Synthesis job queue.

Turns synthesis into asynchronous jobs so the UI can submit work, watch a
queue, and cancel items instead of blocking on a slow CPU render.

Model:
  - Jobs are processed FIFO by a single background worker thread that delegates
    the actual render to a separate OS process (see worker.ProcessRenderer).
  - States: queued -> running -> done | error | canceled.
  - Cancel works on BOTH queued and running jobs. Cancelling a queued job just
    drops it; cancelling the running job terminates the render process (the
    model's generate() call is blocking and can't be interrupted in-thread),
    and a fresh worker is spawned lazily for the next job.
  - Finished audio is stored in memory keyed by job id and served on demand.
    Old finished jobs are trimmed to bound memory use.
"""

import threading
import time
import uuid
from collections import OrderedDict

from worker import ProcessRenderer, Canceled

# Keep at most this many finished (done/error/canceled) jobs before trimming
# the oldest, to bound memory from stored WAVs.
MAX_FINISHED = 50


class Job:
    def __init__(self, model_id: str, params: dict, label: str):
        self.id = uuid.uuid4().hex[:12]
        self.model_id = model_id
        self.params = params
        self.label = label          # short human description for the queue UI
        self.state = "queued"       # queued | running | done | error | canceled
        self.error = None
        self.created = time.time()
        self.started = None
        self.finished = None
        self.sample_rate = None
        self.wav_bytes = None       # encoded WAV once done
        # Character count drives the throughput-based time estimate.
        self.char_count = len((params or {}).get("text") or "")
        # Seconds-per-char estimate for this model at submit time (may be None
        # until at least one render of this model has completed).
        self.eta_seconds = None

    def public(self, position=None, now=None) -> dict:
        now = now or time.time()
        elapsed = None
        if self.started:
            end = self.finished or now
            elapsed = round(end - self.started, 1)

        # Estimated progress for a running job: elapsed / predicted total.
        est_percent = None
        if self.state in ("running", "canceling") and self.eta_seconds and elapsed is not None:
            est_percent = max(1, min(99, round(elapsed / self.eta_seconds * 100)))

        d = {
            "id": self.id,
            "model_id": self.model_id,
            "label": self.label,
            "state": self.state,
            "error": self.error,
            "created": self.created,
            "started": self.started,
            "finished": self.finished,
            "elapsed": elapsed,
            "eta_seconds": round(self.eta_seconds, 1) if self.eta_seconds else None,
            "est_percent": est_percent,
            "char_count": self.char_count,
            "has_result": self.wav_bytes is not None,
        }
        if position is not None:
            d["position"] = position
        return d


class JobQueue:
    def __init__(self, synth_fn=None):
        # synth_fn is accepted for backward compatibility but unused: rendering
        # now happens in a separate process via ProcessRenderer.
        self._renderer = ProcessRenderer()
        self._lock = threading.Lock()
        self._cv = threading.Condition(self._lock)
        self._jobs = OrderedDict()     # id -> Job (insertion order = history)
        self._pending = []             # ids waiting to run (FIFO)
        self._current = None           # id of the running job, or None
        # Learned seconds-per-character per model, for time estimates. Updated
        # with an exponential moving average as jobs complete.
        self._spc = {}                 # model_id -> seconds per char
        self._worker = threading.Thread(target=self._run, daemon=True)
        self._worker.start()

    def _estimate_for(self, model_id: str, char_count: int):
        """Predicted render seconds for a model + text length, or None if unknown."""
        spc = self._spc.get(model_id)
        if not spc:
            return None
        # Floor the char count so very short texts still get a sane estimate.
        return spc * max(char_count, 8)

    def _learn(self, model_id: str, char_count: int, seconds: float):
        """Update the seconds-per-char EMA for a model from a finished render."""
        if char_count < 1 or seconds <= 0:
            return
        sample = seconds / char_count
        prev = self._spc.get(model_id)
        # EMA smooths out one-off variance (first sample seeds the value).
        self._spc[model_id] = sample if prev is None else (0.6 * prev + 0.4 * sample)

    # ---- submission ----
    def submit(self, model_id: str, params: dict, label: str) -> dict:
        job = Job(model_id, params, label)
        with self._cv:
            self._jobs[job.id] = job
            self._pending.append(job.id)
            self._trim_finished_locked()
            self._cv.notify()
            return job.public(position=self._position_locked(job.id))

    # ---- queries ----
    def list(self) -> list:
        now = time.time()
        with self._lock:
            out = []
            for jid, job in self._jobs.items():
                out.append(job.public(position=self._position_locked(jid), now=now))
            # newest first is friendlier for a history list
            out.sort(key=lambda d: d["created"], reverse=True)
            return out

    def get(self, job_id: str):
        with self._lock:
            job = self._jobs.get(job_id)
            if not job:
                return None
            return job.public(position=self._position_locked(job_id))

    def result_bytes(self, job_id: str):
        with self._lock:
            job = self._jobs.get(job_id)
            if not job or job.wav_bytes is None:
                return None
            return job.wav_bytes

    # ---- mutations ----
    def cancel(self, job_id: str) -> dict:
        running_to_kill = None
        with self._lock:
            job = self._jobs.get(job_id)
            if not job:
                raise KeyError(job_id)
            if job.state == "queued":
                job.state = "canceled"
                job.finished = time.time()
                if job_id in self._pending:
                    self._pending.remove(job_id)
                return job.public()
            if job.state == "running":
                # Mark intent; the render loop will observe the kill and finalize
                # the job as canceled. Also signal the renderer to terminate.
                job.state = "canceling"
                running_to_kill = job_id
            else:
                # done / error / canceled already
                return job.public()

        # Terminate the render process outside the lock.
        if running_to_kill:
            self._renderer.request_cancel(running_to_kill)
        with self._lock:
            return self._jobs[job_id].public()

    def remove(self, job_id: str) -> bool:
        with self._lock:
            job = self._jobs.get(job_id)
            if not job:
                return False
            if job.state == "running":
                raise ValueError("Can't remove a job while it is rendering.")
            if job_id in self._pending:
                self._pending.remove(job_id)
            del self._jobs[job_id]
            return True

    def clear_finished(self) -> int:
        with self._lock:
            done_ids = [
                jid for jid, j in self._jobs.items()
                if j.state in ("done", "error", "canceled")
            ]
            for jid in done_ids:
                del self._jobs[jid]
            return len(done_ids)

    # ---- internals ----
    def _position_locked(self, job_id: str):
        """1-based position in the pending queue, or None if not queued."""
        if job_id in self._pending:
            return self._pending.index(job_id) + 1
        return None

    def _trim_finished_locked(self):
        finished = [
            jid for jid, j in self._jobs.items()
            if j.state in ("done", "error", "canceled")
        ]
        excess = len(finished) - MAX_FINISHED
        for jid in finished[:max(0, excess)]:
            del self._jobs[jid]

    def _next_locked(self):
        while self._pending:
            jid = self._pending.pop(0)
            job = self._jobs.get(jid)
            if job and job.state == "queued":
                return job
        return None

    def _run(self):
        while True:
            with self._cv:
                job = self._next_locked()
                while job is None:
                    self._cv.wait()
                    job = self._next_locked()
                job.state = "running"
                job.started = time.time()
                job.eta_seconds = self._estimate_for(job.model_id, job.char_count)
                self._current = job.id

            # Render in the worker process so it can be killed on cancel.
            try:
                wav_bytes, sr = self._renderer.render(
                    job.id, job.model_id, job.params
                )
                with self._lock:
                    job.wav_bytes = wav_bytes
                    job.sample_rate = int(sr)
                    job.state = "done"
                    job.finished = time.time()
                    self._learn(job.model_id, job.char_count,
                                job.finished - job.started)
            except Canceled:
                with self._lock:
                    job.state = "canceled"
                    job.finished = time.time()
            except Exception as e:  # noqa: BLE001
                with self._lock:
                    job.state = "error"
                    job.error = str(e)
                    job.finished = time.time()
            finally:
                with self._lock:
                    self._current = None
