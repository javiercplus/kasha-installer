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


void on_reboot_clicked(GtkWidget *widget, AppData *app) { (void)widget; (void)app; system("reboot"); }

void on_popup_reboot(GtkDialog *dialog, gint response_id, gpointer user_data) {
    (void)dialog; (void)response_id; (void)user_data;
    system("reboot");
}

// Forward declaration
static void update_nav_buttons(AppData *app, guint page_num);

// Returns NULL if the current tab is valid, or a human-readable error message.
static const char* validate_tab(AppData *app, guint page_num) {
    int lang = app->current_lang;
    switch (page_num) {

    case 2: // Partitions — disk must be selected and a root partition must exist
        if (!app->selected_disk || strlen(app->selected_disk) == 0)
            return get_loc("val_select_disk", lang);
        {
            gboolean has_root = FALSE;
            GSList *l = app->part_config_list;
            while (l) {
                PartitionConfig *cfg = (PartitionConfig *)l->data;
                if (cfg->mountpoint && strcmp(cfg->mountpoint, "/") == 0)
                    has_root = TRUE;
                l = l->next;
            }
            if (!has_root)
                return get_loc("val_no_root", lang);
        }
        break;

    case 3: // Bootloader — GRUB disk must be selected
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(app->grub_disk_combo)) < 0)
            return get_loc("val_grub_disk", lang);
        break;

    case 4: // System — hostname must not be empty
        {
            const gchar *hostname = gtk_entry_get_text(GTK_ENTRY(app->hostname_entry));
            if (!hostname || strlen(g_strstrip((gchar*)hostname)) == 0)
                return get_loc("val_hostname", lang);
        }
        break;

    case 5: // Users — username, password, confirm and root password must be filled
        {
            const gchar *username  = gtk_entry_get_text(GTK_ENTRY(app->user_login_entry));
            const gchar *password  = gtk_entry_get_text(GTK_ENTRY(app->user_pass_entry));
            const gchar *confirm   = gtk_entry_get_text(GTK_ENTRY(app->user_pass_confirm_entry));
            const gchar *root_pass = gtk_entry_get_text(GTK_ENTRY(app->root_pass_entry));

            if (!username || strlen(username) == 0)
                return get_loc("val_username", lang);
            if (!password || strlen(password) == 0)
                return get_loc("val_password", lang);
            if (!confirm  || strcmp(password, confirm) != 0)
                return get_loc("val_password_match", lang);
            if (!root_pass || strlen(root_pass) == 0)
                return get_loc("val_root_pass", lang);
        }
        break;

    default:
        break;
    }
    return NULL; // valid
}

static void show_validation_error(AppData *app, const char *msg) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_OK,
        "%s", msg);
    gtk_window_set_title(GTK_WINDOW(dialog), get_loc("val_incomplete", app->current_lang));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void on_next_clicked(GtkWidget *widget, AppData *app) {
    (void)widget;
    gint current = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    const char *err = validate_tab(app, (guint)current);
    if (err) {
        show_validation_error(app, err);
        return;
    }
    gtk_notebook_next_page(GTK_NOTEBOOK(app->notebook));
}

void on_back_clicked(GtkWidget *widget, AppData *app) {
    (void)widget;
    gtk_notebook_prev_page(GTK_NOTEBOOK(app->notebook));
}

static void update_nav_buttons(AppData *app, guint page_num) {
    // Back: disabled on first page
    gtk_widget_set_sensitive(app->btn_back, (page_num > 0));

    // Next: hidden/disabled on last page (Install tab)
    gint total = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    gboolean on_last = ((gint)page_num >= total - 1);
    gtk_widget_set_visible(app->btn_next, !on_last);

    // Install button: only enabled on the last tab AND all prior tabs valid
    if (on_last) {
        gboolean all_valid = TRUE;
        for (guint i = 2; i <= 5; i++) {
            if (validate_tab(app, i) != NULL) {
                all_valid = FALSE;
                break;
            }
        }
        gtk_widget_set_sensitive(app->btn_install, all_valid);
    } else {
        gtk_widget_set_sensitive(app->btn_install, FALSE);
    }
}

void on_page_changed(GtkNotebook *notebook, GtkWidget *page, guint page_num, AppData *app) {
    (void)notebook; (void)page;
    update_nav_buttons(app, page_num);
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

    // --- Set Keyboard Layout ---
    if (info->x11_layout) {
        const KbdLayout *kbd = find_keyboard_layout(info->x11_layout);
        if (kbd) {
            int kbd_count = 0;
            const KbdLayout *all_kbd = get_keyboard_layouts(&kbd_count);
            for (int i = 0; i < kbd_count; i++) {
                if (&all_kbd[i] == kbd) {
                    gtk_combo_box_set_active(GTK_COMBO_BOX(app->kbd_layout_combo), i);
                    break;
                }
            }
        }
    }
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

void on_kbd_layout_changed(GtkComboBox *widget, AppData *app) {
    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(app->kbd_layout_combo));
    if (active < 0) return;

    int count = 0;
    const KbdLayout *layouts = get_keyboard_layouts(&count);
    if (active >= count) return;

    const KbdLayout *layout = &layouts[active];

    // Repopulate variant combo
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->kbd_variant_combo));
    for (int i = 0; layout->variants[i].name != NULL; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->kbd_variant_combo),
                                        layout->variants[i].name);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->kbd_variant_combo), 0);
}

void on_insert_text_username(GtkEditable *editable, gchar *new_text, gint new_text_length, gint *position, gpointer data) {
    /* Convert entire input to lowercase first */
    gchar *lower_text = g_utf8_strdown(new_text, new_text_length);
    gint lower_len = (gint)strlen(lower_text);

    /* Get current content length to know if we're at position 0 */
    const gchar *current = gtk_entry_get_text(GTK_ENTRY(editable));
    gint current_len = (gint)strlen(current);
    gint insert_pos = *position;

    /* Filter: keep only valid Linux username characters [a-z0-9_-]
     * First character of the username must be [a-z_] */
    GString *filtered = g_string_sized_new(lower_len);
    const gchar *p = lower_text;
    while (*p) {
        gunichar ch = g_utf8_get_char(p);
        gboolean valid = FALSE;

        if ((ch >= 'a' && ch <= 'z') || ch == '_') {
            valid = TRUE;
        } else if (ch >= '0' && ch <= '9') {
            /* Digits allowed only if not the very first character of the username */
            if (current_len > 0 || filtered->len > 0 || insert_pos > 0) {
                valid = TRUE;
            }
        } else if (ch == '-') {
            /* Hyphen allowed only if not the very first character */
            if (current_len > 0 || filtered->len > 0 || insert_pos > 0) {
                valid = TRUE;
            }
        }

        if (valid) {
            g_string_append_unichar(filtered, ch);
        }
        p = g_utf8_next_char(p);
    }

    /* Always stop the original emission and insert our sanitized version */
    g_signal_stop_emission_by_name(editable, "insert-text");

    if (filtered->len > 0) {
        g_signal_handlers_block_by_func(editable, on_insert_text_username, data);
        gtk_editable_insert_text(editable, filtered->str, (gint)filtered->len, position);
        g_signal_handlers_unblock_by_func(editable, on_insert_text_username, data);
    }

    g_string_free(filtered, TRUE);
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
