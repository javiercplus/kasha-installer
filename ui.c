/*
 * ui.c
 * POPUP DIALOG IMPLEMENTATION FOR REBOOT
 */
#include "neko_installer.h"
#include <stdio.h>

void on_disk_changed(GtkComboBox *widget, AppData *app) {
    GtkTreeIter iter;
    if (gtk_combo_box_get_active_iter(widget, &iter)) {
        GtkTreeModel *model = gtk_combo_box_get_model(widget);
        gchar *disk_name;
        gtk_tree_model_get(model, &iter, 0, &disk_name, -1);
        app->selected_disk = disk_name; 
        g_print("Disk selected: %s\n", disk_name);
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
    gtk_widget_set_sensitive(app->btn_next, (page_num < 4));
}

void on_next_clicked(GtkWidget *widget, AppData *app) {
    gtk_notebook_next_page(GTK_NOTEBOOK(app->notebook));
}

void on_back_clicked(GtkWidget *widget, AppData *app) {
    gtk_notebook_prev_page(GTK_NOTEBOOK(app->notebook));
}

// LOGIC: Handle the Reboot Dialog Button Click
void on_reboot_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    if (response_id == GTK_RESPONSE_ACCEPT) {
        system("reboot");
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

// LOGIC: Show Reboot Dialog when finished
gboolean set_ui_finished_safe(gpointer data) {
    AppData *app = (AppData *)data;
    
    // Keep UI locked (Navigation disabled, Notebook locked)
    gtk_widget_set_sensitive(app->btn_back, FALSE);
    gtk_widget_set_sensitive(app->btn_next, FALSE);
    gtk_widget_set_sensitive(app->notebook, FALSE);
    
    // Create Popup Dialog
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,            // Bloquea la ventana principal
        GTK_MESSAGE_INFO,             // Icono de información
        GTK_BUTTONS_NONE,             // Sin botones estándar, añadiremos uno custom
        "NEKO-VOID is READY!!!"
    );
    
    // Add the Reboot button
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Reboot System", GTK_RESPONSE_ACCEPT);
    
    // Connect the button click event
    g_signal_connect(dialog, "response", G_CALLBACK(on_reboot_dialog_response), NULL);
    
    // Show the dialog
    gtk_widget_show_all(dialog);
    
    return FALSE;
}

void set_ui_finished(AppData *app) {
    g_idle_add(set_ui_finished_safe, app);
}

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

void build_ui(AppData *app) {
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Kasha Installer - Neko Void");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 850, 600);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); 
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    app->notebook = gtk_notebook_new();
    gtk_widget_set_margin_start(app->notebook, 10);
    gtk_widget_set_margin_end(app->notebook, 10);
    gtk_widget_set_margin_top(app->notebook, 10);
    gtk_box_pack_start(GTK_BOX(vbox), app->notebook, TRUE, TRUE, 0);

    g_signal_connect(app->notebook, "switch-page", G_CALLBACK(on_page_changed), app);

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
    
    gtk_box_pack_start(GTK_BOX(page_disk), gtk_label_new("IMPORTANT: Create a root partition (e.g., /dev/sda1) and EFI/BIOS partition if needed."), FALSE, FALSE, 0);
    app->mount_list = gtk_tree_view_new(); 
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
    gtk_box_pack_start(GTK_BOX(vbox_user), create_form_row("Login:", &app->user_login_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_user), create_form_row("Full Name:", &app->user_fullname_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_user), create_form_row("Password:", &app->user_pass_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_user), create_form_row("Confirm Password:", &app->user_pass_confirm_entry), FALSE, FALSE, 0);
    gtk_entry_set_visibility(GTK_ENTRY(app->user_pass_entry), FALSE);
    gtk_entry_set_visibility(GTK_ENTRY(app->user_pass_confirm_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(page_user), frame_user, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_user, gtk_label_new("4. Users"));

    // --- TAB 5: INSTALLATION ---
    GtkWidget *page_install = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(page_install), 10);
    
    PangoFontDescription *font_desc = pango_font_description_from_string("Monospace 10");
    
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    app->console_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->console_text), FALSE);
    gtk_widget_override_font(app->console_text, font_desc);
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

    pango_font_description_free(font_desc);
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
}