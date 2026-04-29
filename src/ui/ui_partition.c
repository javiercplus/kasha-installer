/*
 * ui_partition.c
 * Partition Dialog and List Management
 */
#include "neko_installer.h"
#include "lang.h"
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>

static const char* get_part_loc(const char *key, AppData *app) {
    extern const LangInfo* get_lang(const char *code);
    extern const char** get_available_langs(void);
    const char **langs = get_available_langs();
    int lang_idx = app->current_lang;
    if (lang_idx < 0) lang_idx = 0;
    if (lang_idx >= 3) lang_idx = 0;
    const LangInfo *li = get_lang(langs[lang_idx]);
    if (!li) return key;
    
    if (strcmp(key, "title_add") == 0) return li->dialog_add;
    if (strcmp(key, "title_edit") == 0) return li->dialog_edit;
    if (strcmp(key, "select_partition") == 0) return li->select_partition;
    if (strcmp(key, "filesystem") == 0) return li->filesystem;
    if (strcmp(key, "mount_point") == 0) return li->mount_point;
    if (strcmp(key, "format_partition") == 0) return li->format_partition;
    if (strcmp(key, "encrypt_luks") == 0) return li->encrypt_luks;
    if (strcmp(key, "general") == 0) return li->general;
    if (strcmp(key, "encryption") == 0) return li->encryption;
    if (strcmp(key, "update") == 0) return li->dialog_update;
    if (strcmp(key, "add") == 0) return li->save;
    if (strcmp(key, "cancel") == 0) return li->cancel;
    if (strcmp(key, "error_no_disk") == 0) return "Please select a disk in Tab 1 first!";
    if (strcmp(key, "error_pass_mismatch") == 0) return "Encryption passwords do not match or are empty!";
    return key;
}

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

void add_partition_config(AppData *app, const gchar *dev, const gchar *fs, const gchar *mp, gboolean fmt, gboolean encrypt, const gchar *pass) {
    PartitionConfig *conf = g_new(PartitionConfig, 1);
    conf->device = g_strdup(dev);
    conf->original_device = g_strdup(dev);
    conf->luks_uuid = NULL;
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
            get_part_loc("error_no_disk", app));
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        return;
    }

    const char *title = edit_conf ? get_part_loc("title_edit", app) : get_part_loc("title_add", app);
    const char *btn_label = edit_conf ? get_part_loc("update", app) : get_part_loc("add", app);

    GtkWidget *dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(app->window),
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 get_part_loc("cancel", app), GTK_RESPONSE_CANCEL,
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
    gtk_box_pack_start(GTK_BOX(h_dev), gtk_label_new(get_part_loc("select_partition", app)), FALSE, FALSE, 0);
    
    GtkComboBoxText *combo_part = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    populate_partitions_combo(combo_part, app->selected_disk);
    
    gtk_box_pack_start(GTK_BOX(h_dev), GTK_WIDGET(combo_part), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_gen), h_dev, FALSE, FALSE, 0);

    // ... Filesystem ...
    GtkWidget *h_fs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(h_fs), gtk_label_new(get_part_loc("filesystem", app)), FALSE, FALSE, 0);
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
    gtk_box_pack_start(GTK_BOX(h_mp), gtk_label_new(get_part_loc("mount_point", app)), FALSE, FALSE, 0);
    GtkWidget *entry_mp_w = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry_mp_w), "/");
    gtk_box_pack_start(GTK_BOX(h_mp), entry_mp_w, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_gen), h_mp, FALSE, FALSE, 0);

    // ... Format ...
    GtkCheckButton *chk_fmt = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(get_part_loc("format_partition", app)));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_fmt), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox_gen), GTK_WIDGET(chk_fmt), FALSE, FALSE, 0);

    // --- Tab 2: Encryption ---
    GtkWidget *vbox_enc = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_enc), 10);

    GtkCheckButton *chk_encrypt = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(get_part_loc("encrypt_luks", app)));
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

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_gen, gtk_label_new(get_part_loc("general", app)));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_enc, gtk_label_new(get_part_loc("encryption", app)));

    // PRE-FILL IF EDITING
    if (edit_conf) {
        gtk_widget_set_sensitive(GTK_WIDGET(combo_part), FALSE);
        
        const char *short_dev = edit_conf->device;
        if (strncmp(short_dev, "/dev/", 5) == 0) short_dev += 5;
        
        gint n_items = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_part));
        gint found_idx = -1;
        
        for (gint i = 0; i < 20; i++) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo_part), i);
            gchar *text = gtk_combo_box_text_get_active_text(combo_part);
            if (text) {
                if (strcmp(text, short_dev) == 0) {
                    found_idx = i;
                    g_free(text);
                    break;
                }
                g_free(text);
            } else {
                break;
            }
        }
        
        if (found_idx >= 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo_part), found_idx);
        } else {
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo_part), 0);
        }
        
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
                get_part_loc("error_pass_mismatch", app));
             gtk_dialog_run(GTK_DIALOG(err));
             gtk_widget_destroy(err);
             gtk_widget_destroy(dialog);
             g_free(dev_short);
             g_free(fs);
             return;
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
                if (edit_conf->original_device) g_free(edit_conf->original_device);
                if (edit_conf->luks_uuid) g_free(edit_conf->luks_uuid);
                
                edit_conf->device = g_strdup(full_dev);
                edit_conf->original_device = g_strdup(full_dev);
                edit_conf->fstype = g_strdup(fs);
                edit_conf->mountpoint = g_strdup(mp);
                edit_conf->format = fmt;
                edit_conf->encrypt = encrypt;
                edit_conf->luks_pass = (encrypt && pass && strlen(pass) > 0) ? g_strdup(pass) : NULL;
                
                refresh_partition_list_ui(app);
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
    
    if (!app->selected_disk) {
        GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(app->window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Please select a disk first!");
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        return;
    }
    
    if (!app->part_config_list) {
        populate_defaults(app, app->selected_disk);
        refresh_partition_list_ui(app);
    }
    
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->mount_list));
    GtkTreeModel *model;
    GtkTreeIter iter;
    
    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        gchar *dev;
        gtk_tree_model_get(model, &iter, 0, &dev, -1);
        
        PartitionConfig *conf = find_config_by_device(app, dev);
        if (conf) {
            show_partition_dialog(app, conf);
        } else {
            // Fallback: edit first partition
            conf = (PartitionConfig*)app->part_config_list->data;
            show_partition_dialog(app, conf);
        }
        g_free(dev);
    } else {
        // Edit first partition in the list
        if (app->part_config_list) {
            PartitionConfig *conf = (PartitionConfig*)app->part_config_list->data;
            show_partition_dialog(app, conf);
        }
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
            gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
            remove_partition_config(app, conf);
        }
        g_free(dev);
    }
}

void on_reset_partitions_clicked(GtkWidget *widget, AppData *app) {
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

void init_existing_partitions_view(AppData *app) {
    if (!app->existing_part_list) return;

    GtkListStore *store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(app->existing_part_list), GTK_TREE_MODEL(store));

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Device", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->existing_part_list), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Size", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->existing_part_list), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("FS Type", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->existing_part_list), col);
}

void refresh_existing_partitions_ui(AppData *app) {
    if (!app->existing_part_list || !app->selected_disk) return;
    
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(app->existing_part_list)));
    gtk_list_store_clear(store);
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "lsblk -rn -o NAME,SIZE,FSTYPE /dev/%s 2>/dev/null | grep -E '^%s[0-9]'", app->selected_disk, app->selected_disk);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char name[64], size[32], fstype[32];
        if (sscanf(line, "%63s %31s %31s", name, size, fstype) >= 2) {
            char dev_path[128];
            snprintf(dev_path, sizeof(dev_path), "/dev/%s", name);
            
            if (strlen(fstype) == 0 || strcmp(fstype, "-") == 0 || strlen(fstype) > 16) {
                strcpy(fstype, "-");
            }
            
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, dev_path, 1, size, 2, fstype, -1);
        }
    }
    pclose(fp);
}
