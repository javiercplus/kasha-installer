/*
 * ui.c
 */
#include "neko_installer.h"
#include <stdio.h>
#include <dirent.h>      
#include <sys/stat.h>   
#include <string.h>
#include <stdlib.h>      
#include "logo.h"

//read partitions
void scan_partitions_for_dialog(GtkComboBoxText *combo) {
    gtk_combo_box_text_remove_all(combo);
    
    DIR *d = opendir("/sys/block");
    if (!d) return;
    
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        char path[256];
        
        // scan partitions of disk
        if (strncmp(ent->d_name, "sd", 2) == 0 || strncmp(ent->d_name, "vd", 2) == 0) {
            int i = 1;
            for(i=1; i<=4; i++) {
                snprintf(path, sizeof(path), "/sys/block/%s/%s%d", ent->d_name, ent->d_name, i);
                if (access(path, F_OK) == 0) {
                     gchar *part_name = g_strdup_printf("/dev/%s%d", ent->d_name, i);
                     gtk_combo_box_text_append_text(combo, part_name);
                     g_free(part_name);
                }
            }
        }

        else if (strncmp(ent->d_name, "nvme", 4) == 0) {
             int i = 1;
             for(i=1; i<=4; i++) {
                 snprintf(path, sizeof(path), "/sys/block/%s/%sp%d", ent->d_name, ent->d_name, i);
                 if (access(path, F_OK) == 0) {
                     gchar *part_name = g_strdup_printf("/dev/%sp%d", ent->d_name, i);
                     gtk_combo_box_text_append_text(combo, part_name);
                     g_free(part_name);
                 }
             }
        }
    }
    closedir(d);
}


void on_insert_text_username(GtkEditable *editable, gchar *new_text, gint new_text_length, gint *position, gpointer data) {
    const gchar *current_text = gtk_entry_get_text(GTK_ENTRY(editable));
    if (strlen(current_text) > 0 || strlen(new_text) > 0) {
        gchar *lower_text = g_ascii_strdown(current_text, -1);
        
        g_signal_handlers_block_by_func(editable, on_insert_text_username, data);
        gtk_entry_set_text(GTK_ENTRY(editable), lower_text);
        g_signal_handlers_unblock_by_func(editable, on_insert_text_username, data);
        g_free(lower_text);
    }
    g_signal_stop_emission_by_name(editable, "insert-text");
}

// Helper to check for NTFS partition on the selected disk
char* find_ntfs_partition(const char *disk_name) {
    char cmd[256];
    // lsblk -rn -o NAME,FSTYPE /dev/sda | grep ntfs
    snprintf(cmd, sizeof(cmd), "lsblk -rn -o NAME,FSTYPE /dev/%s | grep ntfs | head -n1 | awk '{print $1}'", disk_name);
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    
    char part_name[128];
    if (fgets(part_name, sizeof(part_name), fp)) {
        part_name[strcspn(part_name, "\n")] = 0;
        pclose(fp);
        if (strlen(part_name) > 0) {
            // lsblk returns name (sda1), we need /dev/sda1
            return g_strdup_printf("/dev/%s", part_name);
        }
    } else {
        pclose(fp);
    }
    return NULL;
}

// Helper to generate partition name (e.g., sda -> sda1, nvme0n1 -> nvme0n1p1)
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
    // Clear existing
    on_reset_partitions_clicked(NULL, app);
    
    char *p1 = get_partition_path(disk_name, 1);
    char *p2 = get_partition_path(disk_name, 2);
    
    if (app->is_efi) {
        // UEFI Scheme
        // 1. ESP
        add_partition_config(app, p1, "vfat", "/boot/efi", TRUE, FALSE, NULL);
        // 2. Root
        add_partition_config(app, p2, "ext4", "/", TRUE, FALSE, NULL);
    } else {
        // BIOS Scheme
        // 1. Root
        add_partition_config(app, p1, "ext4", "/", TRUE, FALSE, NULL);
    }
    
    g_free(p1);
    g_free(p2);
    
    app->install_mode = INSTALL_MODE_ERASE;
}

void on_disk_changed(GtkComboBox *widget, AppData *app) {
    GtkTreeIter iter;
    if (gtk_combo_box_get_active_iter(widget, &iter)) {
        GtkTreeModel *model = gtk_combo_box_get_model(widget);
        gchar *disk_name;
        gtk_tree_model_get(model, &iter, 0, &disk_name, -1);
        
        if (app->selected_disk) g_free(app->selected_disk);
        app->selected_disk = g_strdup(disk_name); 
        g_print("Disk selected: %s\n", disk_name);
        g_free(disk_name);

        // Auto-Detection Logic
        if (app->detected_ntfs_partition) {
            g_free(app->detected_ntfs_partition);
            app->detected_ntfs_partition = NULL;
        }

        char *ntfs_part = find_ntfs_partition(app->selected_disk);
        
        if (ntfs_part) {
            app->detected_ntfs_partition = ntfs_part;
            
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                "Windows (NTFS) partition detected on %s.", ntfs_part);
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), 
                "To dual boot, please manually resize your Windows partition using GParted or Windows Disk Management to create free space.\n\n"
                "Then select 'Manual Partitioning' to install Neko-void in the free space.\n\n"
                "If you wish to erase the entire disk, proceed with the defaults.");
            
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            
            // Populate defaults for Erase mode anyway, user can clear if they want Manual
            populate_defaults(app, app->selected_disk);
            
        } else {
            // No NTFS -> Default Erase
            populate_defaults(app, app->selected_disk);
        }
    }
}

void launch_gparted(GtkWidget *widget, AppData *app) {
    if (!app->selected_disk) return;
    gchar *cmd = g_strdup_printf("gparted /dev/%s", app->selected_disk);
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
}

void on_page_changed(GtkNotebook *notebook, GtkWidget *page, guint page_num, AppData *app) {
    gtk_widget_set_sensitive(app->btn_back, (page_num > 0));
    gtk_widget_set_sensitive(app->btn_next, (page_num < 5));
}

void on_next_clicked(GtkWidget *widget, AppData *app) { gtk_notebook_next_page(GTK_NOTEBOOK(app->notebook)); }
void on_back_clicked(GtkWidget *widget, AppData *app) { gtk_notebook_prev_page(GTK_NOTEBOOK(app->notebook)); }
void on_reboot_clicked(GtkWidget *widget, AppData *app) { system("reboot"); }

void on_popup_reboot(GtkDialog *dialog, gint response_id, gpointer user_data) {
    system("reboot");
}

gboolean set_ui_finished_safe(gpointer data) {
    AppData *app = (AppData *)data;
    
    gtk_widget_set_sensitive(app->btn_back, FALSE);
    gtk_widget_set_sensitive(app->btn_next, FALSE);
    gtk_widget_set_sensitive(app->notebook, FALSE);
    

    gtk_button_set_label(GTK_BUTTON(app->btn_install), "Reboot System");
    g_signal_handlers_disconnect_by_func(app->btn_install, G_CALLBACK(start_installation), app);
    g_signal_connect(app->btn_install, "clicked", G_CALLBACK(on_reboot_clicked), app);
    gtk_widget_set_sensitive(app->btn_install, TRUE);


    GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE,
        "NEKO-VOID is READY!!!");

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(success_dialog), 
        "Installation completed successfully.\nThe system is ready to restart.");

    GtkWidget *btn_reboot_popup = gtk_dialog_add_button(GTK_DIALOG(success_dialog), "REBOOT NOW", GTK_RESPONSE_ACCEPT);
    GtkStyleContext *context = gtk_widget_get_style_context(btn_reboot_popup);
    gtk_style_context_add_class(context, "suggested-action");
    g_signal_connect(success_dialog, "response", G_CALLBACK(on_popup_reboot), NULL);
    gtk_widget_show_all(success_dialog);
    return FALSE;
}

void set_ui_finished(AppData *app) { g_idle_add(set_ui_finished_safe, app); }

GtkWidget* create_form_row(const gchar *label_text, GtkWidget **entry_ptr) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_bottom(hbox, 5); 
    
    GtkWidget *label = gtk_label_new(label_text);
    gtk_label_set_xalign(GTK_LABEL(label), 1.0); 
    gtk_widget_set_size_request(label, 180, -1); 
    
    *entry_ptr = gtk_entry_new();
    gtk_widget_set_hexpand(*entry_ptr, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), *entry_ptr, TRUE, TRUE, 0);
    return hbox;
}

//add settings of partition
void add_partition_config(AppData *app, const gchar *dev, const gchar *fs, const gchar *mp, gboolean fmt, gboolean encrypt, const gchar *pass) {
    PartitionConfig *conf = g_new(PartitionConfig, 1);
    conf->device = g_strdup(dev);
    conf->fstype = g_strdup(fs);
    conf->mountpoint = g_strdup(mp);
    conf->format = fmt;
    conf->encrypt = encrypt;
    conf->luks_pass = (encrypt && pass) ? g_strdup(pass) : NULL;
    
    app->part_config_list = g_slist_append(app->part_config_list, conf);
    
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(app->mount_list)));
    GtkTreeIter iter;
    gtk_list_store_append(store, &iter);
    gchar *fmt_str = fmt ? "YES" : "NO";
    gchar *enc_str = encrypt ? "LUKS" : "-";
    gtk_list_store_set(store, &iter, 0, dev, 1, mp, 2, fs, 3, fmt_str, 4, enc_str, -1);
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

void populate_partitions_combo(GtkComboBoxText *combo, const char *disk_name) {
    char sys_path[256];
    snprintf(sys_path, sizeof(sys_path), "/sys/block/%s", disk_name);
    
    DIR *d = opendir(sys_path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, disk_name, strlen(disk_name)) == 0 && 
            strcmp(ent->d_name, disk_name) != 0) {
            char part_path[512];
            snprintf(part_path, sizeof(part_path), "%s/%s", sys_path, ent->d_name);
            char size_path[550];
            snprintf(size_path, sizeof(size_path), "%s/size", part_path);
            if (access(size_path, F_OK) == 0) {
                char dev_full_path[512];
                snprintf(dev_full_path, sizeof(dev_full_path), "/dev/%s", ent->d_name);
                
                char fstype[64];
                get_partition_fstype(dev_full_path, fstype, sizeof(fstype));
                
                if (strcmp(fstype, "iso9660") == 0 || 
                    strcmp(fstype, "crypto_LUKS") == 0 || 
                    strcmp(fstype, "LVM2_member") == 0) {
                    continue;
                }

                gtk_combo_box_text_append_text(combo, ent->d_name);
            }
        }
    }
    closedir(d);
}


// Helper to find config by device path (simple search)
PartitionConfig* find_config_by_device(AppData *app, const gchar *device) {
    GSList *l = app->part_config_list;
    while (l) {
        PartitionConfig *c = (PartitionConfig*)l->data;
        if (strcmp(c->device, device) == 0) return c;
        l = l->next;
    }
    return NULL;
}

// Helper to remove config from list
void remove_partition_config(AppData *app, PartitionConfig *conf) {
    app->part_config_list = g_slist_remove(app->part_config_list, conf);
    g_free(conf->device);
    g_free(conf->fstype);
    g_free(conf->mountpoint);
    if(conf->luks_pass) g_free(conf->luks_pass);
    g_free(conf);
}

void refresh_partition_list_ui(AppData *app) {
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(app->mount_list)));
    gtk_list_store_clear(store);
    
    GSList *l = app->part_config_list;
    while (l) {
        PartitionConfig *c = (PartitionConfig*)l->data;
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gchar *fmt_str = c->format ? "YES" : "NO";
        gchar *enc_str = c->encrypt ? "LUKS" : "-";
        gtk_list_store_set(store, &iter, 0, c->device, 1, c->mountpoint, 2, c->fstype, 3, fmt_str, 4, enc_str, -1);
        l = l->next;
    }
}

void show_partition_dialog(AppData *app, PartitionConfig *edit_conf) {
    if (!app->selected_disk) {
        GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(app->window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Please select a disk in Tab 1 first!");
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        return;
    }

    const char *title = edit_conf ? "Edit Partition" : "Add Partition";
    const char *btn_label = edit_conf ? "_Update" : "_Add";

    GtkWidget *dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(app->window),
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "_Cancel", GTK_RESPONSE_CANCEL,
                                                 btn_label, GTK_RESPONSE_ACCEPT,
                                                 NULL);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 10);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(content), notebook);

    // --- Tab 1: General ---
    GtkWidget *vbox_gen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_gen), 10);

    GtkWidget *h_dev = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(h_dev), gtk_label_new("Select Partition:"), FALSE, FALSE, 0);
    
    GtkComboBoxText *combo_part = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    populate_partitions_combo(combo_part, app->selected_disk);
    
    // Select existing if editing
    
    gtk_box_pack_start(GTK_BOX(h_dev), GTK_WIDGET(combo_part), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_gen), h_dev, FALSE, FALSE, 0);

    // ... Filesystem ...
    GtkWidget *h_fs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(h_fs), gtk_label_new("Filesystem:"), FALSE, FALSE, 0);
    GtkComboBoxText *combo_fs = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(combo_fs, "ext4");
    gtk_combo_box_text_append_text(combo_fs, "btrfs");
    gtk_combo_box_text_append_text(combo_fs, "xfs");
    gtk_combo_box_text_append_text(combo_fs, "f2fs");
    gtk_combo_box_text_append_text(combo_fs, "vfat");
    gtk_combo_box_text_append_text(combo_fs, "swap"); 
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_fs), 0);
    gtk_box_pack_start(GTK_BOX(h_fs), GTK_WIDGET(combo_fs), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_gen), h_fs, FALSE, FALSE, 0);

    // ... Mount Point ...
    GtkWidget *h_mp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(h_mp), gtk_label_new("Mount Point:"), FALSE, FALSE, 0);
    GtkWidget *entry_mp_w = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry_mp_w), "/");
    gtk_box_pack_start(GTK_BOX(h_mp), entry_mp_w, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_gen), h_mp, FALSE, FALSE, 0);

    // ... Format ...
    GtkCheckButton *chk_fmt = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Format Partition?"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_fmt), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox_gen), GTK_WIDGET(chk_fmt), FALSE, FALSE, 0);

    // --- Tab 2: Encryption ---
    GtkWidget *vbox_enc = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_enc), 10);

    GtkCheckButton *chk_encrypt = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Encrypt (LUKS)?"));
    gtk_box_pack_start(GTK_BOX(vbox_enc), GTK_WIDGET(chk_encrypt), FALSE, FALSE, 0);

    GtkWidget *vbox_pass = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_sensitive(vbox_pass, FALSE);
    gtk_widget_set_margin_start(vbox_pass, 20);
    
    GtkWidget *entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_pass), "Encryption Password");
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox_pass), entry_pass, FALSE, FALSE, 0);

    GtkWidget *entry_pass_conf = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_pass_conf), "Confirm Password");
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass_conf), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox_pass), entry_pass_conf, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox_enc), vbox_pass, FALSE, FALSE, 0);

    g_object_bind_property(chk_encrypt, "active", vbox_pass, "sensitive", G_BINDING_DEFAULT);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_gen, gtk_label_new("General"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_enc, gtk_label_new("Encryption"));

    // PRE-FILL IF EDITING
    if (edit_conf) {
        // Device: Try to set active.
        const char *short_dev = edit_conf->device; // e.g /dev/sda1
        if (strncmp(short_dev, "/dev/", 5) == 0) short_dev += 5; // sda1
        
        // FS
        if (strcmp(edit_conf->fstype, "ext4") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(combo_fs), 0);
        else if (strcmp(edit_conf->fstype, "btrfs") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(combo_fs), 1);
        else if (strcmp(edit_conf->fstype, "xfs") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(combo_fs), 2);
        else if (strcmp(edit_conf->fstype, "f2fs") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(combo_fs), 3);
        else if (strcmp(edit_conf->fstype, "vfat") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(combo_fs), 4);
        else if (strcmp(edit_conf->fstype, "swap") == 0) gtk_combo_box_set_active(GTK_COMBO_BOX(combo_fs), 5);

        gtk_entry_set_text(GTK_ENTRY(entry_mp_w), edit_conf->mountpoint);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_fmt), edit_conf->format);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_encrypt), edit_conf->encrypt);
        if (edit_conf->luks_pass) {
            gtk_entry_set_text(GTK_ENTRY(entry_pass), edit_conf->luks_pass);
            gtk_entry_set_text(GTK_ENTRY(entry_pass_conf), edit_conf->luks_pass);
        }
    } else {
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo_part), 0);
    }

    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *dev_short = gtk_combo_box_text_get_active_text(combo_part);
        char *fs = gtk_combo_box_text_get_active_text(combo_fs);
        const gchar *mp = gtk_entry_get_text(GTK_ENTRY(entry_mp_w));
        gboolean fmt = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_fmt));
        gboolean encrypt = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_encrypt));
        const gchar *pass = gtk_entry_get_text(GTK_ENTRY(entry_pass));
        const gchar *pass_conf = gtk_entry_get_text(GTK_ENTRY(entry_pass_conf));

        if (encrypt && (strlen(pass) < 1 || strcmp(pass, pass_conf) != 0)) {
             GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(dialog),
                GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                "Encryption passwords do not match or are empty!");
            gtk_dialog_run(GTK_DIALOG(err));
            gtk_widget_destroy(err);
        } 
        else if (dev_short && mp && strlen(mp) > 0) {
            app->install_mode = INSTALL_MODE_MANUAL;

            gchar *full_dev = g_strdup_printf("/dev/%s", dev_short);

            if (edit_conf) {
                // UPDATE EXISTING
                g_free(edit_conf->device);
                g_free(edit_conf->fstype);
                g_free(edit_conf->mountpoint);
                if (edit_conf->luks_pass) g_free(edit_conf->luks_pass);
                
                edit_conf->device = full_dev;
                edit_conf->device = g_strdup(full_dev);
                edit_conf->fstype = g_strdup(fs);
                edit_conf->mountpoint = g_strdup(mp);
                edit_conf->format = fmt;
                edit_conf->encrypt = encrypt;
                edit_conf->luks_pass = (encrypt && pass) ? g_strdup(pass) : NULL;
                
                g_free(full_dev);
            } else {
                // ADD NEW
                if (strcmp(fs, "swap") == 0) {
                    add_partition_config(app, full_dev, fs, "[SWAP]", fmt, encrypt, pass);
                } else {
                    add_partition_config(app, full_dev, fs, mp, fmt, encrypt, pass);
                }
                g_free(full_dev);
            }
            
            g_free(dev_short); 
            g_free(fs);
            
            if (edit_conf) refresh_partition_list_ui(app);
        }
    }
    gtk_widget_destroy(dialog);
}

void on_add_partition_clicked(GtkWidget *widget, gpointer user_data) {
    show_partition_dialog((AppData *)user_data, NULL);
}

void on_edit_partition_clicked(GtkWidget *widget, gpointer user_data) {
    AppData *app = (AppData *)user_data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->mount_list));
    GtkTreeModel *model;
    GtkTreeIter iter;
    
    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        gchar *dev;
        gtk_tree_model_get(model, &iter, 0, &dev, -1);
        
        PartitionConfig *conf = find_config_by_device(app, dev);
        if (conf) {
            show_partition_dialog(app, conf);
        }
        g_free(dev);
    }
}

void on_delete_partition_clicked(GtkWidget *widget, gpointer user_data) {
    AppData *app = (AppData *)user_data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->mount_list));
    GtkTreeModel *model;
    GtkTreeIter iter;
    
    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        gchar *dev;
        gtk_tree_model_get(model, &iter, 0, &dev, -1);
        
        PartitionConfig *conf = find_config_by_device(app, dev);
        if (conf) {
            app->install_mode = INSTALL_MODE_MANUAL;
            remove_partition_config(app, conf);
            gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
        }
        g_free(dev);
    }
}
void open_partition_manager(GtkWidget *widget, AppData *app) {
    if (!app->mount_list) return;

    GtkListStore *store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING); 
    gtk_tree_view_set_model(GTK_TREE_VIEW(app->mount_list), GTK_TREE_MODEL(store));

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Device", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->mount_list), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Mount Point", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->mount_list), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("FS Type", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->mount_list), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Format?", renderer, "text", 3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->mount_list), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Encrypted?", renderer, "text", 4, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->mount_list), col);
}

// ui.c
GtkWidget* create_welcome_page(AppData *app) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20); 
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 20); 

    // Picture
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    if (gdk_pixbuf_loader_write(loader, logo_png, logo_png_len, NULL)) {
        gdk_pixbuf_loader_close(loader, NULL);
        GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
        
        if (pixbuf) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, 180, 450, GDK_INTERP_BILINEAR);
            GtkWidget *image = gtk_image_new_from_pixbuf(scaled);
            gtk_widget_set_valign(image, GTK_ALIGN_START); 
            gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0); 
            
            g_object_unref(scaled); 
        }
    }
    g_object_unref(loader);

    GtkWidget *vbox_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0); 
    gtk_label_set_yalign(GTK_LABEL(label), 0.0); 
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE); 
    gtk_label_set_max_width_chars(GTK_LABEL(label), 45); 
    gtk_label_set_selectable(GTK_LABEL(label), TRUE); 

    const char *welcome_text = 
        "<span size='xx-large' weight='bold' foreground='#33d17a'>🐱 WELCOME TO NEKO VOID!!!</span>\n\n"
        "Neko-Void is an unofficial respin of Void Linux featuring a "
        "<span weight='bold'>MATE desktop environment</span> with carefully selected software.\n\n"
        
        "<span size='large' weight='bold' foreground='#3584e4'>🖥️ Core System</span>\n"
        "• Void Linux base (rolling release)\n"
        "• MATE Desktop optimized\n"
        "• UEFI and Legacy BIOS support\n\n"

        "<span size='large' weight='bold' foreground='#e01b24'>🎮 Gaming and Multimedia</span>\n"
        "• Steam preinstalled\n"
        "• Intel and AMD GPU drivers (Vulkan)\n\n"

        "<span size='large' weight='bold'>🚀 Features</span>\n"
        "<span font_family='monospace'>[Easy] [Gaming] [Music]</span>\n"
        "<span font_family='monospace'>[Void Base] [Mate Desktop]</span>";

    gtk_label_set_markup(GTK_LABEL(label), welcome_text);
    
    gtk_box_pack_start(GTK_BOX(vbox_text), label, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(hbox), vbox_text, TRUE, TRUE, 10);
    gtk_widget_show_all(hbox);
    
    return hbox;
}

void on_timezone_area_changed(GtkComboBox *widget, AppData *app) {
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->tz_city_combo));
    char *area = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->tz_area_combo));
    if (!area) return;
    char path[256];
    snprintf(path, sizeof(path), "/usr/share/zoneinfo/%s", area);
    
    DIR *d = opendir(path);
    if (d) {
        struct dirent *dir;
        GSList *city_list = NULL; 

        while ((dir = readdir(d)) != NULL) {
            if (dir->d_name[0] != '.') {
                city_list = g_slist_prepend(city_list, g_strdup(dir->d_name));
            }
        }
        closedir(d);

        city_list = g_slist_sort(city_list, (GCompareFunc)strcmp);
        GSList *l = city_list;
        while (l) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->tz_city_combo), (char *)l->data);
            g_free(l->data);
            l = l->next;
        }
        g_slist_free(city_list);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->tz_city_combo), 0);
    g_free(area);
}

//custom themes
void load_custom_css() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const gchar *css_data = 
        "progressbar trough { min-height: 20px; }"
        "progressbar progress { min-height: 20px; background-color: #33d17a; }"; 

    GError *error = NULL;
    gtk_css_provider_load_from_data(provider, css_data, -1, &error);
    if (error) {
        g_printerr("Error loading CSS: %s\n", error->message);
        g_error_free(error);
    } else {
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                                  GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);
}


void on_reset_partitions_clicked(GtkWidget *widget, AppData *app) {
    // Switch to CUSTOM mode to avoid auto-partitioning override
    app->install_mode = INSTALL_MODE_MANUAL;
    
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(app->mount_list)));
    gtk_list_store_clear(store);
    if (app->part_config_list) {
        GSList *l = app->part_config_list;
        while (l != NULL) {
            PartitionConfig *conf = (PartitionConfig *)l->data;
            g_free(conf->device);
            g_free(conf->fstype);
            g_free(conf->mountpoint);
            if(conf->luks_pass) g_free(conf->luks_pass);
            g_free(conf);
            l = l->next;
        }
        g_slist_free(app->part_config_list);
        app->part_config_list = NULL;
    }

    g_print("[INFO] Partition list reset.\n");
}

void build_ui(AppData *app) {
    load_custom_css();
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Kasha Installer - Neko Void");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 850, 500);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); 
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    app->notebook = gtk_notebook_new();
    gtk_widget_set_margin_start(app->notebook, 10);
    gtk_widget_set_margin_end(app->notebook, 10);
    gtk_widget_set_margin_top(app->notebook, 10);
    gtk_box_pack_start(GTK_BOX(vbox), app->notebook, TRUE, TRUE, 0);
  
  //WELCOME
    GtkWidget *page_welcome = create_welcome_page(app);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_welcome, gtk_label_new("Welcome"));
    // --- TAB 1: DISKS ---
    GtkWidget *page_disk = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(page_disk), 15);
    
    GtkWidget *hbox_disk = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(page_disk), hbox_disk, FALSE, FALSE, 0);
    
    app->disk_combo = gtk_combo_box_new();
    GtkListStore *disk_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    gtk_combo_box_set_model(GTK_COMBO_BOX(app->disk_combo), GTK_TREE_MODEL(disk_store));
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(app->disk_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(app->disk_combo), renderer, "text", 0);
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(app->disk_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(app->disk_combo), renderer, "text", 1);
    
    g_signal_connect(app->disk_combo, "changed", G_CALLBACK(on_disk_changed), app);
    gtk_box_pack_start(GTK_BOX(hbox_disk), gtk_label_new("Hard Disk:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_disk), app->disk_combo, TRUE, TRUE, 0);

    GtkWidget *btn_part = gtk_button_new_with_label("Partition (GParted)");
    g_signal_connect(btn_part, "clicked", G_CALLBACK(launch_gparted), app);
    gtk_box_pack_start(GTK_BOX(hbox_disk), btn_part, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(page_disk), gtk_label_new("IMPORTANT: Create partitions in GParted, then configure them below."), FALSE, FALSE, 0);
    
    // Partition Manager UI
    GtkWidget *hbox_pm = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(hbox_pm), gtk_label_new("Mount Points:"), FALSE, FALSE, 0);
    GtkWidget *btn_add = gtk_button_new_with_label("Add");
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_partition_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_pm), btn_add, FALSE, FALSE, 0);

    GtkWidget *btn_edit = gtk_button_new_with_label("Edit");
    g_signal_connect(btn_edit, "clicked", G_CALLBACK(on_edit_partition_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_pm), btn_edit, FALSE, FALSE, 0);

    GtkWidget *btn_del = gtk_button_new_with_label("Delete");
    g_signal_connect(btn_del, "clicked", G_CALLBACK(on_delete_partition_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_pm), btn_del, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page_disk), hbox_pm, FALSE, FALSE, 0);
    GtkWidget *btn_reset = gtk_button_new_with_label("Reset mount points");
    g_signal_connect(btn_reset, "clicked", G_CALLBACK(on_reset_partitions_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_pm), btn_reset, FALSE, FALSE, 0);
  
    app->mount_list = gtk_tree_view_new();
    open_partition_manager(NULL, app); 
    gtk_box_pack_start(GTK_BOX(page_disk), app->mount_list, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_disk, gtk_label_new("1. Disks"));

    // --- TAB 2: BOOTLOADER ---
    GtkWidget *page_boot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(page_boot), 15);
    app->grub_disk_combo = gtk_combo_box_text_new(); 
    gtk_box_pack_start(GTK_BOX(page_boot), gtk_label_new("Select MBR/EFI disk for Bootloader:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page_boot), app->grub_disk_combo, FALSE, FALSE, 0);
    
    app->label_boot_status = gtk_label_new("Detecting firmware...");
    gtk_widget_set_margin_top(app->label_boot_status, 10);
    gtk_box_pack_start(GTK_BOX(page_boot), app->label_boot_status, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_boot, gtk_label_new("2. Bootloader"));

    // --- TAB 3: SYSTEM ---
    GtkWidget *page_system = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(page_system), 15);
    gtk_box_pack_start(GTK_BOX(page_system), create_form_row("Machine Name (Hostname):", &app->hostname_entry), FALSE, FALSE, 0);
    gtk_entry_set_text(GTK_ENTRY(app->hostname_entry), "neko-void"); 

    GtkWidget *hbox_locale = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_bottom(hbox_locale, 5);
    gtk_box_pack_start(GTK_BOX(hbox_locale), gtk_label_new("System Language:"), FALSE, FALSE, 0);
    app->locale_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->locale_combo), "en_US.UTF-8");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->locale_combo), "es_ES.UTF-8");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->locale_combo), "pt_BR.UTF-8");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->locale_combo), 0);
    gtk_box_pack_start(GTK_BOX(hbox_locale), app->locale_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page_system), hbox_locale, FALSE, FALSE, 0);


    // --- TIMEZONE SECTION ---
    GtkWidget *hbox_tz = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(hbox_tz, 10); 
    
    gtk_box_pack_start(GTK_BOX(hbox_tz), gtk_label_new("Timezone:"), FALSE, FALSE, 0);

    // 1. Regions
    app->tz_area_combo = gtk_combo_box_text_new();
    const char *areas[] = {"Africa", "America", "Antarctica", "Arctic", "Asia", "Atlantic", "Australia", "Europe", "Indian", "Pacific", "UTC", NULL};
    
    for (int i = 0; areas[i] != NULL; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->tz_area_combo), areas[i]);
    }
    
    g_signal_connect(app->tz_area_combo, "changed", G_CALLBACK(on_timezone_area_changed), app);

    app->tz_city_combo = gtk_combo_box_text_new();

    gtk_box_pack_start(GTK_BOX(hbox_tz), app->tz_area_combo, FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(hbox_tz), app->tz_city_combo, TRUE, TRUE, 0);   
    gtk_box_pack_start(GTK_BOX(page_system), hbox_tz, FALSE, FALSE, 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->tz_area_combo), 1); 

  
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_system, gtk_label_new("3. System"));

  
    // --- TAB 4: USERS ---
    GtkWidget *page_user = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(page_user), 15);
    
    GtkWidget *frame_root = gtk_frame_new("Superuser (root)");
    GtkWidget *vbox_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_root), 15);
    gtk_container_add(GTK_CONTAINER(frame_root), vbox_root);
    gtk_box_pack_start(GTK_BOX(vbox_root), create_form_row("Root Password:", &app->root_pass_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_root), create_form_row("Confirm Root:", &app->root_pass_confirm_entry), FALSE, FALSE, 0);
    gtk_entry_set_visibility(GTK_ENTRY(app->root_pass_entry), FALSE);
    gtk_entry_set_visibility(GTK_ENTRY(app->root_pass_confirm_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(page_user), frame_root, FALSE, FALSE, 0);

    GtkWidget *frame_user = gtk_frame_new("New User");
    GtkWidget *vbox_user = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_user), 15);
    gtk_container_add(GTK_CONTAINER(frame_user), vbox_user);
    
    gtk_box_pack_start(GTK_BOX(vbox_user), create_form_row("Username:", &app->user_login_entry), FALSE, FALSE, 0);
    // Connect-After para minúsculas
    g_signal_connect_after(GTK_EDITABLE(app->user_login_entry), "insert-text", G_CALLBACK(on_insert_text_username), app); 
    
    gtk_box_pack_start(GTK_BOX(vbox_user), create_form_row("Full Name:", &app->user_fullname_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_user), create_form_row("Password:", &app->user_pass_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_user), create_form_row("Confirm Password:", &app->user_pass_confirm_entry), FALSE, FALSE, 0);
    gtk_entry_set_visibility(GTK_ENTRY(app->user_pass_entry), FALSE);
    gtk_entry_set_visibility(GTK_ENTRY(app->user_pass_confirm_entry), FALSE);

    app->autologin_check = gtk_check_button_new_with_label("Log in automatically");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->autologin_check), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox_user), app->autologin_check, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page_user), frame_user, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_user, gtk_label_new("4. Users"));

    // --- TAB 5: INSTALLATION ---
    GtkWidget *page_install = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page_install), 10);
    
    // No override_font
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    app->console_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->console_text), FALSE);
    gtk_container_add(GTK_CONTAINER(scroll), app->console_text);
    gtk_box_pack_start(GTK_BOX(page_install), scroll, TRUE, TRUE, 0);
    
    app->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_margin_top(app->progress_bar, 10);
    gtk_box_pack_start(GTK_BOX(page_install), app->progress_bar, FALSE, FALSE, 0);

    app->btn_install = gtk_button_new_with_label("START INSTALLATION");
    gtk_widget_set_halign(app->btn_install, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(app->btn_install, 15);
    g_signal_connect(app->btn_install, "clicked", G_CALLBACK(start_installation), app);
    gtk_box_pack_start(GTK_BOX(page_install), app->btn_install, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_install, gtk_label_new("5. Install"));

    // --- NAV BAR ---
    GtkWidget *hbox_nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(hbox_nav, 10);
    gtk_widget_set_margin_bottom(hbox_nav, 10);
    gtk_widget_set_margin_start(hbox_nav, 20);
    gtk_widget_set_margin_end(hbox_nav, 20);
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_nav, FALSE, FALSE, 0);

    app->btn_back = gtk_button_new_with_label("Back");
    gtk_widget_set_sensitive(app->btn_back, FALSE); 
    g_signal_connect(app->btn_back, "clicked", G_CALLBACK(on_back_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_nav), app->btn_back, FALSE, FALSE, 0);
    
    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(hbox_nav), spacer, TRUE, TRUE, 0);
    
    app->btn_next = gtk_button_new_with_label("Next >");
    g_signal_connect(app->btn_next, "clicked", G_CALLBACK(on_next_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_nav), app->btn_next, FALSE, FALSE, 0);

    gtk_widget_show_all(app->window);
    g_signal_connect(app->notebook, "switch-page", G_CALLBACK(on_page_changed), app);
}