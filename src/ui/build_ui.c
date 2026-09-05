#include "neko_installer.h"
#include "ui_internal.h"
#include "lang.h"
#include "country_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//custom themes
void build_ui(AppData *app) {
    load_custom_css();
    app->current_lang = 0; // Default EN

    // Initialize language module
    extern void lang_init(void);
    lang_init();

    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), get_loc("window_title", 0));
    gtk_window_set_default_size(GTK_WINDOW(app->window), 800, 480);
    gtk_window_set_resizable(GTK_WINDOW(app->window), TRUE);
    gtk_window_set_position(GTK_WINDOW(app->window), GTK_WIN_POS_CENTER);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Allow minimum window size
    gtk_widget_set_size_request(app->window, 640, 400);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    app->notebook = gtk_notebook_new();
    gtk_notebook_set_show_border(GTK_NOTEBOOK(app->notebook), FALSE);
    gtk_widget_set_margin_start(app->notebook, 10);
    gtk_widget_set_margin_end(app->notebook, 10);
    gtk_widget_set_margin_top(app->notebook, 10);
    gtk_box_pack_start(GTK_BOX(vbox), app->notebook, TRUE, TRUE, 0);

    //WELCOME
    GtkWidget *page_welcome = create_welcome_page(app);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_welcome, gtk_label_new("Welcome"));

    // --- TAB 1: INSTALLATION TYPE ---
    GtkWidget *page_install_type = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(page_install_type), 10);
    gtk_widget_set_vexpand(page_install_type, TRUE);

    app->lbl_install_type_title = gtk_label_new("Select installation type:");
    gtk_label_set_xalign(GTK_LABEL(app->lbl_install_type_title), 0.0);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(app->lbl_install_type_title), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(page_install_type), app->lbl_install_type_title, FALSE, FALSE, 0);

    app->radio_clean_install = gtk_radio_button_new_with_label(NULL, "Clean Install (erase entire disk)");
    gtk_box_pack_start(GTK_BOX(page_install_type), app->radio_clean_install, FALSE, FALSE, 0);

    app->radio_alongside = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(app->radio_clean_install), "Install alongside another OS (resize)");
    gtk_box_pack_start(GTK_BOX(page_install_type), app->radio_alongside, FALSE, FALSE, 0);

    app->lbl_install_type_desc = gtk_label_new("All data on the selected disk will be erased and partitions will be created automatically.");
    gtk_label_set_xalign(GTK_LABEL(app->lbl_install_type_desc), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(app->lbl_install_type_desc), TRUE);
    gtk_widget_set_margin_top(app->lbl_install_type_desc, 5);
    gtk_box_pack_start(GTK_BOX(page_install_type), app->lbl_install_type_desc, FALSE, FALSE, 0);

    g_signal_connect(app->radio_clean_install, "toggled", G_CALLBACK(on_install_type_changed), app);
    g_signal_connect(app->radio_alongside, "toggled", G_CALLBACK(on_install_type_changed), app);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_install_type, gtk_label_new("Installation Type"));

    // --- TAB 2: PARTITIONS (Split View) ---
    GtkWidget *page_disk = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(page_disk), 8);
    gtk_widget_set_vexpand(page_disk, TRUE);

    // Disk selector row
    GtkWidget *hbox_disk = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
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

    app->lbl_disk_title = gtk_label_new("Disk:");
    gtk_box_pack_start(GTK_BOX(hbox_disk), app->lbl_disk_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_disk), app->disk_combo, TRUE, TRUE, 0);

    app->btn_part_gparted = gtk_button_new_with_label("Partition (GParted)");
    g_signal_connect(app->btn_part_gparted, "clicked", G_CALLBACK(launch_gparted), app);
    gtk_box_pack_start(GTK_BOX(hbox_disk), app->btn_part_gparted, FALSE, FALSE, 0);

    app->lbl_disk_info = gtk_label_new("Note: You can modify the partitions with GParted and then configure them below.");
    gtk_label_set_line_wrap(GTK_LABEL(app->lbl_disk_info), TRUE);
    gtk_label_set_xalign(GTK_LABEL(app->lbl_disk_info), 0.0);
    gtk_box_pack_start(GTK_BOX(page_disk), app->lbl_disk_info, FALSE, FALSE, 0);

    // ===== FRAME 1: Existing Partitions (read-only) =====
    app->frame_existing = gtk_frame_new(get_loc("existing_parts", app->current_lang));
    gtk_box_pack_start(GTK_BOX(page_disk), app->frame_existing, TRUE, TRUE, 0);

    app->existing_part_list = gtk_tree_view_new();
    init_existing_partitions_view(app);

    GtkWidget *scroll_existing = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll_existing), 60);
    gtk_widget_set_vexpand(scroll_existing, TRUE);
    gtk_container_add(GTK_CONTAINER(scroll_existing), app->existing_part_list);
    gtk_container_add(GTK_CONTAINER(app->frame_existing), scroll_existing);

    // ===== MANUAL CHECKBOX =====
    app->chk_manual_partitions = gtk_check_button_new_with_label(get_loc("manual_partitioning", app->current_lang));
    g_signal_connect(app->chk_manual_partitions, "toggled", G_CALLBACK(on_manual_check_toggled), app);
    gtk_box_pack_start(GTK_BOX(page_disk), app->chk_manual_partitions, FALSE, FALSE, 0);

    // ===== FRAME 2: Installation Partitions (editable) =====
    app->frame_install_parts = gtk_frame_new(get_loc("install_parts", app->current_lang));
    gtk_box_pack_start(GTK_BOX(page_disk), app->frame_install_parts, TRUE, TRUE, 0);

    GtkWidget *vbox_install_parts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_install_parts), 5);
    gtk_container_add(GTK_CONTAINER(app->frame_install_parts), vbox_install_parts);

    // Buttons row
    GtkWidget *hbox_pm = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    app->lbl_part_mount = gtk_label_new("Mount Points:");
    gtk_box_pack_start(GTK_BOX(hbox_pm), app->lbl_part_mount, FALSE, FALSE, 0);

    app->btn_part_add = gtk_button_new_with_label("Add");
    g_signal_connect(app->btn_part_add, "clicked", G_CALLBACK(on_add_partition_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_pm), app->btn_part_add, FALSE, FALSE, 0);

    app->btn_part_edit = gtk_button_new_with_label("Edit");
    g_signal_connect(app->btn_part_edit, "clicked", G_CALLBACK(on_edit_partition_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_pm), app->btn_part_edit, FALSE, FALSE, 0);

    app->btn_part_del = gtk_button_new_with_label("Delete");
    g_signal_connect(app->btn_part_del, "clicked", G_CALLBACK(on_delete_partition_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_pm), app->btn_part_del, FALSE, FALSE, 0);

    app->btn_part_reset = gtk_button_new_with_label("Reset mount points");
    g_signal_connect(app->btn_part_reset, "clicked", G_CALLBACK(on_reset_partitions_clicked), app);
    gtk_box_pack_start(GTK_BOX(hbox_pm), app->btn_part_reset, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox_install_parts), hbox_pm, FALSE, FALSE, 0);

    app->mount_list = gtk_tree_view_new();
    open_partition_manager(app->mount_list, app);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 60);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), app->mount_list);
    gtk_box_pack_start(GTK_BOX(vbox_install_parts), scrolled, TRUE, TRUE, 0);

    // Start with manual mode DISABLED (auto-partitioning default)
    gtk_widget_set_sensitive(app->frame_install_parts, FALSE);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_disk, gtk_label_new("Partitions"));



    // --- TAB 2: BOOTLOADER ---
    GtkWidget *page_boot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(page_boot), 15);

    app->lbl_boot_status = gtk_label_new("Detecting firmware...");
    app->label_boot_status = app->lbl_boot_status;
    gtk_box_pack_start(GTK_BOX(page_boot), app->lbl_boot_status, FALSE, FALSE, 0);

    GtkWidget *hbox_grub = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    app->lbl_grub_install = gtk_label_new("Install GRUB to:");
    gtk_box_pack_start(GTK_BOX(hbox_grub), app->lbl_grub_install, FALSE, FALSE, 0);

    app->grub_disk_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(hbox_grub), app->grub_disk_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page_boot), hbox_grub, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_boot, gtk_label_new("Bootloader"));

    // --- TAB 3: SYSTEM CONFIG ---
    GtkWidget *page_sys = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(page_sys), 10);
    gtk_widget_set_vexpand(page_sys, TRUE);

    // Hostname
    gtk_box_pack_start(GTK_BOX(page_sys), create_form_row("Hostname:", &app->hostname_entry, &app->lbl_hostname), FALSE, FALSE, 0);
    gtk_entry_set_text(GTK_ENTRY(app->hostname_entry), "neko-void");
    g_signal_connect(app->hostname_entry, "insert-text", G_CALLBACK(on_insert_text_hostname), NULL);

    // Country
    {
        GtkWidget *vbox_country = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_bottom(vbox_country, 10);

        app->lbl_country = gtk_label_new("Country:");
        gtk_label_set_xalign(GTK_LABEL(app->lbl_country), 0.5);

        app->country_combo = gtk_combo_box_text_new();

        int country_count = 0;
        const CountryInfo *countries = get_country_list(&country_count);
        for (int i = 0; i < country_count; i++) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->country_combo), countries[i].name);
        }
        // NOTE: Don't set active here — timezone widgets don't exist yet.
        // Initial selection is done at end of build_ui().

        g_signal_connect(app->country_combo, "changed", G_CALLBACK(on_country_changed), app);

        gtk_box_pack_start(GTK_BOX(vbox_country), app->lbl_country, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox_country), app->country_combo, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(page_sys), vbox_country, FALSE, FALSE, 0);
    }
    // Locale
    GtkWidget *vbox_loc = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_bottom(vbox_loc, 10);

    app->lbl_locale = gtk_label_new("Locale:");
    gtk_label_set_xalign(GTK_LABEL(app->lbl_locale), 0.5);

    app->locale_combo = gtk_combo_box_text_new();
    // Dynamically load all UTF-8 locales from the system
    {
        FILE *fp = fopen("/etc/default/libc-locales", "r");
        int count = 0;
        int default_idx = 0;
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (line[0] == '\n' || line[0] == '\0') continue;
                if (!strstr(line, "UTF-8")) continue;
                char *p = line;
                while (*p == '#' || *p == ' ' || *p == '\t') p++;
                char locale_name[128];
                int i = 0;
                while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i < (int)sizeof(locale_name) - 1) {
                    locale_name[i++] = *p++;
                }
                locale_name[i] = '\0';
                if (i == 0) continue;
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->locale_combo), locale_name);
                if (g_strcmp0(locale_name, "en_US.UTF-8") == 0) {
                    default_idx = count;
                }
                count++;
            }
            fclose(fp);
        }
        if (count == 0) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->locale_combo), "en_US.UTF-8");
            default_idx = 0;
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->locale_combo), default_idx);
    }
    gtk_box_pack_start(GTK_BOX(vbox_loc), app->lbl_locale, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_loc), app->locale_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page_sys), vbox_loc, FALSE, FALSE, 0);

    // Region
    GtkWidget *vbox_tz = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_bottom(vbox_tz, 10);

    app->lbl_region = gtk_label_new("Region:");
    gtk_label_set_xalign(GTK_LABEL(app->lbl_region), 0.5);

    GtkWidget *hbox_tz_combos = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    app->tz_area_combo = gtk_combo_box_text_new();
    const char *areas[] = { "Africa", "America", "Antarctica", "Arctic", "Asia", "Atlantic", "Australia", "Europe", "Indian", "Pacific", NULL };
    for (int i=0; areas[i]; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->tz_area_combo), areas[i]);
    g_signal_connect(app->tz_area_combo, "changed", G_CALLBACK(on_timezone_area_changed), app);

    app->tz_city_combo = gtk_combo_box_text_new();

    gtk_box_pack_start(GTK_BOX(hbox_tz_combos), app->tz_area_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_tz_combos), app->tz_city_combo, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox_tz), app->lbl_region, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_tz), hbox_tz_combos, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page_sys), vbox_tz, FALSE, FALSE, 0);

    // Keyboard Layout
    {
        GtkWidget *vbox_kbd = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_bottom(vbox_kbd, 10);

        app->lbl_kbd_layout = gtk_label_new("Keyboard Layout:");
        gtk_label_set_xalign(GTK_LABEL(app->lbl_kbd_layout), 0.5);

        app->kbd_layout_combo = gtk_combo_box_text_new();
        int kbd_count = 0;
        const KbdLayout *layouts = get_keyboard_layouts(&kbd_count);
        for (int i = 0; i < kbd_count; i++) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->kbd_layout_combo), layouts[i].name);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->kbd_layout_combo), 0); // Default: English (US)
        g_signal_connect(app->kbd_layout_combo, "changed", G_CALLBACK(on_kbd_layout_changed), app);

        gtk_box_pack_start(GTK_BOX(vbox_kbd), app->lbl_kbd_layout, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox_kbd), app->kbd_layout_combo, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(page_sys), vbox_kbd, FALSE, FALSE, 0);
    }

    // Keyboard Variant
    {
        GtkWidget *vbox_kvar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_bottom(vbox_kvar, 10);

        app->lbl_kbd_variant = gtk_label_new("Variant:");
        gtk_label_set_xalign(GTK_LABEL(app->lbl_kbd_variant), 0.5);

        app->kbd_variant_combo = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->kbd_variant_combo), "Default");
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->kbd_variant_combo), 0);

        gtk_box_pack_start(GTK_BOX(vbox_kvar), app->lbl_kbd_variant, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox_kvar), app->kbd_variant_combo, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(page_sys), vbox_kvar, FALSE, FALSE, 0);
    }

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_sys, gtk_label_new("System"));

    #ifdef HAS_DESKTOP_TAB
    // --- TAB 5: DESKTOP ---
    {
        GtkWidget *page_desktop = create_desktop_page(app);
        gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_desktop, gtk_label_new("Desktop"));
    }
    #endif

    // --- TAB 5/6: USERS ---
    GtkWidget *page_user = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(page_user), 10);
    gtk_widget_set_vexpand(page_user, TRUE);


    // Full Name
    gtk_box_pack_start(GTK_BOX(page_user), create_vertical_input("Full Name:", &app->user_fullname_entry, &app->lbl_fullname), FALSE, FALSE, 0);

    // Username
    gtk_box_pack_start(GTK_BOX(page_user), create_vertical_input("Username:", &app->user_login_entry, &app->lbl_username), FALSE, FALSE, 0);
    g_signal_connect(app->user_login_entry, "insert-text", G_CALLBACK(on_insert_text_username), NULL);

    // Password
    gtk_box_pack_start(GTK_BOX(page_user), create_vertical_input("Password:", &app->user_pass_entry, &app->lbl_user_pass), FALSE, FALSE, 0);
    gtk_entry_set_visibility(GTK_ENTRY(app->user_pass_entry), FALSE);

    // Confirm
    gtk_box_pack_start(GTK_BOX(page_user), create_vertical_input("Confirm:", &app->user_pass_confirm_entry, &app->lbl_user_confirm), FALSE, FALSE, 0);
    gtk_entry_set_visibility(GTK_ENTRY(app->user_pass_confirm_entry), FALSE);

    // Root Password
    gtk_box_pack_start(GTK_BOX(page_user), create_vertical_input("Root Password:", &app->root_pass_entry, &app->lbl_root_pass), FALSE, FALSE, 0);
    gtk_entry_set_visibility(GTK_ENTRY(app->root_pass_entry), FALSE);

    // Autologin
    app->autologin_check = gtk_check_button_new_with_label("Enable Auto-Login");
    app->chk_autologin = app->autologin_check;
    gtk_widget_set_halign(app->autologin_check, GTK_ALIGN_CENTER);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->autologin_check), FALSE);
    gtk_box_pack_start(GTK_BOX(page_user), app->autologin_check, FALSE, FALSE, 10);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_user, gtk_label_new("Users"));

    // --- TAB 5: SECURITY ---
    GtkWidget *page_sec = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_container_set_border_width(GTK_CONTAINER(page_sec), 20);

    // Title
    app->lbl_priv_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(app->lbl_priv_title),
                         "<b><span size='large'>Privilege Escalation</span></b>");
    gtk_widget_set_halign(app->lbl_priv_title, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(app->lbl_priv_title, 5);
    gtk_box_pack_start(GTK_BOX(page_sec), app->lbl_priv_title, FALSE, FALSE, 0);

    // Subtitle / description
    app->lbl_priv_desc = gtk_label_new("Choose between traditional sudo or the lightweight doas:");
    gtk_widget_set_halign(app->lbl_priv_desc, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(app->lbl_priv_desc, 20);
    gtk_box_pack_start(GTK_BOX(page_sec), app->lbl_priv_desc, FALSE, FALSE, 0);

    // sudo option
    {
        GtkWidget *frame = gtk_frame_new(NULL);
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(hbox), 12);

        app->radio_priv_sudo = gtk_radio_button_new_with_label(NULL, "sudo");
        app->lbl_priv_sudo = app->radio_priv_sudo;

        app->lbl_priv_sudo_desc = gtk_label_new("Standard tool. Widely compatible, complex codebase.");
        gtk_label_set_xalign(GTK_LABEL(app->lbl_priv_sudo_desc), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(app->lbl_priv_sudo_desc), TRUE);
        gtk_widget_set_hexpand(app->lbl_priv_sudo_desc, TRUE);

        gtk_box_pack_start(GTK_BOX(hbox), app->radio_priv_sudo, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), app->lbl_priv_sudo_desc, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(frame), hbox);
        gtk_box_pack_start(GTK_BOX(page_sec), frame, FALSE, FALSE, 0);
    }

    // doas option
    {
        GtkWidget *frame = gtk_frame_new(NULL);
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(hbox), 12);

        app->radio_priv_doas = gtk_radio_button_new_with_label_from_widget(
            GTK_RADIO_BUTTON(app->radio_priv_sudo), "doas");
        app->lbl_priv_doas = app->radio_priv_doas;

        app->lbl_priv_doas_desc = gtk_label_new("Minimal alternative. Simpler, fewer attack vectors. Recommended.");
        gtk_label_set_xalign(GTK_LABEL(app->lbl_priv_doas_desc), 0.0);
        gtk_label_set_line_wrap(GTK_LABEL(app->lbl_priv_doas_desc), TRUE);
        gtk_widget_set_hexpand(app->lbl_priv_doas_desc, TRUE);

        gtk_box_pack_start(GTK_BOX(hbox), app->radio_priv_doas, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), app->lbl_priv_doas_desc, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(frame), hbox);
        gtk_box_pack_start(GTK_BOX(page_sec), frame, FALSE, FALSE, 0);
    }

    // Default: sudo
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->radio_priv_doas), TRUE);
    //gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->radio_priv_sudo), TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_sec, gtk_label_new("Security"));

    // --- TAB 6: INSTALL ---
    GtkWidget *page_inst = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(page_inst), 10);

    app->console_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->console_text), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app->console_text), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->console_text), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->console_text), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->console_text), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->console_text), 6);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->console_text), 6);
    gtk_widget_set_name(app->console_text, "console_view");

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    app->console_scroll = scroll;
    gtk_widget_set_name(scroll, "console_scroll");
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_NONE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 120);
    gtk_container_add(GTK_CONTAINER(scroll), app->console_text);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(page_inst), scroll, TRUE, TRUE, 0);

    app->progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(page_inst), app->progress_bar, FALSE, FALSE, 0);

    app->btn_install = gtk_button_new_with_label("Start Installation");
    gtk_widget_set_size_request(app->btn_install, -1, 40);
    gtk_widget_set_sensitive(app->btn_install, FALSE); // disabled until all tabs are valid
    GtkStyleContext *ctx = gtk_widget_get_style_context(app->btn_install);
    gtk_style_context_add_class(ctx, "destructive-action");

    g_signal_connect(app->btn_install, "clicked", G_CALLBACK(start_installation), app);
    gtk_box_pack_start(GTK_BOX(page_inst), app->btn_install, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_inst, gtk_label_new("Install"));

    // --- BOTTOM NAV ---
    GtkWidget *hbox_nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(hbox_nav), 8);

    app->btn_back = gtk_button_new_with_label("Back");
    gtk_widget_set_sensitive(app->btn_back, FALSE);
    g_signal_connect(app->btn_back, "clicked", G_CALLBACK(on_back_clicked), app);

    app->btn_next = gtk_button_new_with_label("Next");
    g_signal_connect(app->btn_next, "clicked", G_CALLBACK(on_next_clicked), app);

    gtk_box_pack_start(GTK_BOX(hbox_nav), app->btn_back, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_nav), app->btn_next, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), hbox_nav, FALSE, FALSE, 0);

    g_signal_connect(app->notebook, "switch-page", G_CALLBACK(on_page_changed), app);

    // INITIAL LOCALIZE
    update_ui_language(app);

    // Set initial country selection (must be after all widgets are built)
    {
        int country_count = 0;
        const CountryInfo *countries = get_country_list(&country_count);
        for (int i = 0; i < country_count; i++) {
            if (strcmp(countries[i].name, "United States") == 0) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(app->country_combo), i);
                break;
            }
        }
    }

    gtk_widget_show_all(app->window);

}
