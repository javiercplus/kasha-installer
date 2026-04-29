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

// Comparator: Shortest mountpoint first
gint sort_partitions(gconstpointer a, gconstpointer b) {
    const PartitionConfig *pa = (const PartitionConfig*)a;
    const PartitionConfig *pb = (const PartitionConfig*)b;
    gsize la = strlen(pa->mountpoint);
    gsize lb = strlen(pb->mountpoint);
    return (la > lb) - (la < lb);
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
            log_to_ui(app, g_strdup_printf("Warning: No UUID for %s, using device path.", conf->device), 0.0);
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

int run_sync(AppData *app, const char *fmt, ...) {
    char cmd[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);
    
    log_to_ui(app, cmd, -1.0);
    
    if (app->debug_mode) {
        g_print("[DEBUG-SIM] %s\n", cmd);
        return 0;
    }
    
    return system(cmd);
}

void set_safe_password(AppData *app, const gchar *username, const gchar *password, const gchar *target_dir) {
    char live_tmp[256];
    char chroot_tmp[256];
    snprintf(live_tmp, sizeof(live_tmp), "/tmp/.kasha_%s", username);
    snprintf(chroot_tmp, sizeof(chroot_tmp), "%s/tmp/.kasha_%s", target_dir, username);
    FILE *fp = fopen(live_tmp, "w");
    if (fp) {
        fprintf(fp, "%s:%s\n", username, password);
        fclose(fp);
        chmod(live_tmp, 0600); 
    } else {
        log_to_ui(app, "ERROR: Cannot create temp password file.", 0.0);
        return;
    }
    char cmd_cp[512];
    snprintf(cmd_cp, sizeof(cmd_cp), "cp %s %s", live_tmp, chroot_tmp);
    system(cmd_cp);
    char cmd_chroot[512];
    snprintf(cmd_chroot, sizeof(cmd_chroot), "chroot %s chpasswd -c SHA512 < /tmp/.kasha_%s", target_dir, username);
    system(cmd_chroot);
    remove(live_tmp);
    remove(chroot_tmp);
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
    const gchar *hostname = gtk_entry_get_text(GTK_ENTRY(app->hostname_entry));
    const gchar *locale = "en_US.UTF-8";
    gboolean autologin_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->autologin_check));

    if (!root_pass || strlen(root_pass) < 1) { 
        stop_progress_pulse(app);
        log_to_ui(app, "Error: Root password missing.", 0.0); 
        app->installing = FALSE; return NULL; 
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
        app->installing = FALSE; return NULL;
    }

    if (step_partitioning(app, disk_name) != 0) {
        stop_progress_pulse(app);
        app->installing = FALSE; return NULL;
    }

    if (step_format_and_mount(app, TARGETDIR) != 0) {
        app->installing = FALSE; return NULL;
    }
    
    if (step_install_base_system(app, TARGETDIR) != 0) {
        app->installing = FALSE; return NULL;
    }
    
    if (step_configure_system(app, TARGETDIR, hostname, locale, root_pass, user_login, user_fullname, user_pass, autologin_enabled) != 0) {
        app->installing = FALSE; return NULL;
    }
    
    if (step_install_bootloader(app, TARGETDIR, disk_name) != 0) {
        stop_progress_pulse(app);
        app->installing = FALSE; return NULL;
    }

    step_finalize(app, TARGETDIR);
    
    stop_progress_pulse(app);
    log_to_ui(app, "--- INSTALLATION COMPLETED ---", 1.0);
    app->installing = FALSE;
    set_ui_finished(app);
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