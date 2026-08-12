/*
 * easy partition flowwwwww
 */
#include "neko_installer.h"
#include <string.h>
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
