/*
 * partition_utils.c
 * Utilities for partition management (non-UI)
 */
#include "neko_installer.h"
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

// Helper to check for NTFS partition on the selected disk
char* find_ntfs_partition(const char *disk_name) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "lsblk -rn -o NAME,FSTYPE /dev/%s | grep ntfs | head -n1 | awk '{print $1}'", disk_name);
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    
    char part_name[128];
    if (fgets(part_name, sizeof(part_name), fp)) {
        part_name[strcspn(part_name, "\n")] = 0;
        pclose(fp);
        if (strlen(part_name) > 0) {
            return g_strdup_printf("/dev/%s", part_name);
        }
    } else {
        pclose(fp);
    }
    return NULL;
}

// Helper to find existing EFI System Partition on the selected disk
char* find_efi_partition(const char *disk_name) {
    char cmd[256];
    // Use sfdisk to find partitions with EFI type (C12A7328-F81F-11D2-BA4B-00A0C93EC93B for GPT, or type=ef for MBR)
    snprintf(cmd, sizeof(cmd), 
        "lsblk -rn -o NAME,PARTTYPE /dev/%s 2>/dev/null | grep -i 'c12a7328\\|0xef' | head -n1 | awk '{print $1}'",
        disk_name);
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    
    char part_name[128];
    if (fgets(part_name, sizeof(part_name), fp)) {
        part_name[strcspn(part_name, "\n")] = 0;
        pclose(fp);
        if (strlen(part_name) > 0) {
            return g_strdup_printf("/dev/%s", part_name);
        }
    } else {
        pclose(fp);
    }
    return NULL;
}

// Validate that an EFI partition is usable (can be mounted, has FAT filesystem)
gboolean validate_efi_partition(AppData *app, const char *efi_device) {
    if (!efi_device) return FALSE;
    
    // Check filesystem type is vfat
    char fstype[64];
    get_partition_fstype(efi_device, fstype, sizeof(fstype));
    
    if (strlen(fstype) == 0 || strcmp(fstype, "vfat") != 0) {
        if (app) log_to_ui_printf(app, "WARNING: EFI partition %s has fstype '%s', not vfat", efi_device, fstype);
        return FALSE;
    }
    
    // Try to mount it temporarily to verify it works
    const char *test_mount = "/tmp/kasha_efi_test";
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s && mount -t vfat %s %s 2>/dev/null", test_mount, efi_device, test_mount);
    int ret = system(cmd);
    
    if (ret != 0) {
        if (app) log_to_ui_printf(app, "WARNING: Could not mount EFI partition %s for validation", efi_device);
        return FALSE;
    }
    
    // Unmount
    snprintf(cmd, sizeof(cmd), "umount %s 2>/dev/null && rmdir %s 2>/dev/null", test_mount, test_mount);
    system(cmd);
    
    return TRUE;
}

// Helper to generate partition name
char* get_partition_path(const char *disk, int part_num) {
    if (g_str_has_suffix(disk, "0") || g_str_has_suffix(disk, "1") || 
        g_str_has_suffix(disk, "2") || g_str_has_suffix(disk, "3") ||
        g_str_has_suffix(disk, "4") || g_str_has_suffix(disk, "5") ||
        g_str_has_suffix(disk, "6") || g_str_has_suffix(disk, "7") ||
        g_str_has_suffix(disk, "8") || g_str_has_suffix(disk, "9")) {
        // likely nvme0n1 or mmcblk0
        return g_strdup_printf("/dev/%sp%d", disk, part_num);
    } else {
        // likely sda, vda
        return g_strdup_printf("/dev/%s%d", disk, part_num);
    }
}

void populate_defaults(AppData *app, const char *disk_name) {
    /* Save the intended mode BEFORE reset (on_reset_partitions_clicked
       clobbers install_mode to INSTALL_MODE_MANUAL) */
    InstallMode intended_mode = app->install_mode;

    on_reset_partitions_clicked(NULL, app);
    
    char *p1 = get_partition_path(disk_name, 1);
    char *p2 = get_partition_path(disk_name, 2);
    
    if (intended_mode == INSTALL_MODE_DUAL_BOOT) {
        // Dual boot: detect and reuse existing EFI partition
        if (app->detected_efi_partition) {
            g_free(app->detected_efi_partition);
            app->detected_efi_partition = NULL;
        }
        
        if (app->is_efi) {
            // Look for existing EFI partition on the disk
            char *existing_efi = find_efi_partition(disk_name);
            
            if (existing_efi && validate_efi_partition(app, existing_efi)) {
                // Reuse existing EFI — do NOT format
                app->detected_efi_partition = g_strdup(existing_efi);
                add_partition_config(app, existing_efi, "vfat", "/boot/efi", FALSE, FALSE, NULL);
                g_free(existing_efi);
            } else {
                // No valid EFI found — will need to create one
                if (existing_efi) g_free(existing_efi);
                add_partition_config(app, "/dev/NEW_EFI", "vfat", "/boot/efi", TRUE, FALSE, NULL);
            }
        }
        
        // Root partition in free space (device resolved at install time)
        add_partition_config(app, "/dev/NEW", "ext4", "/", TRUE, FALSE, NULL);
        app->install_mode = INSTALL_MODE_DUAL_BOOT;
    } else {
        // Clean install (erase)
        if (app->is_efi) {
            add_partition_config(app, p1, "vfat", "/boot/efi", TRUE, FALSE, NULL);
            add_partition_config(app, p2, "ext4", "/", TRUE, FALSE, NULL);
        } else {
            add_partition_config(app, p1, "ext4", "/", TRUE, FALSE, NULL);
        }
        app->install_mode = INSTALL_MODE_ERASE;
    }
    
    g_free(p1);
    g_free(p2);
}

void get_partition_fstype(const char *device_path, char *out_type, size_t max_len) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "lsblk -nno FSTYPE %s", device_path);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        if (fgets(out_type, max_len, fp) != NULL) {
            size_t len = strlen(out_type);
            if (len > 0 && out_type[len-1] == '\n') {
                out_type[len-1] = '\0';
            }
        } else {
            out_type[0] = '\0';
        }
        pclose(fp);
    } else {
        out_type[0] = '\0';
    }
}

// Helper to find config by device path (simple search)
PartitionConfig* find_config_by_device(AppData *app, const gchar *device) {
    GSList *l = app->part_config_list;
    while (l) {
        PartitionConfig *c = (PartitionConfig*)l->data;
        const char *d1 = c->device;
        const char *d2 = device;
        
        if (strncmp(d1, "/dev/", 5) == 0) d1 += 5;
        if (strncmp(d2, "/dev/", 5) == 0) d2 += 5;
        
        if (strcmp(d1, d2) == 0) return c;
        l = l->next;
    }
    return NULL;
}

// Helper to remove config from list
void remove_partition_config(AppData *app, PartitionConfig *conf) {
    app->part_config_list = g_slist_remove(app->part_config_list, conf);
    g_free(conf->device);
    g_free(conf->original_device);
    g_free(conf->luks_uuid);
    g_free(conf->fstype);
    g_free(conf->mountpoint);
    if(conf->luks_pass) g_free(conf->luks_pass);
    g_free(conf);
}

/* ------------------------------------------------------------------ *
 *  get_partition_size_mb                                               *
 *  Get the size of a partition/device in megabytes.                    *
 * ------------------------------------------------------------------ */
glong get_partition_size_mb(const char *device) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "blockdev --getsize64 %s 2>/dev/null", device);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char buf[64];
    glong result = -1;
    if (fgets(buf, sizeof(buf), fp)) {
        long long bytes = atoll(buf);
        result = (glong)(bytes / (1024LL * 1024LL));
    }
    pclose(fp);
    return result;
}

/* ------------------------------------------------------------------ *
 *  get_fs_min_size_mb                                                  *
 *  Get minimum possible size for a filesystem in MB.                  *
 *  Uses filesystem-specific tools to determine the minimum.           *
 * ------------------------------------------------------------------ */
glong get_fs_min_size_mb(const char *device) {
    char fstype[64];
    get_partition_fstype(device, fstype, sizeof(fstype));

    if (strcmp(fstype, "ext4") == 0 || strcmp(fstype, "ext3") == 0 ||
        strcmp(fstype, "ext2") == 0) {
        /* resize2fs -P gives minimum number of filesystem blocks */
        char cmd[256];
        snprintf(cmd, sizeof(cmd),
            "resize2fs -P %s 2>/dev/null | awk -F': ' '{print $2}'", device);
        FILE *fp = popen(cmd, "r");
        if (!fp) return -1;

        char buf[64];
        glong result = -1;
        if (fgets(buf, sizeof(buf), fp)) {
            long long min_blocks = atoll(buf);
            /* Get block size from dumpe2fs */
            char cmd2[256];
            snprintf(cmd2, sizeof(cmd2),
                "dumpe2fs -h %s 2>/dev/null | grep 'Block size' | awk '{print $NF}'",
                device);
            FILE *fp2 = popen(cmd2, "r");
            long block_size = 4096; /* default */
            if (fp2) {
                char buf2[32];
                if (fgets(buf2, sizeof(buf2), fp2)) {
                    block_size = atol(buf2);
                }
                pclose(fp2);
            }
            result = (glong)((min_blocks * block_size) / (1024LL * 1024LL));
        }
        pclose(fp);
        return result;
    }
    else if (strcmp(fstype, "ntfs") == 0) {
        /* ntfsresize --info gives "You might resize at <bytes> bytes" */
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "ntfsresize --info --force --no-progress-bar %s 2>/dev/null "
            "| grep -i 'resize at' | grep -oE '[0-9]+' | tail -n1",
            device);
        FILE *fp = popen(cmd, "r");
        if (!fp) return -1;

        char buf[64];
        glong result = -1;
        if (fgets(buf, sizeof(buf), fp)) {
            long long bytes = atoll(buf);
            result = (glong)(bytes / (1024LL * 1024LL));
        }
        pclose(fp);
        return result;
    }

    return -1; /* Unsupported filesystem */
}

/* ------------------------------------------------------------------ *
 *  get_disk_free_space_mb                                              *
 *  Get total unallocated free space on a disk in MB.                  *
 * ------------------------------------------------------------------ */
glong get_disk_free_space_mb(const char *disk_name) {
    /* Use sfdisk -F (show free space) — available on all systems */
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "sfdisk -F /dev/%s 2>/dev/null", disk_name);
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    glong total_free = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        /* sfdisk -F output: "Unpartitioned space ... X bytes, Y MiB" or
         *   start    size   sectors" lines with sector counts.
         * Look for lines with sector counts (numeric start) */
        unsigned long long start_s = 0, size_s = 0;
        if (sscanf(line, " %llu %llu", &start_s, &size_s) == 2 && size_s > 0) {
            total_free += (glong)(size_s / 2048); /* sectors → MB (512B/sector) */
        }
    }
    pclose(fp);
    return total_free;
}

/* ------------------------------------------------------------------ *
 *  find_largest_resizable_partition                                    *
 *  Find the last large partition with a supported fs (ext4/ntfs).     *
 *  Returns allocated device path or NULL.                             *
 * ------------------------------------------------------------------ */
char* find_largest_resizable_partition(const char *disk_name) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "lsblk -rn -o NAME,FSTYPE /dev/%s | grep -v '^%s '",
        disk_name, disk_name);
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    char best_name[128] = {0};
    glong best_size = 0;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char name[64] = {0}, fstype[32] = {0};
        sscanf(line, "%63s %31s", name, fstype);

        /* Only consider resizable filesystems */
        if (strcmp(fstype, "ext4") != 0 && strcmp(fstype, "ext3") != 0 &&
            strcmp(fstype, "ext2") != 0 && strcmp(fstype, "ntfs") != 0) {
            continue;
        }

        char dev_path[128];
        snprintf(dev_path, sizeof(dev_path), "/dev/%s", name);
        glong size_mb = get_partition_size_mb(dev_path);

        if (size_mb > best_size) {
            best_size = size_mb;
            strncpy(best_name, name, sizeof(best_name) - 1);
        }
    }
    pclose(fp);

    if (best_name[0] != '\0') {
        return g_strdup_printf("/dev/%s", best_name);
    }
    return NULL;
}

/* ------------------------------------------------------------------ *
 *  parse_disk_and_partnum                                              *
 *  Split "/dev/sda2" → disk="/dev/sda", partnum=2                     *
 *  Split "/dev/nvme0n1p3" → disk="/dev/nvme0n1", partnum=3            *
 * ------------------------------------------------------------------ */
static int parse_disk_and_partnum(const char *device, char *disk_out,
                                  size_t disk_sz, int *partnum_out) {
    const char *dev = device;
    if (strncmp(dev, "/dev/", 5) == 0) dev += 5;

    if (strncmp(dev, "nvme", 4) == 0 || strncmp(dev, "mmcblk", 6) == 0) {
        /* Find last 'p' followed by digits only */
        const char *last_p = NULL;
        for (const char *s = dev; *s; s++) {
            if (*s == 'p' && s[1] >= '0' && s[1] <= '9') {
                gboolean all_d = TRUE;
                for (const char *c = s + 1; *c; c++)
                    if (!g_ascii_isdigit(*c)) { all_d = FALSE; break; }
                if (all_d) last_p = s;
            }
        }
        if (!last_p) return -1;
        int disk_len = (int)(last_p - dev);
        snprintf(disk_out, disk_sz, "/dev/%.*s", disk_len, dev);
        *partnum_out = atoi(last_p + 1);
    } else {
        /* sda2 → disk=sda, part=2 */
        int i = (int)strlen(dev) - 1;
        while (i >= 0 && g_ascii_isdigit(dev[i])) i--;
        snprintf(disk_out, disk_sz, "/dev/%.*s", i + 1, dev);
        *partnum_out = atoi(dev + i + 1);
    }
    return (*partnum_out > 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ *
 *  resize_existing_partition                                           *
 *  Shrink filesystem + partition table entry.                         *
 *  new_size_mb = desired new total partition size in MB.               *
 * ------------------------------------------------------------------ */
int resize_existing_partition(AppData *app, const char *device,
                              glong new_size_mb) {
    char fstype[64];
    get_partition_fstype(device, fstype, sizeof(fstype));

    /* 1. Unmount */
    log_to_ui_printf(app, "Unmounting %s if mounted...", device);
    run_sync(app, "umount %s 2>/dev/null || true", device);

    /* 2. Shrink filesystem */
    if (strcmp(fstype, "ext4") == 0 || strcmp(fstype, "ext3") == 0 ||
        strcmp(fstype, "ext2") == 0) {
        log_to_ui_printf(app, "Checking filesystem on %s...", device);
        if (run_sync(app, "e2fsck -f -y %s", device) != 0) {
            log_to_ui(app, "ERROR: Filesystem check failed!", 0.0);
            return -1;
        }
        log_to_ui_printf(app, "Resizing ext filesystem on %s to %ldM...",
                         device, new_size_mb);
        if (run_sync(app, "resize2fs %s %ldM", device, new_size_mb) != 0) {
            log_to_ui(app, "ERROR: Filesystem resize failed!", 0.0);
            return -1;
        }
    }
    else if (strcmp(fstype, "ntfs") == 0) {
        long long new_bytes = (long long)new_size_mb * 1024LL * 1024LL;
        log_to_ui_printf(app, "Resizing NTFS on %s to %ldMB...",
                         device, new_size_mb);
        if (run_sync(app, "echo y | ntfsresize --no-progress-bar --size %lld %s",
                     new_bytes, device) != 0) {
            log_to_ui(app, "ERROR: NTFS resize failed!", 0.0);
            return -1;
        }
    }
    else {
        log_to_ui_printf(app, "ERROR: Unsupported filesystem '%s' for resize!",
                         fstype);
        return -1;
    }

    /* 3. Shrink partition table entry via sfdisk (parted not available on live ISO) */
    char disk_dev[128] = {0};
    int part_num = 0;
    if (parse_disk_and_partnum(device, disk_dev, sizeof(disk_dev),
                               &part_num) != 0) {
        log_to_ui(app, "ERROR: Could not determine disk/partition number!", 0.0);
        return -1;
    }

    /* Read partition start sector from sysfs (always available) */
    const char *disk_base = disk_dev + 5; /* skip /dev/ */
    const char *part_base = device + 5;   /* skip /dev/ */
    char sysfs_path[256];
    snprintf(sysfs_path, sizeof(sysfs_path),
        "/sys/block/%s/%s/start", disk_base, part_base);

    FILE *fp_s = fopen(sysfs_path, "r");
    long long start_sectors = 0;
    if (fp_s) {
        fscanf(fp_s, "%lld", &start_sectors);
        fclose(fp_s);
    }
    if (start_sectors <= 0) {
        log_to_ui_printf(app, "ERROR: Could not read partition start from %s",
                         sysfs_path);
        return -1;
    }

    /* Calculate new size in sectors (1 MB = 2048 sectors of 512 bytes) */
    long long new_size_sectors = (long long)new_size_mb * 2048LL;

    log_to_ui_printf(app,
        "Updating partition table: part %d start=%lld size=%lld sectors",
        part_num, start_sectors, new_size_sectors);

    /* Use sfdisk -N to modify just this partition, keeping type/uuid intact */
    if (run_sync(app, "echo '%lld %lld' | sfdisk --no-reread -N %d %s 2>/dev/null",
                 start_sectors, new_size_sectors, part_num, disk_dev) != 0) {
        log_to_ui(app, "ERROR: Partition table resize failed!", 0.0);
        return -1;
    }

    sleep(1);
    run_sync(app, "blockdev --rereadpt %s 2>/dev/null || true", disk_dev);
    sleep(1);
    return 0;
}
