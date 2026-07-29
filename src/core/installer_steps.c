/*
 * installer_steps.c
 * Installation Steps Implementation
 */
#include "neko_installer.h"
#include "country_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <ctype.h>

int check_filesystems(AppData *app) {
    gboolean root_found = FALSE;
    gboolean usr_found = FALSE;
    gboolean efi_partition_found = FALSE;

    GSList *l = app->part_config_list;
    while (l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;

        if (strcmp(conf->mountpoint, "/") == 0) {
            root_found = TRUE;
        } else if (strcmp(conf->mountpoint, "/usr") == 0) {
            usr_found = TRUE;
        } else if (strcmp(conf->mountpoint, "/boot/efi") == 0 &&
                  (strcmp(conf->fstype, "vfat") == 0 || strcmp(conf->fstype, "fat32") == 0)) {
            efi_partition_found = TRUE;
        }
        l = l->next;
    }

    if (!root_found) {
        log_to_ui(app, "ERROR: Root (/) partition not configured!", 0.0);
        return -1;
    }

    if (usr_found) {
        log_to_ui(app, "ERROR: /usr as separate partition is not supported!", 0.0);
        return -1;
    }

    // EFI requires EFI partition, but skip for now - bootloader step will handle
    return 0;
}

int step_partitioning(AppData *app, const char *disk_name) {
    if (app->install_mode == INSTALL_MODE_MANUAL) return 0; // Skip if manual

    log_to_ui(app, "Partitioning Disk...", 0.15);

    char disk_dev[64];
    snprintf(disk_dev, sizeof(disk_dev), "/dev/%s", disk_name);

    // ===== PRE-PARTITION CLEANUP =====
    // Deactivate swap on all partitions of this disk
    log_to_ui(app, "Deactivating swap partitions on target disk...", 0.12);
    run_sync(app, "for p in /dev/%s*; do swapoff \"$p\" 2>/dev/null; done || true", disk_name);

    // Unmount all partitions currently mounted from this disk
    log_to_ui(app, "Unmounting any existing partitions on target disk...", 0.13);
    run_sync(app, "for p in $(lsblk -rn -o NAME /dev/%s 2>/dev/null | grep -v '^%s$'); do umount -lf /dev/$p 2>/dev/null; done || true", disk_name, disk_name);

    // Close any LUKS devices backed by partitions on this disk
    run_sync(app, "for p in $(lsblk -rn -o NAME /dev/%s 2>/dev/null | grep -v '^%s$'); do "
                   "cryptsetup close /dev/mapper/$(lsblk -rn -o NAME /dev/$p 2>/dev/null | head -1) 2>/dev/null; "
                   "done || true", disk_name, disk_name);

    if (app->install_mode == INSTALL_MODE_ERASE) {
        // ===== CLEAN INSTALL: ERASE / CREATE NEW TABLE =====

        // Wipe old filesystem signatures from the entire disk
        log_to_ui(app, "Wiping old signatures from disk...", 0.14);
        run_sync(app, "wipefs -af %s 2>/dev/null || true", disk_dev);

        char cmd_buf[256];
        snprintf(cmd_buf, sizeof(cmd_buf), "sfdisk --wipe always %s", disk_dev);
        log_to_ui_printf(app, "Running: %s", cmd_buf);
        FILE *sf = popen(cmd_buf, "w");
        if (sf) {
             if (app->is_efi) fprintf(sf, "label: gpt\n");
             else fprintf(sf, "label: dos\n");

             if (app->is_efi) {
                 fprintf(sf, ",512M,U\n"); // ESP
                 fprintf(sf, ",,L\n"); // Root (Rest)
             } else {
                 fprintf(sf, ",,L,*\n"); // Root – MBR Boot Flag
             }

             int sfdisk_ret = pclose(sf);
             if (sfdisk_ret != 0) {
                 log_to_ui_printf(app, "ERROR: sfdisk failed (exit code %d)! "
                                  "Disk may be busy or have I/O errors.", WEXITSTATUS(sfdisk_ret));
                 return -1;
             }

             sleep(2);
             run_sync(app, "blockdev --rereadpt %s 2>/dev/null || true", disk_dev);
             run_sync(app, "udevadm settle --timeout=10 2>/dev/null || sleep 2");
             sleep(1);
        } else {
             log_to_ui(app, "ERROR: Could not execute sfdisk!", 0.0);
             return -1;
        }

        // Assign device paths for clean install
        GSList *l = app->part_config_list;
        const char *sep = "";
        int len = strlen(disk_name);
        if (g_ascii_isdigit(disk_name[len-1])) sep = "p";

        while(l) {
            PartitionConfig *cfg = (PartitionConfig*)l->data;
            g_free(cfg->device);

            if (app->is_efi) {
                if (strcmp(cfg->mountpoint, "/boot/efi") == 0) {
                    cfg->device = g_strdup_printf("/dev/%s%s1", disk_name, sep);
                } else {
                    cfg->device = g_strdup_printf("/dev/%s%s2", disk_name, sep);
                }
            } else {
                cfg->device = g_strdup_printf("/dev/%s%s1", disk_name, sep);
            }
            l = l->next;
        }

        // Verify partition device nodes were actually created
        l = app->part_config_list;
        while(l) {
            PartitionConfig *cfg = (PartitionConfig*)l->data;
            int tries = 0;
            while (access(cfg->device, F_OK) != 0 && tries < 15) {
                log_to_ui_printf(app, "Waiting for %s to appear... (%d/15)", cfg->device, tries + 1);
                run_sync(app, "udevadm settle --timeout=3 2>/dev/null || sleep 1");
                sleep(1);
                tries++;
            }
            if (access(cfg->device, F_OK) != 0) {
                log_to_ui_printf(app, "ERROR: Partition %s was not created! "
                                 "sfdisk may have failed silently.", cfg->device);
                return -1;
            }
            log_to_ui_printf(app, "Partition %s is ready.", cfg->device);
            l = l->next;
        }

    } else if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
        // ===== DUAL BOOT: Resize existing partition if needed, then create new =====
        gboolean has_existing_efi = (app->detected_efi_partition != NULL);
        gboolean need_new_efi = (app->is_efi && !has_existing_efi);

        if (has_existing_efi) {
            log_to_ui_printf(app, "Reusing existing EFI partition: %s", app->detected_efi_partition);
        }

        // --- MBR validation: check partition table type and slot availability ---
        char *pt_type = get_disk_partition_table_type(disk_name);
        gboolean is_mbr = (pt_type && strcmp(pt_type, "dos") == 0);
        log_to_ui_printf(app, "Partition table type: %s", pt_type ? pt_type : "unknown");

        if (is_mbr) {
            int primary_count = get_mbr_primary_count(disk_name);
            int slots_needed = 1; // root partition
            if (need_new_efi) slots_needed++; // should not happen on MBR, but safety

            log_to_ui_printf(app, "MBR: %d primary partitions found, need %d more slot(s)",
                             primary_count, slots_needed);

            if (primary_count + slots_needed > 4) {
                log_to_ui_printf(app,
                    "ERROR: MBR disk already has %d primary partitions (max 4). "
                    "Cannot add %d more. Use GParted to free a partition slot, "
                    "or convert the disk to GPT.", primary_count, slots_needed);
                g_free(pt_type);
                return -1;
            }
        }
        g_free(pt_type);

        // Step 1: Check if there's already enough free space
        glong free_space_mb = get_disk_free_space_mb(disk_name);
        glong min_needed_mb = 7 * 1024; // 7GB minimum for Neko-Void root
        if (need_new_efi) min_needed_mb += 512;

        log_to_ui_printf(app, "Free space on disk: %ld MB, needed: %ld MB",
                         free_space_mb, min_needed_mb);

        if (free_space_mb < min_needed_mb) {
            // Not enough free space — must resize an existing partition
            log_to_ui(app, "Not enough free space, will resize an existing partition...", 0.16);

            char *resize_target = find_largest_resizable_partition(disk_name);
            if (!resize_target) {
                log_to_ui(app,
                    "ERROR: No resizable partition found on this disk! "
                    "Please free up space manually using GParted or "
                    "your existing OS disk management tool before trying "
                    "dual boot installation.", 0.0);
                return -1;
            }

            // Check filesystem type of the resize target
            char target_fstype[64];
            get_partition_fstype(resize_target, target_fstype, sizeof(target_fstype));

            glong part_size = get_partition_size_mb(resize_target);
            glong min_size  = get_fs_min_size_mb(resize_target);

            // For btrfs/xfs: get_fs_min_size_mb returns -1 (unsupported)
            // Use a conservative estimate: 50% of current usage or 2GB minimum
            if (min_size < 0) {
                if (strcmp(target_fstype, "btrfs") == 0 ||
                    strcmp(target_fstype, "xfs") == 0) {
                    // Cannot reliably shrink btrfs/xfs in-place without btrfs tools
                    // or xfs_growfs (xfs cannot shrink at all)
                    if (strcmp(target_fstype, "xfs") == 0) {
                        log_to_ui_printf(app,
                            "ERROR: Partition %s uses XFS which cannot be shrunk. "
                            "Please free up space manually using GParted or "
                            "your existing OS disk management tool.", resize_target);
                        g_free(resize_target);
                        return -1;
                    }
                    // btrfs: estimate minimum as 50% of current size
                    min_size = part_size / 2;
                    if (min_size < 2048) min_size = 2048; // 2GB floor
                    log_to_ui_printf(app,
                        "Filesystem %s on %s: using estimated minimum %ld MB",
                        target_fstype, resize_target, min_size);
                } else {
                    log_to_ui_printf(app,
                        "ERROR: Cannot determine minimum size for %s (%s). "
                        "Please free up space manually.", resize_target, target_fstype);
                    g_free(resize_target);
                    return -1;
                }
            }

            /* Safety margin: leave 512MB above fs minimum for the existing OS */
            glong safety_margin = 512;
            glong available_for_neko = part_size - min_size - safety_margin;

            if (available_for_neko < min_needed_mb) {
                log_to_ui_printf(app,
                    "ERROR: Not enough space to resize! "
                    "Partition %s (%ldMB) needs %ldMB minimum free. "
                    "Please free up space manually using your existing OS.",
                    resize_target, part_size,
                    min_needed_mb + min_size + safety_margin);
                g_free(resize_target);
                return -1;
            }

            /* Give 50% of available space to Neko, ensure at least min_needed */
            glong space_for_neko = available_for_neko / 2;
            if (space_for_neko < min_needed_mb) space_for_neko = min_needed_mb;
            glong new_partition_size = part_size - space_for_neko;

            /* Clamp: new size must stay above minimum + safety */
            if (new_partition_size < min_size + safety_margin) {
                new_partition_size = min_size + safety_margin;
            }

            log_to_ui_printf(app,
                "Resizing %s: %ld MB → %ld MB (freeing %ld MB for Neko-Void)",
                resize_target, part_size, new_partition_size,
                part_size - new_partition_size);

            // resize_existing_partition handles ext2/3/4, ntfs, and btrfs
            if (resize_existing_partition(app, resize_target,
                                          new_partition_size) != 0) {
                log_to_ui(app, "ERROR: Partition resize failed!", 0.0);
                g_free(resize_target);
                return -1;
            }
            g_free(resize_target);
            log_to_ui(app, "Partition resize completed successfully.", 0.17);
        } else {
            log_to_ui(app, "Sufficient free space found, skipping resize.", 0.16);
        }

        // Step 2: Count existing partitions BEFORE adding new ones
        char count_cmd[256];
        snprintf(count_cmd, sizeof(count_cmd),
            "lsblk -rn -o NAME /dev/%s | grep -v '^%s$' | wc -l",
            disk_name, disk_name);
        FILE *fp_count = popen(count_cmd, "r");
        int existing_part_count = 0;
        if (fp_count) {
            char count_buf[16];
            if (fgets(count_buf, sizeof(count_buf), fp_count)) {
                existing_part_count = atoi(count_buf);
            }
            pclose(fp_count);
        }
        log_to_ui_printf(app, "Existing partitions on disk: %d", existing_part_count);

        // Step 3: Create new partition(s) in the free space (now guaranteed to exist)
        char cmd_buf2[256];
        snprintf(cmd_buf2, sizeof(cmd_buf2), "sfdisk -a %s", disk_dev);
        log_to_ui_printf(app, "Running: %s", cmd_buf2);
        FILE *sf = popen(cmd_buf2, "w");
        if (sf) {
            if (need_new_efi) {
                log_to_ui(app, "Creating new EFI partition...", 0.18);
                fprintf(sf, ",512M,U\n"); // ESP in free space
            }
            fprintf(sf, ",,L\n"); // Root in remaining free space

            int sfdisk_ret = pclose(sf);
            if (sfdisk_ret != 0) {
                log_to_ui_printf(app, "ERROR: sfdisk failed (exit code %d)! "
                                 "Disk may be busy or have I/O errors.", WEXITSTATUS(sfdisk_ret));
                return -1;
            }

            sleep(2);
            run_sync(app, "blockdev --rereadpt %s 2>/dev/null || true", disk_dev);
            run_sync(app, "udevadm settle --timeout=10 2>/dev/null || sleep 2");
            sleep(1);
        } else {
            log_to_ui(app, "ERROR: Could not execute sfdisk!", 0.0);
            return -1;
        }

        // Step 4: Assign device paths for dual boot partitions
        const char *sep = "";
        int len = strlen(disk_name);
        if (g_ascii_isdigit(disk_name[len-1])) sep = "p";

        int new_part_base = existing_part_count + 1;

        GSList *l = app->part_config_list;
        while(l) {
            PartitionConfig *cfg = (PartitionConfig*)l->data;

            if (strcmp(cfg->mountpoint, "/boot/efi") == 0) {
                g_free(cfg->device);
                if (has_existing_efi) {
                    cfg->device = g_strdup(app->detected_efi_partition);
                } else {
                    cfg->device = g_strdup_printf("/dev/%s%s%d",
                        disk_name, sep, new_part_base);
                }
            } else if (strcmp(cfg->mountpoint, "/") == 0) {
                g_free(cfg->device);
                if (need_new_efi) {
                    cfg->device = g_strdup_printf("/dev/%s%s%d",
                        disk_name, sep, new_part_base + 1);
                } else {
                    cfg->device = g_strdup_printf("/dev/%s%s%d",
                        disk_name, sep, new_part_base);
                }
            }
            l = l->next;
        }

        // Step 5: Set boot flag on root partition for MBR/Legacy
        if (!app->is_efi) {
            // Find the root partition number
            int root_part_num = need_new_efi ? new_part_base + 1 : new_part_base;
            log_to_ui_printf(app, "Setting boot flag on partition %d (MBR/Legacy)...", root_part_num);
            run_sync(app, "sfdisk --activate %s %d 2>/dev/null || true", disk_dev, root_part_num);
        }

        // Verify partition device nodes were actually created
        l = app->part_config_list;
        while(l) {
            PartitionConfig *cfg = (PartitionConfig*)l->data;
            int tries = 0;
            while (access(cfg->device, F_OK) != 0 && tries < 15) {
                log_to_ui_printf(app, "Waiting for %s to appear... (%d/15)", cfg->device, tries + 1);
                run_sync(app, "udevadm settle --timeout=3 2>/dev/null || sleep 1");
                sleep(1);
                tries++;
            }
            if (access(cfg->device, F_OK) != 0) {
                log_to_ui_printf(app, "ERROR: Partition %s was not created! "
                                 "sfdisk may have failed silently.", cfg->device);
                return -1;
            }
            log_to_ui_printf(app, "Partition %s is ready.", cfg->device);
            l = l->next;
        }
    }
    return 0;
}


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

// UPDATE DEVICE PATH to /dev/mapper/...
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

int step_install_base_system(AppData *app, const char *TARGETDIR) {
    // COPY ROOTFS
    log_to_ui(app, "Copying Live Image to Target...", 0.3);
    char tar_cmd[512];
    snprintf(tar_cmd, sizeof(tar_cmd),
        "tar -cf - --one-file-system --xattrs / 2>/dev/null | "
        "tar --extract --xattrs --xattrs-include='*' --preserve-permissions -f - -C %s", TARGETDIR);
    int ret = system(tar_cmd);
    if (WEXITSTATUS(ret) != 0) {
        log_to_ui(app, "ERROR: Failed to copy filesystem.", 0.0);
        return -1;
    }

    // CLEANUP LIVE FILES
    log_to_ui(app, "Cleaning up live image files...", 0.4);

    run_sync(app, "rm -f %s/etc/motd", TARGETDIR);
    run_sync(app, "rm -f %s/etc/issue", TARGETDIR);
    run_sync(app, "rm -f %s/usr/sbin/void-installer", TARGETDIR);
    run_sync(app, "rm -f %s/etc/sddm.conf", TARGETDIR);
    run_sync(app, "rmdir %s/mnt/target 2>/dev/null", TARGETDIR);

    // MOUNT DEV/PROC/SYS
    log_to_ui(app, "Mounting virtual filesystems...", 0.5);
    run_sync(app, "mount --rbind /dev %s/dev", TARGETDIR);
    run_sync(app, "mount --rbind /proc %s/proc", TARGETDIR);
    run_sync(app, "mount --rbind /sys %s/sys", TARGETDIR);

    // INSTALL CRYPTSETUP IF NEEDED
    gboolean has_crypto = FALSE;
    GSList *chk = app->part_config_list;
    while(chk) {
      if(((PartitionConfig*)chk->data)->encrypt) has_crypto = TRUE;
      chk = chk->next;
    }

#ifndef UNIVERSAL_BUILD
    /* --- Void Linux: xbps-install cryptsetup + copy XBPS keys --- */
    if (has_crypto) {
        void_install_crypto_packages(app, TARGETDIR);
    }
    void_copy_xbps_keys(app, TARGETDIR);
#else
    /* --- Universal: cryptsetup must already be in the base image --- */
    if (has_crypto) {
        log_to_ui(app, "[Universal] cryptsetup required – ensure it is pre-installed in the base image.", 0.55);
    }
#endif

    // REBUILD INITRAMFS (generic, with AHCI driver for SATA support)
    log_to_ui(app, "Rebuilding initramfs...", 0.6);
    run_sync(app, "chroot %s dracut --no-hostonly --add-drivers \"ahci\" --force", TARGETDIR);

#ifndef UNIVERSAL_BUILD
    /* --- Void Linux: reconfigure base packages with xbps-reconfigure --- */
    void_reconfigure_base(app, TARGETDIR);
#else
    log_to_ui(app, "[Universal] Skipping xbps-reconfigure (not a Void system).", 0.63);
#endif

    return 0;
}

int step_configure_system(AppData *app, const char *TARGETDIR, const gchar *hostname, const gchar *locale, const gchar *root_pass, const gchar *user_login, const gchar *user_fullname, const gchar *user_pass, gboolean autologin) {
#ifndef UNIVERSAL_BUILD
    /* --- Void Linux: remove live packages with xbps-remove --- */
    void_remove_live_packages(app, TARGETDIR);
#else
    log_to_ui(app, "[Universal] Skipping xbps-remove of live packages (not a Void system).", 0.70);
#endif

    // REMOVE LIVE USER FIRST (before creating new user to avoid UID conflicts)
    log_to_ui(app, "Removing live user (anon) from target system...", 0.72);
    if (run_sync(app, "chroot %s userdel anon 2>/dev/null", TARGETDIR) != 0) {
        log_to_ui(app, "WARNING: userdel anon failed, forcing removal...", 0.72);
    }
    run_sync(app, "rm -rf %s/home/anon 2>/dev/null || true", TARGETDIR);
    run_sync(app, "rm -f %s/var/spool/mail/anon 2>/dev/null || true", TARGETDIR);
    run_sync(app, "rm -f %s/etc/sudoers.d/99-void-live 2>/dev/null || true", TARGETDIR);
    run_sync(app, "sed -i 's|GETTY_ARGS=\"--noclear -a anon\"|GETTY_ARGS=\"--noclear\"|g' %s/etc/sv/agetty-tty1/conf", TARGETDIR);
    run_sync(app, "rm -f %s/etc/polkit-1/rules.d/void-live.rules 2>/dev/null || true", TARGETDIR);

    // CLEANUP CLONED LIVE STATE
    log_to_ui(app, "Cleaning up machine-id and network state...", 0.73);
    run_sync(app, "rm -f %s/etc/machine-id", TARGETDIR);
    run_sync(app, "rm -f %s/var/lib/dbus/machine-id", TARGETDIR);

    // Copy NetworkManager WiFi/network connections from live system
    run_sync(app, "mkdir -p %s/etc/NetworkManager/system-connections", TARGETDIR);
    run_sync(app, "cp -rf /etc/NetworkManager/system-connections/* %s/etc/NetworkManager/system-connections/ 2>/dev/null || true", TARGETDIR);


    // CONFIGURATION (Hostname, Locale)
    log_to_ui(app, "Applying System Configuration...", 0.75);
    run_sync(app, "echo '%s' > %s/etc/hostname", hostname, TARGETDIR);

    // Enable locale
    run_sync(app, "echo 'LANG=%s' > %s/etc/locale.conf", locale, TARGETDIR);
#ifndef UNIVERSAL_BUILD
    /* --- Void Linux: enable locale in libc-locales and reconfigure with xbps --- */
    void_reconfigure_locales(app, TARGETDIR, locale);
#else
    /* --- Universal: use locale-gen or another base distro mechanism --- */
    run_sync(app, "chroot %s locale-gen 2>/dev/null || true", TARGETDIR);
#endif

    // KEYMAP SETUP — configure keyboard layout for console, X11 and Wayland
    {
        const char *console_kmap = "us";
        const char *x11_layout = "us";
        const char *x11_variant = "";

        // 1st priority: user-selected keyboard layout from UI
        int kbd_active = gtk_combo_box_get_active(GTK_COMBO_BOX(app->kbd_layout_combo));
        if (kbd_active >= 0) {
            int kbd_count = 0;
            const KbdLayout *layouts = get_keyboard_layouts(&kbd_count);
            if (kbd_active < kbd_count) {
                console_kmap = layouts[kbd_active].console_kmap;
                x11_layout   = layouts[kbd_active].layout;
            }

            // Get selected variant
            int var_active = gtk_combo_box_get_active(GTK_COMBO_BOX(app->kbd_variant_combo));
            if (var_active > 0 && kbd_active < kbd_count) {
                // variant index 0 = "Default" = empty string
                const KbdVariant *vars = layouts[kbd_active].variants;
                int vi = 0;
                for (int i = 0; vars[i].name != NULL; i++) {
                    if (vi == var_active) {
                        x11_variant = vars[i].code;
                        break;
                    }
                    vi++;
                }
            }
        } else {
            // 2nd priority: derive from country selection
            gchar *country_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->country_combo));
            if (country_name && strlen(country_name) > 0) {
                const CountryInfo *ci = find_country_by_name(country_name);
                if (ci && ci->console_kmap && ci->x11_layout) {
                    console_kmap = ci->console_kmap;
                    x11_layout   = ci->x11_layout;
                }
                g_free(country_name);
            }
        }

        log_to_ui_printf(app, "Keyboard: console=%s, X11=%s, variant=%s",
                         console_kmap, x11_layout,
                         strlen(x11_variant) ? x11_variant : "none");

        // 1. Console TTY keymap → /etc/rc.conf (Runit/Void Linux)
        run_sync(app, "mkdir -p %s/etc", TARGETDIR);
        run_sync(app, "if grep -q '^KEYMAP=' %s/etc/rc.conf 2>/dev/null; then "
                       "sed -i 's/^KEYMAP=.*/KEYMAP=\"%s\"/' %s/etc/rc.conf; "
                       "else echo 'KEYMAP=\"%s\"' >> %s/etc/rc.conf; fi",
                 TARGETDIR, console_kmap, TARGETDIR, console_kmap, TARGETDIR);

        // 2. X11 keyboard → /etc/X11/xorg.conf.d/00-keyboard.conf
        run_sync(app, "mkdir -p %s/etc/X11/xorg.conf.d", TARGETDIR);
        if (strlen(x11_variant) > 0) {
            run_sync(app, "printf 'Section \"InputClass\"\\n"
                           "        Identifier \"system-keyboard\"\\n"
                           "        MatchIsKeyboard \"on\"\\n"
                           "        Option \"XkbLayout\" \"%s\"\\n"
                           "        Option \"XkbVariant\" \"%s\"\\n"
                           "EndSection\\n' > %s/etc/X11/xorg.conf.d/00-keyboard.conf",
                     x11_layout, x11_variant, TARGETDIR);
        } else {
            run_sync(app, "printf 'Section \"InputClass\"\\n"
                           "        Identifier \"system-keyboard\"\\n"
                           "        MatchIsKeyboard \"on\"\\n"
                           "        Option \"XkbLayout\" \"%s\"\\n"
                           "EndSection\\n' > %s/etc/X11/xorg.conf.d/00-keyboard.conf",
                     x11_layout, TARGETDIR);
        }

        // 3. Environment variables for Wayland / XKB → /etc/profile.d/keyboard.sh
        run_sync(app, "mkdir -p %s/etc/profile.d", TARGETDIR);
        if (strlen(x11_variant) > 0) {
            run_sync(app, "printf 'export XKB_DEFAULT_LAYOUT=\"%s\"\\n"
                           "export XKB_DEFAULT_VARIANT=\"%s\"\\n' > %s/etc/profile.d/keyboard.sh",
                     x11_layout, x11_variant, TARGETDIR);
        } else {
            run_sync(app, "printf 'export XKB_DEFAULT_LAYOUT=\"%s\"\\n' > %s/etc/profile.d/keyboard.sh",
                     x11_layout, TARGETDIR);
        }
        run_sync(app, "chmod 0644 %s/etc/profile.d/keyboard.sh", TARGETDIR);
    }

    // TIMEZONE SETUP
    char *tz_area = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->tz_area_combo));
    char *tz_city = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->tz_city_combo));

    if (tz_area && tz_city) {
        log_to_ui(app, "Setting Timezone...", 0.78);
        run_sync(app, "ln -sf /usr/share/zoneinfo/%s/%s %s/etc/localtime", tz_area, tz_city, TARGETDIR);
        g_free(tz_area);
        g_free(tz_city);
    } else {
        log_to_ui(app, "Timezone not selected, defaulting to UTC.", 0.78);
        run_sync(app, "ln -sf /usr/share/zoneinfo/UTC %s/etc/localtime", TARGETDIR);
    }

#ifdef HAS_DESKTOP_TAB
    // INSTALL DESKTOP ENVIRONMENT (before /etc/skel copy and user creation)
    g_free(app->selected_desktop);
    app->selected_desktop = NULL;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_xfce)))
        app->selected_desktop = g_strdup("xfce");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_niri)))
        app->selected_desktop = g_strdup("niri");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_kde)))
        app->selected_desktop = g_strdup("kde");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_icejwm)))
        app->selected_desktop = g_strdup("icejwm");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_mate)))
        app->selected_desktop = g_strdup("mate");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_labwc)))
        app->selected_desktop = g_strdup("labwc");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_lxqt)))
        app->selected_desktop = g_strdup("lxqt");

    if (app->selected_desktop && app->selected_desktop[0] != '\0') {
        log_to_ui_printf(app, "Installing desktop environment: %s", app->selected_desktop);
        if (strcmp(app->selected_desktop, "xfce") == 0)
            void_xfce(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "niri") == 0)
            void_niri(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "kde") == 0)
            void_kde(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "icejwm") == 0)
            void_icejwm(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "mate") == 0)
            void_mate(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "labwc") == 0)
            void_labwc(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "lxqt") == 0)
            void_lxqt(app, TARGETDIR);
    }
#endif

    // ROOT USER
    log_to_ui(app, "Setting Root Password (SHA512)...", 0.80);
    if (!set_safe_password(app, "root", root_pass, TARGETDIR)) {
        return -1;
    }

    // Copy /etc/skel files for root
    run_sync(app, "cp %s/etc/skel/.[bix]* %s/root/ 2>/dev/null", TARGETDIR, TARGETDIR);

    // CREATE USER ACCOUNT (with full group membership)
    if (strlen(user_login) > 0) {
        log_to_ui(app, "Creating user account...", 0.82);

        // Step 1: Ensure ALL required groups exist
        // Note: nopasswdlogin is NOT included here — it is only added conditionally
        // when autologin is enabled (it grants password-bypass via PAM for emptty)
        const char *all_groups[] = {
            "wheel", "floppy", "audio", "video", "cdrom", "optical",
            "storage", "network", "kvm", "input", "plugdev", "users",
            "xbuilder", "render", "fuse", "disk", NULL
        };

        for (int i = 0; all_groups[i] != NULL; i++) {
            log_to_ui_printf(app, "Ensuring group '%s' exists...", all_groups[i]);
            run_sync(app, "chroot %s getent group %s > /dev/null 2> /dev/null || chroot %s groupadd %s", TARGETDIR, all_groups[i], TARGETDIR, all_groups[i]);
        }

        // Step 2: Build list of groups that actually exist
        char valid_groups[1024] = {0};
        int first = 1;

        for (int i = 0; all_groups[i] != NULL; i++) {
            char check_cmd[256];
            snprintf(check_cmd, sizeof(check_cmd), "chroot %s getent group %s > /dev/null 2> /dev/null", TARGETDIR, all_groups[i]);
            if (system(check_cmd) == 0) {
                if (!first) {
                    strcat(valid_groups, ",");
                }
                strcat(valid_groups, all_groups[i]);
                first = 0;
            } else {
                log_to_ui_printf(app, "WARNING: Group '%s' does not exist, skipping", all_groups[i]);
            }
        }

        log_to_ui_printf(app, "Using groups: %s", valid_groups);

        // Step 3: Create the user
        gchar *safe_fullname = NULL;
        if (user_fullname && strlen(user_fullname) > 0) {
            safe_fullname = g_shell_quote(user_fullname);
        }

        gchar *useradd_cmd = NULL;
        if (safe_fullname) {
            useradd_cmd = g_strdup_printf(
                "chroot %s useradd -m -c %s -G %s -s /bin/bash %s",
                TARGETDIR, safe_fullname, valid_groups, user_login);
        } else {
            useradd_cmd = g_strdup_printf(
                "chroot %s useradd -m -G %s -s /bin/bash %s",
                TARGETDIR, valid_groups, user_login);
        }

        g_free(safe_fullname);

        log_to_ui(app, useradd_cmd, -1);
        int add_result = system(useradd_cmd);
        g_free(useradd_cmd);
        if (WEXITSTATUS(add_result) != 0) {
            log_to_ui(app, "ERROR: useradd failed! Aborting configuration.", 0.0);
            return -1;
        }

        log_to_ui(app, "Setting User Password (SHA512)...", 0.84);
        if (!set_safe_password(app, user_login, user_pass, TARGETDIR)) {
            return -1;
        }

        // Verify user was created
        if (run_sync(app, "chroot %s id %s", TARGETDIR, user_login) != 0) {
            log_to_ui_printf(app, "ERROR: User %s not found after creation!", user_login);
            return -1;
        }
        log_to_ui_printf(app, "User %s created successfully.", user_login);

        // Neko Void customizations
        log_to_ui(app, "Applying Neko Void customizations...", 0.85);

        // Add rice_set as autostart entry (script is already in the distro)
        log_to_ui(app, "Creating rice_set autostart entry...", 0.86);
        run_sync(app, "mkdir -p %s/home/%s/.config/autostart", TARGETDIR, user_login);
        run_sync(app, "printf '[Desktop Entry]\\nType=Application\\nName=Rice Set\\nExec=/usr/bin/rice_set\\nX-MATE-Autostart-enabled=true\\n' > %s/home/%s/.config/autostart/rice_set.desktop", TARGETDIR, user_login);

        // Copy themes if exists
        run_sync(app, "mkdir -p %s/home/%s/.themes", TARGETDIR, user_login);
        run_sync(app, "cp -rf /home/anon/.themes/* %s/home/%s/.themes/ 2>/dev/null || true", TARGETDIR, user_login);

        // Copy flatpak if exists
        run_sync(app, "cp -rf /var/lib/flatpak %s/var/lib/ 2>/dev/null || true", TARGETDIR);

#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: copy XBPS repository configuration --- */
        void_copy_xbpsd_config(app, TARGETDIR);
#endif

        // Fix ownership of user home directory
        run_sync(app, "chroot %s chown -R %s:%s /home/%s 2>/dev/null || true", TARGETDIR, user_login, user_login, user_login);

        // Autologin
        char emptty_conf_path[512];
        snprintf(emptty_conf_path, sizeof(emptty_conf_path), "%s/etc/emptty/conf", TARGETDIR);

        if (autologin) {
            // Check if SDDM is present on the target
            char sddm_path[512];
            char lightdm_conf_path[512];
            snprintf(sddm_path, sizeof(sddm_path), "%s/usr/bin/sddm", TARGETDIR);
            snprintf(lightdm_conf_path, sizeof(lightdm_conf_path), "%s/etc/lightdm/lightdm.conf", TARGETDIR);
            if (access(sddm_path, F_OK) == 0) {
                log_to_ui(app, "Configuring SDDM autologin...", 0.87);
                // Create sddm.conf.d directory if it doesn't exist
                run_sync(app, "mkdir -p %s/etc/sddm.conf.d", TARGETDIR);
                // Write SDDM autologin configuration
                run_sync(app, "printf '[Autologin]\\nUser=%s\\n' > %s/etc/sddm.conf.d/autologin.conf", user_login, TARGETDIR);
            } else if (access(lightdm_conf_path, F_OK) == 0) {
                log_to_ui(app, "Configuring LightDM autologin...", 0.87);
                run_sync(app, "sed -i 's/^autologin-user=.*/autologin-user=%s/' %s/etc/lightdm/lightdm.conf", user_login, TARGETDIR);
                run_sync(app, "grep -q '^autologin-user=' %s/etc/lightdm/lightdm.conf || sed -i '/^\\[Seat:\\*\\]/a autologin-user=%s' %s/etc/lightdm/lightdm.conf", TARGETDIR, user_login, TARGETDIR);
            }

            // emptty (independent - can coexist with graphical DMs)
            // Add user to nopasswdlogin group (required by emptty/PAM for password bypass)
            run_sync(app, "chroot %s getent group nopasswdlogin > /dev/null 2>/dev/null || chroot %s groupadd -r nopasswdlogin", TARGETDIR, TARGETDIR);
            run_sync(app, "chroot %s usermod -aG nopasswdlogin %s", TARGETDIR, user_login);
            if (access(emptty_conf_path, F_OK) == 0) {
                log_to_ui(app, "Configuring emptty autologin...", 0.87);
                run_sync(app, "sed -i 's/^#\\?DEFAULT_USER=.*/DEFAULT_USER=%s/' %s/etc/emptty/conf", user_login, TARGETDIR);
                run_sync(app, "sed -i 's/^#\\?AUTOLOGIN=.*/AUTOLOGIN=true/' %s/etc/emptty/conf", TARGETDIR);
            }
        } else {
            // Ensure autologin is explicitly disabled if the user unchecked the box
            // SDDM
            run_sync(app, "rm -f %s/etc/sddm.conf.d/autologin.conf", TARGETDIR);
            // LightDM
            run_sync(app, "sed -i '/^autologin-user=/d' %s/etc/lightdm/lightdm.conf 2>/dev/null || true", TARGETDIR);
            // emptty: disable autologin in config AND ensure user is NOT in nopasswdlogin group
            if (access(emptty_conf_path, F_OK) == 0) {
                run_sync(app, "sed -i 's/^#\\?DEFAULT_USER=.*/#DEFAULT_USER=/' %s/etc/emptty/conf", TARGETDIR);
                run_sync(app, "sed -i 's/^#\\?AUTOLOGIN=.*/AUTOLOGIN=false/' %s/etc/emptty/conf", TARGETDIR);
            }
            // Remove user from nopasswdlogin group and delete the group entirely.
            // This prevents emptty/PAM from bypassing the password prompt.
            run_sync(app, "chroot %s gpasswd -d %s nopasswdlogin 2>/dev/null || true", TARGETDIR, user_login);
            run_sync(app, "chroot %s groupdel nopasswdlogin 2>/dev/null || true", TARGETDIR);
        }

        // Privilege Manager: doas or sudo
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->radio_priv_doas))) {
            log_to_ui(app, "Configuring doas (lightweight privilege manager)...", 0.88);

            // 1. Prevent sudo from being pulled as a dependency
            run_sync(app, "mkdir -p %s/etc/xbps.d", TARGETDIR);
            run_sync(app, "if ! grep -q 'ignorepkg=sudo' %s/etc/xbps.d/10-ignore.conf 2>/dev/null; then "
                           "echo 'ignorepkg=sudo' >> %s/etc/xbps.d/10-ignore.conf; fi",
                     TARGETDIR, TARGETDIR);

            // 2. Install opendoas
            run_sync(app, "chroot %s xbps-install -Sy --repository=https://repo-de.voidlinux.org/current/ opendoas", TARGETDIR);

            // 3. Configure doas.conf
            run_sync(app, "printf '# doas configuration\npermit persist keepenv :wheel\n' > %s/etc/doas.conf", TARGETDIR);
            run_sync(app, "chmod 0400 %s/etc/doas.conf", TARGETDIR);

            // 4. Remove sudo (will be ignored by xbps from now on)
            run_sync(app, "chroot %s xbps-remove -Ry sudo", TARGETDIR);

            // Still write sudoers as fallback
            run_sync(app, "mkdir -p %s/etc/sudoers.d", TARGETDIR);
            run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > %s/etc/sudoers.d/wheel", TARGETDIR);
            run_sync(app, "chmod 0440 %s/etc/sudoers.d/wheel", TARGETDIR);
        } else {
            // Traditional sudo
            log_to_ui(app, "Configuring sudo...", 0.88);
            run_sync(app, "chroot %s xbps-install -Sy --repository=https://repo-de.voidlinux.org/current/ sudo", TARGETDIR);
            run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > %s/etc/sudoers.d/wheel", TARGETDIR);
            run_sync(app, "chmod 0440 %s/etc/sudoers.d/wheel", TARGETDIR);
        }
    }

    generate_fstab(app, TARGETDIR);
    generate_crypttab(app, TARGETDIR);
    return 0;
}

int step_install_bootloader(AppData *app, const char *TARGETDIR, const char *disk_name) {
    log_to_ui(app, "Installing GRUB Bootloader...", 0.9);
    char disk_path[64];
    snprintf(disk_path, sizeof(disk_path), "/dev/%s", disk_name);

    // CONFIGURE GRUB FOR LUKS
    gboolean has_crypto = FALSE;
    GSList *chk = app->part_config_list;
    while(chk) {
      if(((PartitionConfig*)chk->data)->encrypt) has_crypto = TRUE;
      chk = chk->next;
    }

if (has_crypto) {
        log_to_ui(app, "Configuring GRUB for LUKS...", 0.91);

        GSList *f = app->part_config_list;
        while(f) {
            PartitionConfig *c = (PartitionConfig*)f->data;
            if (c->encrypt && strcmp(c->mountpoint, "/") == 0 && c->luks_uuid) {
                log_to_ui_printf(app, "Setting GRUB LUKS UUID: %s", c->luks_uuid);

                // Enable cryptodisk in GRUB
                run_sync(app, "sed -i 's/GRUB_ENABLE_CRYPTODISK=.*//' %s/etc/default/grub", TARGETDIR);
                run_sync(app, "echo 'GRUB_ENABLE_CRYPTODISK=y' >> %s/etc/default/grub", TARGETDIR);

                // Set rd.luks.name for boot
                run_sync(app, "sed -i 's|GRUB_CMDLINE_LINUX_DEFAULT=\"|GRUB_CMDLINE_LINUX_DEFAULT=\"rd.luks.name=%s=cryptroot |' %s/etc/default/grub", c->luks_uuid, TARGETDIR);
            }
            f = f->next;
        }
    }

    if (app->is_efi) {
#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: install grub-*-efi via xbps --- */
        void_install_grub_efi_pkg(app, TARGETDIR);
#else
        log_to_ui(app, "[Universal] grub EFI package must be pre-installed in the base image.", 0.91);
#endif
        if (run_sync(app, "chroot %s grub-install --target=%s --efi-directory=/boot/efi --bootloader-id=BOOT --recheck --removable", TARGETDIR, app->efi_target) != 0) {
            log_to_ui(app, "ERROR: grub-install (EFI) failed! Check that the EFI partition is mounted at /boot/efi.", 0.0);
            return -1;
        }
    } else {
#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: install grub-i386-pc for MBR/Legacy BIOS --- */
        void_install_grub_bios_pkg(app, TARGETDIR);
#else
        log_to_ui(app, "[Universal] grub-i386-pc package must be pre-installed in the base image.", 0.91);
#endif
        /* Ensure /dev is fully accessible inside chroot for grub-install to probe the disk */
        run_sync(app, "mount --bind /dev %s/dev 2>/dev/null || true", TARGETDIR);

        if (run_sync(app, "chroot %s grub-install --target=i386-pc --recheck --force %s", TARGETDIR, disk_path) != 0) {
            log_to_ui(app, "ERROR: grub-install (BIOS/MBR) failed! The disk may be busy or GRUB i386-pc modules are missing.", 0.0);
            return -1;
        }
    }

    run_sync(app, "mkdir -p %s/boot/grub", TARGETDIR);

    // DUAL BOOT: Install and enable os-prober to detect other operating systems
    if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: install os-prober and ntfs-3g via xbps --- */
        void_install_osprober(app, TARGETDIR);
#else
        log_to_ui(app, "[Universal] os-prober/ntfs-3g must be pre-installed in the base image.", 0.92);
#endif

        // Ensure GRUB_DISABLE_OS_PROBER is not set to true
        run_sync(app, "sed -i '/GRUB_DISABLE_OS_PROBER/d' %s/etc/default/grub", TARGETDIR);
        run_sync(app, "echo 'GRUB_DISABLE_OS_PROBER=false' >> %s/etc/default/grub", TARGETDIR);

        // Mount other partitions so os-prober can detect them
        log_to_ui(app, "Scanning for other operating systems...", 0.925);
        run_sync(app, "mkdir -p /tmp/kasha_osprobe");

        // Scan all partitions on the system for other OS
        char probe_cmd[512];
        snprintf(probe_cmd, sizeof(probe_cmd),
            "lsblk -rn -o NAME,FSTYPE /dev/%s 2>/dev/null | while read name fstype; do "
            "  case \"$fstype\" in "
            "    ntfs|ext4|ext3|btrfs|xfs) "
            "      dev=\"/dev/$name\"; "
            "      mp=\"/tmp/kasha_osprobe/$name\"; "
            "      mkdir -p \"$mp\"; "
            "      mount -o ro \"$dev\" \"$mp\" 2>/dev/null || true; "
            "    ;; "
            "  esac; "
            "done", disk_name);
        system(probe_cmd);
    }

    run_sync(app, "chroot %s grub-mkconfig -o /boot/grub/grub.cfg", TARGETDIR);

    // Cleanup os-prober mounts
    if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
        run_sync(app, "umount -R /tmp/kasha_osprobe 2>/dev/null || true");
        run_sync(app, "rm -rf /tmp/kasha_osprobe 2>/dev/null || true");
    }


    if (has_crypto) {
        log_to_ui(app, "Configuring dracut for LUKS...", 0.93);
        run_sync(app, "mkdir -p %s/etc/dracut.conf.d", TARGETDIR);
        run_sync(app, "echo 'hostonly=yes' > %s/etc/dracut.conf.d/10-crypt.conf", TARGETDIR);
        run_sync(app, "echo 'add_dracutmodules+=\" crypt \"' >> %s/etc/dracut.conf.d/10-crypt.conf", TARGETDIR);

        log_to_ui(app, "Regenerating initramfs with LUKS support...", 0.935);
#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: install base-system-dracut and reconfigure with xbps --- */
        void_install_dracut_luks(app, TARGETDIR);
#else
        /* --- Universal: regenerate initramfs directly with dracut --- */
        run_sync(app, "chroot %s dracut --force 2>/dev/null || true", TARGETDIR);
#endif
    }
    return 0;
}

int step_finalize(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "Syncing...", 0.95);
    system("sync");

    // Disable swap partitions before unmounting
    GSList *sw = app->part_config_list;
    while(sw) {
        PartitionConfig *pc = (PartitionConfig*)sw->data;
        if (strcmp(pc->fstype, "swap") == 0) {
            run_sync(app, "swapoff %s 2>/dev/null", pc->device);
        }
        sw = sw->next;
    }

    run_sync(app, "umount -R %s", TARGETDIR);

    // CLOSE LUKS DEVICES
    gboolean has_crypto = FALSE;
    GSList *chk = app->part_config_list;
    while(chk) {
      if(((PartitionConfig*)chk->data)->encrypt) has_crypto = TRUE;
      chk = chk->next;
    }

    if (has_crypto) {
        log_to_ui(app, "Closing encrypted devices...", 0.98);
        run_sync(app, "cryptsetup close cryptroot 2>/dev/null || true");
    }
    return 0;
}
