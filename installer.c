/*
 * installer.c
 * Installation Logic Module. Reads values from UI and executes commands.
 */
#include "neko_installer.h"
#include <sys/mount.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>

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

// Helper to run sync commands and return exit code
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
    char disk_path[64];      // /dev/sda
    char root_part[64];      // /dev/sda1
    char efi_part[64];       // /dev/sda2 (if EFI)
    
    // 1. Retrieve data from UI
    const gchar *disk_name = app->selected_disk; // "sda"
    if (!disk_name) {
        log_to_ui(app, "CRITICAL ERROR: No disk selected.", 0.0);
        app->installing = FALSE; return NULL;
    }
    
    snprintf(disk_path, sizeof(disk_path), "/dev/%s", disk_name);
    snprintf(root_part, sizeof(root_part), "/dev/%s1", disk_name); // Assuming partition 1 is root
    snprintf(efi_part, sizeof(efi_part), "/dev/%s2", disk_name);   // Assuming partition 2 is EFI

    const gchar *root_pass = gtk_entry_get_text(GTK_ENTRY(app->root_pass_entry));
    const gchar *user_login = gtk_entry_get_text(GTK_ENTRY(app->user_login_entry));
    const gchar *user_pass = gtk_entry_get_text(GTK_ENTRY(app->user_pass_entry));
    const gchar *hostname = gtk_entry_get_text(GTK_ENTRY(app->hostname_entry));
    const gchar *locale = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->locale_combo));

    // 2. Basic validation
    if (!root_pass || strlen(root_pass) < 1) { log_to_ui(app, "Error: Root password missing.", 0.0); app->installing = FALSE; return NULL; }
    
    log_to_ui(app, "--- STARTING NEKO VOID INSTALLATION ---", 0.1);
    sleep(1);

    // 3. Format Root Partition (assuming ext4 for simplicity)
    log_to_ui(app, g_strdup_printf("Formatting %s as ext4...", root_part), 0.2);
    if (run_sync(app, "mkfs.ext4 -F %s", root_part) != 0) {
        log_to_ui(app, "ERROR formatting root partition. Make sure you created it in GParted.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // 4. Mount Root
    log_to_ui(app, "Mounting root partition...", 0.3);
    run_sync(app, "mkdir -p /mnt/target");
    if (mount(root_part, "/mnt/target", "ext4", 0, NULL) != 0) {
        log_to_ui(app, "ERROR: Could not mount root partition.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // 5. Mount EFI if applicable
    if (app->is_efi) {
        log_to_ui(app, "Mounting EFI partition...", 0.35);
        run_sync(app, "mkdir -p /mnt/target/boot/efi");
        run_sync(app, "mount %s /mnt/target/boot/efi", efi_part); // Assumes partition 2 is EFI
        // Note: If EFI mount fails, GRUB will fail later.
    }

    // 6. Install Base System (XBPS)
    log_to_ui(app, "Installing base packages (this may take a while)...", 0.4);
    // Note: You need internet access on the Live USB for xbps-install
    if (run_sync(app, "XBPS_ARCH=x86_64 xbps-install -Sy -r /mnt/target base-system grub") != 0) {
        log_to_ui(app, "ERROR: Failed to install base packages.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // 7. Basic Configuration
    log_to_ui(app, "Configuring Hostname and Locale...", 0.7);
    run_sync(app, "echo %s > /mnt/target/etc/hostname", hostname);
    
    // Enable locale (simple)
    run_sync(app, "sed -i 's/#%s/%s/' /mnt/target/etc/default/libc-locales", locale, locale);
    run_sync(app, "echo LANG=%s > /mnt/target/etc/locale.conf", locale);
    run_sync(app, "chroot /mnt/target xbps-reconfigure -f glibc-locales");

    // 8. Configure users and passwords
    log_to_ui(app, "Configuring users...", 0.8);
    
    // Root password
    gchar *cmd_root = g_strdup_printf("echo 'root:%s' | chroot /mnt/target chpasswd", root_pass);
    system(cmd_root);
    g_free(cmd_root);

    // New user
    if (strlen(user_login) > 0) {
        run_sync(app, "chroot /mnt/target useradd -m -G wheel,audio,video -s /bin/bash %s", user_login);
        gchar *cmd_user = g_strdup_printf("echo '%s:%s' | chroot /mnt/target chpasswd", user_login, user_pass);
        system(cmd_user);
        g_free(cmd_user);
        
        // Setup sudo for wheel
        run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > /mnt/target/etc/sudoers.d/10-wheel");
        run_sync(app, "chmod 0440 /mnt/target/etc/sudoers.d/10-wheel");
    }

    // 9. Install GRUB
    log_to_ui(app, "Installing GRUB...", 0.9);
    if (app->is_efi) {
        run_sync(app, "grub-install --target=x86_64-efi --efi-directory=/mnt/target/boot/efi --bootloader-id=void_grub --recheck %s", disk_path);
    } else {
        run_sync(app, "grub-install --recheck %s", disk_path);
    }
    
    // Configure Grub
    run_sync(app, "chroot /mnt/target grub-mkconfig -o /boot/grub/grub.cfg");

    // 10. Finalize
    log_to_ui(app, "--- INSTALLATION COMPLETED ---", 1.0);
    log_to_ui(app, "You may now reboot the system.", 1.0);
    
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
