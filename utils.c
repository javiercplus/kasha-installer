/*
 * utils.c
 */
#include "neko_installer.h"
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

gboolean check_efi() {
    if (access("/sys/firmware/efi/efivars", F_OK) == 0) {
        return TRUE;
    }
    return FALSE;
}

void read_efi_bits(AppData *app) {
    if (!app->is_efi) {
        app->efi_target = NULL;
        // ACTUALIZAR UI
        if (app->label_boot_status)
            gtk_label_set_text(GTK_LABEL(app->label_boot_status), "BIOS/Legacy System Detected.");
        return;
    }

    char fw_size_str[16];
    FILE *f = fopen("/sys/firmware/efi/fw_platform_size", "r");
    
    if (f) {
        if (fgets(fw_size_str, sizeof(fw_size_str), f) != NULL) {
            fw_size_str[strcspn(fw_size_str, "\n")] = 0;
            
            gchar *ui_text;
            if (strcmp(fw_size_str, "32") == 0) {
                app->efi_target = "i386-efi";
                ui_text = g_strdup("EFI System Detected (32-bit).");
            } else {
                app->efi_target = "x86_64-efi";
                ui_text = g_strdup("EFI System Detected (64-bit).");
            }
            
            // ACTUALIZAR UI
            if (app->label_boot_status)
                gtk_label_set_text(GTK_LABEL(app->label_boot_status), ui_text);
                
            g_free(ui_text);
        }
        fclose(f);
    } else {
        app->efi_target = "x86_64-efi";
        if (app->label_boot_status)
            gtk_label_set_text(GTK_LABEL(app->label_boot_status), "EFI System Detected (Assuming 64-bit).");
    }
}

void scan_disks(AppData *app) {
    struct dirent *entry;
    DIR *dp = opendir("/sys/block");
    
    if (dp == NULL) return;

    GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(app->disk_combo)));
    gtk_list_store_clear(store);

    while ((entry = readdir(dp)) != NULL) {
        if (strncmp(entry->d_name, "sd", 2) == 0 || 
            strncmp(entry->d_name, "vd", 2) == 0 ||
            strncmp(entry->d_name, "nvme", 4) == 0 ||
            strncmp(entry->d_name, "hd", 2) == 0 ||
            strncmp(entry->d_name, "mmcblk", 7) == 0) {
            
            char path[256];
            char size_str[64];
            unsigned long long size_bytes = 0;
            
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

void sync_grub_list(AppData *app) {
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->grub_disk_combo));

    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(app->disk_combo));
    GtkTreeIter iter;
    
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    
    while (valid) {
        gchar *disk_name;
        gchar *disk_size;
        
        gtk_tree_model_get(model, &iter, 0, &disk_name, 1, &disk_size, -1);
        
        gchar *label = g_strdup_printf("/dev/%s (%s)", disk_name, disk_size);
        
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->grub_disk_combo), label);
        
        g_free(disk_name);
        g_free(disk_size);
        g_free(label);
        
        valid = gtk_tree_model_iter_next(model, &iter);
    }
    
    if (gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->grub_disk_combo))) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->grub_disk_combo), 0);
    }
}

void init_utils(AppData *app) {
    app->is_efi = check_efi();
    read_efi_bits(app); 
    scan_disks(app);
    sync_grub_list(app); 
}