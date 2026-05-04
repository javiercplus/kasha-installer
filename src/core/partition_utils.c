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
    on_reset_partitions_clicked(NULL, app);
    
    char *p1 = get_partition_path(disk_name, 1);
    char *p2 = get_partition_path(disk_name, 2);
    
    if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
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
