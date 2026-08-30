"""
Ollama chat client.

Talks to a local Ollama server (default http://localhost:11434) over HTTP using
stdlib urllib - no API key, nothing leaves the machine. Keeps a short rolling
conversation history in memory so the assistant has context across turns.

Ollama endpoints used:
  POST /api/chat   -> generate a reply from a messages list
  GET  /api/tags   -> list locally installed models
"""

import json
import threading
import urllib.error
import urllib.request

import settings

# Cap the rolling history to bound request size / context. Counts individual
# messages (user + assistant), so 12 == roughly the last 6 exchanges.
_MAX_HISTORY = 12

_lock = threading.Lock()
_history = []  # list of {"role": "user"|"assistant", "content": str}


def reset_history() -> None:
    with _lock:
        _history.clear()


def _host() -> str:
    return (settings.get("ollama_host") or "http://localhost:11434").rstrip("/")


def _post_json(url: str, payload: dict, timeout: float = 120.0) -> dict:
    body = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=body, method="POST")
    req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def chat(prompt: str) -> str:
    """Send `prompt` to Ollama with system prompt + rolling history.

    Returns the assistant's reply text. Raises RuntimeError with a clear message
    on connection / model / timeout errors.
    """
    prompt = (prompt or "").strip()
    if not prompt:
        raise ValueError("`text` is required.")

    model = settings.get("ollama_model") or "llama3.2"
    system_prompt = settings.get("ollama_system_prompt")
    host = _host()

    messages = []
    if system_prompt:
        messages.append({"role": "system", "content": system_prompt})
    with _lock:
        messages.extend(list(_history))
    messages.append({"role": "user", "content": prompt})

    payload = {
        "model": model,
        "messages": messages,
        "stream": False,
        "options": {"temperature": 0.7, "num_predict": 512},
    }

    url = f"{host}/api/chat"
    try:
        data = _post_json(url, payload)
    except TimeoutError:
        raise RuntimeError(
            "Ollama timed out. A large model on CPU can be slow, especially on "
            "the first request while it loads. Try again or pick a smaller model."
        )
    except urllib.error.HTTPError as e:
        detail = ""
        try:
            detail = (json.loads(e.read().decode("utf-8")) or {}).get("error") or ""
        except Exception:
            pass
        if e.code == 404 or "not found" in detail.lower():
            raise RuntimeError(
                f"Ollama model '{model}' isn't installed. Pull it first: "
                f"`ollama pull {model}` (or pick an installed model in Settings)."
            )
        raise RuntimeError(f"Ollama API error {e.code}: {detail or e.reason}")
    except urllib.error.URLError as e:
        raise RuntimeError(
            f"Could not reach Ollama at {host}. Is it running? Start it with "
            f"`ollama serve`. ({e.reason})"
        )

    reply = ((data.get("message") or {}).get("content") or "").strip()
    if not reply:
        raise RuntimeError("Ollama returned an empty reply.")

    # Commit this turn to history (trim to the cap).
    with _lock:
        _history.append({"role": "user", "content": prompt})
        _history.append({"role": "assistant", "content": reply})
        if len(_history) > _MAX_HISTORY:
            del _history[: len(_history) - _MAX_HISTORY]

    return reply


def list_models() -> list:
    """Return the names of models installed on the Ollama server."""
    host = _host()
    url = f"{host}/api/tags"
    req = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except TimeoutError:
        raise RuntimeError(f"Ollama timed out listing models at {host}.")
    except urllib.error.URLError as e:
        raise RuntimeError(
            f"Could not reach Ollama at {host}. Is it running? ({e.reason})"
        )

    names = []
    for m in data.get("models", []):
        name = m.get("name") or m.get("model") or ""
        if name:
            names.append(name)
    return sorted(set(names))


def reachable() -> bool:
    """True if the Ollama server responds to a tags request."""
    try:
        list_models()
        return True
    except Exception:
        return False
