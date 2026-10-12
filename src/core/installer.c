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

/* ------------------------------------------------------------------ *
 *  Thread-safe log queue                                              *
 *  ------------------------------------------------------------------ *
 *  The install worker thread pushes CoreLogEntry items; the wxWidgets *
 *  UI drains them from a timer on the main thread. This replaces the  *
 *  old g_idle_add() marshalling that was coupled to the GTK loop.     *
 * ------------------------------------------------------------------ */
static GMutex log_mutex;
static GQueue *log_queue = NULL;

static void log_queue_init(void) {
    if (!log_queue) log_queue = g_queue_new();
}

static void log_queue_push(CoreLogEntry *e) {
    log_queue_init();
    g_mutex_lock(&log_mutex);
    g_queue_push_tail(log_queue, e);
    g_mutex_unlock(&log_mutex);
}

CoreLogEntry *core_drain_log(void) {
    CoreLogEntry *head = NULL, *tail = NULL;
    if (!log_queue) return NULL;
    g_mutex_lock(&log_mutex);
    while (!g_queue_is_empty(log_queue)) {
        CoreLogEntry *e = (CoreLogEntry *)g_queue_pop_head(log_queue);
        e->next = NULL;
        if (tail) tail->next = e; else head = e;
        tail = e;
    }
    g_mutex_unlock(&log_mutex);
    return head;
}

void log_to_ui(AppData *app, const char *msg, gdouble fraction) {
    (void)app;
    CoreLogEntry *e = g_new0(CoreLogEntry, 1);
    e->message = g_strdup_printf("%s\n", msg ? msg : "");
    e->fraction = fraction;
    log_queue_push(e);
}

void log_to_ui_printf(AppData *app, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gchar *msg = g_strdup_vprintf(fmt, args);
    va_end(args);
    log_to_ui(app, msg, -1.0);
    g_free(msg);
}

void set_ui_finished(AppData *app) {
    app->finished = TRUE;
    CoreLogEntry *e = g_new0(CoreLogEntry, 1);
    e->message = g_strdup("");
    e->fraction = -1.0;
    e->finished = 1;
    log_queue_push(e);
}

/* Indeterminate progress is now just a flag the UI's pulse timer reads. */
static void start_progress_pulse(AppData *app) { app->pulsing = TRUE; }
static void stop_progress_pulse(AppData *app)  { app->pulsing = FALSE; }

/* ------------------------------------------------------------------ *
 *  Streaming capture for run_sync                                     *
 *  ------------------------------------------------------------------ *
 *  run_sync executes commands with fork/pipe/exec (instead of system())
 *  so their real stdout+stderr can be shown in the GUI log, in real
 *  time. The worker (install) thread appends output lines to a buffer
 *  guarded by a mutex; the UI's 100ms timer calls core_flush_capture()
 *  to move that buffer into the log queue on the main thread.
 * ------------------------------------------------------------------ */
static GString *capture_pending = NULL;
static GMutex capture_mutex;

static void capture_append(const char *s, gssize len) {
    g_mutex_lock(&capture_mutex);
    if (!capture_pending) capture_pending = g_string_new(NULL);
    g_string_append_len(capture_pending, s, len);
    g_mutex_unlock(&capture_mutex);
}

/* Append a chunk to the pending buffer stripping everything a terminal
 * would interpret but the console widget cannot render: control characters
 * (\b, \x1b ESC, NUL, \x7f, ...) and ANSI escape sequences ("ESC [ 0 m").
 * Keeps \n and \t. High bytes (multi-byte UTF-8) pass through untouched. */
static void capture_append_clean(const char *s, gssize len) {
    if (len <= 0) return;
    char *tmp = g_newa(char, len + 1);
    gssize j = 0;
    for (gssize i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\x1b') {
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
        if (c < 0x20 || c == 0x7f) continue;
        tmp[j++] = (char)c;
    }
    if (j > 0) capture_append(tmp, j);
}

/* Runs on the UI's timer (main thread) — swaps the buffer and pushes it. */
void core_flush_capture(AppData *app) {
    (void)app;
    char *out = NULL;
    g_mutex_lock(&capture_mutex);
    if (capture_pending && capture_pending->len > 0) {
        out = g_string_free(capture_pending, FALSE);
        capture_pending = NULL;
    }
    g_mutex_unlock(&capture_mutex);

    if (out) {
        gchar *valid = g_utf8_make_valid(out, -1);
        g_free(out);

        CoreLogEntry *e = g_new0(CoreLogEntry, 1);
        e->message = valid;
        e->fraction = -1.0;
        log_queue_push(e);
    }
}

// Helper to get UUID
char* get_uuid(const char *device) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "blkid -s UUID -o value %s", device);
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    char uuid[128];
    if (fgets(uuid, sizeof(uuid), fp) != NULL) {
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

        char *fs_spec = NULL;
        if (conf->encrypt) {
            fs_spec = g_strdup(conf->device);
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

        const char *opts = "defaults";
        int dump = 0;
        int pass = 2;

        if (strcmp(conf->mountpoint, "/") == 0) {
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

/*
 * Validates and normalizes hostname to lowercase only.
 * ALWAYS returns a heap-allocated string (g_strdup).
 */
gchar *validate_and_normalize_hostname(const gchar *raw_hostname) {
    if (!raw_hostname || strlen(raw_hostname) == 0) {
        log_to_ui(NULL, "ERROR: Hostname cannot be empty.", 0.0);
        return g_strdup("localhost");
    }

    for (int i = 0; raw_hostname[i]; i++) {
        if (!((raw_hostname[i] >= 'a' && raw_hostname[i] <= 'z') ||
              (raw_hostname[i] >= '0' && raw_hostname[i] <= '9') ||
              raw_hostname[i] == '-' || raw_hostname[i] == '.' ||
              raw_hostname[i] == '_')) {
            log_to_ui(NULL, "ERROR: Hostname contains invalid characters. "
                            "Only lowercase letters, numbers, hyphens, dots, "
                            "and underscores are allowed.", 0.0);
            return g_strdup("localhost");
        }
    }

    gchar *hostname = g_strdup(raw_hostname);
    for (int i = 0; hostname[i]; i++) {
        if (hostname[i] >= 'A' && hostname[i] <= 'Z') {
            hostname[i] = tolower(hostname[i]);
        }
    }

    size_t hlen = strlen(hostname);
    if (hlen > 1 && (hostname[0] == '-' || hostname[0] == '.' ||
                     hostname[hlen - 1] == '-' || hostname[hlen - 1] == '.')) {
        log_to_ui(NULL, "ERROR: Hostname cannot start or end with a hyphen or dot.", 0.0);
        g_free(hostname);
        return g_strdup("localhost");
    }

    return hostname;
}

int run_sync(AppData *app, const char *fmt, ...) {
    char cmd[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);

    capture_append(cmd, strlen(cmd));
    capture_append("\n", 1);

    if (app->debug_mode) {
        g_print("[DEBUG-SIM] %s\n", cmd);
        return 0;
    }

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

    if (ret == -1 || !WIFEXITED(ret) || WEXITSTATUS(ret) != 0) {
        log_to_ui(app, "ERROR: Failed to set password.", 0.0);
        return FALSE;
    }
    return TRUE;
}

static gpointer install_thread(gpointer data) {
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
    const gchar *root_pass = app->ui_root_pass;
    const gchar *user_login = app->ui_user_login;
    const gchar *user_pass = app->ui_user_pass;
    const gchar *user_fullname = app->ui_user_fullname;
    const gchar *raw_hostname = app->ui_hostname;
    gchar *hostname = validate_and_normalize_hostname(raw_hostname);
    const gchar *locale = (app->ui_locale && strlen(app->ui_locale) > 0) ? app->ui_locale : "en_US.UTF-8";
    gboolean autologin_enabled = app->ui_autologin;

    if (!root_pass || strlen(root_pass) < 1) {
        stop_progress_pulse(app);
        log_to_ui(app, "Error: Root password missing.", 0.0);
        g_free(hostname);
        app->installing = FALSE;
        return NULL;
    }

    log_to_ui(app, "--- STARTING LOCAL INSTALLATION ---", 0.1);

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
        g_free(hostname);
        app->installing = FALSE;
        return NULL;
    }

    if (step_partitioning(app, disk_name) != 0) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: Partitioning failed!", 0.0);
        g_free(hostname);
        app->installing = FALSE;
        return NULL;
    }

    if (step_format_and_mount(app, TARGETDIR) != 0) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: Format and mount failed!", 0.0);
        g_free(hostname);
        app->installing = FALSE;
        return NULL;
    }

    if (step_install_base_system(app, TARGETDIR) != 0) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: Base system installation failed!", 0.0);
        g_free(hostname);
        app->installing = FALSE;
        return NULL;
    }

    if (step_configure_system(app, TARGETDIR, hostname, locale, root_pass, user_login, user_fullname, user_pass, autologin_enabled) != 0) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: System configuration failed!", 0.0);
        g_free(hostname);
        app->installing = FALSE;
        return NULL;
    }

    if (step_install_bootloader(app, TARGETDIR, disk_name) != 0) {
        stop_progress_pulse(app);
        log_to_ui(app, "ERROR: Bootloader installation failed!", 0.0);
        g_free(hostname);
        app->installing = FALSE;
        return NULL;
    }

    step_finalize(app, TARGETDIR);

    stop_progress_pulse(app);
    log_to_ui(app, "--- INSTALLATION COMPLETED ---", 1.0);
    g_free(hostname);
    app->installing = FALSE;
    set_ui_finished(app);
    return NULL;
}

void start_installation(AppData *app) {
    if (app->installing) return;
    app->installing = TRUE;

    GError *error = NULL;
    g_thread_try_new("installer", install_thread, app, &error);
    if (error) g_printerr("Error creating thread: %s\n", error->message);
}
