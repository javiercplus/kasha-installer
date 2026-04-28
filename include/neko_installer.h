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
    GtkWidget *locale_combo;    
    GtkWidget *tz_area_combo; 
    GtkWidget *tz_city_combo;
    
    // Tab 4: Users
    GtkWidget *root_pass_entry;
    GtkWidget *root_pass_confirm_entry;
    GtkWidget *user_login_entry;
    GtkWidget *user_fullname_entry;
    GtkWidget *user_pass_entry;
    GtkWidget *user_pass_confirm_entry;
    GtkWidget *autologin_check;
        
    // Tab 5: Installation
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
    
    // Auto-Partitioning
    InstallMode install_mode;
    gchar *detected_ntfs_partition;
    glong ntfs_resize_mb; // Target size for NTFS in MB

    // Localization
    int current_lang; // 0=EN, 1=ES
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
char* get_partition_path(const char *disk, int part_num);
void populate_defaults(AppData *app, const char *disk_name);
void get_partition_fstype(const char *device_path, char *out_type, size_t max_len);
PartitionConfig* find_config_by_device(AppData *app, const gchar *device);
void remove_partition_config(AppData *app, PartitionConfig *conf);

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
void on_timezone_area_changed(GtkComboBox *widget, AppData *app);
void on_insert_text_username(GtkEditable *editable, gchar *new_text, gint new_text_length, gint *position, gpointer data);
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
int run_sync(AppData *app, const char *fmt, ...);
char* get_uuid(const char *device);
gint sort_partitions(gconstpointer a, gconstpointer b); // exposed for steps if needed
void unmount_safety(AppData *app);
void generate_fstab(AppData *app, const char *target_dir);
void generate_crypttab(AppData *app, const char *target_dir);
void set_safe_password(AppData *app, const gchar *username, const gchar *password, const gchar *target_dir);

// installer_steps.c
int step_partitioning(AppData *app, const char *disk_name);
int step_format_and_mount(AppData *app, const char *TARGETDIR);
int step_install_base_system(AppData *app, const char *TARGETDIR);
int step_configure_system(AppData *app, const char *TARGETDIR, const gchar *hostname, const gchar *locale, const gchar *root_pass, const gchar *user_login, const gchar *user_fullname, const gchar *user_pass, gboolean autologin);
int step_install_bootloader(AppData *app, const char *TARGETDIR, const char *disk_name);
int step_finalize(AppData *app, const char *TARGETDIR);
int check_filesystems(AppData *app);

#endif