#include "private.h"

#include "text.h"

#include <stdio.h>

/*
 * 审阅页面只把存储操作、系统摘要和验证结果整理成带颜色的滚动列表；
 * 生成、预览和执行操作由独立的 Output 页面负责。
 */
void draw_review(UiState *state)
{
    ValidationReport report;
    StoragePlan *storage = &state->plan->storage;
    char lines[AI_MAX_PLAN_DISKS * (AI_MAX_PARTITIONS + 2) + AI_MAX_ISSUES + 20][256];
    int colors[AI_MAX_PLAN_DISKS * (AI_MAX_PARTITIONS + 2) + AI_MAX_ISSUES + 20] = {0};
    size_t count = 0;
    int y = 4;
    validate_plan(state->plan, &report);
    draw_shell(state, "Review", "Up/Down/PgUp/PgDn scroll   Esc back");
    if (report.error_count == 0 && state->target_identity_matches) {
        (void)snprintf(lines[count], sizeof(lines[count]), "Plan is ready (%zu warning(s)).", report.count);
        colors[count++] = COLOR_OK;
    } else {
        (void)snprintf(lines[count], sizeof(lines[count]), "Plan has %zu blocking error(s)%s.",
                       report.error_count,
                       state->target_identity_matches ? "" : " plus a target identity mismatch");
        colors[count++] = COLOR_ERROR;
    }
    lines[count++][0] = '\0';
    copy_text(lines[count], sizeof(lines[count]), "DESTRUCTIVE AND STORAGE OPERATIONS");
    colors[count++] = COLOR_TITLE;
    for (size_t disk_index = 0; disk_index < storage->disk_count; ++disk_index) {
        const DiskPlan *disk = &storage->disks[disk_index];
        (void)snprintf(lines[count], sizeof(lines[count]), "%s %.120s (%.100s) — %s",
                       disk->mode == STORAGE_EXISTING ? "DISK " : "ERASE",
                       disk->path, disk->model, storage_mode_name(disk->mode));
        colors[count++] = disk->mode == STORAGE_EXISTING ? COLOR_TITLE : COLOR_ERROR;
        for (size_t index = 0; index < disk->partition_count; ++index) {
            const PartitionPlan *part = &disk->partitions[index];
            /* 与生成器采用相同的“可执行分区”口径，忽略纯 KEEP 的未分配分区。 */
            if ((part->usage == PART_UNUSED && part->action != ACTION_FORMAT && !part->planned) ||
                count >= sizeof(lines) / sizeof(lines[0])) continue;
            (void)snprintf(lines[count], sizeof(lines[count]), "  %-7s %-20.120s -> %-8.8s %.40s",
                           part->action == ACTION_FORMAT ? (part->planned ? "CREATE" : "FORMAT") : "KEEP",
                           part->device,
                           part->action == ACTION_FORMAT ? filesystem_name(part->target_fs) : part->current_fs,
                           partition_mountpoint(part->usage));
            colors[count++] = part->action == ACTION_FORMAT ? COLOR_WARNING : 0;
        }
    }
    lines[count++][0] = '\0';
    copy_text(lines[count], sizeof(lines[count]), "SYSTEM");
    colors[count++] = COLOR_TITLE;
    (void)snprintf(lines[count++], sizeof(lines[0]), "%s | %s | %s | %s",
                   platform_name(state->plan->system.platform), kernel_name(state->plan->system.kernel),
                   desktop_name(state->plan->system.desktop), locale_name(state->plan->system.locale));
    (void)snprintf(lines[count++], sizeof(lines[0]), "%.48s@%.63s | %.100s | Secure Boot %s",
                   state->plan->system.username, state->plan->system.hostname,
                   state->plan->system.timezone, state->plan->system.secure_boot ? "on" : "off");
    lines[count++][0] = '\0';
    copy_text(lines[count], sizeof(lines[count]), "VALIDATION");
    colors[count++] = COLOR_TITLE;
    if (!state->target_identity_matches) {
        copy_text(lines[count], sizeof(lines[count]),
                  "ERROR One or more disk identities no longer match the saved plan.");
        colors[count++] = COLOR_ERROR;
    }
    for (size_t index = 0; index < report.count && count < sizeof(lines) / sizeof(lines[0]); ++index) {
        (void)snprintf(lines[count], sizeof(lines[count]), "%s %.240s",
                       report.issues[index].severity == ISSUE_ERROR ? "ERROR" : "WARN ",
                       report.issues[index].message);
        colors[count++] = report.issues[index].severity == ISSUE_ERROR ? COLOR_ERROR : COLOR_WARNING;
    }
    if (report.count == 0 && state->target_identity_matches) {
        copy_text(lines[count], sizeof(lines[count]), "No validation issues.");
        colors[count++] = COLOR_OK;
    }
    {
        int page = LINES - 8;
        int maximum = count > (size_t)page ? (int)count - page : 0;
        /* handle_review 可先把偏移推过末尾；绘制时依据本次实际行数收敛。 */
        if (state->review_offset > maximum) state->review_offset = maximum;
        for (int line = 0; line < page && (size_t)(state->review_offset + line) < count; ++line) {
            size_t index = (size_t)(state->review_offset + line);
            if (colors[index] != 0) attron(COLOR_PAIR(colors[index]));
            if (colors[index] == COLOR_TITLE || index == 0) attron(A_BOLD);
            put_clipped(y++, 2, COLS - 4, lines[index]);
            if (colors[index] == COLOR_TITLE || index == 0) attroff(A_BOLD);
            if (colors[index] != 0) attroff(COLOR_PAIR(colors[index]));
        }
        if (maximum > 0) mvprintw(LINES - 4, COLS - 22, "lines %d-%d of %zu",
                                  state->review_offset + 1,
                                  state->review_offset + page < (int)count ?
                                  state->review_offset + page : (int)count, count);
    }
}

void handle_review(UiState *state, int key)
{
    if (key == 27) { state->screen = SCREEN_HOME; state->row = 5; return; }
    if (key == KEY_UP && state->review_offset > 0) --state->review_offset;
    else if (key == KEY_DOWN) ++state->review_offset;
    else if (key == KEY_PPAGE) {
        state->review_offset -= 10;
        if (state->review_offset < 0) state->review_offset = 0;
    } else if (key == KEY_NPAGE) {
        state->review_offset += 10;
    }
}
