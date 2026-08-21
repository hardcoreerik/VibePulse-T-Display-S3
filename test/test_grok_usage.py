#!/usr/bin/env python3
"""Grok Build usage stays content-free and never invents a weekly percent."""

from pathlib import Path
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "tokenserver"))

from grok_usage import GrokUsage  # noqa: E402
from max_tracker import MaxTrackerStore  # noqa: E402


class GrokUsageWiring(unittest.TestCase):
    def test_tokenserver_imports_grok(self):
        src = (ROOT / "tools/tokenserver/tokenserver.py").read_text()
        self.assertIn("from grok_usage import GrokUsage", src)
        self.assertIn("grokWeekPct", src)
        self.assertIn("set_volume(\"grok\"", src)
        self.assertNotIn("observe_volume(\"grok\"", src)

    def test_mini_has_grok_pages(self):
        mini = (ROOT / "components/app_tokens/usage_screen_mini.c").read_text()
        self.assertIn("USAGE_QUOTA_GROK_WEEK", mini)
        self.assertIn("COL_GROK", mini)
        self.assertIn("TODAY TOKENS", mini)

    def test_max_tracker_includes_grok(self):
        src = (ROOT / "tools/tokenserver/max_tracker.py").read_text()
        self.assertIn('"grok"', src)
        self.assertIn("def set_volume", src)


class GrokUsageScan(unittest.TestCase):
    def test_unix_timestamp_and_no_invented_week(self):
        with tempfile.TemporaryDirectory() as raw:
            home = Path(raw)
            session = home / "sessions" / "proj" / "sid"
            session.mkdir(parents=True)
            (session / "summary.json").write_text(json.dumps({
                "info": {"id": "sid", "cwd": "/tmp/demo"},
                "current_model_id": "grok-4.6",
                "updated_at": "2026-08-20T12:00:00Z",
            }), encoding="utf-8")
            (session / "updates.jsonl").write_text(
                json.dumps({
                    "timestamp": 1787260563,
                    "params": {
                        "update": {
                            "sessionUpdate": "turn_completed",
                            "usage": {
                                "totalTokens": 428571,
                                "costUsdTicks": 688142600,
                            },
                        }
                    },
                }) + "\n",
                encoding="utf-8",
            )
            snap = GrokUsage(home).snapshot(now=0)
            self.assertIsNone(snap.get("weekPct"))
            self.assertEqual(snap["model"], "grok-4.6")
            self.assertEqual(sum(snap["byDay"].values()), 428571)
            self.assertNotIn("prompt", json.dumps(snap))

    def test_set_volume_does_not_stack(self):
        with tempfile.TemporaryDirectory() as raw:
            store = MaxTrackerStore(
                Path(raw) / "state.json",
                Path(raw) / "codex",
                Path(raw) / "claude",
            )
            store.set_volume("grok", "2026-08-20", 100)
            store.set_volume("grok", "2026-08-20", 100)
            self.assertEqual(
                store._state["grok"]["days"]["2026-08-20"]["vol"], 100)
            payload = store.snapshot("2026-08-20", {})
            self.assertIn("grok", payload)


if __name__ == "__main__":
    unittest.main()
