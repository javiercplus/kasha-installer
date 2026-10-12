/*
 * loc.c
 * Localization helper (pure C, shared with the C++ UI)
 */
#include "neko_installer.h"
#include "lang.h"
#include <string.h>
#include <stdlib.h>

const char* get_loc(const char *key, int lang) {
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
    if (strcmp(key, "d_icewm_desc") == 0) return li->d_icewm_desc;
    if (strcmp(key, "d_i3_desc") == 0) return li->d_i3_desc;
    if (strcmp(key, "d_jwm_desc") == 0) return li->d_jwm_desc;
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
