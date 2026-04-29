/*
 * ui.c
 * Main UI Construction
 */
#include "neko_installer.h"
#include "lang.h"
#include <stdio.h>
#include <stdlib.h>      
#include <string.h>
#include "logo.h"

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

// --- LOCALIZATION HELPER ---
const char* get_loc(const char *key, int lang) {
    // Get language from module
    extern const LangInfo* get_lang(const char *code);
    extern const char** get_available_langs(void);
    
    const char **langs = get_available_langs();
    lang = (lang >= 0) ? lang : 0;
    if (lang >= 3) lang = 0;  // safety
    
    const char *code = langs[lang];
    const LangInfo *li = get_lang(code);
    if (!li) return key;
    
    if (strcmp(key, "welcome_title") == 0) return li->welcome_title;
    if (strcmp(key, "welcome_body") == 0) return li->welcome_body;
    if (strcmp(key, "disk_title") == 0) return li->disk;
    if (strcmp(key, "disk_info") == 0) return li->disk_note;
    if (strcmp(key, "part_mount") == 0) return li->install_parts;
    if (strcmp(key, "btn_add") == 0) return li->btn_add;
    if (strcmp(key, "btn_edit") == 0) return li->btn_edit;
    if (strcmp(key, "btn_del") == 0) return li->btn_delete;
    if (strcmp(key, "btn_reset") == 0) return li->btn_reset;
    if (strcmp(key, "btn_gparted") == 0) return li->btn_gparted;
    
    if (strcmp(key, "tab_install_type") == 0) return li->tab_install_type;
    if (strcmp(key, "install_type_title") == 0) return li->install_type_title;
    if (strcmp(key, "clean_install") == 0) return li->clean_install;
    if (strcmp(key, "install_alongside") == 0) return li->install_alongside;
    if (strcmp(key, "clean_install_desc") == 0) return li->clean_install_desc;
    if (strcmp(key, "alongside_desc") == 0) return li->alongside_desc;
    if (strcmp(key, "manual_partitioning") == 0) return li->manual_partitioning;
    if (strcmp(key, "existing_parts") == 0) return li->existing_parts;
    if (strcmp(key, "install_parts") == 0) return li->install_parts;
    
    if (strcmp(key, "boot_detect") == 0) return li->boot_detect;
    if (strcmp(key, "grub_install") == 0) return li->grub_install;
    
    if (strcmp(key, "hostname") == 0) return li->hostname;
    if (strcmp(key, "locale") == 0) return li->locale;
    if (strcmp(key, "region") == 0) return li->region;
    if (strcmp(key, "city") == 0) return li->city;
    
    if (strcmp(key, "root_pass") == 0) return li->root_pass;
    if (strcmp(key, "user_acc_title") == 0) return li->user_account;
    if (strcmp(key, "fullname") == 0) return li->fullname;
    if (strcmp(key, "username") == 0) return li->username;
    if (strcmp(key, "password") == 0) return li->password;
    if (strcmp(key, "confirm") == 0) return li->confirm;
    if (strcmp(key, "autologin") == 0) return li->autologin;
    
    if (strcmp(key, "install_btn") == 0) return li->install_btn;
    if (strcmp(key, "reboot_btn") == 0) return li->reboot_btn;
    if (strcmp(key, "back") == 0) return li->back;
    if (strcmp(key, "next") == 0) return li->next;
    
    if (strcmp(key, "tab_welcome") == 0) return li->tab_welcome;
    if (strcmp(key, "tab_partitions") == 0) return li->tab_partitions;
    if (strcmp(key, "tab_bootloader") == 0) return li->tab_bootloader;
    if (strcmp(key, "tab_system") == 0) return li->tab_system;
    if (strcmp(key, "tab_users") == 0) return li->tab_users;
if (strcmp(key, "tab_install") == 0) return li->tab_install;
    if (strcmp(key, "welcome_title_markup") == 0) return li->welcome_title_markup;
    if (strcmp(key, "welcome_body_markup") == 0) return li->welcome_body_markup;
    if (strcmp(key, "part_mount") == 0) return li->part_mount;
    if (strcmp(key, "btn_del") == 0) return li->btn_delete;
    if (strcmp(key, "select_partition") == 0) return li->select_partition;
    if (strcmp(key, "filesystem") == 0) return li->filesystem;
    if (strcmp(key, "mount_point") == 0) return li->mount_point;
    if (strcmp(key, "format_partition") == 0) return li->format_partition;
    if (strcmp(key, "encrypt_luks") == 0) return li->encrypt_luks;
    if (strcmp(key, "general") == 0) return li->general;
    if (strcmp(key, "encryption") == 0) return li->encryption;
    if (strcmp(key, "dialog_add") == 0) return li->dialog_add;
    if (strcmp(key, "dialog_edit") == 0) return li->dialog_edit;
    if (strcmp(key, "dialog_update") == 0) return li->dialog_update;
    if (strcmp(key, "save") == 0) return li->save;
    if (strcmp(key, "cancel") == 0) return li->cancel;
    if (strcmp(key, "window_title") == 0) return li->window_title;
    
    return key;
}

void update_ui_language(AppData *app) {
    int lang = app->current_lang;
    
    // Welcome
    if(app->lbl_welcome_title) gtk_label_set_markup(GTK_LABEL(app->lbl_welcome_title), get_loc("welcome_title", lang));
    if(app->lbl_welcome_body) gtk_label_set_markup(GTK_LABEL(app->lbl_welcome_body), get_loc("welcome_body", lang));
    
    // Install Type
    if(app->lbl_install_type_title) gtk_label_set_text(GTK_LABEL(app->lbl_install_type_title), get_loc("install_type_title", lang));
    if(app->radio_clean_install) gtk_button_set_label(GTK_BUTTON(app->radio_clean_install), get_loc("clean_install", lang));
    if(app->radio_alongside) gtk_button_set_label(GTK_BUTTON(app->radio_alongside), get_loc("install_alongside", lang));
    if(app->lbl_install_type_desc) gtk_label_set_text(GTK_LABEL(app->lbl_install_type_desc), get_loc("clean_install_desc", lang));

    // Disks
    if(app->lbl_disk_title) gtk_label_set_text(GTK_LABEL(app->lbl_disk_title), get_loc("disk_title", lang));
    if(app->lbl_disk_info) gtk_label_set_text(GTK_LABEL(app->lbl_disk_info), get_loc("disk_info", lang));
    if(app->lbl_part_mount) gtk_label_set_text(GTK_LABEL(app->lbl_part_mount), get_loc("part_mount", lang));
    if(app->btn_part_add) gtk_button_set_label(GTK_BUTTON(app->btn_part_add), get_loc("btn_add", lang));
    if(app->btn_part_edit) gtk_button_set_label(GTK_BUTTON(app->btn_part_edit), get_loc("btn_edit", lang));
    if(app->btn_part_del) gtk_button_set_label(GTK_BUTTON(app->btn_part_del), get_loc("btn_del", lang));
    if(app->btn_part_reset) gtk_button_set_label(GTK_BUTTON(app->btn_part_reset), get_loc("btn_reset", lang));
    if(app->btn_part_gparted) gtk_button_set_label(GTK_BUTTON(app->btn_part_gparted), get_loc("btn_gparted", lang));
    if(app->chk_manual_partitions) gtk_button_set_label(GTK_BUTTON(app->chk_manual_partitions), get_loc("manual_partitioning", lang));
    
    // Frame labels
    if(app->frame_existing) {
        GtkWidget *frame_label = gtk_frame_get_label_widget(GTK_FRAME(app->frame_existing));
        if(frame_label) gtk_label_set_text(GTK_LABEL(frame_label), get_loc("existing_parts", lang));
    }
    if(app->frame_install_parts) {
        GtkWidget *frame_label = gtk_frame_get_label_widget(GTK_FRAME(app->frame_install_parts));
        if(frame_label) gtk_label_set_text(GTK_LABEL(frame_label), get_loc("install_parts", lang));
    }

    // Boot
    // if(app->lbl_boot_status) gtk_label_set_text(GTK_LABEL(app->lbl_boot_status), get_loc("boot_detect", lang));
    if(app->lbl_grub_install) gtk_label_set_text(GTK_LABEL(app->lbl_grub_install), get_loc("grub_install", lang));
    
    // System
    if(app->lbl_hostname) gtk_label_set_text(GTK_LABEL(app->lbl_hostname), get_loc("hostname", lang));
    if(app->lbl_locale) gtk_label_set_text(GTK_LABEL(app->lbl_locale), get_loc("locale", lang));
    if(app->lbl_region) gtk_label_set_text(GTK_LABEL(app->lbl_region), get_loc("region", lang));
    if(app->lbl_city) gtk_label_set_text(GTK_LABEL(app->lbl_city), get_loc("city", lang));
    
    // User
    if(app->lbl_root_pass) gtk_label_set_text(GTK_LABEL(app->lbl_root_pass), get_loc("root_pass", lang));
    if(app->lbl_user_account) gtk_label_set_text(GTK_LABEL(app->lbl_user_account), get_loc("user_acc_title", lang));
    if(app->lbl_fullname) gtk_label_set_text(GTK_LABEL(app->lbl_fullname), get_loc("fullname", lang));
    if(app->lbl_username) gtk_label_set_text(GTK_LABEL(app->lbl_username), get_loc("username", lang));
    if(app->lbl_user_pass) gtk_label_set_text(GTK_LABEL(app->lbl_user_pass), get_loc("password", lang));
    if(app->lbl_user_confirm) gtk_label_set_text(GTK_LABEL(app->lbl_user_confirm), get_loc("confirm", lang));
    if(app->chk_autologin) gtk_button_set_label(GTK_BUTTON(app->chk_autologin), get_loc("autologin", lang));
    
    // Common
    if(!app->installing) gtk_button_set_label(GTK_BUTTON(app->btn_install), get_loc("install_btn", lang));
    gtk_button_set_label(GTK_BUTTON(app->btn_back), get_loc("back", lang));
    gtk_button_set_label(GTK_BUTTON(app->btn_next), get_loc("next", lang));
    
    // Update Tab Labels (7 tabs: Welcome, Install Type, Partitions, Bootloader, System, Users, Install)
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 0), get_loc("tab_welcome", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 1), get_loc("tab_install_type", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 2), get_loc("tab_partitions", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 3), get_loc("tab_bootloader", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 4), get_loc("tab_system", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 5), get_loc("tab_users", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 6), get_loc("tab_install", lang));
}

void on_lang_toggled(GtkWidget *widget, AppData *app) {
    extern int get_lang_count(void);
    extern const LangInfo* get_lang(const char *code);
    extern const char** get_available_langs(void);
    
    app->current_lang++;
    if (app->current_lang >= get_lang_count()) {
        app->current_lang = 0;
    }
    
    const char **langs = get_available_langs();
    const char *code = langs[app->current_lang];
    const LangInfo *li = get_lang(code);
    
    const char *name = li ? li->name : code;
    gtk_button_set_label(GTK_BUTTON(widget), name);
    
    // Update window title
    const char *title = get_loc("window_title", app->current_lang);
    gtk_window_set_title(GTK_WINDOW(app->window), title);
    
    update_ui_language(app);
}

void set_ui_finished_safe(gpointer data) {
    AppData *app = (AppData *)data;
    
    gtk_widget_set_sensitive(app->btn_back, FALSE);
    gtk_widget_set_sensitive(app->btn_next, FALSE);
    gtk_widget_set_sensitive(app->notebook, FALSE);
    
    gtk_button_set_label(GTK_BUTTON(app->btn_install), get_loc("reboot_btn", app->current_lang));
    g_signal_handlers_disconnect_by_func(app->btn_install, G_CALLBACK(start_installation), app);
    g_signal_connect(app->btn_install, "clicked", G_CALLBACK(on_reboot_clicked), app);
    gtk_widget_set_sensitive(app->btn_install, TRUE);

    GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE,
        get_loc("success_title", app->current_lang));

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(success_dialog), 
        get_loc("success_body", app->current_lang));

    GtkWidget *btn_reboot_popup = gtk_dialog_add_button(GTK_DIALOG(success_dialog), get_loc("success_reboot", app->current_lang), GTK_RESPONSE_ACCEPT);
    GtkStyleContext *context = gtk_widget_get_style_context(btn_reboot_popup);
    gtk_style_context_add_class(context, "suggested-action");
    g_signal_connect(success_dialog, "response", G_CALLBACK(on_popup_reboot), NULL);
    gtk_widget_show_all(success_dialog);
    // return FALSE; // void function signature mismatch if we return FALSE here? No, g_idle_add expects FALSE
}
// wait wrapper for g_idle_add
gboolean set_ui_finished_wrapper(gpointer data) {
    set_ui_finished_safe(data);
    return FALSE;
}

void set_ui_finished(AppData *app) { g_idle_add(set_ui_finished_wrapper, app); }

GtkWidget* create_form_row(const gchar *label_text, GtkWidget **entry_ptr, GtkWidget **label_ptr) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_bottom(hbox, 5); 
    
    GtkWidget *label = gtk_label_new(label_text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0); 

    if(label_ptr) *label_ptr = label;
    
    *entry_ptr = gtk_entry_new();
    gtk_widget_set_hexpand(*entry_ptr, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), *entry_ptr, TRUE, TRUE, 0);
    return hbox;
}

GtkWidget* create_vertical_input(const gchar *label_text, GtkWidget **entry_ptr, GtkWidget **label_ptr) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_margin_bottom(vbox, 5); 

    GtkWidget *label = gtk_label_new(label_text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0); 
    if(label_ptr) *label_ptr = label;
    
    *entry_ptr = gtk_entry_new();
    
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), *entry_ptr, FALSE, FALSE, 0);
    return vbox;
}

GtkWidget* create_welcome_page(AppData *app) {
    GtkWidget *align_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(align_box, 10);
    gtk_widget_set_margin_bottom(align_box, 10);
    gtk_widget_set_margin_start(align_box, 10);
    gtk_widget_set_margin_end(align_box, 10);
    
    GtkWidget *vbox_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8); 
    gtk_widget_set_halign(vbox_main, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(vbox_main, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(vbox_main, TRUE);

    // Picture - smaller for low res
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    if (gdk_pixbuf_loader_write(loader, logo_png, logo_png_len, NULL)) {
        gdk_pixbuf_loader_close(loader, NULL);
        GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
        
        if (pixbuf) {
            int width = gdk_pixbuf_get_width(pixbuf);
            int height = gdk_pixbuf_get_height(pixbuf);
            int target_width = 180;
            int target_height = target_width * height / width;
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, target_width, target_height, GDK_INTERP_BILINEAR);
            GtkWidget *image = gtk_image_new_from_pixbuf(scaled);
            gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
            gtk_widget_set_margin_bottom(image, 10);
            gtk_box_pack_start(GTK_BOX(vbox_main), image, FALSE, FALSE, 0); 
            
            g_object_unref(scaled); 
        }
    }
    g_object_unref(loader);

    app->lbl_welcome_title = gtk_label_new(NULL);
    gtk_label_set_justify(GTK_LABEL(app->lbl_welcome_title), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(app->lbl_welcome_title), 0.5);
    gtk_box_pack_start(GTK_BOX(vbox_main), app->lbl_welcome_title, FALSE, FALSE, 0);

    app->lbl_welcome_body = gtk_label_new(NULL);
    gtk_label_set_justify(GTK_LABEL(app->lbl_welcome_body), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(app->lbl_welcome_body), 0.5); 
    gtk_label_set_yalign(GTK_LABEL(app->lbl_welcome_body), 0.5); 
    gtk_label_set_line_wrap(GTK_LABEL(app->lbl_welcome_body), TRUE); 
    gtk_label_set_max_width_chars(GTK_LABEL(app->lbl_welcome_body), 50); 
    gtk_label_set_selectable(GTK_LABEL(app->lbl_welcome_body), TRUE);
    gtk_widget_set_margin_start(app->lbl_welcome_body, 10);
    gtk_widget_set_margin_end(app->lbl_welcome_body, 10);
    gtk_box_pack_start(GTK_BOX(vbox_main), app->lbl_welcome_body, FALSE, FALSE, 5);

    // Lang Button - get first language name
    extern const LangInfo* get_lang(const char *code);
    extern const char** get_available_langs(void);
    const char **langs = get_available_langs();
    const LangInfo *first_lang = get_lang(langs[0]);
    const char *first_lang_name = first_lang ? first_lang->name : "English";
    
    GtkWidget *btn_lang = gtk_button_new_with_label(first_lang_name);
    gtk_widget_set_halign(btn_lang, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(btn_lang, 5);
    g_signal_connect(btn_lang, "clicked", G_CALLBACK(on_lang_toggled), app);
    gtk_box_pack_start(GTK_BOX(vbox_main), btn_lang, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(align_box), vbox_main, TRUE, TRUE, 0);
    gtk_widget_show_all(align_box);
    
    return align_box;
}

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

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_sys, gtk_label_new("System"));

    // --- TAB 4: USERS ---
    GtkWidget *page_user = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(page_user), 10);
    gtk_widget_set_vexpand(page_user, TRUE);
    
    // Root Password
    gtk_box_pack_start(GTK_BOX(page_user), create_vertical_input("Root Password:", &app->root_pass_entry, &app->lbl_root_pass), FALSE, FALSE, 0);
    gtk_entry_set_visibility(GTK_ENTRY(app->root_pass_entry), FALSE);

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
    
    // Autologin
    app->autologin_check = gtk_check_button_new_with_label("Enable Auto-Login");
    app->chk_autologin = app->autologin_check;
    gtk_widget_set_halign(app->autologin_check, GTK_ALIGN_CENTER);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->autologin_check), TRUE);
    gtk_box_pack_start(GTK_BOX(page_user), app->autologin_check, FALSE, FALSE, 10);

    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), page_user, gtk_label_new("Users"));

    // --- TAB 5: INSTALL ---
    GtkWidget *page_inst = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(page_inst), 10);

    app->console_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->console_text), FALSE);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), app->console_text);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(page_inst), scroll, TRUE, TRUE, 0);
    
    app->progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(page_inst), app->progress_bar, FALSE, FALSE, 0);

    app->btn_install = gtk_button_new_with_label("Start Installation");
    gtk_widget_set_size_request(app->btn_install, -1, 40);
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

    gtk_widget_show_all(app->window);

}
