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
#include <errno.h>

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
        // Force unmount (no lazy flag — lazy hides mounts but keeps device busy)
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "fuser -km %s 2>/dev/null; umount -f %s 2>/dev/null", c->device, c->device);
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
        
        /* For encrypted partitions use the mapper device path directly;
         * crypttab opens the LUKS container before fstab is processed,
         * so /dev/mapper/cryptroot is guaranteed to exist at mount time.
         * UUID lookup on mapper devices is fragile across reboots. */
        char *fs_spec = NULL;
        if (conf->encrypt) {
            fs_spec = g_strdup(conf->device);  /* e.g. /dev/mapper/cryptroot */
        } else {
            char *uuid = get_uuid(conf->device);
            if (uuid) {
                fs_spec = g_strdup_printf("UUID=%s", uuid);
                g_free(uuid);
            } else {
                log_to_ui_printf(app, "Warning: No UUID for %s, using device path.", conf->device);
                fs_spec = g_strdup(conf->device);
            }
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
             fprintf(fp, "%s\t%s\t%s\t%s\t%d\t%d\n", fs_spec, "none", "swap", opts, dump, pass);
        } else {
             fprintf(fp, "%s\t%s\t%s\t%s\t%d\t%d\n", fs_spec, conf->mountpoint, conf->fstype, opts, dump, pass);
        }
        
        g_free(fs_spec);
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
            fprintf(fp, "cryptroot UUID=%s /boot/volume.key luks\n", conf->luks_uuid);
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
/* ------------------------------------------------------------------ *
 *  Deferred auto-scroll for the console log                           *
 *  ------------------------------------------------------------------ *
 *  After inserting text the GtkTextView layout has NOT been validated  *
 *  yet, so the scrolled-window adjustment still has the OLD upper     *
 *  value.  Attempting to scroll immediately therefore does nothing.    *
 *  We schedule the scroll at G_PRIORITY_LOW (300) which runs AFTER    *
 *  GTK_PRIORITY_RESIZE (110), guaranteeing the layout — and thus the  *
 *  adjustment — is up-to-date when we set the value.                  *
 * ------------------------------------------------------------------ */
static guint scroll_idle_id = 0;

static gboolean scroll_console_to_bottom(gpointer data) {
    AppData *app = (AppData *)data;
    scroll_idle_id = 0;
    if (!app->console_scroll) return G_SOURCE_REMOVE;

    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(app->console_scroll));
    if (vadj) {
        gdouble upper = gtk_adjustment_get_upper(vadj);
        gdouble page  = gtk_adjustment_get_page_size(vadj);
        if (upper > page)
            gtk_adjustment_set_value(vadj, upper - page);
    }
    return G_SOURCE_REMOVE;
}

gboolean update_log_ui(gpointer data) {
    LogMessage *msg = (LogMessage *)data;
    AppData *app = msg->app;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->console_text));

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, msg->message, -1);

    /* Queue a single deferred scroll (coalesced: only one pending at a time).
     * The G_PRIORITY_LOW callback fires after GTK finishes its resize /
     * layout pass, so the adjustment upper is already correct. */
    if (scroll_idle_id == 0)
        scroll_idle_id = g_idle_add_full(G_PRIORITY_LOW,
                                         scroll_console_to_bottom, app, NULL);

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

/* ------------------------------------------------------------------ *
 *  Streaming capture for run_sync                                     *
 *  ------------------------------------------------------------------ *
 *  run_sync executes commands with fork/pipe/exec (instead of system())
 *  so their real stdout+stderr can be shown in the GUI log, in real
 *  time. The worker (install) thread appends output lines to a buffer
 *  guarded by a mutex; a 100ms timer on the main thread flushes the
 *  buffer into the console via the existing update_log_ui() path.
 *  The command echo goes through the SAME buffer so ordering is exact.
 * ------------------------------------------------------------------ */
static GString *capture_pending = NULL;
static GMutex  capture_mutex;
static guint   capture_flush_id = 0;

static void capture_append(const char *s, gssize len) {
    g_mutex_lock(&capture_mutex);
    if (!capture_pending) capture_pending = g_string_new(NULL);
    g_string_append_len(capture_pending, s, len);
    g_mutex_unlock(&capture_mutex);
}

/* Append a chunk to the pending buffer stripping everything a terminal
 * would interpret but GtkTextView cannot render: control characters
 * (\b, \x1b ESC, NUL, \x7f, ...) and ANSI escape sequences ("ESC [ 0 m").
 * Keeps \n and \t. High bytes (multi-byte UTF-8) pass through untouched. */
static void capture_append_clean(const char *s, gssize len) {
    if (len <= 0) return;
    char *tmp = g_newa(char, len + 1);
    gssize j = 0;
    for (gssize i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\x1b') {
            /* ANSI escape sequence: skip ESC [ ... <final letter> */
            if (i + 1 < len && s[i + 1] == '[') {
                i += 2;
                while (i < len) {
                    unsigned char f = (unsigned char)s[i];
                    if ((f >= 'A' && f <= 'Z') || (f >= 'a' && f <= 'z')) break;
                    i++;
                }
            }
            continue;
        }
        if (c == '\n' || c == '\t') { tmp[j++] = (char)c; continue; }
        if (c < 0x20 || c == 0x7f) continue;   /* strip other control chars */
        tmp[j++] = (char)c;
    }
    if (j > 0) capture_append(tmp, j);
}

/* Runs on the main thread (GTK main loop) every 100ms. */
static gboolean capture_flush_cb(gpointer data) {
    AppData *app = data;
    char *out = NULL;

    g_mutex_lock(&capture_mutex);
    if (capture_pending && capture_pending->len > 0) {
        out = g_string_free(capture_pending, FALSE);   /* transfer content */
        capture_pending = NULL;
    }
    g_mutex_unlock(&capture_mutex);

    if (out) {
        /* Commands can emit non-UTF-8 bytes (locale-encoded output); GTK
         * requires valid UTF-8, otherwise it renders garbage glyphs. */
        gchar *valid = g_utf8_make_valid(out, -1);
        g_free(out);

        LogMessage *msg = g_new(LogMessage, 1);
        msg->message = valid;                         /* freed by update_log_ui */
        msg->fraction = -1.0;
        msg->app = app;
        g_idle_add(update_log_ui, msg);
    }
    return G_SOURCE_CONTINUE;
}

static void ensure_capture_flush(AppData *app) {
    if (capture_flush_id) return;
    capture_flush_id = g_timeout_add(100, capture_flush_cb, app);
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

    /* Echo the command to the log (same buffer as its output → correct order). */
    capture_append(cmd, strlen(cmd));
    capture_append("\n", 1);
    ensure_capture_flush(app);

    if (app->debug_mode) {
        g_print("[DEBUG-SIM] %s\n", cmd);
        return 0;
    }

    /* fork + pipe + exec: captures stdout AND stderr of the command so the
     * GUI shows its real output in real time (equivalent to system()). */
    int fds[2];
    if (pipe(fds) != 0) {
        log_to_ui(app, "ERROR: run_sync: pipe() failed.", 0.0);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_to_ui(app, "ERROR: run_sync: fork() failed.", 0.0);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    close(fds[1]);

    char buf[8192];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
        /* Split on \n AND \r (xbps-install paints progress with \r), and
         * strip control chars/ANSI so the GUI renders clean text. */
        char *start = buf;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n' || buf[i] == '\r') {
                capture_append_clean(start, &buf[i] - start);
                capture_append("\n", 1);
                start = &buf[i] + 1;
            }
        }
        if (start < &buf[n])
            capture_append_clean(start, &buf[n] - start);
    }
    close(fds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
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

    /* Disable every notebook page EXCEPT the install page (last page)
     * so the log scrollbar stays interactive during installation.
     * Setting the whole notebook insensitive would propagate to ALL
     * children, including the scrolled window and its scrollbars. */
    {
        gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
        for (gint i = 0; i < n - 1; i++) {
            GtkWidget *pg = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
            gtk_widget_set_sensitive(pg, FALSE);
        }
    }

    gtk_widget_set_sensitive(widget, FALSE);

    /* Start the 100ms log-flush timer for run_sync output streaming. */
    ensure_capture_flush(app);

    GError *error = NULL;
    g_thread_try_new("installer", install_thread, app, &error);
    if (error) g_printerr("Error creating thread: %s\n", error->message);
}