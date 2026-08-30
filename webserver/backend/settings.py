"""
Persistent app settings (Ollama host, model, system prompt).

Stored in a gitignored settings.json next to this file. Environment variables
act as a fallback/override for headless setups:

  OLLAMA_HOST, OLLAMA_MODEL, OLLAMA_SYSTEM_PROMPT

Precedence: a non-empty value saved in settings.json wins; otherwise the env
var; otherwise a built-in default. Ollama is keyless, so there's no secret to
manage here.
"""

import json
import os
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
SETTINGS_PATH = os.path.join(HERE, "settings.json")

_lock = threading.Lock()

_ENV_MAP = {
    "ollama_host": "OLLAMA_HOST",
    "ollama_model": "OLLAMA_MODEL",
    "ollama_system_prompt": "OLLAMA_SYSTEM_PROMPT",
}

_DEFAULTS = {
    "ollama_host": "http://localhost:11434",
    "ollama_model": "llama3.2",
    "ollama_system_prompt": (
        "You are Mochi, a friendly and concise voice assistant. Your replies "
        "are read aloud by a small speaker, so keep them short and "
        "conversational, usually one to three sentences. Avoid markdown, "
        "bullet lists, code blocks, and emoji. Speak naturally."
    ),
}


def _load_file() -> dict:
    try:
        with open(SETTINGS_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
            return data if isinstance(data, dict) else {}
    except FileNotFoundError:
        return {}
    except Exception:
        return {}


def get(key: str) -> str:
    """Resolve a single setting: file -> env -> default."""
    with _lock:
        data = _load_file()
    raw = data.get(key)
    val = raw.strip() if isinstance(raw, str) else raw
    if val:
        return val
    env = os.environ.get(_ENV_MAP.get(key, ""), "")
    if env:
        return env
    return _DEFAULTS.get(key, "")


def all_settings() -> dict:
    """Every resolved setting."""
    return {k: get(k) for k in _DEFAULTS}


def save(updates: dict) -> None:
    """Merge updates into settings.json.

    Only keys present in `updates` with a non-None value are written.
    """
    with _lock:
        data = _load_file()
        for key in _DEFAULTS:
            if key in updates and updates[key] is not None:
                data[key] = updates[key]
        tmp = SETTINGS_PATH + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
        os.replace(tmp, SETTINGS_PATH)
