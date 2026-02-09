/*
 * installer.c
 * FINAL VERSION: Clean partition logic, Popup support, SHA512 passwords, Progress Pulse.
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

// --- UI HELPERS ---
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
    } else {
        // Si es un paso sin porcentaje (-1.0), pulsar la barra
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(app->progress_bar));
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
    // Nota: chpasswd se ejecuta dentro de chroot, leyendo el archivo que copiamos
    snprintf(cmd_chroot, sizeof(cmd_chroot), "chroot %s chpasswd -c SHA512 < /tmp/.kasha_%s", target_dir, username);
    system(cmd_chroot);
    
    remove(live_tmp);
    remove(chroot_tmp);
}

// --- INSTALLATION THREAD ---
gpointer install_thread(gpointer data) {
    AppData *app = (AppData *)data;
    const char *TARGETDIR = "/mnt/target";
    
    // CHECK: Did user add partitions?
    if (!app->part_config_list) {
        log_to_ui(app, "ERROR: No partitions configured. Use 'Add/Edit Partition' in Tab 1.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // GET CONFIG
    const gchar *disk_name = app->selected_disk;
    const gchar *root_pass = gtk_entry_get_text(GTK_ENTRY(app->root_pass_entry));
    const gchar *user_login = gtk_entry_get_text(GTK_ENTRY(app->user_login_entry));
    const gchar *user_pass = gtk_entry_get_text(GTK_ENTRY(app->user_pass_entry));
    const gchar *hostname = gtk_entry_get_text(GTK_ENTRY(app->hostname_entry));
    const gchar *locale = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->locale_combo));

    if (!root_pass || strlen(root_pass) < 1) { log_to_ui(app, "Error: Root password missing.", 0.0); app->installing = FALSE; return NULL; }
    
    log_to_ui(app, "--- STARTING LOCAL INSTALLATION ---", 0.1);

    // 1. FORMAT AND MOUNT PARTITIONS
    log_to_ui(app, "Configuring partitions from user list...", 0.2);
    GSList *l = app->part_config_list;
    while(l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;
        
        gchar *fs_cmd = NULL;
        if (conf->format) {
             log_to_ui(app, g_strdup_printf("Formatting %s as %s...", conf->device, conf->fstype), 0.25);
             
             if (strcmp(conf->fstype, "ext4") == 0) fs_cmd = "mkfs.ext4 -F";
             else if (strcmp(conf->fstudo, "btrfs") == 0) fs_cmd = "mkfs.btrfs -f";
             else if (strcmp(conf->fstype, "xfs") == 0) fs_cmd = "mkfs.xfs -f";
             else if (strcmp(conf->fstype, "f2fs") == 0) fs_cmd = "mkfs.f2fs -f";
             else if (strcmp(conf->fstype, "vfat") == 0) fs_cmd = "mkfs.vfat -F32";
             else if (strcmp(conf->fstype, "swap") == 0) fs_cmd = "mkswap";
             
             if (fs_cmd) {
                 run_sync(app, "%s %s", fs_cmd, conf->device);
             }
        }
        
        // Mounting / Swapon
        if (strcmp(conf->fstype, "swap") == 0) {
            run_sync(app, "swapon %s", conf->device);
        } else {
            gchar *target_path = g_strdup_printf("%s%s", TARGETDIR, conf->mountpoint);
            log_to_ui(app, g_strdup_printf("Mounting %s to %s...", conf->device, target_path), 0.28);
            run_sync(app, "mkdir -p %s", target_path);
            mount(conf->device, target_path, conf->fstype, 0, NULL);
            g_free(target_path);
        }
        
        l = l->next;
    }

    // 2. COPY ROOTFS
    log_to_ui(app, "Copying Live Image to Target...", 0.3);
    int ret = system("tar -cf - --one-file-system --xattrs / 2>/dev/null | tar --extract --xattrs --xattrs-include='*' --preserve-permissions -f - -C /mnt/target");
    if (WEXITSTATUS(ret) != 0) {
        log_to_ui(app, "ERROR: Failed to copy filesystem.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // 3. CLEANUP LIVE FILES
    log_to_ui(app, "Cleaning up live image files...", 0.4);
    run_sync(app, "rm -f %s/etc/motd", TARGETDIR);
    run_sync(app, "rm -f %s/etc/issue", TARGETDIR);
    run_sync(app, "rm -f %s/usr/sbin/void-installer", TARGETDIR);
    run_sync(app, "rm -f %s/etc/sddm.conf", TARGETDIR);
    run_sync(app, "sed -i 's|GETTY_ARGS=\"--noclear -a void\"|GETTY_ARGS=\"--noclear\"|g' %s/etc/sv/agetty-tty1/conf", TARGETDIR);

    // 4. MOUNT DEV/PROC/SYS
    log_to_ui(app, "Mounting virtual filesystems...", 0.5);
    run_sync(app, "mount --rbind /dev %s/dev", TARGETDIR);
    run_sync(app, "mount --rbind /proc %s/proc", TARGETDIR);
    run_sync(app, "mount --rbind /sys %s/sys", TARGETDIR);

    // 5. REBUILD INITRAMFS
    log_to_ui(app, "Rebuilding initramfs...", 0.6);
    run_sync(app, "chroot %s dracut --force --no-hostonly-cmdline", TARGETDIR);

    // 6. REMOVE TEMPORARY PACKAGES
    log_to_ui(app, "Removing temporary live packages...", 0.7);
    run_sync(app, "chroot %s xbps-remove -Ry dialog xtools-minimal xmirror espeakup brltty 2>/dev/null", TARGETDIR);

    // 7. CONFIGURATION (Hostname, Locale)
    log_to_ui(app, "Applying System Configuration...", 0.8);
    run_sync(app, "echo %s > %s/etc/hostname", hostname, TARGETDIR);
    run_sync(app, "sed -i 's/#%s/%s/' %s/etc/default/libc-locales", locale, locale, TARGETDIR);
    run_sync(app, "echo LANG=%s > %s/etc/locale.conf", locale, TARGETDIR);
    run_sync(app, "chroot %s xbps-reconfigure -f glibc-locales", TARGETDIR);

    // 8. USERS
    log_to_ui(app, "Setting Root Password (SHA512)...", 0.82);
    set_safe_password(app, "root", root_pass, TARGETDIR);

    if (strlen(user_login) > 0) {
        run_sync(app, "chroot %s useradd -m -G wheel,audio,video -s /bin/bash %s", TARGETDIR, user_login);
        log_to_ui(app, "Setting User Password (SHA512)...", 0.84);
        set_safe_password(app, user_login, user_pass, TARGETDIR);
        
        log_to_ui(app, "Applying Neko Void customizations...", 0.85);
        run_sync(app, "cp -rf /var/lib/flatpak %s/var/lib/", TARGETDIR);
        run_sync(app, "mkdir -p %s/etc/xbps.d", TARGETDIR);
        run_sync(app, "cp -f /etc/xbps.d/* %s/etc/xbps.d/ 2>/dev/null", TARGETDIR);
        run_sync(app, "cp -f /home/.profile %s/home/%s/", TARGETDIR, user_login);
        run_sync(app, "cp -rf /home/anon/.themes %s/home/%s/", TARGETDIR, user_login);
        run_sync(app, "chown -R %s:users %s/home/%s", user_login, TARGETDIR, user_login);
        run_sync(app, "sed -i 's/^autologin-user=.*/autologin-user=%s/' %s/etc/lightdm/lightdm.conf", user_login, TARGETDIR);
        run_sync(app, "grep -q '^autologin-user=' %s/etc/lightdm/lightdm.conf || sed -i '/^\\[Seat:\\*\\]/a autologin-user=%s' %s/etc/lightdm/lightdm.conf", user_login, TARGETDIR);
        run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > %s/etc/sudoers.d/wheel", TARGETDIR);
        run_sync(app, "chmod 0440 %s/etc/sudoers.d/wheel", TARGETDIR);
    }
    run_sync(app, "rm -f %s/etc/polkit-1/rules.d/void-live.rules", TARGETDIR);

    // 9. BOOTLOADER
    log_to_ui(app, "Installing GRUB Bootloader...", 0.9);
    char disk_path[64];
    snprintf(disk_path, sizeof(disk_path), "/dev/%s", disk_name);
    
    if (app->is_efi) {
        run_sync(app, "chroot %s grub-install --target=%s --efi-directory=/boot/efi --bootloader-id=void_grub --recheck %s", TARGETDIR, app->efi_target, disk_path);
    } else {
        run_sync(app, "chroot %s grub-install --recheck %s", TARGETDIR, disk_path);
    }
    run_sync(app, "chroot %s grub-mkconfig -o /boot/grub/grub.cfg", TARGETDIR);

    // 10. SYNC AND UNMOUNT (Popup BEFORE UNMOUNT)
    log_to_ui(app, "--- INSTALLATION COMPLETED ---", 1.0);
    app->installing = FALSE;
    

    set_ui_finished(app);

    // 11. UNMOUNT (Do this last)
    log_to_ui(app, "Unmounting...", 0.95);
    system("sync");
    run_sync(app, "umount -R %s", TARGETDIR);
    
    return NULL;
}

void start_installation(GtkWidget *widget, AppData *app) {
    if (app->installing) return;
    app->installing = TRUE;
    gtk_widget_set_sensitive(app->btn_back, FALSE);
    gtk_button_set_sensitive(app->btn_next, FALSE);
    gtk_widget_set_sensitive(app->notebook, FALSE);
    gtk_widget_set_sensitive(widget, FALSE);
    
    GError *error = NULL;
    g_thread_try_new("installer", install_thread, app, &error);
    if (error) g_printerr("Error creating thread: %s\n", error->message);
}