#include "tdisplay_cfg.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "secrets.h"
#endif

/* One copy in .rodata so the web installer can find each unique string
 * and patch it in the merged flash image. */
__attribute__((used)) const char vp_cfg_ssid[64] = VP_CFG_SSID_PLACEHOLDER;
__attribute__((used)) const char vp_cfg_pass[64] = VP_CFG_PASS_PLACEHOLDER;
__attribute__((used)) const char vp_cfg_base_url[96] = VP_CFG_URL_PLACEHOLDER;

static bool is_placeholder(const char *value, const char *prefix) {
  return value && strncmp(value, prefix, strlen(prefix)) == 0;
}

const char *tk_tdisplay_wifi_ssid(void) {
  if (!is_placeholder(vp_cfg_ssid, "VPSSID") && vp_cfg_ssid[0])
    return vp_cfg_ssid;
#ifdef TG_WIFI_SSID
  if (TG_WIFI_SSID[0]) return TG_WIFI_SSID;
#endif
  return "";
}

const char *tk_tdisplay_wifi_pass(void) {
  if (!is_placeholder(vp_cfg_pass, "VPPASS")) return vp_cfg_pass;
#ifdef TG_WIFI_PASS
  if (TG_WIFI_SSID[0]) return TG_WIFI_PASS;
#endif
  return "";
}

const char *tk_tdisplay_base_url(void) {
  if (!is_placeholder(vp_cfg_base_url, "http://VIBEPULSE-HOST") &&
      vp_cfg_base_url[0]) {
    return vp_cfg_base_url;
  }
#ifdef TK_VIBEPULSE_BASE_URL
  if (TK_VIBEPULSE_BASE_URL[0] &&
      strstr(TK_VIBEPULSE_BASE_URL, "VIBEPULSE-HOST") == NULL) {
    return TK_VIBEPULSE_BASE_URL;
  }
#endif
  return "";
}

bool tk_tdisplay_wifi_configured(void) {
  const char *ssid = tk_tdisplay_wifi_ssid();
  return ssid && ssid[0];
}

bool tk_tdisplay_make_url(char *out, size_t capacity, const char *path) {
  const char *base = tk_tdisplay_base_url();
  if (!out || capacity < 8 || !base || !base[0] || !path) return false;
  if (strstr(base, "VIBEPULSE-HOST")) return false;
  int n = snprintf(out, capacity, "%s%s", base, path);
  return n > 0 && (size_t)n < capacity;
}
