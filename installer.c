/*
 * installer.c
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
    // 1. Root always first (len 1)
    // 2. Length ascending
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
    // Also unmount target dir if mounted

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
    
    // Header
    fprintf(fp, "# /etc/fstab: static file system information.\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# <file system> <mount point>   <type>  <options>       <dump>  <pass>\n\n");
    
    GSList *l = app->part_config_list;
    while (l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;
        

        
        char *uuid = get_uuid(conf->device);
        if (!uuid) {
            // Fallback to device name if UUID fails
            log_to_ui(app, g_strdup_printf("Warning: No UUID for %s, using device path.", conf->device), 0.0);
            uuid = g_strdup(conf->device);
        }
        
        // Options
        const char *opts = "defaults";
        int dump = 0;
        int pass = 2; // others
        
        if (strcmp(conf->mountpoint, "/") == 0) {
            pass = 1;
        } else if (strcmp(conf->fstype, "swap") == 0) {
            opts = "sw";
            pass = 0;
        } else if (strcmp(conf->fstype, "vfat") == 0 || strcmp(conf->fstype, "fat32") == 0) {
            opts = "umask=0077";
            pass = 2;
        }
        
        // Swap handling
        if (strcmp(conf->fstype, "swap") == 0) {
             fprintf(fp, "UUID=%s\t%s\t%s\t%s\t%d\t%d\n", uuid, "none", "swap", opts, dump, pass);
        } else {
             fprintf(fp, "UUID=%s\t%s\t%s\t%s\t%d\t%d\n", uuid, conf->mountpoint, conf->fstype, opts, dump, pass);
        }
        
        g_free(uuid);
        l = l->next;
    }
    
    fclose(fp);
    g_free(fstab_path);
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
    } else {
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
    snprintf(cmd_chroot, sizeof(cmd_chroot), "chroot %s chpasswd -c SHA512 < /tmp/.kasha_%s", target_dir, username);
    system(cmd_chroot);
    remove(live_tmp);
    remove(chroot_tmp);
}

gpointer install_thread(gpointer data) {
    AppData *app = (AppData *)data;
    const char *TARGETDIR = "/mnt/target";
    
    // SAFETY: Unmount everything first to avoid "device busy"
    unmount_safety(app);
    
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
    gboolean autologin_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->autologin_check));

    if (!root_pass || strlen(root_pass) < 1) { log_to_ui(app, "Error: Root password missing.", 0.0); app->installing = FALSE; return NULL; }
    
    log_to_ui(app, "--- STARTING LOCAL INSTALLATION ---", 0.1);

    // 0. PARTITIONING PHASE
    if (app->install_mode == INSTALL_MODE_ERASE) {
        log_to_ui(app, "Partitioning Disk...", 0.15);
        
        char disk_dev[64];
        snprintf(disk_dev, sizeof(disk_dev), "/dev/%s", disk_name);
        
        // ERASE / CREATE NEW
        FILE *sf = popen(g_strdup_printf("sfdisk %s", disk_dev), "w");
        if (sf) {
             // WIPE HEADER
             if (app->is_efi) fprintf(sf, "label: gpt\n");
             else fprintf(sf, "label: dos\n");
             
             // Define Partitions
             // If UEFI:
             if (app->is_efi) {
                 fprintf(sf, "size=512M, type=U\n"); // ESP
                 fprintf(sf, "type=L\n"); // Root (Rest)
             } else {
                 // BIOS
                 fprintf(sf, "type=L\n"); // Root (Rest)
             }
             
             pclose(sf);
             
             // WAIT for kernel to update table
             sleep(2);
             system("partprobe");
             sleep(1);
        }
        
        // RE-SCAN and UPDATE `part_config_list` real devices
        
        GSList *l = app->part_config_list;
        while(l) {
             PartitionConfig *cfg = (PartitionConfig*)l->data;
             g_free(cfg->device);
             
             // Determine separator (p for nvme/mmc, nothing for sd/vd/hd)
             // simplified check: if last char is digit, add 'p'
             const char *sep = "";
             int len = strlen(disk_name);
             if (g_ascii_isdigit(disk_name[len-1])) sep = "p";
             
             if (app->is_efi) {
                 if (strcmp(cfg->mountpoint, "/boot/efi") == 0) {
                     cfg->device = g_strdup_printf("/dev/%s%s1", disk_name, sep); 
                 } else {
                     cfg->device = g_strdup_printf("/dev/%s%s2", disk_name, sep);
                 }
             } else {
                 cfg->device = g_strdup_printf("/dev/%s%s1", disk_name, sep);
             }
             l = l->next;
        }
    }

    // 1. FORMAT AND MOUNT PARTITIONS
    log_to_ui(app, "Configuring partitions...", 0.2);
    
    // SORT: Ensure proper mount order (Shortest mountpoint first: / before /home)
    app->part_config_list = g_slist_sort(app->part_config_list, sort_partitions);
    
    GSList *l = app->part_config_list;
    
    // First pass: LUKS Format & Open all encrypted partitions
    // We need to do this before filesystem formatting
    GSList *l_luks = app->part_config_list;
    while(l_luks) {
        PartitionConfig *conf = (PartitionConfig*)l_luks->data;
        if (conf->encrypt && conf->luks_pass) {
             log_to_ui(app, g_strdup_printf("Encrypting %s...", conf->device), 0.21);
             
             // 1. Format LUKS
             char *cmd_fmt = g_strdup_printf("echo -n '%s' | cryptsetup luksFormat -q %s -", conf->luks_pass, conf->device);
             if (run_sync(app, cmd_fmt) != 0) {
                 log_to_ui(app, "ERROR: LUKS Format failed.", 0.0);
                 app->installing = FALSE; return NULL;
             }
             g_free(cmd_fmt);

             // 2. Open LUKS
             char *dev_base = g_path_get_basename(conf->device);
             char *mapper_name = g_strdup_printf("%s_crypt", dev_base);
             char *cmd_open = g_strdup_printf("echo -n '%s' | cryptsetup open %s %s -", conf->luks_pass, conf->device, mapper_name);
             
             if (run_sync(app, cmd_open) != 0) {
                 log_to_ui(app, "ERROR: LUKS Open failed.", 0.0);
                 app->installing = FALSE; return NULL;
             }
             g_free(cmd_open);
             
             // UPDATE DEVICE PATH to /dev/mapper/...
             g_free(conf->device);
             conf->device = g_strdup_printf("/dev/mapper/%s", mapper_name);
             
             g_free(dev_base);
             g_free(mapper_name);
        }
        l_luks = l_luks->next;
    }

    // Standard Loop for Root
    l = app->part_config_list;
    while(l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;
        
        // Skip Windows / existing partitions that we don't want to touch (except resize which is done)
        if (strcmp(conf->fstype, "ntfs") == 0) { l = l->next; continue; }

        if (strcmp(conf->mountpoint, "/") == 0) {
            if (conf->format) {
                gchar *fs_cmd = NULL;
                log_to_ui(app, g_strdup_printf("Formatting Root %s as %s...", conf->device, conf->fstype), 0.22);
                
                if (strcmp(conf->fstype, "ext4") == 0) fs_cmd = "mkfs.ext4 -F";
                else if (strcmp(conf->fstype, "btrfs") == 0) fs_cmd = "mkfs.btrfs -f";
                else if (strcmp(conf->fstype, "xfs") == 0) fs_cmd = "mkfs.xfs -f";
                else if (strcmp(conf->fstype, "f2fs") == 0) fs_cmd = "mkfs.f2fs -f";
                
                if (fs_cmd) {
                     if (run_sync(app, "%s %s", fs_cmd, conf->device) != 0) {
                         log_to_ui(app, "ERROR: Formatting failed!", 0.0);
                         app->installing = FALSE; return NULL;
                     }
                }
            }

            log_to_ui(app, g_strdup_printf("Mounting Root %s...", conf->device), 0.25);
            run_sync(app, "mkdir -p %s", TARGETDIR);
            
            if (run_sync(app, "mount %s %s", conf->device, TARGETDIR) != 0) {
                 log_to_ui(app, g_strdup_printf("ERROR: Failed to mount %s to %s!", conf->device, TARGETDIR), 0.0);
                 app->installing = FALSE; return NULL;
            }
        }
        l = l->next;
    }
    
    // Standard Loop for Others
    l = app->part_config_list;
    while(l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;
        
        if (strcmp(conf->fstype, "ntfs") == 0) { l = l->next; continue; }

        if (strcmp(conf->mountpoint, "/") != 0) {
            gchar *fs_cmd = NULL;
            
            // 2.1 Format EFI
            if (conf->format) {
                log_to_ui(app, g_strdup_printf("Formatting %s...", conf->mountpoint), 0.26);
                
                if (strcmp(conf->fstype, "ext4") == 0) fs_cmd = "mkfs.ext4 -F";
                else if (strcmp(conf->fstype, "btrfs") == 0) fs_cmd = "mkfs.btrfs -f";
                else if (strcmp(conf->fstype, "xfs") == 0) fs_cmd = "mkfs.xfs -f";
                else if (strcmp(conf->fstype, "vfat") == 0) fs_cmd = "mkfs.vfat -F32"; 
                else if (strcmp(conf->fstype, "swap") == 0) fs_cmd = "mkswap";
                
                if (fs_cmd) {
                     if (run_sync(app, "%s %s", fs_cmd, conf->device) != 0) {
                         log_to_ui(app, g_strdup_printf("ERROR: Formatting %s failed!", conf->mountpoint), 0.0);
                         app->installing = FALSE; return NULL;
                     }
                }
            }

            // 2.2 Mount
            if (strcmp(conf->fstype, "swap") == 0) {
                run_sync(app, "swapon %s", conf->device);
            } else {
                gchar *target_path = g_strdup_printf("%s%s", TARGETDIR, conf->mountpoint);
                run_sync(app, "mkdir -p %s", target_path);
                log_to_ui(app, g_strdup_printf("Mounting %s...", conf->mountpoint), 0.28);
                
                if (run_sync(app, "mount %s %s", conf->device, target_path) != 0) {
                     log_to_ui(app, g_strdup_printf("ERROR: Failed to mount %s to %s!", conf->device, target_path), 0.0);
                     g_free(target_path);
                     app->installing = FALSE; return NULL;
                }
                
                g_free(target_path);
            }
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

    // 4.5 INSTALL CRYPTSETUP IF NEEDED
    gboolean has_crypto = FALSE;
    GSList *chk = app->part_config_list;
    while(chk) {
      if(((PartitionConfig*)chk->data)->encrypt) has_crypto = TRUE;
      chk = chk->next;
    }
    
    if (has_crypto) {
        log_to_ui(app, "Installing cryptsetup...", 0.55);
        run_sync(app, "chroot %s xbps-install -y cryptsetup", TARGETDIR);
    }

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

    // --- TIMEZONE SETUP ---
    char *tz_area = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->tz_area_combo));
    char *tz_city = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->tz_city_combo));

    if (tz_area && tz_city) {
        log_to_ui(app, g_strdup_printf("Setting Timezone: %s/%s", tz_area, tz_city), 0.81);
        run_sync(app, "ln -sf /usr/share/zoneinfo/%s/%s %s/etc/localtime", tz_area, tz_city, TARGETDIR);
        g_free(tz_area);
        g_free(tz_city);
    } else {
        log_to_ui(app, "Timezone not selected, defaulting to UTC.", 0.81);
        run_sync(app, "ln -sf /usr/share/zoneinfo/UTC %s/etc/localtime", TARGETDIR);
    }

    // 8. USERS (Password setup)
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
        
        if (autologin_enabled) {
            run_sync(app, "sed -i 's/^autologin-user=.*/autologin-user=%s/' %s/etc/lightdm/lightdm.conf", user_login, TARGETDIR);
            run_sync(app, "grep -q '^autologin-user=' %s/etc/lightdm/lightdm.conf || sed -i '/^\\[Seat:\\*\\]/a autologin-user=%s' %s/etc/lightdm/lightdm.conf", TARGETDIR, user_login, TARGETDIR);
        }
        
        run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > %s/etc/sudoers.d/wheel", TARGETDIR);
        run_sync(app, "chmod 0440 %s/etc/sudoers.d/wheel", TARGETDIR);
    }
    run_sync(app, "rm -f %s/etc/polkit-1/rules.d/void-live.rules", TARGETDIR);
    
    log_to_ui(app, "Removing live user (anon) from target system...", 0.88);
    run_sync(app, "chroot %s userdel -r anon 2>/dev/null", TARGETDIR);
    run_sync(app, "rm -f %s/etc/sudoers.d/99-void-live", TARGETDIR);
    run_sync(app, "sed -i 's|GETTY_ARGS=\"--noclear -a anon\"|GETTY_ARGS=\"--noclear\"|g' %s/etc/sv/agetty-tty1/conf", TARGETDIR);
    run_sync(app, "sed -i 's|GETTY_ARGS=\"--noclear -a anon\"|GETTY_ARGS=\"--noclear\"|g' %s/etc/sv/agetty-tty1/conf", TARGETDIR);
   
    // GENERATE FSTAB
    generate_fstab(app, TARGETDIR);

    // 9. BOOTLOADER
    log_to_ui(app, "Installing GRUB Bootloader...", 0.9);
    char disk_path[64];
    snprintf(disk_path, sizeof(disk_path), "/dev/%s", disk_name);
    
    // 9.1 CONFIGURE GRUB FOR LUKS
        if (has_crypto) {
        log_to_ui(app, "Configuring GRUB for LUKS...", 0.91);
        
        // Find UUID of root partition
        GSList *f = app->part_config_list;
        while(f) {
            PartitionConfig *c = (PartitionConfig*)f->data;
            if (c->encrypt && strcmp(c->mountpoint, "/") == 0) {
                // The conf->device is /dev/mapper/sdxY_crypt
                // We need to resolve what the underlying device is.
               
               char *map_name = g_path_get_basename(c->device); // sdxY_crypt
               char *raw_name = g_strndup(map_name, strlen(map_name) - 6); // remove _crypt
               char *raw_dev = g_strdup_printf("/dev/%s", raw_name);
               
               char uuid[128] = {0};
               FILE *fp = popen(g_strdup_printf("blkid -s UUID -o value %s", raw_dev), "r");
               if (fp) {
                   fgets(uuid, sizeof(uuid), fp);
                   uuid[strcspn(uuid, "\n")] = 0;
                   pclose(fp);
               }
               
               // Add GRUB_CMDLINE_LINUX_DEFAULT="... rd.luks.uuid=UUID ..."
               run_sync(app, "sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT=\"/GRUB_CMDLINE_LINUX_DEFAULT=\"rd.luks.uuid=%s /' %s/etc/default/grub", uuid, TARGETDIR);
               
               // Enable CRYPTODISK
               run_sync(app, "echo 'GRUB_ENABLE_CRYPTODISK=y' >> %s/etc/default/grub", TARGETDIR);
               
               g_free(raw_name);
               g_free(raw_dev);
               g_free(map_name);
           }
           f = f->next;
        }
    }

    if (app->is_efi) {
        log_to_ui(app, "Downloading GRUB EFI support...", 0.91);
        if (strcmp(app->efi_target, "x86_64-efi") == 0) {
            run_sync(app, "chroot %s xbps-install -y grub-x86_64-efi", TARGETDIR);
        } else {
            run_sync(app, "chroot %s xbps-install -y grub-i386-efi", TARGETDIR);
        }

        run_sync(app, "chroot %s grub-install --target=%s --efi-directory=/boot/efi --bootloader-id=void_grub --recheck %s", TARGETDIR, app->efi_target, disk_path);
    } else {
        run_sync(app, "chroot %s grub-install --recheck %s", TARGETDIR, disk_path);
    }
  
    run_sync(app, "mkdir -p %s/boot/grub", TARGETDIR);
    run_sync(app, "chroot %s grub-mkconfig -o /boot/grub/grub.cfg", TARGETDIR);

    // 10. SYNC AND UNMOUNT
    log_to_ui(app, "Syncing...", 0.95);
    system("sync");
    run_sync(app, "umount -R %s", TARGETDIR);
    
    // CLOSE LUKS DEVICES
    if (has_crypto) {
        log_to_ui(app, "Closing encrypted devices...", 0.98);
        run_sync(app, "cryptsetup close /dev/mapper/*_crypt"); 

        GSList *c = app->part_config_list;
        while(c) {
            PartitionConfig *pc = (PartitionConfig*)c->data;
            if (pc->encrypt && strstr(pc->device, "/dev/mapper/")) {
                run_sync(app, "cryptsetup close %s", pc->device);
            }
            c = c->next;
        }
    }
    
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