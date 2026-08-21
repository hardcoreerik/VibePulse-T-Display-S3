#!/usr/bin/env python3
"""The T-Display-S3 port must carry every original VibePulse screen."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MINI = (ROOT / "components/app_tokens/usage_screen_mini.c").read_text()
HEADER = (ROOT / "components/app_tokens/usage_screen.h").read_text()
HOST = (ROOT / "main/main_tdisplay_s3.c").read_text()
CMAKE = (ROOT / "components/app_tokens/CMakeLists.txt").read_text()
INSTALL = (ROOT / "install/index.html").read_text()


def must(name: str, haystack: str, needle: str) -> None:
    assert needle in haystack, f"{name} missing {needle!r}"


must("usage_screen.h", HEADER, "#define TK_USAGE_SCREEN_VIEWS 8")
for view in (
    "VIEW_CLAUDE_FABLE",
    "VIEW_CLAUDE_ALL",
    "VIEW_CODEX_WEEKLY",
    "VIEW_BURN_RATE",
    "VIEW_TRACKER_CLAUDE",
    "VIEW_TRACKER_CODEX",
    "VIEW_GITHUB",
    "VIEW_VALUE",
):
    must("mini", MINI, view)

must("mini", MINI, "create_quota_page")
must("mini", MINI, "create_burn_rate_page")
must("mini", MINI, "create_tracker_page")
must("mini", MINI, "create_github_page")
must("mini", MINI, "create_value_page")
must("mini", MINI, "tk_agent_monitor_create")
must("mini", MINI, "usage_presenter_build_quota_page")
must("mini", MINI, "usage_presenter_build_forecasts")
must("mini", MINI, "usage_presenter_build_value")
must("cmake", CMAKE, "agent_monitor.c")
must("cmake", CMAKE, "agent_completion_policy.c")
must("host", HOST, "TK_USAGE_SCREEN_VIEWS")
must("host", HOST, "tk_agent_monitor_dismiss_current")
must("install", INSTALL, "esp-web-tools")
must("install", INSTALL, "T-Display-S3")

cfg = (ROOT / "components/app_tokens/tdisplay_cfg.h").read_text()
for token in (
    "VPSSID______________________________",
    "VPPASS__________________________________________________________",
    "http://VIBEPULSE-HOST:8737________________________________________",
):
    must("cfg", cfg, token)
    must("install", INSTALL, token)
print("tdisplay screens: ok")
