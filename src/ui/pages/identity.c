#include "private.h"

void draw_identity(UiState *state)
{
    SystemPlan *system = &state->plan->system;

    draw_shell(state, "Identity", "Up/Down move   Enter/Space edit   Esc back");
    draw_property_row(6, 0, state->row, "Hostname", system->hostname);
    draw_property_row(8, 1, state->row, "Username", system->username);
    draw_property_row(10, 2, state->row, "Timezone", system->timezone);
    /* 密码不进入方案文件或生成器进程，只由最终脚本在目标环境中读取。 */
    draw_property_row(12, 3, state->row, "Root password", "Prompt during installation");
    draw_property_row(14, 4, state->row, "User password", "Prompt during installation");
}

void handle_identity(UiState *state, int key)
{
    SystemPlan *system = &state->plan->system;
    bool changed = false;

    if (key == 27) { state->screen = SCREEN_HOME; state->row = 4; return; }
    if (key == KEY_UP && state->row > 0) --state->row;
    else if (key == KEY_DOWN && state->row < 4) ++state->row;
    else if (key == ' ' || page_enter_pressed(key)) {
        /* 后两行仅说明运行时行为，不提供编辑入口，也不会把方案标记为已修改。 */
        if (state->row == 0) {
            changed = text_dialog("Hostname", system->hostname,
                                  sizeof(system->hostname));
        } else if (state->row == 1) {
            changed = text_dialog("Username", system->username,
                                  sizeof(system->username));
        } else if (state->row == 2) {
            changed = text_dialog("Timezone (for example Asia/Shanghai)",
                                  system->timezone, sizeof(system->timezone));
        }
        if (changed) state->dirty = true;
    }
}
