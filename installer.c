/*
 * installer.c
 * LOCAL INSTALLATION + CUSTOM NANO VOID CONFIGS (Anon/Flatpak/LightDM)
 */
#include "neko_installer.h"
#include <sys/mount.h>
#include <sys/stat.h> // Needed for mkdir flags if used directly
#include <sys/types.h>
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
    return system(cmd);
}

gpointer install_thread(gpointer data) {
    AppData *app = (AppData *)data;
    char disk_path[64];
    char root_part[64];
    char efi_part[64];
    const char *TARGETDIR = "/mnt/target";
    
    // 1. Retrieve data
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
    
    log_to_ui(app, "--- STARTING LOCAL INSTALLATION (Live Copy) ---", 0.1);

    // 2. Mount Target
    log_to_ui(app, "Formatting and mounting target partitions...", 0.2);
    run_sync(app, "mkfs.ext4 -F %s", root_part);
    run_sync(app, "mkdir -p %s", TARGETDIR);
    mount(root_part, TARGETDIR, "ext4", 0, NULL);

    if (app->is_efi) {
        run_sync(app, "mkdir -p %s/boot/efi", TARGETDIR);
        run_sync(app, "mount %s %s/boot/efi", efi_part, TARGETDIR);
    }

    // 3. COPY ROOTFS
    log_to_ui(app, "Copying Live Image to Target...", 0.3);
    int ret = system("tar -cf - --one-file-system --xattrs / 2>/dev/null | tar --extract --xattrs --xattrs-include='*' --preserve-permissions -f - -C /mnt/target");
    
    if (WEXITSTATUS(ret) != 0) {
        log_to_ui(app, "ERROR: Failed to copy filesystem.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // 4. CLEANUP LIVE FILES
    log_to_ui(app, "Cleaning up live image files...", 0.4);
    run_sync(app, "rm -f %s/etc/motd", TARGETDIR);
    run_sync(app, "rm -f %s/etc/issue", TARGETDIR);
    run_sync(app, "rm -f %s/usr/sbin/void-installer", TARGETDIR);
    run_sync(app, "rm -f %s/etc/sddm.conf", TARGETDIR);
    run_sync(app, "sed -i 's|GETTY_ARGS=\"--noclear -a void\"|GETTY_ARGS=\"--noclear\"|g' %s/etc/sv/agetty-tty1/conf", TARGETDIR);

    // 5. MOUNT DEV/PROC/SYS
    log_to_ui(app, "Mounting virtual filesystems for configuration...", 0.5);
    run_sync(app, "mount --rbind /dev %s/dev", TARGETDIR);
    run_sync(app, "mount --rbind /proc %s/proc", TARGETDIR);
    run_sync(app, "mount --rbind /sys %s/sys", TARGETDIR);

    // 6. REBUILD INITRAMFS
    log_to_ui(app, "Rebuilding initramfs...", 0.6);
    run_sync(app, "chroot %s dracut --force --no-hostonly-cmdline", TARGETDIR);

    // 7. REMOVE TEMPORARY PACKAGES
    log_to_ui(app, "Removing temporary live packages...", 0.7);
    run_sync(app, "chroot %s xbps-remove -Ry dialog xtools-minimal xmirror espeakup brltty 2>/dev/null", TARGETDIR);

    // 8. CONFIGURATION (Hostname, Locale)
    log_to_ui(app, "Applying System Configuration...", 0.8);
    run_sync(app, "echo %s > %s/etc/hostname", hostname, TARGETDIR);
    run_sync(app, "sed -i 's/#%s/%s/' %s/etc/default/libc-locales", locale, locale, TARGETDIR);
    run_sync(app, "echo LANG=%s > %s/etc/locale.conf", locale, TARGETDIR);
    run_sync(app, "chroot %s xbps-reconfigure -f glibc-locales", TARGETDIR);

    // Root Password
    gchar *cmd_root = g_strdup_printf("echo 'root:%s' | chroot %s chpasswd", root_pass, TARGETDIR);
    system(cmd_root);
    g_free(cmd_root);

    // 9. USER CREATION & NEKO VOID CUSTOMIZATIONS
    if (strlen(user_login) > 0) {
        run_sync(app, "chroot %s useradd -m -G wheel,audio,video -s /bin/bash %s", TARGETDIR, user_login);
        gchar *cmd_user = g_strdup_printf("echo '%s:%s' | chroot %s chpasswd", user_login, user_pass, TARGETDIR);
        system(cmd_user);
        g_free(cmd_user);
        
        log_to_ui(app, "Applying Neko Void customizations (Flatpak, Themes)...", 0.82);

        // Copy Flatpak Data
        run_sync(app, "cp -rf /var/lib/flatpak %s/var/lib/", TARGETDIR);

        // Copy XBPS Repos
        // FIXED: Use run_sync to create directory in TARGETDIR
        run_sync(app, "mkdir -p %s/etc/xbps.d", TARGETDIR);
        run_sync(app, "cp -f /etc/xbps.d/* %s/etc/xbps.d/ 2>/dev/null", TARGETDIR);

        // Copy User Profile & Themes from 'anon'
        run_sync(app, "cp -f /home/.profile %s/home/%s/", TARGETDIR, user_login);
        // run_sync(app, "cp -rf /home/anon/.icons %s/home/%s/", TARGETDIR, user_login); // Commented
        run_sync(app, "cp -rf /home/anon/.themes %s/home/%s/", TARGETDIR, user_login);
        
        // Fix Ownership of copied files
        run_sync(app, "chown -R %s:users %s/home/%s", user_login, TARGETDIR, user_login);

        // AUTOLOGIN (LightDM)
        run_sync(app, "sed -i 's/^autologin-user=.*/autologin-user=%s/' %s/etc/lightdm/lightdm.conf", user_login, TARGETDIR);
        // Append if not exists
        run_sync(app, "grep -q '^autologin-user=' %s/etc/lightdm/lightdm.conf || sed -i '/^\\[Seat:\\*\\]/a autologin-user=%s' %s/etc/lightdm/lightdm.conf", TARGETDIR, user_login, TARGETDIR);

        // SUDOERS
        run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > %s/etc/sudoers.d/wheel", TARGETDIR);
        run_sync(app, "chmod 0440 %s/etc/sudoers.d/wheel", TARGETDIR);
    }

    // Clean up Polkit rules (Live only)
    run_sync(app, "rm -f %s/etc/polkit-1/rules.d/void-live.rules", TARGETDIR);

    // 10. BOOTLOADER
    log_to_ui(app, "Installing GRUB Bootloader...", 0.9);
    if (app->is_efi) {
        run_sync(app, "chroot %s grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=void_grub --recheck %s", TARGETDIR, disk_path);
    } else {
        run_sync(app, "chroot %s grub-install --recheck %s", TARGETDIR, disk_path);
    }
    
    run_sync(app, "chroot %s grub-mkconfig -o /boot/grub/grub.cfg", TARGETDIR);

    // 11. UNMOUNT & FINALIZE
    log_to_ui(app, "Unmounting filesystems...", 0.95);
    run_sync(app, "umount -R %s", TARGETDIR);
    
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