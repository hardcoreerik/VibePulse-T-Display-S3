# VibePulse som Torget-app: usage + statisk agentmonitor för Claude/Codex.
# app.c, parsrar och monitorlogik delas
# byte-identiskt med simulatorn; nätadaptrarna finns bara i det här bygget.
set(TK_USAGE_SCREEN_SRC "usage_screen.c")
set(TK_AGENT_MONITOR_SRCS "agent_monitor_policy.c" "agent_completion_policy.c"
                          "agent_monitor.c" "agent_assets.c")
if(TORGET_TDISPLAY_S3)
  set(TK_USAGE_SCREEN_SRC "usage_screen_mini.c")
  list(APPEND TK_USAGE_SCREEN_SRC "tdisplay_cfg.c")
endif()

idf_component_register(
  SRCS "app.c" "tokens_parse.c" "usage_presenter.c" ${TK_USAGE_SCREEN_SRC}
       "agent_status_parse.c" "agent_usage.c"
       ${TK_AGENT_MONITOR_SRCS} "usage_live_policy.c"
       "github_status_parse.c" "project_star_popup_policy.c"
       "project_star_assets.c" "project_star_chime.c" "project_star_popup.c"
       "agent_net_policy.c" "net.c" "agent_net.c"
       "github_net.c"
       "max_tracker_parse.c" "max_tracker_presenter.c"
  INCLUDE_DIRS "."
  REQUIRES torget_app torget_fmt cjson
  # secrets.h bor i repytroten (gitignorad), därav "../..".
  PRIV_INCLUDE_DIRS "../.."
  PRIV_REQUIRES torget_net esp_http_client)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
if(TORGET_TDISPLAY_S3)
  target_compile_definitions(${COMPONENT_LIB} PUBLIC TORGET_TDISPLAY_S3)
endif()
