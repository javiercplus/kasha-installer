/*
 * neko_installer.h
 * Modularized Header — C core shared with the C++ wxWidgets UI.
 */
#ifndef NEKO_INSTALLER_H
#define NEKO_INSTALLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <glib.h>
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

/*
 * Core application state (no UI widget pointers — the wxWidgets layer keeps
 * those itself). The UI fills the fields below and the C core only reads them.
 */
typedef struct {
    gboolean is_efi;
    gchar *efi_target;
    gchar *selected_disk;
    gboolean installing;
    gboolean debug_mode;
    gboolean finished;
    gboolean pulsing;         // indeterminate progress animation requested

    InstallMode install_mode;
    gchar *detected_ntfs_partition;
    gchar *detected_efi_partition; // Existing EFI partition for dual boot
    glong ntfs_resize_mb;          // Target size for NTFS in MB

    gboolean manual_partitioning;  // set by the UI's "manual" checkbox

    int current_lang; // 0=EN, 1=ES, 2=JA

    GSList *part_config_list;

    /* ---- values collected from the UI right before starting the install ---- */
    gchar *ui_hostname;
    gchar *ui_locale;
    gchar *ui_root_pass;
    gchar *ui_user_login;
    gchar *ui_user_pass;
    gchar *ui_user_fullname;
    gboolean ui_autologin;

    /* ---- system configuration selections (kept in sync by the UI) ---- */
    gchar *tz_area;
    gchar *tz_city;
    gchar *country_name;
    gint kbd_layout_active;   // index into get_keyboard_layouts()
    gint kbd_variant_active;  // 0 = default
    gint grub_disk_active;    // -1 when none selected

    /* ---- privilege manager ---- */
    gboolean privilege_doas;

#ifdef HAS_DESKTOP_TAB
    gchar *selected_desktop;  // "local", "xfce", "niri", ...
#endif

} AppData;

/*
 * Thread-safe log queue. The install thread pushes entries; the UI drains
 * them from its own timer on the main thread.
 */
typedef struct CoreLogEntry {
    char *message;            // UTF-8, heap allocated (g_free by consumer)
    double fraction;          // < 0 means "no explicit progress update"
    int finished;             // non-zero when this entry signals completion
    struct CoreLogEntry *next;
} CoreLogEntry;

CoreLogEntry *core_drain_log(void);        // detach all queued entries (caller frees)
void core_flush_capture(AppData *app);     // move buffered command output into the log queue

// --- PROTOTYPES ---

// utils.c
gboolean check_efi(void);
void core_detect_hardware(AppData *app);

// partition_utils.c
char* find_ntfs_partition(const char *disk_name);
char* find_efi_partition(const char *disk_name);
gboolean validate_efi_partition(AppData *app, const char *efi_device);
char* get_partition_path(const char *disk, int part_num);
void populate_defaults(AppData *app, const char *disk_name);
void get_partition_fstype(const char *device_path, char *out_type, size_t max_len);
PartitionConfig* find_config_by_device(AppData *app, const gchar *device);
void remove_partition_config(AppData *app, PartitionConfig *conf);
void add_partition_config(AppData *app, const gchar *dev, const gchar *fs, const gchar *mp, gboolean fmt, gboolean encrypt, const gchar *pass);
void core_reset_partitions(AppData *app);
glong get_partition_size_mb(const char *device);
glong get_fs_min_size_mb(const char *device);
glong get_disk_free_space_mb(const char *disk_name);
char* find_largest_resizable_partition(const char *disk_name);
int resize_existing_partition(AppData *app, const char *device, glong new_size_mb);
char* get_disk_partition_table_type(const char *disk_name);
int get_mbr_primary_count(const char *disk_name);

// installer.c
void start_installation(AppData *app);
void log_to_ui(AppData *app, const char *msg, gdouble fraction);
void log_to_ui_printf(AppData *app, const char *fmt, ...);
int run_sync(AppData *app, const char *fmt, ...);
char* get_uuid(const char *device);
gint sort_partitions(gconstpointer a, gconstpointer b);
void unmount_safety(AppData *app);
void generate_fstab(AppData *app, const char *target_dir);
void generate_crypttab(AppData *app, const char *target_dir);
gboolean set_safe_password(AppData *app, const gchar *username, const gchar *password, const gchar *target_dir);
gchar *validate_and_normalize_hostname(const gchar *raw_hostname);
void set_ui_finished(AppData *app);

// installer steps
int step_partitioning(AppData *app, const char *disk_name);
int step_format_and_mount(AppData *app, const char *TARGETDIR);
int step_install_base_system(AppData *app, const char *TARGETDIR);
int step_configure_system(AppData *app, const char *TARGETDIR, const gchar *hostname, const gchar *locale, const gchar *root_pass, const gchar *user_login, const gchar *user_fullname, const gchar *user_pass, gboolean autologin);
int step_install_bootloader(AppData *app, const char *TARGETDIR, const char *disk_name);
int step_finalize(AppData *app, const char *TARGETDIR);
int check_filesystems(AppData *app);

// step_base_system.c
int  install_base_copy_rootfs(AppData *app, const char *TARGETDIR);
void install_base_fix_permissions(AppData *app, const char *TARGETDIR);
void install_base_cleanup_live(AppData *app, const char *TARGETDIR);
void install_base_mount_vfs(AppData *app, const char *TARGETDIR);
void install_base_crypto(AppData *app, const char *TARGETDIR);
void install_base_initramfs(AppData *app, const char *TARGETDIR);

// step_config_system.c
void config_sys_remove_live_user(AppData *app, const char *TARGETDIR);
void config_sys_locale_and_hostname(AppData *app, const char *TARGETDIR, const gchar *hostname, const gchar *locale);
void config_sys_keyboard(AppData *app, const char *TARGETDIR);
void config_sys_timezone(AppData *app, const char *TARGETDIR);
#ifdef HAS_DESKTOP_TAB
void config_sys_desktop_env(AppData *app, const char *TARGETDIR);
#endif
int  config_sys_create_user(AppData *app, const char *TARGETDIR, const gchar *locale, const gchar *root_pass, const gchar *user_login, const gchar *user_fullname, const gchar *user_pass);
void config_sys_privileges(AppData *app, const char *TARGETDIR, const gchar *user_login);
void config_sys_autologin(AppData *app, const char *TARGETDIR, const gchar *user_login, gboolean autologin);

// step_bootloader.c
void bootloader_config_luks(AppData *app, const char *TARGETDIR);
int  bootloader_install_grub(AppData *app, const char *TARGETDIR, const char *disk_path);
void bootloader_setup_os_prober(AppData *app, const char *TARGETDIR, const char *disk_name);
void bootloader_cleanup_os_prober(AppData *app);
void bootloader_dracut_luks(AppData *app, const char *TARGETDIR);

// flatpak-config.c
void configure_flatpak_repo(AppData *app, const char *TARGETDIR);

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

// rootfs-base.c  (normal build only, NOT in UNIVERSAL_BUILD)
int  void_install_rootfs_base(AppData *app, const char *TARGETDIR);
void void_install_desktop_packages(AppData *app, const char *TARGETDIR, const char *desktop);
void void_ensure_core_packages(AppData *app, const char *TARGETDIR);
void void_enable_desktop_services(AppData *app, const char *TARGETDIR, const char *desktop);
#endif /* !UNIVERSAL_BUILD */

// desktops-setup.c  (normal build only, guarded by HAS_DESKTOP_TAB)
#ifdef HAS_DESKTOP_TAB
void void_xfce(AppData *app, const char *TARGETDIR);
void void_niri(AppData *app, const char *TARGETDIR);
void void_kde(AppData *app, const char *TARGETDIR);
void void_icewm(AppData *app, const char *TARGETDIR);
void void_i3(AppData *app, const char *TARGETDIR);
void void_jwm(AppData *app, const char *TARGETDIR);
void void_mate(AppData *app, const char *TARGETDIR);
void void_labwc(AppData *app, const char *TARGETDIR);
void void_lxqt(AppData *app, const char *TARGETDIR);
#endif /* HAS_DESKTOP_TAB */

// i18n (C)
void lang_init(void);
const char* get_loc(const char *key, int lang);

#ifdef __cplusplus
}
#endif

#endif /* NEKO_INSTALLER_H */
