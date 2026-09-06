#include "../private.h"

#include <stdio.h>

/* Bash 模板由 CMake 转成字节数组，运行时不依赖源码目录中的模板文件。 */
static const unsigned char preamble_template[] = {
#include "generated/generator/preamble.inc"
};

/*
 * 先写脚本前导，再写磁盘、分区、内核和软件包变量。
 * 后续固定模板只消费这些只读变量，不再直接访问 C 数据模型。
 */
bool emit_header_and_plan(ScriptWriter *writer, const InstallPlan *plan,
                          const PackageConfig *packages)
{
    const PartitionPlan *root = find_partition(plan, PART_ROOT);
    const PartitionPlan *boot = find_partition(plan, PART_BOOT);
    const DiskPlan *root_disk = find_partition_disk(plan, root);
    const char *kernel = kernel_name(plan->system.kernel);
    char kernel_image[AI_TEXT_LEN];
    char initramfs_image[AI_TEXT_LEN];

    /* TARGET_DISK 由根分区反查；正常入口已验证 /boot 与它位于同一块磁盘。 */
    (void)snprintf(kernel_image, sizeof(kernel_image), "vmlinuz-%s", kernel);
    (void)snprintf(initramfs_image, sizeof(initramfs_image), "initramfs-%s.img", kernel);

    writer_write(writer, preamble_template, sizeof(preamble_template));
    if (!emit_assignment(writer, "TARGET_DISK", root_disk == NULL ? "" : root_disk->path) ||
        !emit_assignment(writer, "TARGET_TIMEZONE", plan->system.timezone) ||
        !emit_assignment(writer, "ROOT_DEVICE", root == NULL ? "" : root->device) ||
        !emit_assignment(writer, "BOOT_DEVICE", boot == NULL ? "" : boot->device) ||
        !emit_assignment(writer, "KERNEL_IMAGE", kernel_image) ||
        !emit_assignment(writer, "INITRAMFS_IMAGE", initramfs_image)) {
        return false;
    }
    emit_disk_plan(writer, plan);
    emit_boolean(writer, "USE_LOCAL_MIRROR", plan->system.local_mirror);
    emit_boolean(writer, "CREATE_EFI_ENTRY", plan->system.create_efi_entry);
    emit_partition_plan(writer, plan);
    emit_live_package_plan(writer, plan, packages);
    writer_puts(writer, "\n");
    return writer->ok;
}
