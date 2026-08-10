/*
 * neko_installer.h
 * Modularized Header
 */
#ifndef NEKO_INSTALLER_H
#define NEKO_INSTALLER_H

#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    gchar *device;     
    gchar *fstype;      
    gchar *mountpoint;  
    gboolean format;     
    gboolean encrypt;
    gchar *luks_pass;
    gchar *original_device;
    gchar *luks_uuid; // LUKS header UUID (for GRUB and crypttab)
} PartitionConfig;

typedef enum {
    INSTALL_MODE_MANUAL,
    INSTALL_MODE_ERASE,
    INSTALL_MODE_DUAL_BOOT
} InstallMode;

typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    
    // Tab 1: Disks
    GtkWidget *disk_combo;     
    GtkWidget *mount_list;     
    GSList *part_config_list;
    
    // Tab 2: Bootloader
    GtkWidget *grub_disk_combo;
    GtkWidget *label_boot_status; 
    
    // Tab 3: System
    GtkWidget *hostname_entry;
    GtkWidget *country_combo;
    GtkWidget *locale_combo;    
    GtkWidget *tz_area_combo; 
    GtkWidget *tz_city_combo;
    GtkWidget *kbd_layout_combo;
    GtkWidget *kbd_variant_combo;
    GtkWidget *lbl_kbd_layout;
    GtkWidget *lbl_kbd_variant;
    
    // Tab 4: Users
    GtkWidget *root_pass_entry;
    GtkWidget *root_pass_confirm_entry;
    GtkWidget *user_login_entry;
    GtkWidget *user_fullname_entry;
    GtkWidget *user_pass_entry;
    GtkWidget *user_pass_confirm_entry;
    GtkWidget *autologin_check;

    // Tab 5: Privilege Manager
    GtkWidget *radio_priv_sudo;
    GtkWidget *radio_priv_doas;
    GtkWidget *lbl_priv_title;
    GtkWidget *lbl_priv_desc;
    GtkWidget *lbl_priv_sudo;
    GtkWidget *lbl_priv_doas;
    GtkWidget *lbl_priv_sudo_desc;
    GtkWidget *lbl_priv_doas_desc;

    // Tab 6: Installation
    GtkWidget *console_text;
    GtkWidget *progress_bar;
    GtkWidget *btn_install;
    
    // Navigation
    GtkWidget *btn_back;
    GtkWidget *btn_next;
    
    // Others
    gboolean is_efi;
    gchar *efi_target; 
    gchar *selected_disk;       
    gboolean installing;
    gboolean debug_mode;
    guint progress_pulse_id;
    
    // Auto-Partitioning
    InstallMode install_mode;
    gchar *detected_ntfs_partition;
    gchar *detected_efi_partition; // Existing EFI partition for dual boot
    glong ntfs_resize_mb; // Target size for NTFS in MB

    // Localization
    int current_lang; // 0=EN, 1=ES
    GtkWidget *lbl_lang_selection;
    GtkWidget *lbl_welcome_title;
    GtkWidget *lbl_welcome_body;
    GtkWidget *lbl_disk_title;
    GtkWidget *lbl_disk_info;
    GtkWidget *lbl_part_mount;
    GtkWidget *btn_part_add;
    GtkWidget *btn_part_edit;
    GtkWidget *btn_part_del;
    GtkWidget *btn_part_reset;
    GtkWidget *lbl_boot_status;
    GtkWidget *lbl_grub_install;
    GtkWidget *lbl_hostname;
    GtkWidget *lbl_country;
    GtkWidget *lbl_locale;
    GtkWidget *lbl_region;
    GtkWidget *lbl_city;
    GtkWidget *lbl_root_pass;
    GtkWidget *lbl_user_account;
    GtkWidget *lbl_fullname;
    GtkWidget *lbl_username;
    GtkWidget *lbl_user_pass;
    GtkWidget *lbl_user_confirm;
    GtkWidget *chk_autologin;
    GtkWidget *btn_part_gparted;

    // Install Type Tab
    GtkWidget *radio_clean_install;
    GtkWidget *radio_alongside;
    GtkWidget *lbl_install_type_title;
    GtkWidget *lbl_install_type_desc;
    
    // Split Partition View
    GtkWidget *existing_part_list;
    GtkWidget *frame_existing;
    
    // Manual Partitioning
    GtkWidget *chk_manual_partitions;
    GtkWidget *frame_install_parts;

#ifdef HAS_DESKTOP_TAB
    // Desktop Tab
    GtkWidget *lbl_desktop_title;
    GtkWidget *lbl_desktop_desc;
    GtkWidget *chk_desktop_default;
    GtkWidget *chk_desktop_xfce;
    GtkWidget *chk_desktop_niri;
    GtkWidget *chk_desktop_kde;
    GtkWidget *chk_desktop_icejwm;
    GtkWidget *chk_desktop_mate;
    GtkWidget *chk_desktop_labwc;
    GtkWidget *chk_desktop_lxqt;
    GtkWidget *lbl_d_default_desc;
    GtkWidget *lbl_d_xfce_desc;
    GtkWidget *lbl_d_niri_desc;
    GtkWidget *lbl_d_kde_desc;
    GtkWidget *lbl_d_icejwm_desc;
    GtkWidget *lbl_d_mate_desc;
    GtkWidget *lbl_d_labwc_desc;
    GtkWidget *lbl_d_lxqt_desc;
    gchar *selected_desktop;
#endif

} AppData;

typedef struct {
    gchar *message;
    gdouble fraction;
    AppData *app; 
} LogMessage;


// --- PROTOTYPES ---

// utils.c
gboolean check_efi(void); 
void init_utils(AppData *app);
void sync_grub_list(AppData *app);

// partition_utils.c
char* find_ntfs_partition(const char *disk_name);
char* find_efi_partition(const char *disk_name);
gboolean validate_efi_partition(AppData *app, const char *efi_device);
char* get_partition_path(const char *disk, int part_num);
void populate_defaults(AppData *app, const char *disk_name);
void get_partition_fstype(const char *device_path, char *out_type, size_t max_len);
PartitionConfig* find_config_by_device(AppData *app, const gchar *device);
void remove_partition_config(AppData *app, PartitionConfig *conf);
glong get_partition_size_mb(const char *device);
glong get_fs_min_size_mb(const char *device);
glong get_disk_free_space_mb(const char *disk_name);
char* find_largest_resizable_partition(const char *disk_name);
int resize_existing_partition(AppData *app, const char *device, glong new_size_mb);
char* get_disk_partition_table_type(const char *disk_name);
int get_mbr_primary_count(const char *disk_name);


// ui_partition.c
void scan_partitions_for_dialog(GtkComboBoxText *combo);
void populate_partitions_combo(GtkComboBoxText *combo, const char *disk_name);
void add_partition_config(AppData *app, const gchar *dev, const gchar *fs, const gchar *mp, gboolean fmt, gboolean encrypt, const gchar *pass);
void refresh_partition_list_ui(AppData *app);
void refresh_existing_partitions_ui(AppData *app);
void show_partition_dialog(AppData *app, PartitionConfig *edit_conf);
void on_add_partition_clicked(GtkWidget *widget, gpointer user_data);
void on_edit_partition_clicked(GtkWidget *widget, gpointer user_data);
void on_delete_partition_clicked(GtkWidget *widget, gpointer user_data);
void on_reset_partitions_clicked(GtkWidget *widget, AppData *app);
void open_partition_manager(GtkWidget *widget, AppData *app);
void init_existing_partitions_view(AppData *app);

// ui_callbacks.c
void on_next_clicked(GtkWidget *widget, AppData *app);
void on_back_clicked(GtkWidget *widget, AppData *app);
void on_page_changed(GtkNotebook *notebook, GtkWidget *page, guint page_num, AppData *app);
void on_disk_changed(GtkComboBox *widget, AppData *app);
void on_country_changed(GtkComboBox *widget, AppData *app);
void on_timezone_area_changed(GtkComboBox *widget, AppData *app);
void on_kbd_layout_changed(GtkComboBox *widget, AppData *app);
void on_insert_text_username(GtkEditable *editable, gchar *new_text, gint new_text_length, gint *position, gpointer data);
void on_insert_text_hostname(GtkEditable *editable, gchar *new_text, gint new_text_length, gint *position, gpointer data);
void launch_gparted(GtkWidget *widget, AppData *app);
void on_reboot_clicked(GtkWidget *widget, AppData *app);
void on_popup_reboot(GtkDialog *dialog, gint response_id, gpointer user_data);
void on_lang_toggled(GtkWidget *widget, AppData *app);
void on_manual_check_toggled(GtkToggleButton *toggle, AppData *app);
void on_install_type_changed(GtkToggleButton *toggle, AppData *app);

// ui.c (Main)
void build_ui(AppData *app);
void set_ui_finished(AppData *app);
GtkWidget* create_form_row(const gchar *label_text, GtkWidget **entry_ptr, GtkWidget **label_ptr);
const char* get_loc(const char *key, int lang); // Localization helper
void update_ui_language(AppData *app);

// installer.c
void start_installation(GtkWidget *widget, AppData *app); 
gpointer install_thread(gpointer data);
void log_to_ui(AppData *app, const char *msg, gdouble fraction);
void log_to_ui_printf(AppData *app, const char *fmt, ...);
void start_progress_pulse(AppData *app);
void stop_progress_pulse(AppData *app);
int run_sync(AppData *app, const char *fmt, ...);
char* get_uuid(const char *device);
gint sort_partitions(gconstpointer a, gconstpointer b); // exposed for steps if needed
void unmount_safety(AppData *app);
void generate_fstab(AppData *app, const char *target_dir);
void generate_crypttab(AppData *app, const char *target_dir);
gboolean set_safe_password(AppData *app, const gchar *username, const gchar *password, const gchar *target_dir);
gchar *validate_and_normalize_hostname(const gchar *raw_hostname); // always returns heap pointer

// installer_steps.c
int step_partitioning(AppData *app, const char *disk_name);
int step_format_and_mount(AppData *app, const char *TARGETDIR);
int step_install_base_system(AppData *app, const char *TARGETDIR);
int step_configure_system(AppData *app, const char *TARGETDIR, const gchar *hostname, const gchar *locale, const gchar *root_pass, const gchar *user_login, const gchar *user_fullname, const gchar *user_pass, gboolean autologin);
int step_install_bootloader(AppData *app, const char *TARGETDIR, const char *disk_name);
int step_finalize(AppData *app, const char *TARGETDIR);
int check_filesystems(AppData *app);

// installer_steps_void.c  (normal build only, NOT in UNIVERSAL_BUILD)
#ifndef UNIVERSAL_BUILD
void void_install_crypto_packages(AppData *app, const char *TARGETDIR);
void void_copy_xbps_keys(AppData *app, const char *TARGETDIR);
void void_reconfigure_base(AppData *app, const char *TARGETDIR);
void void_remove_live_packages(AppData *app, const char *TARGETDIR);
void void_reconfigure_locales(AppData *app, const char *TARGETDIR, const char *locale);
void void_copy_xbpsd_config(AppData *app, const char *TARGETDIR);
void void_install_grub_efi_pkg(AppData *app, const char *TARGETDIR);
void void_install_grub_bios_pkg(AppData *app, const char *TARGETDIR);
void void_install_osprober(AppData *app, const char *TARGETDIR);
void void_install_dracut_luks(AppData *app, const char *TARGETDIR);
#endif /* !UNIVERSAL_BUILD */

// desktops-setup.c  (normal build only, guarded by HAS_DESKTOP_TAB)
#ifdef HAS_DESKTOP_TAB
void void_xfce(AppData *app, const char *TARGETDIR);
void void_niri(AppData *app, const char *TARGETDIR);
void void_kde(AppData *app, const char *TARGETDIR);
void void_icejwm(AppData *app, const char *TARGETDIR);
void void_mate(AppData *app, const char *TARGETDIR);
void void_labwc(AppData *app, const char *TARGETDIR);
void void_lxqt(AppData *app, const char *TARGETDIR);
void void_default(AppData *app, const char *TARGETDIR);
#endif /* HAS_DESKTOP_TAB */

#endif /* NEKO_INSTALLER_H */