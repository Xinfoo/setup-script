#define _POSIX_C_SOURCE 200809L

#include "model.h"

#include "util.h"

#include <json-c/json.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *json_string(struct json_object *object, const char *key, const char *fallback)
{
    struct json_object *value = NULL;
    if (object != NULL && json_object_object_get_ex(object, key, &value) &&
        json_object_is_type(value, json_type_string)) {
        return json_object_get_string(value);
    }
    return fallback;
}

static bool json_boolean_value(struct json_object *object, const char *key, bool fallback)
{
    struct json_object *value = NULL;
    if (object != NULL && json_object_object_get_ex(object, key, &value)) {
        return json_object_get_boolean(value) != 0;
    }
    return fallback;
}

static int json_int(struct json_object *object, const char *key, int fallback)
{
    struct json_object *value = NULL;
    if (object != NULL && json_object_object_get_ex(object, key, &value)) {
        return json_object_get_int(value);
    }
    return fallback;
}

static void add_bool(struct json_object *object, const char *key, bool value)
{
    json_object_object_add(object, key, json_object_new_boolean(value));
}

typedef struct {
    const char *name;
    enum json_type type;
} JsonField;

static bool require_json_fields(struct json_object *object, const char *section,
                                const JsonField *fields, size_t count,
                                char *error, size_t error_size)
{
    for (size_t index = 0; index < count; ++index) {
        struct json_object *value = NULL;
        if (!json_object_object_get_ex(object, fields[index].name, &value) ||
            !json_object_is_type(value, fields[index].type)) {
            (void)snprintf(error, error_size, "invalid or missing JSON field %s.%s",
                           section, fields[index].name);
            return false;
        }
    }
    return true;
}

static bool require_json_int_range(struct json_object *object, const char *section,
                                   const char *name, int minimum, int maximum,
                                   char *error, size_t error_size)
{
    struct json_object *value = NULL;
    int number;
    if (!json_object_object_get_ex(object, name, &value) ||
        !json_object_is_type(value, json_type_int)) return false;
    number = json_object_get_int(value);
    if (number < minimum || number > maximum) {
        (void)snprintf(error, error_size, "JSON field %s.%s is outside its valid range",
                       section, name);
        return false;
    }
    return true;
}

static bool require_json_nonnegative(struct json_object *object, const char *section,
                                     const char *name, int64_t maximum,
                                     char *error, size_t error_size)
{
    struct json_object *value = NULL;
    int64_t number;

    if (!json_object_object_get_ex(object, name, &value) ||
        !json_object_is_type(value, json_type_int)) return false;
    number = json_object_get_int64(value);
    if (number < 0 || number > maximum) {
        (void)snprintf(error, error_size,
                       "JSON field %s.%s must be a non-negative integer",
                       section, name);
        return false;
    }
    return true;
}

static bool validate_partition_array(struct json_object *parts, const char *prefix,
                                     char *error, size_t error_size)
{
    static const JsonField partition_fields[] = {
        {"device", json_type_string}, {"number", json_type_int},
        {"size_bytes", json_type_int}, {"current_fs", json_type_string},
        {"fs_uuid", json_type_string},
        {"part_uuid", json_type_string}, {"part_type", json_type_string},
        {"start_sector", json_type_int}, {"planned", json_type_boolean},
        {"usage", json_type_int}, {"action", json_type_int},
        {"target_fs", json_type_int}, {"f2fs_mode", json_type_int}
    };

    if (!json_object_is_type(parts, json_type_array)) {
        (void)snprintf(error, error_size, "%s must be an array", prefix);
        return false;
    }
    if (json_object_array_length(parts) > AI_MAX_PARTITIONS) {
        (void)snprintf(error, error_size, "%s exceeds the supported limit", prefix);
        return false;
    }
    for (size_t index = 0; index < json_object_array_length(parts); ++index) {
        struct json_object *item = json_object_array_get_idx(parts, index);
        char section[96];
        if (item == NULL || !json_object_is_type(item, json_type_object)) {
            (void)snprintf(error, error_size, "%s[%zu] must be an object", prefix, index);
            return false;
        }
        (void)snprintf(section, sizeof(section), "%s[%zu]", prefix, index);
        if (!require_json_fields(item, section, partition_fields,
                                 sizeof(partition_fields) / sizeof(partition_fields[0]),
                                 error, error_size) ||
            !require_json_nonnegative(item, section, "number", UINT_MAX, error, error_size) ||
            !require_json_nonnegative(item, section, "size_bytes", INT64_MAX, error, error_size) ||
            !require_json_nonnegative(item, section, "start_sector", INT64_MAX, error, error_size) ||
            !require_json_int_range(item, section, "usage", PART_UNUSED, PART_SWAP,
                                    error, error_size) ||
            !require_json_int_range(item, section, "action", ACTION_KEEP, ACTION_FORMAT,
                                    error, error_size) ||
            !require_json_int_range(item, section, "target_fs", FS_NONE, FS_SWAP,
                                    error, error_size) ||
            !require_json_int_range(item, section, "f2fs_mode", F2FS_DEFAULT,
                                    F2FS_COMPRESSED, error, error_size)) return false;
    }
    return true;
}

static bool validate_json_schema(struct json_object *root,
                                 struct json_object **system_out,
                                 struct json_object **disks_out,
                                 char *error, size_t error_size)
{
    static const JsonField root_fields[] = {
        {"version", json_type_int}, {"storage", json_type_object},
        {"system", json_type_object}
    };
    static const JsonField storage_fields[] = {{"disks", json_type_array}};
    static const JsonField disk_fields[] = {
        {"disk", json_type_string}, {"model", json_type_string},
        {"serial", json_type_string}, {"partition_table", json_type_string},
        {"size_bytes", json_type_int}, {"mode", json_type_int},
        {"removable", json_type_boolean}, {"read_only", json_type_boolean},
        {"in_use_when_detected", json_type_boolean}, {"partitions", json_type_array}
    };
    static const JsonField system_fields[] = {
        {"platform", json_type_int}, {"kernel", json_type_int},
        {"locale", json_type_int}, {"desktop", json_type_int},
        {"timezone", json_type_string}, {"hostname", json_type_string},
        {"username", json_type_string}, {"laptop", json_type_boolean},
        {"intel_graphics", json_type_boolean}, {"nvidia_graphics", json_type_boolean},
        {"bluetooth", json_type_boolean}, {"desktop_recommended", json_type_boolean},
        {"chinese_input", json_type_boolean}, {"firewall", json_type_boolean},
        {"printer", json_type_boolean}, {"archive_tools", json_type_boolean},
        {"terminal_tools", json_type_boolean}, {"extra_tools", json_type_boolean},
        {"desktop_apps", json_type_boolean}, {"local_mirror", json_type_boolean},
        {"china_mirrors", json_type_boolean}, {"secure_boot", json_type_boolean},
        {"create_efi_entry", json_type_boolean}
    };
    struct json_object *storage = NULL;
    struct json_object *system = NULL;
    struct json_object *disks = NULL;

    if (root == NULL || !json_object_is_type(root, json_type_object) ||
        !require_json_fields(root, "root", root_fields,
                             sizeof(root_fields) / sizeof(root_fields[0]), error, error_size)) {
        return false;
    }
    (void)json_object_object_get_ex(root, "storage", &storage);
    (void)json_object_object_get_ex(root, "system", &system);
    if (!require_json_fields(storage, "storage", storage_fields,
                             sizeof(storage_fields) / sizeof(storage_fields[0]), error, error_size) ||
        !require_json_fields(system, "system", system_fields,
                             sizeof(system_fields) / sizeof(system_fields[0]), error, error_size)) {
        return false;
    }
    if (!require_json_int_range(system, "system", "platform", PLATFORM_INTEL,
                                PLATFORM_VM, error, error_size) ||
        !require_json_int_range(system, "system", "kernel", KERNEL_LINUX,
                                KERNEL_HARDENED, error, error_size) ||
        !require_json_int_range(system, "system", "locale", LOCALE_EN_US,
                                LOCALE_ZH_CN, error, error_size) ||
        !require_json_int_range(system, "system", "desktop", DESKTOP_KDE,
                                DESKTOP_NONE, error, error_size)) return false;
    (void)json_object_object_get_ex(storage, "disks", &disks);
    if (json_object_array_length(disks) > AI_MAX_PLAN_DISKS) {
        (void)snprintf(error, error_size, "storage.disks exceeds the supported limit");
        return false;
    }
    for (size_t index = 0; index < json_object_array_length(disks); ++index) {
        struct json_object *disk = json_object_array_get_idx(disks, index);
        struct json_object *parts = NULL;
        char section[64];
        char prefix[80];
        if (disk == NULL || !json_object_is_type(disk, json_type_object)) {
            (void)snprintf(error, error_size, "storage.disks[%zu] must be an object", index);
            return false;
        }
        (void)snprintf(section, sizeof(section), "storage.disks[%zu]", index);
        if (!require_json_fields(disk, section, disk_fields,
                                 sizeof(disk_fields) / sizeof(disk_fields[0]), error, error_size) ||
            !require_json_int_range(disk, section, "mode", STORAGE_EXISTING,
                                    STORAGE_AUTO_DATA, error, error_size) ||
            !require_json_nonnegative(disk, section, "size_bytes", INT64_MAX,
                                      error, error_size)) return false;
        (void)json_object_object_get_ex(disk, "partitions", &parts);
        (void)snprintf(prefix, sizeof(prefix), "storage.disks[%zu].partitions", index);
        if (!validate_partition_array(parts, prefix, error, error_size)) return false;
    }
    *system_out = system;
    *disks_out = disks;
    return true;
}

bool plan_save_json(const InstallPlan *plan, const char *path, char *error, size_t error_size)
{
    struct json_object *root = json_object_new_object();
    struct json_object *storage = json_object_new_object();
    struct json_object *system = json_object_new_object();
    struct json_object *disks = json_object_new_array();
    const char *serialized;
    char *temporary = NULL;
    int descriptor = -1;
    int path_result;
    struct stat status;
    if (root == NULL || storage == NULL || system == NULL || disks == NULL) {
        (void)snprintf(error, error_size, "out of memory while creating JSON");
        if (root != NULL) json_object_put(root);
        return false;
    }
    json_object_object_add(root, "version", json_object_new_int((int)plan->version));
    for (size_t disk_index = 0; disk_index < plan->storage.disk_count; ++disk_index) {
        const DiskPlan *disk = &plan->storage.disks[disk_index];
        struct json_object *disk_object = json_object_new_object();
        struct json_object *partitions = json_object_new_array();
        if (disk_object == NULL || partitions == NULL) {
            if (disk_object != NULL) json_object_put(disk_object);
            if (partitions != NULL) json_object_put(partitions);
            (void)snprintf(error, error_size, "out of memory while creating disk JSON");
            json_object_put(root);
            return false;
        }
        json_object_object_add(disk_object, "disk", json_object_new_string(disk->path));
        json_object_object_add(disk_object, "model", json_object_new_string(disk->model));
        json_object_object_add(disk_object, "serial", json_object_new_string(disk->serial));
        json_object_object_add(disk_object, "partition_table",
                               json_object_new_string(disk->partition_table));
        json_object_object_add(disk_object, "size_bytes", json_object_new_uint64(disk->size_bytes));
        json_object_object_add(disk_object, "mode", json_object_new_int((int)disk->mode));
        add_bool(disk_object, "removable", disk->removable);
        add_bool(disk_object, "read_only", disk->read_only);
        add_bool(disk_object, "in_use_when_detected", disk->in_use);
        for (size_t index = 0; index < disk->partition_count; ++index) {
            const PartitionPlan *part = &disk->partitions[index];
            struct json_object *item = json_object_new_object();
            json_object_object_add(item, "device", json_object_new_string(part->device));
            json_object_object_add(item, "number", json_object_new_int64((int64_t)part->number));
            json_object_object_add(item, "size_bytes", json_object_new_uint64(part->size_bytes));
            json_object_object_add(item, "current_fs", json_object_new_string(part->current_fs));
            json_object_object_add(item, "fs_uuid", json_object_new_string(part->fs_uuid));
            json_object_object_add(item, "part_uuid", json_object_new_string(part->part_uuid));
            json_object_object_add(item, "part_type", json_object_new_string(part->part_type));
            json_object_object_add(item, "start_sector", json_object_new_uint64(part->start_sector));
            add_bool(item, "planned", part->planned);
            json_object_object_add(item, "usage", json_object_new_int((int)part->usage));
            json_object_object_add(item, "action", json_object_new_int((int)part->action));
            json_object_object_add(item, "target_fs", json_object_new_int((int)part->target_fs));
            json_object_object_add(item, "f2fs_mode", json_object_new_int((int)part->f2fs_mode));
            json_object_array_add(partitions, item);
        }
        json_object_object_add(disk_object, "partitions", partitions);
        json_object_array_add(disks, disk_object);
    }
    json_object_object_add(storage, "disks", disks);
    json_object_object_add(root, "storage", storage);

    json_object_object_add(system, "platform", json_object_new_int((int)plan->system.platform));
    json_object_object_add(system, "kernel", json_object_new_int((int)plan->system.kernel));
    json_object_object_add(system, "locale", json_object_new_int((int)plan->system.locale));
    json_object_object_add(system, "desktop", json_object_new_int((int)plan->system.desktop));
    json_object_object_add(system, "timezone", json_object_new_string(plan->system.timezone));
    json_object_object_add(system, "hostname", json_object_new_string(plan->system.hostname));
    json_object_object_add(system, "username", json_object_new_string(plan->system.username));
#define ADD_SYSTEM_BOOL(field) add_bool(system, #field, plan->system.field)
    ADD_SYSTEM_BOOL(laptop); ADD_SYSTEM_BOOL(intel_graphics); ADD_SYSTEM_BOOL(nvidia_graphics);
    ADD_SYSTEM_BOOL(bluetooth); ADD_SYSTEM_BOOL(desktop_recommended); ADD_SYSTEM_BOOL(chinese_input);
    ADD_SYSTEM_BOOL(firewall); ADD_SYSTEM_BOOL(printer); ADD_SYSTEM_BOOL(archive_tools);
    ADD_SYSTEM_BOOL(terminal_tools); ADD_SYSTEM_BOOL(extra_tools); ADD_SYSTEM_BOOL(desktop_apps);
    ADD_SYSTEM_BOOL(local_mirror); ADD_SYSTEM_BOOL(china_mirrors); ADD_SYSTEM_BOOL(secure_boot);
    ADD_SYSTEM_BOOL(create_efi_entry);
#undef ADD_SYSTEM_BOOL
    json_object_object_add(root, "system", system);
    path_result = lstat(path, &status);
    if (path_result == 0 && !S_ISREG(status.st_mode)) {
        (void)snprintf(error, error_size, "refusing to replace non-regular plan path: %s", path);
        json_object_put(root);
        return false;
    }
    if (path_result != 0 && errno != ENOENT) {
        (void)snprintf(error, error_size, "cannot inspect plan path %s: %s", path,
                       strerror(errno));
        json_object_put(root);
        return false;
    }
    temporary = malloc(strlen(path) + 16);
    if (temporary == NULL) {
        (void)snprintf(error, error_size, "out of memory while saving plan");
        json_object_put(root);
        return false;
    }
    (void)snprintf(temporary, strlen(path) + 16, "%s.tmp.XXXXXX", path);
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        (void)snprintf(error, error_size, "cannot create temporary plan: %s", strerror(errno));
        free(temporary);
        json_object_put(root);
        return false;
    }
    if (fchmod(descriptor, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0) {
        (void)snprintf(error, error_size, "cannot set plan permissions: %s", strerror(errno));
        (void)close(descriptor);
        (void)unlink(temporary);
        free(temporary);
        json_object_put(root);
        return false;
    }
    serialized = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);
    {
        size_t remaining = strlen(serialized);
        const char *cursor = serialized;
        while (remaining > 0) {
            ssize_t written = write(descriptor, cursor, remaining);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                (void)snprintf(error, error_size, "cannot write plan: %s", strerror(errno));
                (void)close(descriptor);
                (void)unlink(temporary);
                free(temporary);
                json_object_put(root);
                return false;
            }
            cursor += written;
            remaining -= (size_t)written;
        }
    }
    if (fsync(descriptor) != 0) {
        (void)snprintf(error, error_size, "cannot commit plan %s: %s", path, strerror(errno));
        (void)close(descriptor);
        (void)unlink(temporary);
        free(temporary);
        json_object_put(root);
        return false;
    }
    if (close(descriptor) != 0) {
        (void)snprintf(error, error_size, "cannot close plan %s: %s", path, strerror(errno));
        (void)unlink(temporary);
        free(temporary);
        json_object_put(root);
        return false;
    }
    if (rename(temporary, path) != 0) {
        (void)snprintf(error, error_size, "cannot commit plan %s: %s", path, strerror(errno));
        (void)unlink(temporary);
        free(temporary);
        json_object_put(root);
        return false;
    }
    free(temporary);
    json_object_put(root);
    return true;
}

static void load_partitions_json(DiskPlan *disk, struct json_object *parts)
{
    struct json_object *value = NULL;
    size_t count = json_object_array_length(parts);

    disk->partition_count = count > AI_MAX_PARTITIONS ? AI_MAX_PARTITIONS : count;
    for (size_t index = 0; index < disk->partition_count; ++index) {
        struct json_object *item = json_object_array_get_idx(parts, index);
        PartitionPlan *part = &disk->partitions[index];
        memset(part, 0, sizeof(*part));
        copy_text(part->device, sizeof(part->device), json_string(item, "device", ""));
        copy_text(part->current_fs, sizeof(part->current_fs), json_string(item, "current_fs", ""));
        copy_text(part->fs_uuid, sizeof(part->fs_uuid), json_string(item, "fs_uuid", ""));
        copy_text(part->part_uuid, sizeof(part->part_uuid), json_string(item, "part_uuid", ""));
        copy_text(part->part_type, sizeof(part->part_type), json_string(item, "part_type", ""));
        part->number = (unsigned)json_int(item, "number", 0);
        if (json_object_object_get_ex(item, "size_bytes", &value))
            part->size_bytes = json_object_get_uint64(value);
        if (json_object_object_get_ex(item, "start_sector", &value))
            part->start_sector = json_object_get_uint64(value);
        part->planned = json_boolean_value(item, "planned", false);
        part->usage = (PartitionUsage)json_int(item, "usage", PART_UNUSED);
        part->action = (PartitionAction)json_int(item, "action", ACTION_KEEP);
        part->target_fs = (Filesystem)json_int(item, "target_fs", FS_NONE);
        part->f2fs_mode = (F2fsMountMode)json_int(item, "f2fs_mode", F2FS_BALANCED);
    }
}

static void load_disk_json(DiskPlan *disk, struct json_object *object,
                           struct json_object *parts)
{
    struct json_object *value = NULL;
    memset(disk, 0, sizeof(*disk));
    copy_text(disk->path, sizeof(disk->path), json_string(object, "disk", ""));
    copy_text(disk->model, sizeof(disk->model), json_string(object, "model", ""));
    copy_text(disk->serial, sizeof(disk->serial), json_string(object, "serial", ""));
    copy_text(disk->partition_table, sizeof(disk->partition_table),
              json_string(object, "partition_table", ""));
    if (json_object_object_get_ex(object, "size_bytes", &value))
        disk->size_bytes = json_object_get_uint64(value);
    disk->mode = (StorageMode)json_int(object, "mode", STORAGE_EXISTING);
    disk->removable = json_boolean_value(object, "removable", false);
    disk->read_only = json_boolean_value(object, "read_only", false);
    disk->in_use = json_boolean_value(object, "in_use_when_detected", false);
    load_partitions_json(disk, parts);
}

bool plan_load_json(InstallPlan *plan, const char *path, char *error, size_t error_size)
{
    struct json_object *root = json_object_from_file(path);
    struct json_object *system = NULL;
    struct json_object *parts = NULL;
    struct json_object *version_value = NULL;
    int64_t version;
    if (root == NULL) {
        (void)snprintf(error, error_size, "cannot parse plan file: %s", path);
        return false;
    }
    if (!json_object_is_type(root, json_type_object) ||
        !json_object_object_get_ex(root, "version", &version_value) ||
        !json_object_is_type(version_value, json_type_int)) {
        (void)snprintf(error, error_size, "invalid or missing JSON field root.version");
        json_object_put(root);
        return false;
    }
    version = json_object_get_int64(version_value);
    if (version != 3) {
        (void)snprintf(error, error_size, "unsupported plan version: %" PRId64, version);
        json_object_put(root);
        return false;
    }
    if (!validate_json_schema(root, &system, &parts, error, error_size)) {
        json_object_put(root);
        return false;
    }
    plan_init(plan);
    plan->version = 3;
    {
        size_t count = json_object_array_length(parts);
        plan->storage.disk_count = count > AI_MAX_PLAN_DISKS ? AI_MAX_PLAN_DISKS : count;
        for (size_t index = 0; index < plan->storage.disk_count; ++index) {
            struct json_object *disk_object = json_object_array_get_idx(parts, index);
            struct json_object *disk_parts = NULL;
            (void)json_object_object_get_ex(disk_object, "partitions", &disk_parts);
            load_disk_json(&plan->storage.disks[index], disk_object, disk_parts);
        }
    }
    {
        int platform = json_int(system, "platform", PLATFORM_INTEL);
        int kernel = json_int(system, "kernel", KERNEL_LINUX);
        int locale = json_int(system, "locale", LOCALE_EN_US);
        int desktop = json_int(system, "desktop", DESKTOP_KDE);
        plan->system.platform = platform >= PLATFORM_INTEL && platform <= PLATFORM_VM ?
                                (Platform)platform : PLATFORM_INTEL;
        plan->system.kernel = kernel >= KERNEL_LINUX && kernel <= KERNEL_HARDENED ?
                              (Kernel)kernel : KERNEL_LINUX;
        plan->system.locale = locale >= LOCALE_EN_US && locale <= LOCALE_ZH_CN ?
                              (LocaleChoice)locale : LOCALE_EN_US;
        plan->system.desktop = desktop >= DESKTOP_KDE && desktop <= DESKTOP_NONE ?
                               (Desktop)desktop : DESKTOP_KDE;
        copy_text(plan->system.timezone, sizeof(plan->system.timezone), json_string(system, "timezone", "Asia/Shanghai"));
        copy_text(plan->system.hostname, sizeof(plan->system.hostname), json_string(system, "hostname", "ARCH-LINUX"));
        copy_text(plan->system.username, sizeof(plan->system.username), json_string(system, "username", "user"));
#define LOAD_SYSTEM_BOOL(field) plan->system.field = json_boolean_value(system, #field, plan->system.field)
        LOAD_SYSTEM_BOOL(laptop); LOAD_SYSTEM_BOOL(intel_graphics); LOAD_SYSTEM_BOOL(nvidia_graphics);
        LOAD_SYSTEM_BOOL(bluetooth); LOAD_SYSTEM_BOOL(desktop_recommended); LOAD_SYSTEM_BOOL(chinese_input);
        LOAD_SYSTEM_BOOL(firewall); LOAD_SYSTEM_BOOL(printer); LOAD_SYSTEM_BOOL(archive_tools);
        LOAD_SYSTEM_BOOL(terminal_tools); LOAD_SYSTEM_BOOL(extra_tools); LOAD_SYSTEM_BOOL(desktop_apps);
        LOAD_SYSTEM_BOOL(local_mirror); LOAD_SYSTEM_BOOL(china_mirrors); LOAD_SYSTEM_BOOL(secure_boot);
        LOAD_SYSTEM_BOOL(create_efi_entry);
#undef LOAD_SYSTEM_BOOL
    }
    json_object_put(root);
    return true;
}
