/*
 * installer.c
 * Orchestrator
 */
#include "neko_installer.h"
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// Helper to get UUID
char* get_uuid(const char *device) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "blkid -s UUID -o value %s", device);
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    
    char uuid[128];
    if (fgets(uuid, sizeof(uuid), fp) != NULL) {
        // Strip newline
        size_t len = strlen(uuid);
        if (len > 0 && uuid[len-1] == '\n') uuid[len-1] = '\0';
        pclose(fp);
        return g_strdup(uuid);
    }
    pclose(fp);
    return NULL;
}

// Get the path depth: 0 if root; number of slashes otherwise.
static gint path_depth(const char *path) {
    if (strcmp(path, "/") == 0)
        return 0;

    gint no_slashes = 0;

    gsize n = 0;
    while (path[n]) {
        if (path[n] == '/')
            no_slashes++;

        n++;
    }

    // Omit any trailing slashes, except root itself (normalization)
    if (n > 2 && path[n-1] == '/')
        no_slashes--;

    return no_slashes;
}

// Comparator: Proper mount order (by mountpoint depth; root first)
gint sort_partitions(gconstpointer a, gconstpointer b) {
    const PartitionConfig *pa = (const PartitionConfig*)a;
    const PartitionConfig *pb = (const PartitionConfig*)b;
    const char *ma = pa->mountpoint;
    const char *mb = pb->mountpoint;

    return path_depth(ma) - path_depth(mb);
}

// Safety Unmount
void unmount_safety(AppData *app) {
    GSList *l = app->part_config_list;
    while(l) {
        PartitionConfig *c = (PartitionConfig*)l->data;
        // Lazy unmount device to be safe
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "umount -lf %s 2>/dev/null", c->device);
        system(cmd);
        l = l->next;
    }
}

void generate_fstab(AppData *app, const char *target_dir) {
    gchar *fstab_path = g_strdup_printf("%s/etc/fstab", target_dir);
    FILE *fp = fopen(fstab_path, "w");
    if (!fp) {
        log_to_ui(app, "ERROR: Could not write to /etc/fstab!", 0.0);
        g_free(fstab_path);
        return;
    }
    
    log_to_ui(app, "Generating /etc/fstab...", 0.65);
    
    fprintf(fp, "# /etc/fstab: static file system information.\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# <file system> <mount point>   <type>  <options>       <dump>  <pass>\n\n");
    
    GSList *l = app->part_config_list;
    while (l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;
        
        char *uuid = get_uuid(conf->device);
        if (!uuid) {
            log_to_ui_printf(app, "Warning: No UUID for %s, using device path.", conf->device);
            uuid = g_strdup(conf->device);
        }
        
        // Options
        const char *opts = "defaults";
        int dump = 0;
        int pass = 2; // others
        
        if (strcmp(conf->mountpoint, "/") == 0) {
            // btrfs, xfs, f2fs have their own check mechanisms, pass=0
            if (strcmp(conf->fstype, "btrfs") == 0 || strcmp(conf->fstype, "xfs") == 0 || strcmp(conf->fstype, "f2fs") == 0) {
                pass = 0;
            } else {
                pass = 1;
            }
        } else if (strcmp(conf->fstype, "swap") == 0) {
            opts = "sw";
            pass = 0;
        } else if (strcmp(conf->fstype, "vfat") == 0 || strcmp(conf->fstype, "fat32") == 0) {
            opts = "umask=0077";
            pass = 2;
        }
        
        if (strcmp(conf->fstype, "swap") == 0) {
             fprintf(fp, "UUID=%s\t%s\t%s\t%s\t%d\t%d\n", uuid, "none", "swap", opts, dump, pass);
        } else {
             fprintf(fp, "UUID=%s\t%s\t%s\t%s\t%d\t%d\n", uuid, conf->mountpoint, conf->fstype, opts, dump, pass);
        }
        
        g_free(uuid);
        l = l->next;
    }

    // Mount /tmp as tmpfs
    fprintf(fp, "\n# tmpfs\ntmpfs\t/tmp\ttmpfs\tdefaults,nosuid,nodev\t0\t0\n");

    fclose(fp);
    g_free(fstab_path);
}

void generate_crypttab(AppData *app, const char *target_dir) {
    gboolean has_crypto = FALSE;
    GSList *l = app->part_config_list;
    while(l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;
        if (conf->encrypt) { has_crypto = TRUE; break; }
        l = l->next;
    }
    
    if (!has_crypto) return;
    
    gchar *crypttab_path = g_strdup_printf("%s/etc/crypttab", target_dir);
    FILE *fp = fopen(crypttab_path, "w");
    if (!fp) {
        log_to_ui(app, "ERROR: Could not write to /etc/crypttab!", 0.0);
        g_free(crypttab_path);
        return;
    }
    
    log_to_ui(app, "Generating /etc/crypttab...", 0.66);
    
    fprintf(fp, "# /etc/crypttab: encrypted block devices\n");
    fprintf(fp, "# <mapper name> <device> <key file> <options>\n\n");
    
    l = app->part_config_list;
    while(l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;
        if (conf->encrypt && conf->luks_pass && conf->luks_uuid) {
            fprintf(fp, "cryptroot UUID=%s none luks\n", conf->luks_uuid);
        }
        l = l->next;
    }
    
    fclose(fp);
    g_free(crypttab_path);
}

static gboolean pulse_progress_bar(gpointer data) {
    AppData *app = (AppData *)data;
    if (app->progress_bar && !app->installing) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(app->progress_bar));
    }
    return TRUE;
}

void start_progress_pulse(AppData *app) {
    if (app->progress_pulse_id > 0) return;
    app->progress_pulse_id = g_timeout_add(100, pulse_progress_bar, app);
}

void stop_progress_pulse(AppData *app) {
    if (app->progress_pulse_id > 0) {
        g_source_remove(app->progress_pulse_id);
        app->progress_pulse_id = 0;
    }
}

gboolean update_log_ui(gpointer data) {
    LogMessage *msg = (LogMessage *)data;
    AppData *app = msg->app;
    
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->console_text));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, msg->message, -1);
    
    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(app->console_text), mark);
    
    if (msg->fraction >= 0) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), msg->fraction);
    }
    g_free(msg->message);
    g_free(msg);
    return FALSE;
}

void log_to_ui(AppData *app, const char *msg, gdouble fraction) {
    LogMessage *log_msg = g_new(LogMessage, 1);
    log_msg->message = g_strdup_printf("%s\n", msg);
    log_msg->fraction = fraction;
    log_msg->app = app; 
    g_idle_add(update_log_ui, log_msg);
}

void log_to_ui_printf(AppData *app, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gchar *msg = g_strdup_vprintf(fmt, args);
    va_end(args);
    log_to_ui(app, msg, -1.0);
    g_free(msg);
}


/*
 * Validates and normalizes hostname to lowercase only.
 * ALWAYS returns a heap-allocated string (g_strdup) so the caller can
 * safely call g_free() on the result in every code path.
 */
gchar *validate_and_normalize_hostname(const gchar *raw_hostname) {
    if (!raw_hostname || strlen(raw_hostname) == 0) {
        log_to_ui(NULL, "ERROR: Hostname cannot be empty.", 0.0);
        return g_strdup("localhost"); /* always heap-allocated */
    }

    /* Check for invalid characters (only allow alphanumeric, hyphen, dot, underscore) */
    for (int i = 0; raw_hostname[i]; i++) {
        if (!((raw_hostname[i] >= 'a' && raw_hostname[i] <= 'z') ||
              (raw_hostname[i] >= '0' && raw_hostname[i] <= '9') ||
              raw_hostname[i] == '-' || raw_hostname[i] == '.' ||
              raw_hostname[i] == '_')) {
            log_to_ui(NULL, "ERROR: Hostname contains invalid characters. "
                            "Only lowercase letters, numbers, hyphens, dots, "
                            "and underscores are allowed.", 0.0);
            return g_strdup("localhost"); /* always heap-allocated */
        }
    }

    /* Convert to lowercase */
    gchar *hostname = g_strdup(raw_hostname);
    for (int i = 0; hostname[i]; i++) {
        if (hostname[i] >= 'A' && hostname[i] <= 'Z') {
            hostname[i] = tolower(hostname[i]);
        }
    }

    /* Hostname cannot start or end with a hyphen or dot */
    size_t hlen = strlen(hostname);
    if (hlen > 1 && (hostname[0] == '-' || hostname[0] == '.' ||
                     hostname[hlen - 1] == '-' || hostname[hlen - 1] == '.')) {
        log_to_ui(NULL, "ERROR: Hostname cannot start or end with a hyphen or dot.", 0.0);
        g_free(hostname);
        return g_strdup("localhost"); /* always heap-allocated */
    }

    return hostname; /* heap-allocated via g_strdup above */
}

int run_sync(AppData *app, const char *fmt, ...) {
    /* 4096 bytes: generous enough for the longest commands we build
     * (e.g. the chown chain in step_install_base_system with TARGETDIR
     * repeated ~18 times).  1024 was too small and silently truncated. */
    char cmd[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);

    log_to_ui(app, cmd, -1.0);

    if (app->debug_mode) {
        g_print("[DEBUG-SIM] %s\n", cmd);
        return 0;
    }

    int ret = system(cmd);
    if (ret == -1) return -1;
    if (WIFEXITED(ret)) return WEXITSTATUS(ret);
    return -1;
}

gboolean set_safe_password(AppData *app, const gchar *username, const gchar *password, const gchar *target_dir) {
    char target_tmp[256];
    snprintf(target_tmp, sizeof(target_tmp), "%s/tmp/.kasha_%s", target_dir, username);
    
    FILE *fp = fopen(target_tmp, "w");
    if (!fp) {
        log_to_ui(app, "ERROR: Cannot create password file in target.", 0.0);
        return FALSE;
    }
    fprintf(fp, "%s:%s\n", username, password);
    fclose(fp);
    chmod(target_tmp, 0600);
    
    char cmd_chroot[512];
    snprintf(cmd_chroot, sizeof(cmd_chroot), "chroot %s chpasswd -c SHA512 < %s", target_dir, target_tmp);
    int ret = system(cmd_chroot);

    remove(target_tmp);

    /* Use WIFEXITED/WEXITSTATUS so that signals (SIGTERM, SIGKILL) are also
     * treated as errors, not silently swallowed. */
    if (ret == -1 || !WIFEXITED(ret) || WEXITSTATUS(ret) != 0) {
        log_to_ui(app, "ERROR: Failed to set password.", 0.0);
        return FALSE;
    }
    return TRUE;
}

gpointer install_thread(gpointer data) {
    AppData *app = (AppData *)data;
    const char *TARGETDIR = "/mnt/target";
    
    if (app->debug_mode) {
        app->installing = TRUE;
        start_progress_pulse(app);
        
        log_to_ui(app, "[DEBUG MODE] Simulating installation...", 0.0);
        g_usleep(500000);
        log_to_ui(app, "Simulating: Formatting partitions...", 0.2);
        g_usleep(500000);
        log_to_ui(app, "Simulating: Mounting filesystems...", 0.4);
        g_usleep(500000);
        log_to_ui(app, "Simulating: Installing base system...", 0.6);
        g_usleep(500000);
        log_to_ui(app, "Simulating: Configuring system...", 0.8);
        g_usleep(500000);
        log_to_ui(app, "Simulating: Installing bootloader...", 0.9);
        g_usleep(500000);
        
        stop_progress_pulse(app);
        log_to_ui(app, "--- DEBUG INSTALLATION SIMULATED ---", 1.0);
        app->installing = FALSE;
        set_ui_finished(app);
        return NULL;
    }
    
    start_progress_pulse(app);
    unmount_safety(app);
    
    if (!app->part_config_list) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: No partitions configured. Use Add/Edit Partition in Tab 1.", 0.0);
        app->installing = FALSE; return NULL;
    }

    const gchar *disk_name = app->selected_disk;
    const gchar *root_pass = gtk_entry_get_text(GTK_ENTRY(app->root_pass_entry));
    const gchar *user_login = gtk_entry_get_text(GTK_ENTRY(app->user_login_entry));
    const gchar *user_pass = gtk_entry_get_text(GTK_ENTRY(app->user_pass_entry));
    const gchar *user_fullname = gtk_entry_get_text(GTK_ENTRY(app->user_fullname_entry));
    const gchar *raw_hostname = gtk_entry_get_text(GTK_ENTRY(app->hostname_entry));
    /* validate_and_normalize_hostname always returns a heap pointer — must be freed. */
    gchar *hostname = validate_and_normalize_hostname(raw_hostname);
    gchar *locale_selected = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->locale_combo));
    const gchar *locale = (locale_selected && strlen(locale_selected) > 0) ? locale_selected : "en_US.UTF-8";
    gboolean autologin_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->autologin_check));

    if (!root_pass || strlen(root_pass) < 1) { 
        stop_progress_pulse(app);
        log_to_ui(app, "Error: Root password missing.", 0.0); 
        goto fail_cleanup;
    }
    
    log_to_ui(app, "--- STARTING LOCAL INSTALLATION ---", 0.1);

    // Validate partition config
    int r = 0;
    GSList *l = app->part_config_list;
    while (l) {
        PartitionConfig *c = (PartitionConfig*)l->data;
        if (strcmp(c->mountpoint, "/") == 0) { r = 1; break; }
        l = l->next;
    }
    if (!r) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: Root partition not configured.", 0.0);
        goto fail_cleanup;
    }

    if (step_partitioning(app, disk_name) != 0) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: Partitioning failed!", 0.0);
        goto fail_cleanup;
    }

    if (step_format_and_mount(app, TARGETDIR) != 0) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: Format and mount failed!", 0.0);
        goto fail_cleanup;
    }
    
    if (step_install_base_system(app, TARGETDIR) != 0) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: Base system installation failed!", 0.0);
        goto fail_cleanup;
    }
    
    if (step_configure_system(app, TARGETDIR, hostname, locale, root_pass, user_login, user_fullname, user_pass, autologin_enabled) != 0) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: System configuration failed!", 0.0);
        goto fail_cleanup;
    }
    
    if (step_install_bootloader(app, TARGETDIR, disk_name) != 0) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: Bootloader installation failed!", 0.0);
        goto fail_cleanup;
    }

    step_finalize(app, TARGETDIR);
    
    stop_progress_pulse(app);
    log_to_ui(app, "--- INSTALLATION COMPLETED ---", 1.0);
    g_free(hostname);
    g_free(locale_selected);
    app->installing = FALSE;
    set_ui_finished(app);
    return NULL;

fail_cleanup:
    g_free(hostname);
    g_free(locale_selected);
    app->installing = FALSE;
    return NULL;
}

void start_installation(GtkWidget *widget, AppData *app) {
    if (app->installing) return;
    app->installing = TRUE;
    gtk_widget_set_sensitive(app->btn_back, FALSE);
    gtk_widget_set_sensitive(app->btn_next, FALSE);
    gtk_widget_set_sensitive(app->notebook, FALSE);
    gtk_widget_set_sensitive(widget, FALSE);
    
    GError *error = NULL;
    g_thread_try_new("installer", install_thread, app, &error);
    if (error) g_printerr("Error creating thread: %s\n", error->message);
}