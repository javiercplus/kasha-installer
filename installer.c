/*
 * installer.c
 * FULL LOGIC: Replicating original void-installer steps
 */
#include "neko_installer.h"
#include <sys/mount.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

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
    int status = system(cmd);
    return WEXITSTATUS(status);
}

gpointer install_thread(gpointer data) {
    AppData *app = (AppData *)data;
    char disk_path[64];
    char root_part[64];
    char efi_part[64];
    const char *TARGETDIR = "/mnt/target";
    
    // 1. Retrieve data from UI
    const gchar *disk_name = app->selected_disk;
    if (!disk_name) {
        log_to_ui(app, "CRITICAL ERROR: No disk selected.", 0.0);
        app->installing = FALSE; return NULL;
    }
    
    // NVME vs SATA Fix
    gchar *part_suffix = "1";
    if (g_str_has_prefix(disk_name, "nvme") || g_str_has_prefix(disk_name, "mmcblk")) {
        part_suffix = "p1";
    }
    
    snprintf(disk_path, sizeof(disk_path), "/dev/%s", disk_name);
    snprintf(root_part, sizeof(root_part), "/dev/%s%s", disk_name, part_suffix);
    
    if (g_str_has_prefix(disk_name, "nvme") || g_str_has_prefix(disk_name, "mmcblk")) {
        snprintf(efi_part, sizeof(efi_part), "/dev/%sp2", disk_name);
    } else {
        snprintf(efi_part, sizeof(efi_part), "/dev/%s2", disk_name);
    }

    const gchar *root_pass = gtk_entry_get_text(GTK_ENTRY(app->root_pass_entry));
    const gchar *user_login = gtk_entry_get_text(GTK_ENTRY(app->user_login_entry));
    const gchar *user_pass = gtk_entry_get_text(GTK_ENTRY(app->user_pass_entry));
    const gchar *hostname = gtk_entry_get_text(GTK_ENTRY(app->hostname_entry));
    const gchar *locale = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->locale_combo));

    if (!root_pass || strlen(root_pass) < 1) { log_to_ui(app, "Error: Root password missing.", 0.0); app->installing = FALSE; return NULL; }
    
    log_to_ui(app, "--- STARTING NEKO VOID INSTALLATION ---", 0.1);

    // 2. Formatting & Mounting
    log_to_ui(app, "Formatting root partition...", 0.2);
    if (run_sync(app, "mkfs.ext4 -F %s", root_part) != 0) {
        log_to_ui(app, "ERROR formatting root.", 0.0); app->installing = FALSE; return NULL;
    }

    run_sync(app, "mkdir -p %s", TARGETDIR);
    if (mount(root_part, TARGETDIR, "ext4", 0, NULL) != 0) {
        log_to_ui(app, "ERROR mounting root.", 0.0); app->installing = FALSE; return NULL;
    }

    if (app->is_efi) {
        run_sync(app, "mkdir -p %s/boot/efi", TARGETDIR);
        run_sync(app, "mount %s %s/boot/efi", efi_part, TARGETDIR);
    }

    // 3. PREPARE XBPS ENVIRONMENT (The missing steps)
    log_to_ui(app, "Configuring XBPS environment (Keys & Repos)...", 0.3);
    run_sync(app, "mkdir -p %s/var/db/xbps/keys", TARGETDIR);
    run_sync(app, "cp /var/db/xbps/keys/*.plist %s/var/db/xbps/keys/ 2>/dev/null", TARGETDIR);
    
    run_sync(app, "mkdir -p %s/usr/share", TARGETDIR);
    run_sync(app, "cp -r /usr/share/xbps.d %s/usr/share/", TARGETDIR);
    
    // Optional: copy mirrors if they were configured by the user (skipped for this simple installer)
    // but we create the directory just in case.
    run_sync(app, "mkdir -p %s/etc/xbps.d", TARGETDIR);

    // 4. INSTALL PACKAGES (Replicating install_packages logic)
    log_to_ui(app, "Installing base system packages (download + install)...", 0.4);
    
    // Select GRUB package
    gchar *grub_pkg = "grub";
    if (app->is_efi) {
        grub_pkg = "grub-x86_64-efi"; // Simplified for x86_64
    }

    // Run xbps-install. We use system() because handling the environment vars in C is verbose.
    // This command includes the arch detection.
    run_sync(app, "XBPS_ARCH=$(xbps-uhelper arch) xbps-install -Sy -r %s base-system %s", TARGETDIR, grub_pkg);

    // 5. RECONFIGURE PACKAGES (The missing steps)
    log_to_ui(app, "Reconfiguring base-files...", 0.6);
    run_sync(app, "xbps-reconfigure -r %s -f base-files", TARGETDIR);
    
    log_to_ui(app, "Reconfiguring all packages (locales, initramfs)...", 0.7);
    // This step might take a while (glibc-locales, dracut, etc)
    run_sync(app, "chroot %s xbps-reconfigure -a", TARGETDIR);

    // 6. System Settings
    log_to_ui(app, "Applying configuration (Hostname, Locale)...", 0.8);
    run_sync(app, "echo %s > %s/etc/hostname", hostname, TARGETDIR);
    
    // Setup Locale
    run_sync(app, "sed -i 's/#%s/%s/' %s/etc/default/libc-locales", locale, locale, TARGETDIR);
    run_sync(app, "echo LANG=%s > %s/etc/locale.conf", locale, TARGETDIR);
    // Reconfigure glibc-locales again to be sure
    run_sync(app, "chroot %s xbps-reconfigure -f glibc-locales", TARGETDIR);

    // 7. Users
    log_to_ui(app, "Configuring users...", 0.85);
    gchar *cmd_root = g_strdup_printf("echo 'root:%s' | chroot %s chpasswd", root_pass, TARGETDIR);
    system(cmd_root);
    g_free(cmd_root);

    if (strlen(user_login) > 0) {
        run_sync(app, "chroot %s useradd -m -G wheel,audio,video -s /bin/bash %s", TARGETDIR, user_login);
        gchar *cmd_user = g_strdup_printf("echo '%s:%s' | chroot %s chpasswd", user_login, user_pass, TARGETDIR);
        system(cmd_user);
        g_free(cmd_user);
        
        run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > %s/etc/sudoers.d/10-wheel", TARGETDIR);
        run_sync(app, "chmod 0440 %s/etc/sudoers.d/10-wheel", TARGETDIR);
    }

    // 8. Bootloader
    log_to_ui(app, "Installing GRUB...", 0.9);
    if (app->is_efi) {
        run_sync(app, "grub-install --target=x86_64-efi --efi-directory=%s/boot/efi --bootloader-id=void_grub --recheck %s", TARGETDIR, disk_path);
    } else {
        run_sync(app, "grub-install --recheck %s", disk_path);
    }
    run_sync(app, "chroot %s grub-mkconfig -o /boot/grub/grub.cfg", TARGETDIR);

    log_to_ui(app, "--- INSTALLATION COMPLETED ---", 1.0);
    app->installing = FALSE;
    return NULL;
}

void start_installation(GtkWidget *widget, AppData *app) {
    if (app->installing) return;
    app->installing = TRUE;
    gtk_widget_set_sensitive(widget, FALSE);
    
    GError *error = NULL;
    g_thread_try_new("installer", install_thread, app, &error);
    if (error) g_printerr("Error creating thread: %s\n", error->message);
}