/*
 * utils.c
 * Utilities Module: Hardware Detection and Command Execution.
 */
#include "neko_installer.h"
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

gboolean check_efi() {
    gboolean result = (access("/sys/firmware/efi/systab", F_OK) == 0);
    g_print("[INFO] EFI System detected: %s\n", result ? "YES" : "NO");
    return result;
}

void scan_disks(AppData *app) {
    struct dirent *entry;
    DIR *dp = opendir("/sys/block");
    
    if (dp == NULL) return;

    GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(app->disk_combo)));
    gtk_list_store_clear(store);

    while ((entry = readdir(dp)) != NULL) {
        // Common disk filters: sd, vd, nvme, hd, mmcblk
        if (strncmp(entry->d_name, "sd", 2) == 0 || 
            strncmp(entry->d_name, "vd", 2) == 0 ||
            strncmp(entry->d_name, "nvme", 4) == 0 ||
            strncmp(entry->d_name, "hd", 2) == 0 ||
            strncmp(entry->d_name, "mmcblk", 7) == 0) {
            
            char path[256];
            char size_str[64];
            unsigned long long size_bytes = 0;
            
            // Read size
            snprintf(path, sizeof(path), "/sys/block/%s/size", entry->d_name);
            FILE *f = fopen(path, "r");
            if (f) {
                fscanf(f, "%llu", &size_bytes);
                fclose(f);
                double gb = (size_bytes * 512) / (1024.0 * 1024.0 * 1024.0);
                snprintf(size_str, sizeof(size_str), "%.1f GB", gb);
            } else {
                strcpy(size_str, "Unknown");
            }

            g_print("[DEBUG] Disk found: %s (%s)\n", entry->d_name, size_str);

            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, entry->d_name, 1, size_str, -1);
        }
    }
    closedir(dp);
}

// NEW FUNCTION: Copies disks from main list to GRUB list
void sync_grub_list(AppData *app) {
    // Clear GRUB list
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->grub_disk_combo));

    // Get main list model
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(app->disk_combo));
    GtkTreeIter iter;
    
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    
    while (valid) {
        gchar *disk_name;
        gchar *disk_size;
        
        // Read disk name
        gtk_tree_model_get(model, &iter, 0, &disk_name, 1, &disk_size, -1);
        
        // Create nice label for GRUB (e.g., "sda (500GB)")
        gchar *label = g_strdup_printf("/dev/%s (%s)", disk_name, disk_size);
        
        // Add to GRUB list
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->grub_disk_combo), label);
        
        g_free(disk_name);
        g_free(disk_size);
        g_free(label);
        
        valid = gtk_tree_model_iter_next(model, &iter);
    }
    
    // Select first by default
    if (gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->grub_disk_combo))) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->grub_disk_combo), 0);
    }
}

void init_utils(AppData *app) {
    scan_disks(app);
    // Sync bootloader list after scanning
    sync_grub_list(app); 
}
