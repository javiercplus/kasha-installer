/*
 * easy partition flowwwwww
 */
#include "neko_installer.h"
#include <string.h>
#include <unistd.h>

static gboolean is_new_placeholder(const char *dev) {
    return dev && (strcmp(dev, "/dev/NEW") == 0 || strcmp(dev, "/dev/NEW_EFI") == 0);
}

static gboolean partition_needs_creation(const PartitionConfig *cfg) {
    if (!cfg || !cfg->device) return TRUE;
    if (is_new_placeholder(cfg->device)) return TRUE;
    return (access(cfg->device, F_OK) != 0);
}

static gboolean has_missing_or_new_partitions(AppData *app) {
    for (GSList *l = app->part_config_list; l; l = l->next) {
        PartitionConfig *cfg = (PartitionConfig *)l->data;
        if (partition_needs_creation(cfg)) {
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean has_preserved_existing_partition(AppData *app) {
    for (GSList *l = app->part_config_list; l; l = l->next) {
        PartitionConfig *cfg = (PartitionConfig *)l->data;
        if (cfg && !partition_needs_creation(cfg) && !cfg->format) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Priority order for creating partitions in Clean Install (ERASE):
 * 0 = /boot/efi, 1 = /boot, 2 = swap, 3 = /, 4 = other (/home, /var, ...)
 * The very last partition in this order receives the remaining free space. */
static int erase_part_order(const PartitionConfig *cfg) {
    if (!cfg) return 4;
    if (cfg->mountpoint && strcmp(cfg->mountpoint, "/boot/efi") == 0) return 0;
    if (cfg->mountpoint && strcmp(cfg->mountpoint, "/boot") == 0) return 1;
    if (cfg->fstype && strcmp(cfg->fstype, "swap") == 0) return 2;
    if (cfg->mountpoint && strcmp(cfg->mountpoint, "/") == 0) return 3;
    return 4;
}

static gint compare_erase_order(gconstpointer a, gconstpointer b) {
    return erase_part_order((const PartitionConfig *)a) -
           erase_part_order((const PartitionConfig *)b);
}

/* Check if partition number `part_num` currently exists on `disk_name`. */
static gboolean partition_slot_exists(const char *disk_name, const char *sep, int part_num) {
    char check_cmd[160];
    snprintf(check_cmd, sizeof(check_cmd),
             "lsblk -rn -o NAME /dev/%s 2>/dev/null | grep -q '^%s%s%d$'",
             disk_name, disk_name, sep, part_num);
    return (system(check_cmd) == 0);
}

int step_partitioning(AppData *app, const char *disk_name) {
    if (app->install_mode == INSTALL_MODE_MANUAL) {
        if (has_missing_or_new_partitions(app)) {
            /* One or more configured partitions do not exist on disk (either
             * /dev/NEW* placeholders or explicit paths like /dev/sdc2 that have
             * not been created yet). Adapt the target disk's partition table so
             * those partitions are actually created before formatting! */
            gboolean alongside_selected = (app->radio_alongside &&
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->radio_alongside)));

            if (alongside_selected || has_preserved_existing_partition(app)) {
                app->install_mode = INSTALL_MODE_DUAL_BOOT;
                log_to_ui(app, "Missing partition(s) detected — adapting partition table (preserving existing data)...", 0.14);
            } else {
                app->install_mode = INSTALL_MODE_ERASE;
                log_to_ui(app, "Missing partition(s) detected — creating partition table on selected disk...", 0.14);
            }
        } else {
            return 0; // Genuine manual mode: all configured partitions already exist
        }
    }

    log_to_ui(app, "Partitioning Disk...", 0.15);

    char disk_dev[64];
    snprintf(disk_dev, sizeof(disk_dev), "/dev/%s", disk_name);

    // ===== PRE-PARTITION CLEANUP =====
    // Clean up any leftover os-prober / EFI-test mounts from previous runs
    log_to_ui(app, "Cleaning up leftover probe mounts...", 0.11);
    run_sync(app, "umount /tmp/kasha_efi_test 2>/dev/null || true");
    run_sync(app, "rmdir /tmp/kasha_efi_test 2>/dev/null || true");
    run_sync(app, "umount -R /tmp/kasha_osprobe 2>/dev/null || true");
    run_sync(app, "rm -rf /tmp/kasha_osprobe 2>/dev/null || true");

    // Deactivate swap on all partitions of this disk
    log_to_ui(app, "Deactivating swap partitions on target disk...", 0.12);
    run_sync(app, "for p in /dev/%s*; do swapoff \"$p\" 2>/dev/null; done || true", disk_name);

    // Unmount all partitions currently mounted from this disk
    // Kill processes holding partitions open, then force-unmount (NO lazy flag —
    // lazy hides the mountpoint but the kernel keeps the block device busy)
    log_to_ui(app, "Unmounting any existing partitions on target disk...", 0.13);
    run_sync(app, "for p in $(lsblk -rn -o NAME /dev/%s 2>/dev/null | grep -v '^%s$'); do fuser -km /dev/$p 2>/dev/null; done || true", disk_name, disk_name);
    run_sync(app, "for p in $(lsblk -rn -o NAME /dev/%s 2>/dev/null | grep -v '^%s$'); do umount -f /dev/$p 2>/dev/null; done || true", disk_name, disk_name);

    // Close any LUKS devices backed by partitions on this disk
    run_sync(app, "for p in $(lsblk -rn -o NAME /dev/%s 2>/dev/null | grep -v '^%s$'); do "
    "cryptsetup close /dev/mapper/$(lsblk -rn -o NAME /dev/$p 2>/dev/null | head -1) 2>/dev/null; "
    "done || true", disk_name, disk_name);

    /* If DUAL_BOOT was chosen on a completely blank disk (no partition table),
     * sfdisk -a would fail because there is no table to append to — create a
     * fresh partition table with ERASE instead. */
    if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
        char *pt_check = get_disk_partition_table_type(disk_name);
        if (!pt_check || strcmp(pt_check, "unknown") == 0) {
            log_to_ui(app, "No existing partition table found on disk — creating a new partition table...", 0.145);
            app->install_mode = INSTALL_MODE_ERASE;
        }
        g_free(pt_check);
    }

    if (app->install_mode == INSTALL_MODE_ERASE) {
        // ===== CLEAN INSTALL: ERASE / CREATE NEW TABLE =====

        // Wipe old filesystem signatures from the entire disk
        log_to_ui(app, "Wiping old signatures from disk...", 0.14);
        run_sync(app, "wipefs -af %s 2>/dev/null || true", disk_dev);

        /* Build an ordered list of all configured partitions so sfdisk creates
         * the exact layout configured by the user (e.g. EFI + root, or EFI +
         * swap + root, or EFI + root + /home). */
        GSList *ordered = g_slist_copy(app->part_config_list);
        ordered = g_slist_sort(ordered, compare_erase_order);

        glong disk_mb = get_partition_size_mb(disk_dev);
        if (disk_mb < 0) disk_mb = 0;

        guint total_parts = g_slist_length(ordered);
        guint extra_data_parts = 0;
        for (GSList *o = ordered; o; o = o->next) {
            PartitionConfig *c = (PartitionConfig *)o->data;
            if (erase_part_order(c) == 4) extra_data_parts++;
        }

        char cmd_buf[256];
        snprintf(cmd_buf, sizeof(cmd_buf), "sfdisk --wipe always %s", disk_dev);
        log_to_ui_printf(app, "Running: %s", cmd_buf);
        FILE *sf = popen(cmd_buf, "w");
        if (sf) {
            if (app->is_efi) fprintf(sf, "label: gpt\n");
            else fprintf(sf, "label: dos\n");

            guint idx = 0;
            for (GSList *o = ordered; o; o = o->next) {
                PartitionConfig *cfg = (PartitionConfig *)o->data;
                gboolean is_last = (idx == total_parts - 1);
                int kind = erase_part_order(cfg);

                if (kind == 0) {
                    /* EFI System Partition */
                    if (is_last) fprintf(sf, ",,U\n");
                    else fprintf(sf, ",512M,U\n");
                } else if (kind == 1) {
                    /* Separate /boot */
                    if (is_last) fprintf(sf, ",,L\n");
                    else fprintf(sf, ",1024M,L\n");
                } else if (kind == 2) {
                    /* Swap partition */
                    glong swap_mb = 4096;
                    if (disk_mb > 0 && swap_mb > disk_mb / 8) swap_mb = disk_mb / 8;
                    if (swap_mb < 512) swap_mb = 512;
                    if (is_last) fprintf(sf, ",,S\n");
                    else fprintf(sf, ",%ldM,S\n", swap_mb);
                } else if (kind == 3) {
                    /* Root (/) */
                    const char *boot_flag = (!app->is_efi) ? ",*" : "";
                    if (is_last) {
                        fprintf(sf, ",,L%s\n", boot_flag);
                    } else {
                        /* Leave remaining space for /home or extra partitions */
                        glong root_mb = (disk_mb > 0) ? (disk_mb * 2) / 5 : 20480;
                        if (root_mb < 10240) root_mb = 10240;
                        if (disk_mb > 0 && root_mb > disk_mb - 4096) root_mb = disk_mb / 2;
                        fprintf(sf, ",%ldM,L%s\n", root_mb, boot_flag);
                    }
                } else {
                    /* Extra partition (/home, /var, etc.) */
                    if (is_last) {
                        fprintf(sf, ",,L\n");
                    } else {
                        glong share_mb = (disk_mb > 0 && extra_data_parts > 0)
                            ? (disk_mb / 2) / (glong)extra_data_parts
                            : 10240;
                        if (share_mb < 2048) share_mb = 2048;
                        fprintf(sf, ",%ldM,L\n", share_mb);
                    }
                }
                idx++;
            }

            int sfdisk_ret = pclose(sf);
            if (sfdisk_ret != 0) {
                g_slist_free(ordered);
                log_to_ui_printf(app, "ERROR: sfdisk failed (exit code %d)! "
                "Disk may be busy or have I/O errors.", WEXITSTATUS(sfdisk_ret));
                return -1;
            }

            sleep(2);
            run_sync(app, "blockdev --rereadpt %s 2>/dev/null || true", disk_dev);
            run_sync(app, "udevadm settle --timeout=10 2>/dev/null || sleep 2");
            sleep(1);
        } else {
            g_slist_free(ordered);
            log_to_ui(app, "ERROR: Could not execute sfdisk!", 0.0);
            return -1;
        }

        // Assign real device paths matching the exact creation order
        const char *sep = "";
        int len = strlen(disk_name);
        if (g_ascii_isdigit(disk_name[len-1])) sep = "p";

        int part_num = 1;
        for (GSList *o = ordered; o; o = o->next) {
            PartitionConfig *cfg = (PartitionConfig *)o->data;
            g_free(cfg->device);
            g_free(cfg->original_device);
            cfg->device = g_strdup_printf("/dev/%s%s%d", disk_name, sep, part_num);
            cfg->original_device = g_strdup(cfg->device);
            part_num++;
        }
        g_slist_free(ordered);

        // Verify partition device nodes were actually created
        GSList *l = app->part_config_list;
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
        // ===== DUAL BOOT: Resize existing partition if needed, then create missing/new partitions =====
        /* Collect every entry in part_config_list that does not exist on disk yet */
        GSList *to_create = NULL;
        gboolean has_existing_efi = (app->detected_efi_partition != NULL);
        gboolean need_new_efi = FALSE;
        gboolean need_new_swap = FALSE;
        gboolean need_new_root = FALSE;
        gboolean need_new_boot = FALSE;

        for (GSList *chk = app->part_config_list; chk; chk = chk->next) {
            PartitionConfig *cfg = (PartitionConfig *)chk->data;
            if (cfg->mountpoint && strcmp(cfg->mountpoint, "/boot/efi") == 0) {
                if (partition_needs_creation(cfg)) {
                    need_new_efi = TRUE;
                    has_existing_efi = FALSE;
                    to_create = g_slist_append(to_create, cfg);
                } else {
                    has_existing_efi = TRUE;
                }
            } else if (partition_needs_creation(cfg)) {
                if (cfg->fstype && strcmp(cfg->fstype, "swap") == 0) need_new_swap = TRUE;
                if (cfg->mountpoint && strcmp(cfg->mountpoint, "/") == 0) need_new_root = TRUE;
                if (cfg->mountpoint && strcmp(cfg->mountpoint, "/boot") == 0) need_new_boot = TRUE;
                to_create = g_slist_append(to_create, cfg);
            }
        }
        to_create = g_slist_sort(to_create, compare_erase_order);
        guint create_count = g_slist_length(to_create);

        if (has_existing_efi && app->detected_efi_partition) {
            log_to_ui_printf(app, "Reusing existing EFI partition: %s", app->detected_efi_partition);
        }

        if (create_count == 0) {
            g_slist_free(to_create);
            log_to_ui(app, "All configured partitions already exist on disk.", 0.18);
            return 0;
        }

        // --- MBR validation: check partition table type and slot availability ---
        char *pt_type = get_disk_partition_table_type(disk_name);
        gboolean is_mbr = (pt_type && strcmp(pt_type, "dos") == 0);
        log_to_ui_printf(app, "Partition table type: %s", pt_type ? pt_type : "unknown");

        gboolean use_logical = FALSE;

        if (is_mbr) {
            int primary_count = get_mbr_primary_count(disk_name);
            int slots_needed = (int)create_count;

            // Check if an extended partition already exists (types 5, f or 85)
            char check_ext_cmd[256];
            snprintf(check_ext_cmd, sizeof(check_ext_cmd),
                     "sfdisk -d /dev/%s 2>/dev/null | grep -qE 'type=5|type=f|type=85'", disk_name);
            gboolean has_extended = (system(check_ext_cmd) == 0);

            log_to_ui_printf(app, "MBR: %d primary partitions found, extended exists: %s",
                             primary_count, has_extended ? "YES" : "NO");

            if (primary_count + slots_needed > 4) {
                if (!has_extended && primary_count >= 4) {
                    log_to_ui_printf(app,
                                     "ERROR: MBR disk already has 4 primary partitions and NO extended partition. "
                                     "Cannot add %d more partitions safely. Please use GParted to delete or "
                                     "convert a primary partition to 'Extended' before dual-booting.", slots_needed);
                    g_free(pt_type);
                    g_slist_free(to_create);
                    return -1;
                }
                log_to_ui(app, "MBR limit reached: Will create Logical partition(s) inside Extended.", 0.155);
                use_logical = TRUE;
            }
        }
        g_free(pt_type);

        // Step 1: Check if there's already enough free space
        glong free_space_mb = get_disk_free_space_mb(disk_name);
        glong min_needed_mb = need_new_root ? (7 * 1024) : 1024; // 7GB minimum for Neko-Void root
        if (need_new_efi) min_needed_mb += 512;
        if (need_new_swap) min_needed_mb += 2048;
        if (need_new_boot) min_needed_mb += 1024;

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
                g_slist_free(to_create);
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
                        g_slist_free(to_create);
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
                        g_slist_free(to_create);
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
                g_slist_free(to_create);
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
                g_slist_free(to_create);
                return -1;
            }
            g_free(resize_target);
            log_to_ui(app, "Partition resize completed successfully.", 0.17);
        } else {
            log_to_ui(app, "Sufficient free space found, skipping resize.", 0.16);
        }

        // Step 2: Determine the free partition slot numbers BEFORE running sfdisk -a
        const char *sep = "";
        int len = strlen(disk_name);
        if (g_ascii_isdigit(disk_name[len-1])) sep = "p";

        int *assigned_slots = g_new0(int, create_count);
        int next_slot = (use_logical && is_mbr) ? 5 : 1;
        for (guint i = 0; i < create_count; i++) {
            while (partition_slot_exists(disk_name, sep, next_slot)) {
                next_slot++;
            }
            assigned_slots[i] = next_slot++;
        }

        // Step 3: Create new partition(s) in the free space (now guaranteed to exist)
        glong post_resize_free_mb = get_disk_free_space_mb(disk_name);
        char cmd_buf2[256];
        snprintf(cmd_buf2, sizeof(cmd_buf2), "sfdisk -a %s", disk_dev);
        log_to_ui_printf(app, "Running: %s", cmd_buf2);
        FILE *sf = popen(cmd_buf2, "w");
        if (sf) {
            guint idx = 0;
            for (GSList *o = to_create; o; o = o->next) {
                PartitionConfig *cfg = (PartitionConfig *)o->data;
                gboolean is_last = (idx == create_count - 1);
                int kind = erase_part_order(cfg);

                if (kind == 0) {
                    log_to_ui(app, "Creating new EFI partition...", 0.18);
                    if (use_logical && is_mbr) {
                        fprintf(sf, ",512M,L\n");
                    } else {
                        fprintf(sf, ",512M,U\n");
                    }
                } else if (kind == 1) {
                    if (is_last) fprintf(sf, ",,L\n");
                    else fprintf(sf, ",1024M,L\n");
                } else if (kind == 2) {
                    glong swap_mb = 2048;
                    if (post_resize_free_mb > 0 && swap_mb > post_resize_free_mb / 6) {
                        swap_mb = post_resize_free_mb / 6;
                        if (swap_mb < 512) swap_mb = 512;
                    }
                    if (is_last) fprintf(sf, ",,S\n");
                    else fprintf(sf, ",%ldM,S\n", swap_mb);
                } else {
                    const char *ptype = (use_logical && is_mbr) ? "L" : (is_mbr ? "83" : "L");
                    if (is_last) {
                        fprintf(sf, ",,%s\n", ptype);
                    } else {
                        glong part_mb = (post_resize_free_mb > 0) ? (post_resize_free_mb / 2) : 16384;
                        if (part_mb < 2048) part_mb = 2048;
                        fprintf(sf, ",%ldM,%s\n", part_mb, ptype);
                    }
                }
                idx++;
            }

            int sfdisk_ret = pclose(sf);
            if (sfdisk_ret != 0) {
                g_free(assigned_slots);
                g_slist_free(to_create);
                log_to_ui_printf(app, "ERROR: sfdisk failed (exit code %d)! "
                "Disk may be busy or have I/O errors.", WEXITSTATUS(sfdisk_ret));
                return -1;
            }

            sleep(2);
            run_sync(app, "blockdev --rereadpt %s 2>/dev/null || true", disk_dev);
            run_sync(app, "udevadm settle --timeout=10 2>/dev/null || sleep 2");
            sleep(1);
        } else {
            g_free(assigned_slots);
            g_slist_free(to_create);
            log_to_ui(app, "ERROR: Could not execute sfdisk!", 0.0);
            return -1;
        }

        // Step 4: Assign newly created device paths to the corresponding configs
        int root_part_num = -1;
        {
            guint idx = 0;
            for (GSList *o = to_create; o; o = o->next) {
                PartitionConfig *cfg = (PartitionConfig *)o->data;
                g_free(cfg->device);
                g_free(cfg->original_device);
                cfg->device = g_strdup_printf("/dev/%s%s%d", disk_name, sep, assigned_slots[idx]);
                cfg->original_device = g_strdup(cfg->device);
                if (cfg->mountpoint && strcmp(cfg->mountpoint, "/") == 0) {
                    root_part_num = assigned_slots[idx];
                }
                idx++;
            }
        }
        g_free(assigned_slots);
        g_slist_free(to_create);

        // Step 5: Set boot flag on root partition for MBR/Legacy
        if (!app->is_efi && root_part_num > 0) {
            log_to_ui_printf(app, "Setting boot flag on partition %d (MBR/Legacy)...", root_part_num);
            run_sync(app, "sfdisk --activate %s %d 2>/dev/null || true", disk_dev, root_part_num);
        }

        // Verify partition device nodes were actually created
        GSList *l = app->part_config_list;
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
