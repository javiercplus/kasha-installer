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
        // Dual boot: only root partition in free space (device resolved at install time)
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
    g_free(conf->fstype);
    g_free(conf->mountpoint);
    if(conf->luks_pass) g_free(conf->luks_pass);
    g_free(conf);
}
