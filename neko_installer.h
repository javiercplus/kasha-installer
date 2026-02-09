/*
 * neko_installer.h
 */
#ifndef NEKO_INSTALLER_H
#define NEKO_INSTALLER_H

#include <gtk/gtk.h>

// Estructura para guardar la configuración de una partición
typedef struct {
    gchar *device;       // ej: /dev/sda1
    gchar *fstype;       // ej: ext4, btrfs, swap
    gchar *mountpoint;   // ej: /, /home, swap
    gboolean format;      // TRUE si se debe formatear
} PartitionConfig;

typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    
    // Tab 1: Disks
    GtkWidget *disk_combo;     
    GtkWidget *mount_list;     // TreeView para listar particiones configuradas
    GSList *part_config_list; // Lista enlazada con las configuraciones
    
    // Tab 2: Bootloader
    GtkWidget *grub_disk_combo;
    GtkWidget *label_boot_status; 
    
    // Tab 3: System
    GtkWidget *hostname_entry;
    GtkWidget *locale_combo;    
    
    // Tab 4: Users
    GtkWidget *root_pass_entry;
    GtkWidget *root_pass_confirm_entry;
    GtkWidget *user_login_entry;
    GtkWidget *user_fullname_entry;
    GtkWidget *user_pass_entry;
    GtkWidget *user_pass_confirm_entry;
    
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
} AppData;

typedef struct {
    gchar *message;
    gdouble fraction;
    AppData *app; 
} LogMessage;

// Prototypes
gboolean check_efi(void); 
void init_utils(AppData *app);
void sync_grub_list(AppData *app);
void build_ui(AppData *app);

// Navigation & UI Updates
void on_next_clicked(GtkWidget *widget, AppData *app);
void on_back_clicked(GtkWidget *widget, AppData *app);
void on_page_changed(GtkNotebook *notebook, GtkWidget *page, guint page_num, AppData *app);
void set_ui_finished(AppData *app);
void on_reboot_clicked(GtkWidget *widget, AppData *app);
void on_insert_text_username(GtkEditable *editable, gchar *new_text, gint new_text_length, gint *position, gpointer data);

// Partition Manager
void open_partition_manager(GtkWidget *widget, AppData *app);
void add_partition_config(AppData *app, const gchar *dev, const gchar *fs, const gchar *mp, gboolean fmt);

// Installation
void start_installation(GtkWidget *widget, AppData *app); 
gpointer install_thread(gpointer data);
void log_to_ui(AppData *app, const char *msg, gdouble fraction);

#endif