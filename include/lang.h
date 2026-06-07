/*
 * lang.h
 * Internationalization module interface
 */
#ifndef LANG_H
#define LANG_H

typedef struct {
    const char *code;      // "en", "es", etc.
    const char *name;     // "English", "Español"
    const char *language; // "Language", "Idioma", "言語"
    
    // Welcome
    const char *welcome_title;
    const char *welcome_body;
    const char *welcome_title_markup;
    const char *welcome_body_markup;
    
    // Install Type
    const char *install_type_title;
    const char *clean_install;
    const char *install_alongside;
    const char *clean_install_desc;
    const char *alongside_desc;
    
    // Partitions
    const char *disk;
    const char *disk_title;
    const char *disk_note;
    const char *existing_parts;
    const char *install_parts;
    const char *manual_partitioning;
    const char *btn_add;
    const char *btn_edit;
    const char *btn_delete;
    const char *btn_reset;
    const char *btn_gparted;
    const char *part_mount;
    
    // Bootloader
    const char *boot_detect;
    const char *grub_install;
    
    // System
    const char *hostname;
    const char *country;
    const char *locale;
    const char *region;
    const char *city;
    const char *timezone;
    
    // Privilege Manager
    const char *tab_privilege;
    const char *priv_title;
    const char *priv_desc;
    const char *priv_sudo_label;
    const char *priv_sudo_desc;
    const char *priv_doas_label;
    const char *priv_doas_desc;

    // Users
    const char *root_pass;
    const char *user_account;
    const char *fullname;
    const char *username;
    const char *password;
    const char *confirm;
    const char *autologin;
    
    // Install
    const char *install_btn;
    const char *reboot_btn;
    const char *back;
    const char *next;
    
    // Tabs
    const char *tab_welcome;
    const char *tab_install_type;
    const char *tab_partitions;
    const char *tab_bootloader;
    const char *tab_system;
    const char *tab_users;
    const char *tab_install;
    
    // Partition dialog
    const char *select_partition;
    const char *filesystem;
    const char *mount_point;
    const char *format_partition;
    const char *encrypt_luks;
    const char *general;
    const char *encryption;
    const char *dialog_add;
    const char *dialog_edit;
    const char *dialog_update;
    const char *save;
    const char *cancel;
    
    // Messages
    const char *error_root_password;
    const char *error_no_partitions;
    const char *error_no_root;
    const char *error_usr_not_supported;
    const char *error_efi_partition;
    
    // Window title
    const char *window_title;
    
    // Success
    const char *success_title;
    const char *success_body;
    const char *success_reboot;

    // Validation messages
    const char *val_select_disk;
    const char *val_no_root;
    const char *val_grub_disk;
    const char *val_hostname;
    const char *val_username;
    const char *val_password;
    const char *val_password_match;
    const char *val_root_pass;
    const char *val_incomplete;
} LangInfo;

extern const LangInfo *get_lang(const char *code);
extern const char **get_available_langs(void);
extern int get_lang_count(void);

#endif