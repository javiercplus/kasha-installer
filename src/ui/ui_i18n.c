/*
 * ui_i18n.c
 * Localization helpers
 */
#include "neko_installer.h"
#include "lang.h"
#include <string.h>
#include <stdlib.h>

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

    if (strcmp(key, "language") == 0) return li->language;
    if (strcmp(key, "welcome_title") == 0) return li->welcome_title;
    if (strcmp(key, "welcome_body") == 0) return li->welcome_body;
    if (strcmp(key, "disk") == 0) return li->disk;
    if (strcmp(key, "disk_title") == 0) return li->disk_title;
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
    if (strcmp(key, "country") == 0) return li->country;
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
    if (strcmp(key, "tab_desktop") == 0) return li->tab_desktop;
    if (strcmp(key, "tab_users") == 0) return li->tab_users;
    if (strcmp(key, "tab_privilege") == 0) return li->tab_privilege;
    if (strcmp(key, "desktop_title") == 0) return li->desktop_title;
    if (strcmp(key, "desktop_desc") == 0) return li->desktop_desc;
    if (strcmp(key, "d_xfce_desc") == 0) return li->d_xfce_desc;
    if (strcmp(key, "d_niri_desc") == 0) return li->d_niri_desc;
    if (strcmp(key, "d_kde_desc") == 0) return li->d_kde_desc;
    if (strcmp(key, "d_icejwm_desc") == 0) return li->d_icejwm_desc;
    if (strcmp(key, "d_mate_desc") == 0) return li->d_mate_desc;
    if (strcmp(key, "d_labwc_desc") == 0) return li->d_labwc_desc;
    if (strcmp(key, "d_lxqt_desc") == 0) return li->d_lxqt_desc;
    if (strcmp(key, "d_local_label") == 0) return li->d_local_label;
    if (strcmp(key, "priv_title") == 0) return li->priv_title;
    if (strcmp(key, "priv_desc") == 0) return li->priv_desc;
    if (strcmp(key, "priv_sudo_label") == 0) return li->priv_sudo_label;
    if (strcmp(key, "priv_sudo_desc") == 0) return li->priv_sudo_desc;
    if (strcmp(key, "priv_doas_label") == 0) return li->priv_doas_label;
    if (strcmp(key, "priv_doas_desc") == 0) return li->priv_doas_desc;
    if (strcmp(key, "tab_install") == 0) return li->tab_install;
    if (strcmp(key, "welcome_title_markup") == 0) return li->welcome_title_markup;
    if (strcmp(key, "welcome_body_markup") == 0) return li->welcome_body_markup;
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
    if (strcmp(key, "success_title") == 0) return li->success_title;
    if (strcmp(key, "success_body") == 0) return li->success_body;
    if (strcmp(key, "success_reboot") == 0) return li->success_reboot;

    // Validation messages
    if (strcmp(key, "val_select_disk")    == 0) return li->val_select_disk;
    if (strcmp(key, "val_no_root")        == 0) return li->val_no_root;
    if (strcmp(key, "val_grub_disk")      == 0) return li->val_grub_disk;
    if (strcmp(key, "val_hostname")       == 0) return li->val_hostname;
    if (strcmp(key, "val_username")       == 0) return li->val_username;
    if (strcmp(key, "val_password")       == 0) return li->val_password;
    if (strcmp(key, "val_password_match") == 0) return li->val_password_match;
    if (strcmp(key, "val_root_pass")      == 0) return li->val_root_pass;
    if (strcmp(key, "val_incomplete")     == 0) return li->val_incomplete;

    return key;
}

void update_ui_language(AppData *app) {
    int lang = app->current_lang;

    if(app->lbl_lang_selection) gtk_label_set_text(GTK_LABEL(app->lbl_lang_selection), get_loc("language", lang));

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
    if(app->lbl_country) gtk_label_set_text(GTK_LABEL(app->lbl_country), get_loc("country", lang));
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

    // Privilege Manager
    if(app->lbl_priv_title) gtk_label_set_markup(GTK_LABEL(app->lbl_priv_title),
        g_strdup_printf("<b><span size='large'>%s</span></b>", get_loc("priv_title", lang)));
    if(app->lbl_priv_desc) gtk_label_set_text(GTK_LABEL(app->lbl_priv_desc), get_loc("priv_desc", lang));
    if(app->lbl_priv_sudo) gtk_button_set_label(GTK_BUTTON(app->lbl_priv_sudo), get_loc("priv_sudo_label", lang));
    if(app->lbl_priv_doas) gtk_button_set_label(GTK_BUTTON(app->lbl_priv_doas), get_loc("priv_doas_label", lang));
    if(app->lbl_priv_sudo_desc) gtk_label_set_text(GTK_LABEL(app->lbl_priv_sudo_desc), get_loc("priv_sudo_desc", lang));
    if(app->lbl_priv_doas_desc) gtk_label_set_text(GTK_LABEL(app->lbl_priv_doas_desc), get_loc("priv_doas_desc", lang));

#ifdef HAS_DESKTOP_TAB
    // Desktop
    if(app->lbl_desktop_title) gtk_label_set_markup(GTK_LABEL(app->lbl_desktop_title),
        g_strdup_printf("<b><span size='large'>%s</span></b>", get_loc("desktop_title", lang)));
    if(app->lbl_desktop_desc) gtk_label_set_text(GTK_LABEL(app->lbl_desktop_desc), get_loc("desktop_desc", lang));
    if(app->chk_desktop_local) gtk_button_set_label(GTK_BUTTON(app->chk_desktop_local), get_loc("d_local_label", lang));
    if(app->lbl_d_default_desc) gtk_label_set_text(GTK_LABEL(app->lbl_d_default_desc), get_loc("d_default_desc", lang));
    if(app->lbl_d_xfce_desc)   gtk_label_set_text(GTK_LABEL(app->lbl_d_xfce_desc),   get_loc("d_xfce_desc",   lang));
    if(app->lbl_d_niri_desc)   gtk_label_set_text(GTK_LABEL(app->lbl_d_niri_desc),   get_loc("d_niri_desc",   lang));
    if(app->lbl_d_kde_desc)    gtk_label_set_text(GTK_LABEL(app->lbl_d_kde_desc),    get_loc("d_kde_desc",    lang));
    if(app->lbl_d_icejwm_desc) gtk_label_set_text(GTK_LABEL(app->lbl_d_icejwm_desc), get_loc("d_icejwm_desc", lang));
    if(app->lbl_d_mate_desc)   gtk_label_set_text(GTK_LABEL(app->lbl_d_mate_desc),   get_loc("d_mate_desc",   lang));
    if(app->lbl_d_labwc_desc)  gtk_label_set_text(GTK_LABEL(app->lbl_d_labwc_desc),  get_loc("d_labwc_desc",  lang));
    if(app->lbl_d_lxqt_desc)   gtk_label_set_text(GTK_LABEL(app->lbl_d_lxqt_desc),   get_loc("d_lxqt_desc",   lang));
#endif

    // Common
    if(!app->installing) gtk_button_set_label(GTK_BUTTON(app->btn_install), get_loc("install_btn", lang));
    gtk_button_set_label(GTK_BUTTON(app->btn_back), get_loc("back", lang));
    gtk_button_set_label(GTK_BUTTON(app->btn_next), get_loc("next", lang));

    // Update Tab Labels
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 0), get_loc("tab_welcome", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 1), get_loc("tab_install_type", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 2), get_loc("tab_partitions", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 3), get_loc("tab_bootloader", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 4), get_loc("tab_system", lang));
#ifdef HAS_DESKTOP_TAB
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 5), get_loc("tab_desktop", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 6), get_loc("tab_users", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 7), get_loc("tab_privilege", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 8), get_loc("tab_install", lang));
#else
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 5), get_loc("tab_users", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 6), get_loc("tab_privilege", lang));
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(app->notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 7), get_loc("tab_install", lang));
#endif
}

void on_lang_changed(GtkComboBox *widget, AppData *app) {
    int sel = gtk_combo_box_get_active(widget);
    if (sel < 0) return;
    app->current_lang = sel;

    // Update window title
    const char *title = get_loc("window_title", app->current_lang);
    gtk_window_set_title(GTK_WINDOW(app->window), title);

    update_ui_language(app);
}