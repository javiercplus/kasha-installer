/*
 * installer.c
 * CORRECTED LOGIC FOR NVME DISKS
 */
gpointer install_thread(gpointer data) {
    AppData *app = (AppData *)data;
    char disk_path[64];
    char root_part[64];
    char efi_part[64];
    
    // 1. Retrieve data from UI
    const gchar *disk_name = app->selected_disk;
    if (!disk_name) {
        log_to_ui(app, "CRITICAL ERROR: No disk selected.", 0.0);
        app->installing = FALSE; return NULL;
    }
    
    // --- FIX: NVME vs SATA PARTITION NAMING ---
    gchar *part_suffix = "1"; // Default for sda, vda...
    if (g_str_has_prefix(disk_name, "nvme") || g_str_has_prefix(disk_name, "mmcblk")) {
        part_suffix = "p1"; // For nvme0n1 -> nvme0n1p1
    }
    // -------------------------------------------

    snprintf(disk_path, sizeof(disk_path), "/dev/%s", disk_name);
    
    // Root Partition (assuming partition 1)
    snprintf(root_part, sizeof(root_part), "/dev/%s%s", disk_name, part_suffix);
    
    // EFI Partition (assuming partition 2)
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

    // 2. Basic validation
    if (!root_pass || strlen(root_pass) < 1) { log_to_ui(app, "Error: Root password missing.", 0.0); app->installing = FALSE; return NULL; }
    
    log_to_ui(app, "--- STARTING NEKO VOID INSTALLATION ---", 0.1);
    sleep(1);

    // 3. Format Root Partition
    log_to_ui(app, g_strdup_printf("Formatting %s as ext4...", root_part), 0.2);
    if (run_sync(app, "mkfs.ext4 -F %s", root_part) != 0) {
        log_to_ui(app, "ERROR formatting root partition. Make sure you created partition 1 in GParted.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // 4. Mount Root
    log_to_ui(app, "Mounting root partition...", 0.3);
    run_sync(app, "mkdir -p /mnt/target");
    if (mount(root_part, "/mnt/target", "ext4", 0, NULL) != 0) {
        log_to_ui(app, "ERROR: Could not mount root partition.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // 5. Mount EFI
    if (app->is_efi) {
        log_to_ui(app, "Mounting EFI partition...", 0.35);
        run_sync(app, "mkdir -p /mnt/target/boot/efi");
        if (run_sync(app, "mount %s /mnt/target/boot/efi", efi_part) != 0) {
            log_to_ui(app, "WARNING: Could not mount EFI partition. GRUB may fail later.", 0.35);
        }
    }

    // 6. Install Base System
    log_to_ui(app, "Installing base packages (this may take a while)...", 0.4);
    if (run_sync(app, "XBPS_ARCH=x86_64 xbps-install -Sy -r /mnt/target base-system grub") != 0) {
        log_to_ui(app, "ERROR: Failed to install base packages. Check internet connection.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // 7. Basic Configuration
    log_to_ui(app, "Configuring Hostname and Locale...", 0.7);
    run_sync(app, "echo %s > /mnt/target/etc/hostname", hostname);
    
    run_sync(app, "sed -i 's/#%s/%s/' /mnt/target/etc/default/libc-locales", locale, locale);
    run_sync(app, "echo LANG=%s > /mnt/target/etc/locale.conf", locale);
    run_sync(app, "chroot /mnt/target xbps-reconfigure -f glibc-locales");

    // 8. Configure users
    log_to_ui(app, "Configuring users...", 0.8);
    
    gchar *cmd_root = g_strdup_printf("echo 'root:%s' | chroot /mnt/target chpasswd", root_pass);
    system(cmd_root);
    g_free(cmd_root);

    if (strlen(user_login) > 0) {
        run_sync(app, "chroot /mnt/target useradd -m -G wheel,audio,video -s /bin/bash %s", user_login);
        gchar *cmd_user = g_strdup_printf("echo '%s:%s' | chroot /mnt/target chpasswd", user_login, user_pass);
        system(cmd_user);
        g_free(cmd_user);
        
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
    
    run_sync(app, "chroot /mnt/target grub-mkconfig -o /boot/grub/grub.cfg");

    // 10. Finalize
    log_to_ui(app, "--- INSTALLATION COMPLETED ---", 1.0);
    log_to_ui(app, "You may now reboot the system.", 1.0);
    
    app->installing = FALSE;
    return NULL;
}