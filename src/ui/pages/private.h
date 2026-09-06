#ifndef ARCH_INSTALLER_UI_PAGES_PRIVATE_H
#define ARCH_INSTALLER_UI_PAGES_PRIVATE_H

#include "../private.h"

/* 页面共享的微型输入和软件包预览适配器不保存额外状态。 */
static inline bool page_enter_pressed(int key)
{
    return key == '\n' || key == KEY_ENTER;
}

static inline void page_show_packages(UiState *state, const char *title,
                                      const PackageGroup groups[], size_t count)
{
    packages_dialog(title, state->packages, groups, count);
}

#endif
