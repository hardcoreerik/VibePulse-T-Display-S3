#ifndef PROJECT_STAR_POPUP_H
#define PROJECT_STAR_POPUP_H

#include <stdint.h>

#include "lvgl.h"

#include "project_star_event.h"

void tk_project_star_popup_create(lv_obj_t *app_root);
bool tk_project_star_popup_show(const tk_project_star_event *event,
                               int64_t now_us);
void tk_project_star_popup_tick(int64_t now_us);
bool tk_project_star_popup_visible(void);
void tk_project_star_popup_dismiss(void);

#endif
