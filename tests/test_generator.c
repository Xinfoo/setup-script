#define _POSIX_C_SOURCE 200809L

#include "generator.h"
#include "model.h"
#include "util.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GIB (UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024))
#define TIB (UINT64_C(1024) * GIB)
#define GPT_ESP_TYPE "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
#define GPT_LINUX_TYPE "0fc63daf-8483-4772-8e79-3d69d8477de4"

/* 保存临时生成脚本及其文本，便于同时检查文件行为和脚本内容。 */
typedef struct {
    char path[64];
    char *text;
} GeneratedScript;

/* 构造稳定的磁盘与分区夹具，避免测试依赖运行机器的真实硬件。 */
static void make_disk(DiskInfo *disk, size_t partition_count)
{
    memset(disk, 0, sizeof(*disk));
    copy_text(disk->name, sizeof(disk->name), "nvme0n1");
    copy_text(disk->path, sizeof(disk->path), "/dev/nvme0n1");
    copy_text(disk->model, sizeof(disk->model), "Integration Test NVMe");
    copy_text(disk->serial, sizeof(disk->serial), "GENERATOR-TEST-001");
    copy_text(disk->transport, sizeof(disk->transport), "nvme");
    copy_text(disk->partition_table, sizeof(disk->partition_table), "gpt");
    disk->size_bytes = UINT64_C(2) * TIB;
    disk->partition_count = partition_count;
}

static void make_partition(PartitionInfo *partition, unsigned number,
                           uint64_t size_bytes, const char *filesystem)
{
    char path[AI_PATH_LEN];

    memset(partition, 0, sizeof(*partition));
    (void)snprintf(path, sizeof(path), "/dev/nvme0n1p%u", number);
    copy_text(partition->path, sizeof(partition->path), path);
    copy_text(partition->current_fs, sizeof(partition->current_fs), filesystem);
    (void)snprintf(partition->fs_uuid, sizeof(partition->fs_uuid),
                   "40000000-0000-0000-0000-%012u", number);
    (void)snprintf(partition->part_uuid, sizeof(partition->part_uuid),
                   "20000000-0000-0000-0000-%012u", number);
    copy_text(partition->part_type, sizeof(partition->part_type),
              number == 1 ? GPT_ESP_TYPE : GPT_LINUX_TYPE);
    partition->number = number;
    partition->size_bytes = size_bytes;
    partition->start_sector = UINT64_C(2048) + (uint64_t)(number - 1) * UINT64_C(2097152);
}

static void make_second_disk(DiskInfo *disk)
{
    make_disk(disk, 0);
    copy_text(disk->name, sizeof(disk->name), "sdb");
    copy_text(disk->path, sizeof(disk->path), "/dev/sdb");
    copy_text(disk->model, sizeof(disk->model), "Integration Test Data Disk");
    copy_text(disk->serial, sizeof(disk->serial), "GENERATOR-TEST-002");
}

/* 读取生成结果并用 bash -n 做独立语法检查。 */
static char *read_file(const char *path)
{
    FILE *file = fopen(path, "r");
    char *buffer;
    size_t used = 0;
    size_t capacity = 4096;

    if (file == NULL) return NULL;
    buffer = malloc(capacity);
    if (buffer == NULL) {
        (void)fclose(file);
        return NULL;
    }
    for (;;) {
        size_t count;

        if (capacity - used < 2048) {
            size_t new_capacity = capacity * 2;
            char *resized;

            if (new_capacity < capacity) {
                free(buffer);
                (void)fclose(file);
                return NULL;
            }
            resized = realloc(buffer, new_capacity);
            if (resized == NULL) {
                free(buffer);
                (void)fclose(file);
                return NULL;
            }
            buffer = resized;
            capacity = new_capacity;
        }
        count = fread(buffer + used, 1, capacity - used - 1, file);
        used += count;
        if (count != 0) continue;
        if (ferror(file)) {
            free(buffer);
            (void)fclose(file);
            return NULL;
        }
        break;
    }
    buffer[used] = '\0';
    (void)fclose(file);
    return buffer;
}

static bool bash_syntax_is_valid(const char *path)
{
    ProcessResult result = {0};
    char error[256] = {0};
    char *const arguments[] = {"/usr/bin/bash", "-n", (char *)path, NULL};
    bool valid;

    if (!run_capture(arguments[0], arguments, &result, error, sizeof(error))) {
        (void)fprintf(stderr, "cannot run bash -n: %s\n", error);
        return false;
    }
    valid = result.status == 0;
    if (!valid) {
        (void)fprintf(stderr, "bash -n failed with status %d:\n%s\n",
                      result.status, result.output != NULL ? result.output : "");
    }
    process_result_free(&result);
    return valid;
}

static bool generate_script_with_packages(const InstallPlan *plan,
                                          const PackageConfig *packages,
                                          GeneratedScript *script)
{
    char error[512] = {0};
    int descriptor;

    /* 每个场景使用独立临时文件，生成、校验、读取任一步失败都立即清理。 */
    memset(script, 0, sizeof(*script));
    copy_text(script->path, sizeof(script->path),
              "/tmp/arch-install-generator-test-XXXXXX");
    descriptor = mkstemp(script->path);
    if (descriptor < 0) {
        (void)fprintf(stderr, "mkstemp failed\n");
        return false;
    }
    if (close(descriptor) != 0) {
        (void)fprintf(stderr, "cannot close temporary script\n");
        (void)unlink(script->path);
        return false;
    }
    if (!generate_install_script(plan, packages, script->path, error, sizeof(error))) {
        (void)fprintf(stderr, "generate_install_script failed: %s\n", error);
        (void)unlink(script->path);
        script->path[0] = '\0';
        return false;
    }
    if (!bash_syntax_is_valid(script->path)) {
        (void)unlink(script->path);
        script->path[0] = '\0';
        return false;
    }
    script->text = read_file(script->path);
    if (script->text == NULL) {
        (void)fprintf(stderr, "cannot read generated script\n");
        (void)unlink(script->path);
        script->path[0] = '\0';
        return false;
    }
    return true;
}

static bool generate_script(const InstallPlan *plan, GeneratedScript *script)
{
    PackageConfig packages;

    packages_init_defaults(&packages);
    return generate_script_with_packages(plan, &packages, script);
}

static void generated_script_destroy(GeneratedScript *script)
{
    free(script->text);
    script->text = NULL;
    if (script->path[0] != '\0') {
        (void)unlink(script->path);
        script->path[0] = '\0';
    }
}

/* 内容断言同时覆盖片段的存在、缺失及关键安装步骤的先后顺序。 */
static bool require_fragment(const GeneratedScript *script, const char *fragment,
                             const char *description)
{
    if (strstr(script->text, fragment) != NULL) return true;
    (void)fprintf(stderr, "generated script is missing %s:\n%s\n",
                  description, fragment);
    return false;
}

static bool forbid_fragment(const GeneratedScript *script, const char *fragment,
                            const char *description)
{
    if (strstr(script->text, fragment) == NULL) return true;
    (void)fprintf(stderr, "generated script unexpectedly contains %s:\n%s\n",
                  description, fragment);
    return false;
}

static bool require_order(const GeneratedScript *script, const char *first,
                          const char *second, const char *description)
{
    const char *left = strstr(script->text, first);
    const char *right = strstr(script->text, second);

    if (left != NULL && right != NULL && left < right) return true;
    (void)fprintf(stderr, "generated script has the wrong order for %s\n", description);
    return false;
}

/* 自动分区场景覆盖生成脚本的主体流程、运行时复核、清理和 Secure Boot。 */
static bool test_automatic_script(void)
{
    DiskInfo disk;
    InstallPlan plan;
    GeneratedScript script;
    bool passed = true;

    make_disk(&disk, 0);
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan_use_automatic(&plan, &disk, STORAGE_AUTO_HOME_SWAP);
    plan.system.kernel = KERNEL_LTS;

    if (!generate_script(&plan, &script)) return false;
    passed &= require_fragment(&script,
                               "# Generated Arch Linux installation script; review the plan before running it.",
                               "the English generated-script introduction");
    passed &= require_fragment(&script,
                               "# 自动生成的 Arch Linux 安装脚本；运行前请检查安装计划。",
                               "the Chinese generated-script introduction");
    passed &= require_fragment(&script,
                               "# Runtime state and common helpers / 运行时状态与通用辅助函数",
                               "a bilingual outer-script section comment");
    passed &= require_fragment(&script,
                               "# Generated target-system settings / 自动生成的目标系统设置",
                               "a bilingual nested chroot section comment");
    passed &= require_fragment(&script,
                               "cat > /etc/environment <<'ENVIRONMENT'\n#\n"
                               "# This file is parsed by pam_env module\n#\n"
                               "# Syntax: simple \"KEY=VAL\" pairs on separate lines\n#\n\n"
                               "XMODIFIERS=@im=fcitx\n"
                               "SDL_IM_MODULE=fcitx\n"
                               "GLFW_IM_MODULE=ibus\n"
                               "ENVIRONMENT\n}",
                               "the complete Arch environment file with its standard header");
    passed &= forbid_fragment(&script,
                              "cat >> /etc/environment",
                              "append-only environment configuration");
    passed &= require_fragment(&script,
                               "cat > /etc/hosts <<HOSTS\n"
                               "# Static table lookup for hostnames.\n"
                               "# See hosts(5) for details.\n\n"
                               "127.0.0.1 localhost\n"
                               "::1 localhost\n"
                               "127.0.1.1 $HOSTNAME_VALUE.localdomain $HOSTNAME_VALUE\n"
                               "HOSTS",
                               "the complete hosts file with its standard header");
    passed &= require_fragment(&script,
                               "            <family>Noto Sans CJK SC</family>\n"
                               "            <family>Noto Sans CJK TC</family>\n"
                               "            <family>Noto Sans CJK JP</family>\n"
                               "            <family>Noto Sans CJK KR</family>\n",
                               "the complete regional sans-serif CJK fallback order");
    passed &= require_fragment(&script,
                               "            <family>Noto Serif CJK SC</family>\n"
                               "            <family>Noto Serif CJK TC</family>\n"
                               "            <family>Noto Serif CJK JP</family>\n"
                               "            <family>Noto Serif CJK KR</family>\n",
                               "the complete regional serif CJK fallback order");
    passed &= require_fragment(&script,
                               "            <family>Sarasa Mono SC</family>\n"
                               "            <family>Sarasa Mono TC</family>\n"
                               "            <family>Sarasa Mono J</family>\n"
                               "            <family>Sarasa Mono K</family>\n",
                               "the complete regional monospace CJK fallback order");
    passed &= forbid_fragment(&script,
                              "<alias><family>",
                              "the compacted fontconfig format");
    passed &= require_fragment(&script,
                               "# Installation orchestration / 安装流程编排",
                               "a bilingual installation-flow section comment");
    passed &= require_fragment(&script,
                               "readonly TARGET_DISK='/dev/nvme0n1'",
                               "the quoted target disk");
    passed &= require_fragment(&script,
                               "confirm_destructive_actions() {",
                               "the destructive confirmation function");
    passed &= require_fragment(&script,
                               "Type the full target disk path (%s) to continue:",
                               "the target-disk confirmation prompt");
    passed &= require_fragment(&script,
                               "[[ \"$answer\" == \"$TARGET_DISK\" ]]",
                               "an exact target-disk confirmation check");
    passed &= require_fragment(&script,
                               "[[ \"$current_type\" == disk ]]",
                               "a whole-disk runtime check");
    passed &= require_fragment(&script,
                               "confirm_package_preparation() {",
                               "the package preparation confirmation");
    passed &= require_order(&script,
                            "    confirm_package_preparation\n    prepare_package_source\n",
                            "    snapshot_secure_boot_assets\n    probe_kept_filesystems\n    confirm_destructive_actions\n",
                            "package preparation and final disk confirmation");
    passed &= require_fragment(&script,
                               "mount -t tmpfs -o nodev,nosuid,noexec,mode=0700,size=64M tmpfs",
                               "a private tmpfs Secure Boot snapshot");
    passed &= require_fragment(&script,
                               "readonly ASSET_DIR=\"$SCRIPT_DIR\"",
                               "Secure Boot assets beside the builder script");
    passed &= forbid_fragment(&script,
                              "ARCH_INSTALL_ASSET_DIR",
                              "an alternate Secure Boot asset directory");
    passed &= forbid_fragment(&script,
                              "$SCRIPT_DIR/live",
                              "the legacy live asset subdirectory");
    passed &= require_fragment(&script,
                               "pacman -S --needed --noconfirm \"${LIVE_SIGNING_PACKAGES[@]}\"",
                               "Live signing tools installed before disk writes");
    passed &= require_fragment(&script,
                               "require_command sbverify",
                               "Secure Boot signature verification support");
    passed &= require_fragment(&script,
                               "bsdtar -xOf \"$SECURE_BOOT_ASSET_SNAPSHOT/shim-signed.pkg.tar.zst\"",
                               "controlled shim data extraction");
    passed &= forbid_fragment(&script,
                              "pacman -U --needed --noconfirm /root/shim-signed.pkg.tar.zst",
                              "execution of an untrusted shim package in the target");
    passed &= require_fragment(&script,
                               "phase 'Signing boot files outside the target chroot'",
                               "host-side Secure Boot signing");
    passed &= require_order(&script,
                            "arch-chroot \"$TARGET_ROOT\" /bin/bash /root/.arch-install-chroot.sh",
                            "    sign_secure_boot_assets",
                            "chroot completion before private-key use");
    passed &= forbid_fragment(&script,
                              "cp -a -- \"$ASSET_DIR/secure-boot\"",
                              "a private-key copy into the target filesystem");
    passed &= forbid_fragment(&script,
                              "mount --bind -- \"$ASSET_DIR/secure-boot\"",
                              "private-key exposure inside the target chroot");
    passed &= require_fragment(&script,
                               "sbsign --key \"$secure_root/secure-boot/MOK.key\"",
                               "signing from the verified private snapshot");
    passed &= forbid_fragment(&script,
                              "sbsign --key \"$ASSET_DIR/secure-boot/MOK.key\"",
                              "signing directly from mutable source assets");
    passed &= forbid_fragment(&script, ".unsigned", "a renamed-away original kernel");
    passed &= require_order(&script,
                            "    atomic_install_file \"$kernel_signed\" \"$kernel_original\" 0644\n",
                            "    atomic_install_file \"$secure_root/shimx64.efi\" \\\n        \"$TARGET_ROOT/boot/EFI/BOOT/BOOTX64.EFI\" 0644\n",
                            "bootable-prefix Secure Boot commit order");
    passed &= require_fragment(&script,
                               "ROOT_UUID=$(blkid -s UUID -o value -- \"$ROOT_DEVICE\") || {",
                               "checked root UUID discovery");
    passed &= require_fragment(&script,
                               "printf '%s\\n' '# Static information about the filesystems.'",
                               "the standard fstab header");
    passed &= require_fragment(&script,
                               "printf '%s\\n' '# <file system> <dir> <type> <options> <dump> <pass>'",
                               "the standard fstab column header");
    passed &= require_fragment(&script,
                               "genfstab -U \"$TARGET_ROOT\"\n    } > \"$TARGET_ROOT/etc/fstab\"",
                               "complete fstab generation through one overwrite");
    passed &= forbid_fragment(&script,
                              "UUID=%s none swap defaults 0 0",
                              "manual swap fstab generation");
    passed &= forbid_fragment(&script, "blkdiscard", "an unconditional discard command");
    passed &= require_fragment(&script,
                               "if [[ \"$action\" == keep ]]; then",
                               "the KEEP filesystem path");
    passed &= require_fragment(&script,
                               "ext4) mkfs.ext4 -F \"$device\" ;;",
                               "the FORMAT filesystem path");
    passed &= require_fragment(&script,
                               "readonly FALLBACK_IMAGE='initramfs-linux-lts-fallback.img'",
                               "the real fallback initramfs name");
    passed &= require_fragment(&script,
                               "initrd /$FALLBACK_FILE",
                               "the fallback boot entry");
    passed &= require_fragment(&script, "cleanup() {", "the cleanup function");
    passed &= require_fragment(&script, "trap cleanup EXIT", "the cleanup trap");
    passed &= require_fragment(&script,
                               "umount -R -- \"$TARGET_ROOT\"",
                               "target mount cleanup");
    passed &= require_fragment(&script,
                               "swapoff -- \"${SWAPS_TO_DISABLE[index]}\"",
                               "swap cleanup");
    passed &= require_fragment(&script,
                               "probe_kept_filesystems() {",
                               "the read-only KEEP probe");
    passed &= require_fragment(&script,
                               "ext4) options='ro,noload,nodev,nosuid,noexec'",
                               "journal-safe Ext4 KEEP probing");
    passed &= require_fragment(&script,
                               "set_account_password() {",
                               "the bounded password helper");
    passed &= require_fragment(&script,
                               "for attempt in 1 2 3; do",
                               "bounded password attempts");
    passed &= forbid_fragment(&script, "until passwd", "an unbounded password loop");
    passed &= require_fragment(&script,
                               "${entry,,}\" == *\"${boot_partuuid,,}\"*",
                               "EFI entry matching by partition identity");
    passed &= require_fragment(&script,
                               "actual_start_bytes=$((actual_start * 512))",
                               "lsblk START conversion on 512-byte units");
    passed &= require_fragment(&script,
                               "if [[ \"$TARGET_MOUNTED\" == true ]]; then",
                               "cleanup ownership gating for the target mount");
    generated_script_destroy(&script);
    return passed;
}

/* 输出路径安全场景确认符号链接不会被跟随，也不会改写其目标文件。 */
static bool test_output_symlink_is_rejected(void)
{
    DiskInfo disk;
    InstallPlan plan;
    PackageConfig packages;
    char directory[] = "/tmp/arch-install-output-test-XXXXXX";
    char victim[256];
    char output[256];
    char error[512] = {0};
    char *contents;
    FILE *file;
    bool passed = true;

    make_disk(&disk, 0);
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan_use_automatic(&plan, &disk, STORAGE_AUTO_ROOT_ONLY);
    packages_init_defaults(&packages);
    if (mkdtemp(directory) == NULL) return false;
    (void)snprintf(victim, sizeof(victim), "%s/victim", directory);
    (void)snprintf(output, sizeof(output), "%s/install.sh", directory);
    file = fopen(victim, "w");
    if (file == NULL) {
        (void)rmdir(directory);
        return false;
    }
    if (fputs("unchanged\n", file) == EOF) {
        (void)fclose(file);
        (void)unlink(victim);
        (void)rmdir(directory);
        return false;
    }
    if (fclose(file) != 0) {
        (void)unlink(victim);
        (void)rmdir(directory);
        return false;
    }
    if (symlink(victim, output) != 0) {
        (void)unlink(victim);
        (void)rmdir(directory);
        return false;
    }
    if (generate_install_script(&plan, &packages, output, error, sizeof(error))) {
        (void)fprintf(stderr, "generator accepted a symbolic-link output path\n");
        passed = false;
    }
    contents = read_file(victim);
    if (contents == NULL || strcmp(contents, "unchanged\n") != 0) {
        (void)fprintf(stderr, "generator changed the symbolic-link target\n");
        passed = false;
    }
    free(contents);
    passed &= unlink(output) == 0;
    passed &= unlink(victim) == 0;
    passed &= rmdir(directory) == 0;
    return passed;
}

/* 已有分区场景确认 KEEP 与 FORMAT 的身份信息和文件系统动作被正确编码。 */
static bool test_existing_keep_and_format_actions(void)
{
    DiskInfo disk;
    InstallPlan plan;
    GeneratedScript script;
    bool passed = true;

    make_disk(&disk, 3);
    make_partition(&disk.partitions[0], 1, GIB, "vfat");
    make_partition(&disk.partitions[1], 2, UINT64_C(100) * GIB, "ext4");
    make_partition(&disk.partitions[2], 3, UINT64_C(500) * GIB, "xfs");
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan.storage.disks[0].partitions[0].usage = PART_BOOT;
    plan.storage.disks[0].partitions[1].usage = PART_ROOT;
    plan.storage.disks[0].partitions[1].action = ACTION_FORMAT;
    plan.storage.disks[0].partitions[1].target_fs = FS_F2FS;
    plan.storage.disks[0].partitions[1].f2fs_mode = F2FS_COMPRESSED;
    plan.storage.disks[0].partitions[2].usage = PART_HOME;

    if (!generate_script(&plan, &script)) return false;
    passed &= require_fragment(&script,
                               "DISK_MODES=(\n    'existing'\n)",
                               "existing-partition mode");
    passed &= require_fragment(&script,
                               "PART_ACTIONS=(\n    'format'\n    'keep'\n    'keep'\n)",
                               "the mixed FORMAT/KEEP action plan");
    passed &= require_fragment(&script,
                               "PART_FILESYSTEMS=(\n    'f2fs'\n    'vfat'\n    'xfs'\n)",
                               "filesystem preservation and replacement choices");
    passed &= require_fragment(&script,
                               "verify_existing_partition \"${PART_DEVICES[index]}\"",
                               "existing-partition runtime validation");
    passed &= require_fragment(&script,
                               "PART_FS_UUIDS=(",
                               "kept-filesystem UUID identities");
    passed &= require_fragment(&script,
                               "Filesystem UUID changed on $device",
                               "kept-filesystem UUID revalidation");
    generated_script_destroy(&script);
    return passed;
}

/* 临时本地镜像场景确认 nginx 引导、HTTP 切换和目标标准签名策略。 */
static bool test_local_mirror_script(void)
{
    DiskInfo disk;
    InstallPlan plan;
    GeneratedScript script;
    bool passed = true;

    make_disk(&disk, 0);
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan_use_automatic(&plan, &disk, STORAGE_AUTO_ROOT_ONLY);
    plan.system.local_mirror = true;
    plan.system.china_mirrors = true;

    if (!generate_script(&plan, &script)) return false;
    passed &= require_fragment(&script,
                               "Type BOOTSTRAP %s %s to trust this exact source",
                               "an explicit local-server bootstrap confirmation");
    passed &= require_fragment(&script,
                               "LOCAL_MIRROR_UUID=$(blkid -s UUID",
                               "local mirror UUID capture");
    passed &= require_fragment(&script,
                               "verify_local_mirror_identity",
                               "local mirror identity revalidation");
    passed &= require_fragment(&script,
                               "LOCAL_MIRROR_LIVE_PACKAGES=(\n    'nginx'\n)",
                               "the configurable local-mirror server package");
    passed &= require_fragment(&script,
                               "listen 127.0.0.1:2304;",
                               "a loopback-only temporary nginx server");
    passed &= require_fragment(&script,
                               "Server = http://127.0.0.1:2304/$repo/os/$arch",
                               "the HTTP mirror inherited by the target");
    passed &= require_order(&script,
                            "Server = file:///run/media/root/F2FS-DATA/repo/archlinux/",
                            "pacman -S --needed --noconfirm \"${LOCAL_MIRROR_LIVE_PACKAGES[@]}\"",
                            "the file-based nginx bootstrap");
    passed &= require_order(&script,
                            "pacman -S --needed --noconfirm \"${LOCAL_MIRROR_LIVE_PACKAGES[@]}\"",
                            "Server = http://127.0.0.1:2304/$repo/os/$arch",
                            "the switch to HTTP after nginx installation");
    passed &= require_order(&script,
                            "arch-chroot \"$TARGET_ROOT\"",
                            "        stop_local_mirror_server\n",
                            "the local server lifetime through chroot configuration");
    passed &= forbid_fragment(&script,
                              "mount --bind -- /run/media/root/F2FS-DATA/repo/archlinux",
                              "a target local-repository bind mount");
    passed &= forbid_fragment(&script,
                              "\"$TARGET_ROOT/etc/pacman.conf\"",
                              "a target pacman signature-policy modification");
    generated_script_destroy(&script);
    return passed;
}

/* 自定义软件包场景确认生成器实际使用外部 PackageConfig。 */
static bool test_custom_package_config_is_emitted(void)
{
    DiskInfo disk;
    InstallPlan plan;
    PackageConfig packages;
    PackageList *core;
    GeneratedScript script;
    bool passed;

    make_disk(&disk, 0);
    plan_init(&plan);
    plan_select_disk(&plan, &disk);
    plan_use_automatic(&plan, &disk, STORAGE_AUTO_ROOT_ONLY);
    packages_init_defaults(&packages);
    core = &packages.groups[PKG_CORE];
    if (core->count >= AI_MAX_PACKAGES_PER_GROUP) return false;
    copy_text(core->values[core->count], sizeof(core->values[core->count]),
              "custom-repository-package");
    ++core->count;

    if (!generate_script_with_packages(&plan, &packages, &script)) return false;
    passed = require_fragment(&script, "'custom-repository-package'",
                              "a package loaded from packages.json");
    generated_script_destroy(&script);
    return passed;
}

/* 多盘场景确认每块盘的模式、仅格式化分区及跳过挂载逻辑。 */
static bool test_multi_disk_format_only_script(void)
{
    DiskInfo system_disk;
    DiskInfo data_disk;
    InstallPlan plan;
    GeneratedScript script;
    bool passed = true;

    make_disk(&system_disk, 0);
    make_second_disk(&data_disk);
    plan_init(&plan);
    plan_select_disk(&plan, &system_disk);
    plan_use_automatic(&plan, &system_disk, STORAGE_AUTO_ROOT_ONLY);
    if (!plan_add_disk(&plan, &data_disk)) return false;
    disk_plan_use_automatic(&plan.storage.disks[1], &data_disk, STORAGE_AUTO_DATA);

    if (!generate_script(&plan, &script)) return false;
    passed &= require_fragment(&script,
                               "INSTALL_DISKS=(\n    '/dev/nvme0n1'\n    '/dev/sdb'\n)",
                               "both participating disks");
    passed &= require_fragment(&script,
                               "DISK_MODES=(\n    'auto-root-only'\n    'auto-data'\n)",
                               "per-disk partition modes");
    passed &= require_fragment(&script, "'/dev/sdb1'",
                               "the format-only data partition");
    passed &= require_fragment(&script, "'unused'",
                               "an unmounted partition purpose");
    passed &= require_fragment(&script,
                               "[[ \"$usage\" != swap && \"$usage\" != unused ]] || continue",
                               "the format-only mount skip");
    passed &= require_fragment(&script, "auto-data)",
                               "the single data-disk partitioner");
    generated_script_destroy(&script);
    return passed;
}

int main(void)
{
    /* 汇总各集成场景，保留独立结果以便一次运行显示全部失败项。 */
    bool automatic = test_automatic_script();
    bool existing = test_existing_keep_and_format_actions();
    bool symlink = test_output_symlink_is_rejected();
    bool local_mirror = test_local_mirror_script();
    bool custom_packages = test_custom_package_config_is_emitted();
    bool multi_disk = test_multi_disk_format_only_script();

    (void)printf("%s automatic generator integration\n",
                 automatic ? "PASS" : "FAIL");
    (void)printf("%s existing KEEP/FORMAT integration\n",
                 existing ? "PASS" : "FAIL");
    (void)printf("%s output symlink rejection\n",
                 symlink ? "PASS" : "FAIL");
    (void)printf("%s local mirror HTTP architecture\n",
                 local_mirror ? "PASS" : "FAIL");
    (void)printf("%s custom package configuration\n",
                 custom_packages ? "PASS" : "FAIL");
    (void)printf("%s multi-disk format-only generation\n",
                 multi_disk ? "PASS" : "FAIL");
    return automatic && existing && symlink && local_mirror && custom_packages && multi_disk
               ? EXIT_SUCCESS : EXIT_FAILURE;
}
