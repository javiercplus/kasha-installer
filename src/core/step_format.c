/*
 * ERASE YOUR OLD LIVE Baby
 */
#include "neko_installer.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
int step_format_and_mount(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "Configuring partitions...", 0.2);

    // SORT: Ensure proper mount order (by mountpoint depth; root first)
    app->part_config_list = g_slist_sort(app->part_config_list, sort_partitions);

    GSList *l = app->part_config_list;

    // First pass: LUKS Format & Open all encrypted partitions
    GSList *l_luks = app->part_config_list;
    while(l_luks) {
        PartitionConfig *conf = (PartitionConfig*)l_luks->data;
        if (conf->encrypt && conf->luks_pass) {
            log_to_ui_printf(app, "Encrypting %s...", conf->device);

            char keyfile[64];
            snprintf(keyfile, sizeof(keyfile), "/tmp/luks_key_XXXXXX");
            int fd = mkstemp(keyfile);
            if (fd < 0) {
                log_to_ui(app, "ERROR: Failed to create temp keyfile.", 0.0);
                return -1;
            }
            write(fd, conf->luks_pass, strlen(conf->luks_pass));
            close(fd);

            // 1. Format LUKS (LUKS1 for GRUB compatibility)
            char *cmd_fmt = g_strdup_printf("chmod 600 %s && cryptsetup luksFormat --type luks1 -q --key-file=%s %s", keyfile, keyfile, conf->device);
            if (run_sync(app, cmd_fmt) != 0) {
                log_to_ui(app, "ERROR: LUKS Format failed.", 0.0);
                unlink(keyfile);
                g_free(cmd_fmt);
                return -1;
            }
            g_free(cmd_fmt);

            // Get LUKS UUID (header UUID, needed for GRUB)
            log_to_ui(app, "Getting LUKS UUID...", 0.215);
            char luks_uuid[64] = {0};
            char *cmd_uuid = g_strdup_printf("cryptsetup luksUUID %s", conf->device);
            FILE *fp = popen(cmd_uuid, "r");
            if (fp) {
                if (fgets(luks_uuid, sizeof(luks_uuid), fp) != NULL) {
                    size_t len = strlen(luks_uuid);
                    if (len > 0 && luks_uuid[len-1] == '\n') luks_uuid[len-1] = '\0';
                    conf->luks_uuid = g_strdup(luks_uuid);
                    log_to_ui_printf(app, "LUKS UUID: %s", luks_uuid);
                }
                pclose(fp);
            } else {
                log_to_ui(app, "ERROR: Failed to get LUKS UUID", 0.0);
                g_free(cmd_uuid);
                unlink(keyfile);
                return -1;
            }
            g_free(cmd_uuid);

            // Verify UUID was captured
            if (!conf->luks_uuid || strlen(conf->luks_uuid) == 0) {
                log_to_ui(app, "ERROR: LUKS UUID is empty!", 0.0);
                unlink(keyfile);
                return -1;
            }

            // 2. Open LUKS
            char *dev_base = g_path_get_basename(conf->device);
            char *mapper_name = g_strdup_printf("cryptroot"); // Use cryptroot for compatibility
            char *cmd_open = g_strdup_printf("cryptsetup open --key-file=%s %s %s", keyfile, conf->device, mapper_name);

            if (run_sync(app, cmd_open) != 0) {
                log_to_ui(app, "ERROR: LUKS Open failed.", 0.0);
                unlink(keyfile);
                g_free(cmd_open);
                g_free(mapper_name);
                g_free(dev_base);
                return -1;
            }
            g_free(cmd_open);

            unlink(keyfile);

            /* UPDATE DEVICE PATH to /dev/mapper/...
             * g_free the old path first to avoid the memory leak. */
            g_free(conf->device);
            conf->device = g_strdup_printf("/dev/mapper/%s", mapper_name);

            g_free(dev_base);
            g_free(mapper_name);
        }
        l_luks = l_luks->next;
    }

    // Standard Loop for Root
    l = app->part_config_list;
    while(l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;

        if (strcmp(conf->fstype, "ntfs") == 0) { l = l->next; continue; }

        if (strcmp(conf->mountpoint, "/") == 0) {
            if (conf->format) {
                gchar *fs_cmd = NULL;
                log_to_ui_printf(app, "Formatting Root %s as %s...", conf->device, conf->fstype);

                // Wait for device node to appear (kernel may still be creating it)
                int wait_tries = 0;
                while (access(conf->device, F_OK) != 0 && wait_tries < 10) {
                    log_to_ui_printf(app, "Waiting for %s to appear... (%d/10)", conf->device, wait_tries + 1);
                    sleep(1);
                    wait_tries++;
                }
                if (access(conf->device, F_OK) != 0) {
                    log_to_ui_printf(app, "ERROR: Device %s does not exist!", conf->device);
                    return -1;
                }

                // Ensure partition is not busy (unmount + swapoff just in case)
                run_sync(app, "umount -lf %s 2>/dev/null || true", conf->device);
                run_sync(app, "swapoff %s 2>/dev/null || true", conf->device);

                // Wipe old filesystem signatures before formatting
                run_sync(app, "wipefs -af %s 2>/dev/null || true", conf->device);

                if (strcmp(conf->fstype, "ext4") == 0) fs_cmd = "mkfs.ext4 -F";
                else if (strcmp(conf->fstype, "btrfs") == 0) fs_cmd = "mkfs.btrfs -f";
                else if (strcmp(conf->fstype, "xfs") == 0) fs_cmd = "mkfs.xfs -f";
                else if (strcmp(conf->fstype, "f2fs") == 0) fs_cmd = "mkfs.f2fs -f";

                if (fs_cmd) {
                    if (run_sync(app, "%s %s", fs_cmd, conf->device) != 0) {
                        log_to_ui(app, "ERROR: Formatting failed!", 0.0);
                        return -1;
                    }
                }
            }

            log_to_ui_printf(app, "Mounting Root %s...", conf->device);
            run_sync(app, "mkdir -p %s", TARGETDIR);

            if (run_sync(app, "mount %s %s", conf->device, TARGETDIR) != 0) {
                log_to_ui_printf(app, "ERROR: Failed to mount %s to %s!", conf->device, TARGETDIR);
                return -1;
            }
        }
        l = l->next;
    }

    // Standard Loop for Others
    l = app->part_config_list;
    while(l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;

        if (strcmp(conf->fstype, "ntfs") == 0) { l = l->next; continue; }

        if (strcmp(conf->mountpoint, "/") != 0) {
            gchar *fs_cmd = NULL;

            // 2.1 Format
            if (conf->format) {
                log_to_ui_printf(app, "Formatting %s...", conf->mountpoint);

                // Wait for device node to appear
                int wait_tries = 0;
                while (access(conf->device, F_OK) != 0 && wait_tries < 10) {
                    log_to_ui_printf(app, "Waiting for %s to appear... (%d/10)", conf->device, wait_tries + 1);
                    sleep(1);
                    wait_tries++;
                }
                if (access(conf->device, F_OK) != 0) {
                    log_to_ui_printf(app, "ERROR: Device %s does not exist!", conf->device);
                    return -1;
                }

                // Ensure partition is not busy
                run_sync(app, "umount -lf %s 2>/dev/null || true", conf->device);
                run_sync(app, "swapoff %s 2>/dev/null || true", conf->device);

                // Wipe old filesystem signatures before formatting
                run_sync(app, "wipefs -af %s 2>/dev/null || true", conf->device);

                if (strcmp(conf->fstype, "ext4") == 0) fs_cmd = "mkfs.ext4 -F";
                else if (strcmp(conf->fstype, "btrfs") == 0) fs_cmd = "mkfs.btrfs -f";
                else if (strcmp(conf->fstype, "xfs") == 0) fs_cmd = "mkfs.xfs -f";
                else if (strcmp(conf->fstype, "f2fs") == 0) fs_cmd = "mkfs.f2fs -f";
                else if (strcmp(conf->fstype, "vfat") == 0) fs_cmd = "mkfs.vfat -F32";
                else if (strcmp(conf->fstype, "swap") == 0) fs_cmd = "mkswap";

                if (fs_cmd) {
                    if (run_sync(app, "%s %s", fs_cmd, conf->device) != 0) {
                        log_to_ui_printf(app, "ERROR: Formatting %s failed!", conf->mountpoint);
                        return -1;
                    }
                }
            }

            // 2.2 Mount
            if (strcmp(conf->fstype, "swap") == 0) {
                run_sync(app, "swapon %s", conf->device);
            } else {
                gchar *target_path = g_strdup_printf("%s%s", TARGETDIR, conf->mountpoint);
                run_sync(app, "mkdir -p %s", target_path);
                log_to_ui_printf(app, "Mounting %s...", conf->mountpoint);

                if (run_sync(app, "mount %s %s", conf->device, target_path) != 0) {
                    log_to_ui_printf(app, "ERROR: Failed to mount %s to %s!", conf->device, target_path);
                    g_free(target_path);
                    return -1;
                }
                g_free(target_path);
            }
        }
        l = l->next;
    }
    return 0;
}
