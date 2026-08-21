"""Privacy-safe Grok Build usage from local ~/.grok sessions.

The ESP32 cannot read these files. This module is the on-PC half: it
turns Grok CLI sessions into the same glance numbers Claude and Codex
already publish (volume, live agent state, daily tracker volume). Weekly
quota percent is left unset until xAI publishes a comparable usage API —
dashes, never invented percentages.

Only totals, model names, effort, project basenames, and coarse states
are retained. Prompts, tool arguments, and file contents are ignored.
"""

from __future__ import annotations

import json
import os
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

try:
    from .agent_status import Event, sanitize_project
except ImportError:
    from agent_status import Event, sanitize_project

TICKS_PER_USD = 1_000_000_000.0
CACHE_TTL_S = 15.0


def grok_home() -> Path:
    override = os.environ.get("VIBEPULSE_GROK_HOME")
    if override:
        return Path(override)
    return Path.home() / ".grok"


def _pid_alive_windows(pid: int) -> bool:
    import ctypes
    from ctypes import wintypes
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    STILL_ACTIVE = 259
    handle = ctypes.windll.kernel32.OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
    if not handle:
        return False
    code = wintypes.DWORD()
    ok = ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
    ctypes.windll.kernel32.CloseHandle(handle)
    return bool(ok) and code.value == STILL_ACTIVE


def _pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if os.name == "nt":
        return _pid_alive_windows(pid)
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    except AttributeError:
        return False
    return True


def _local_day(ts) -> Optional[str]:
    if isinstance(ts, (int, float)) and ts > 0:
        try:
            return datetime.fromtimestamp(ts).date().isoformat()
        except (OSError, OverflowError, ValueError):
            return None
    if not isinstance(ts, str) or len(ts) < 10:
        return None
    try:
        stamp = datetime.fromisoformat(ts.replace("Z", "+00:00"))
    except ValueError:
        return None
    return stamp.astimezone().date().isoformat()


def _usage_tokens(usage: Any) -> int:
    if not isinstance(usage, dict):
        return 0
    total = usage.get("totalTokens")
    if isinstance(total, (int, float)) and total >= 0:
        return int(total)
    parts = 0
    for key in ("inputTokens", "outputTokens", "cachedReadTokens",
                "cacheCreationTokens", "reasoningTokens"):
        value = usage.get(key)
        if isinstance(value, (int, float)) and value > 0:
            parts += int(value)
    return parts


def _usage_usd(usage: Any) -> float:
    if not isinstance(usage, dict):
        return 0.0
    ticks = usage.get("costUsdTicks")
    if isinstance(ticks, (int, float)) and ticks > 0:
        return float(ticks) / TICKS_PER_USD
    return 0.0


class GrokUsage:
    def __init__(self, home: Optional[Path] = None) -> None:
        self.home = Path(home) if home else grok_home()
        self._lock = threading.Lock()
        self._file_cache: dict[str, tuple[int, int, dict[str, Any]]] = {}
        self._last: Optional[dict[str, Any]] = None
        self._last_at = 0.0

    def snapshot(self, now: Optional[float] = None) -> dict[str, Any]:
        now = time.time() if now is None else now
        with self._lock:
            if self._last is not None and now - self._last_at < CACHE_TTL_S:
                return dict(self._last)
            snap = self._compute()
            self._last = snap
            self._last_at = now
            return dict(snap)

    def _compute(self) -> dict[str, Any]:
        sessions_root = self.home / "sessions"
        today = datetime.now().date().isoformat()
        month_prefix = today[:7]
        day_tokens = 0
        month_tokens = 0
        day_usd = 0.0
        month_usd = 0.0
        day_sessions = set()
        by_day: dict[str, int] = {}
        model = None
        effort = None
        newest = ""
        live_event = None

        if sessions_root.is_dir():
            for summary in sessions_root.rglob("summary.json"):
                try:
                    stat = summary.stat()
                except OSError:
                    continue
                payload = self._cached_file(summary, stat)
                info = payload.get("info") if isinstance(payload.get("info"), dict) else {}
                session_id = info.get("id") if isinstance(info.get("id"), str) else summary.parent.name
                updated = payload.get("updated_at") or payload.get("last_active_at") or ""
                if isinstance(payload.get("current_model_id"), str) and (
                        not model or (isinstance(updated, str) and updated > newest)):
                    model = payload["current_model_id"]
                    newest = updated if isinstance(updated, str) else newest
                if isinstance(payload.get("reasoning_effort"), str) and (
                        not effort or (isinstance(updated, str) and updated >= newest)):
                    effort = payload["reasoning_effort"]

                updates = summary.parent / "updates.jsonl"
                usage_days = self._scan_updates(updates)
                for date_str, tokens, usd in usage_days:
                    by_day[date_str] = by_day.get(date_str, 0) + tokens
                    if date_str.startswith(month_prefix):
                        month_tokens += tokens
                        month_usd += usd
                    if date_str == today:
                        day_tokens += tokens
                        day_usd += usd
                        day_sessions.add(session_id)

        live = self._live_agent(model, effort)
        if live:
            live_event = live

        return {
            "dayTokens": day_tokens,
            "monthTokens": month_tokens,
            "dayUsd": day_usd,
            "monthUsd": month_usd,
            "daySessions": len(day_sessions),
            "byDay": by_day,
            "model": model,
            "effort": effort,
            "event": live_event,
        }

    def _cached_file(self, path: Path, stat: os.stat_result) -> dict[str, Any]:
        key = str(path)
        stamp = (int(stat.st_mtime), int(stat.st_size))
        cached = self._file_cache.get(key)
        if cached and cached[0] == stamp[0] and cached[1] == stamp[1]:
            return cached[2]
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError):
            payload = {}
        if not isinstance(payload, dict):
            payload = {}
        self._file_cache[key] = (stamp[0], stamp[1], payload)
        if len(self._file_cache) > 400:
            self._file_cache.pop(next(iter(self._file_cache)))
        return payload

    def _scan_updates(self, path: Path) -> list[tuple[str, int, float]]:
        out: list[tuple[str, int, float]] = []
        if not path.is_file():
            return out
        try:
            with path.open(encoding="utf-8") as handle:
                for line in handle:
                    try:
                        row = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    params = row.get("params") if isinstance(row, dict) else None
                    update = params.get("update") if isinstance(params, dict) else None
                    if not isinstance(update, dict):
                        continue
                    if update.get("sessionUpdate") != "turn_completed":
                        continue
                    usage = update.get("usage")
                    tokens = _usage_tokens(usage)
                    if tokens <= 0:
                        continue
                    day = _local_day(row.get("timestamp") or update.get("timestamp"))
                    if not day:
                        continue
                    out.append((day, tokens, _usage_usd(usage)))
        except OSError:
            return out
        return out

    def _live_agent(self, model: Optional[str], effort: Optional[str]) -> Optional[Event]:
        path = self.home / "active_sessions.json"
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError):
            return None
        if not isinstance(raw, list) or not raw:
            return None
        chosen = None
        for row in raw:
            if not isinstance(row, dict):
                continue
            session_id = row.get("session_id")
            if not isinstance(session_id, str) or not session_id:
                continue
            pid = row.get("pid")
            alive = isinstance(pid, int) and _pid_alive(pid)
            if chosen is None or alive:
                chosen = (row, session_id, alive)
            if alive:
                break
        if chosen is None:
            return None
        session, session_id, alive = chosen
        state = "working" if alive else "done"
        activity = "thinking" if alive else None
        project = sanitize_project(session.get("cwd"))
        return Event(state, activity, session_id[:64], session_id,
                     project, model, effort)
