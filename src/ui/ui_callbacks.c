/*
 * ui_callbacks.c
 * General UI Event Handlers
 */
#include "neko_installer.h"
#include "country_data.h"
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

void on_next_clicked(GtkWidget *widget, AppData *app) { gtk_notebook_next_page(GTK_NOTEBOOK(app->notebook)); }
void on_back_clicked(GtkWidget *widget, AppData *app) { gtk_notebook_prev_page(GTK_NOTEBOOK(app->notebook)); }
void on_reboot_clicked(GtkWidget *widget, AppData *app) { system("reboot"); }

void on_popup_reboot(GtkDialog *dialog, gint response_id, gpointer user_data) {
    system("reboot");
}

void on_page_changed(GtkNotebook *notebook, GtkWidget *page, guint page_num, AppData *app) {
    gtk_widget_set_sensitive(app->btn_back, (page_num > 0));
    gtk_widget_set_sensitive(app->btn_next, (page_num < 6));
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
            
            // Populate defaults for Erase mode anyway
            populate_defaults(app, app->selected_disk);
            
        } else {
            // No NTFS -> Default Erase
            populate_defaults(app, app->selected_disk);
        }
        
        // Refresh the existing partitions view
        refresh_existing_partitions_ui(app);
    }
}

void on_country_changed(GtkComboBox *widget, AppData *app) {
    gchar *country_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
    if (!country_name) return;
    
    const CountryInfo *info = find_country_by_name(country_name);
    g_free(country_name);
    if (!info) return;
    
    // --- Set Locale ---
    // Find the matching locale in the locale combo box
    GtkTreeModel *locale_model = gtk_combo_box_get_model(GTK_COMBO_BOX(app->locale_combo));
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(locale_model, &iter);
    int idx = 0;
    while (valid) {
        gchar *locale_text;
        gtk_tree_model_get(locale_model, &iter, 0, &locale_text, -1);
        if (locale_text && strcmp(locale_text, info->locale) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(app->locale_combo), idx);
            g_free(locale_text);
            break;
        }
        g_free(locale_text);
        valid = gtk_tree_model_iter_next(locale_model, &iter);
        idx++;
    }
    
    // --- Set Timezone Area ---
    GtkTreeModel *area_model = gtk_combo_box_get_model(GTK_COMBO_BOX(app->tz_area_combo));
    valid = gtk_tree_model_get_iter_first(area_model, &iter);
    idx = 0;
    while (valid) {
        gchar *area_text;
        gtk_tree_model_get(area_model, &iter, 0, &area_text, -1);
        if (area_text && strcmp(area_text, info->tz_area) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(app->tz_area_combo), idx);
            g_free(area_text);
            break;
        }
        g_free(area_text);
        valid = gtk_tree_model_iter_next(area_model, &iter);
        idx++;
    }
    
    // --- Set Timezone City ---
    // Wait for area change to populate cities, then set city
    // The on_timezone_area_changed callback will fire from the area set above,
    // repopulating the city combo. We need to set city after that.
    // Use g_idle_add to defer city selection after GTK processes the area change.
    gchar *city_copy = g_strdup(info->tz_city);
    
    // Try to set city directly (works if area change already happened)
    GtkTreeModel *city_model = gtk_combo_box_get_model(GTK_COMBO_BOX(app->tz_city_combo));
    valid = gtk_tree_model_get_iter_first(city_model, &iter);
    idx = 0;
    gboolean found = FALSE;
    while (valid) {
        gchar *city_text;
        gtk_tree_model_get(city_model, &iter, 0, &city_text, -1);
        if (city_text && strcmp(city_text, city_copy) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(app->tz_city_combo), idx);
            g_free(city_text);
            found = TRUE;
            break;
        }
        g_free(city_text);
        valid = gtk_tree_model_iter_next(city_model, &iter);
        idx++;
    }
    
    if (!found) {
        // City not found (might be a compound like "Argentina/Buenos_Aires")
        // Try matching just the last component
        const char *slash = strrchr(city_copy, '/');
        const char *city_short = slash ? slash + 1 : city_copy;
        
        valid = gtk_tree_model_get_iter_first(city_model, &iter);
        idx = 0;
        while (valid) {
            gchar *city_text;
            gtk_tree_model_get(city_model, &iter, 0, &city_text, -1);
            if (city_text && strcmp(city_text, city_short) == 0) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(app->tz_city_combo), idx);
                g_free(city_text);
                break;
            }
            g_free(city_text);
            valid = gtk_tree_model_iter_next(city_model, &iter);
            idx++;
        }
    }
    g_free(city_copy);
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

void on_insert_text_username(GtkEditable *editable, gchar *new_text, gint new_text_length, gint *position, gpointer data) {
    gchar *lower_text = g_utf8_strdown(new_text, new_text_length);
    
    if (g_strcmp0(lower_text, new_text) != 0) {
        g_signal_handlers_block_by_func(editable, on_insert_text_username, data);
        gtk_editable_insert_text(editable, lower_text, -1, position);
        g_signal_handlers_unblock_by_func(editable, on_insert_text_username, data);
        g_signal_stop_emission_by_name(editable, "insert-text");
    }
    g_free(lower_text);
}

void launch_gparted(GtkWidget *widget, AppData *app) {
    if (!app->selected_disk) return;
    gchar *cmd = g_strdup_printf("gparted /dev/%s", app->selected_disk);
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
}

void on_manual_check_toggled(GtkToggleButton *toggle, AppData *app) {
    gboolean manual = gtk_toggle_button_get_active(toggle);
    gtk_widget_set_sensitive(app->frame_install_parts, manual);
    
    if (!manual && app->selected_disk) {
        // Switching back to auto: reset to defaults
        populate_defaults(app, app->selected_disk);
        refresh_partition_list_ui(app);
    }
}

void on_install_type_changed(GtkToggleButton *toggle, AppData *app) {
    // Only react when a radio is activated (not deactivated)
    if (!gtk_toggle_button_get_active(toggle)) return;
    
    int lang = app->current_lang;
    
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->radio_clean_install))) {
        app->install_mode = INSTALL_MODE_ERASE;
        gtk_label_set_text(GTK_LABEL(app->lbl_install_type_desc), get_loc("clean_install_desc", lang));
    } else {
        app->install_mode = INSTALL_MODE_DUAL_BOOT;
        gtk_label_set_text(GTK_LABEL(app->lbl_install_type_desc), get_loc("alongside_desc", lang));
    }
    
    // Re-populate defaults if a disk is selected
    if (app->selected_disk) {
        populate_defaults(app, app->selected_disk);
        refresh_partition_list_ui(app);
    }
}
