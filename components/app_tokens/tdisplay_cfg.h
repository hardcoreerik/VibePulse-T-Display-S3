#ifndef TDISPLAY_CFG_H
#define TDISPLAY_CFG_H

#include <stdbool.h>
#include <stddef.h>

/* Unique, fixed-length placeholders the web installer patches in the
 * merged flash image. Do not shorten or split these literals. */
#define VP_CFG_SSID_PLACEHOLDER \
  "VPSSID______________________________"
#define VP_CFG_PASS_PLACEHOLDER \
  "VPPASS__________________________________________________________"
#define VP_CFG_URL_PLACEHOLDER \
  "http://VIBEPULSE-HOST:8737________________________________________"

const char *tk_tdisplay_wifi_ssid(void);
const char *tk_tdisplay_wifi_pass(void);
const char *tk_tdisplay_base_url(void);
bool tk_tdisplay_wifi_configured(void);
bool tk_tdisplay_make_url(char *out, size_t capacity, const char *path);

#endif
