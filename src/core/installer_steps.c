/*
 * installer_steps.c
 * Installation Steps Implementation
 */
#include "neko_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <ctype.h>

int check_filesystems(AppData *app) {
    gboolean root_found = FALSE;
    gboolean usr_found = FALSE;
    gboolean efi_partition_found = FALSE;

    GSList *l = app->part_config_list;
    while (l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;

        if (strcmp(conf->mountpoint, "/") == 0) {
            root_found = TRUE;
        } else if (strcmp(conf->mountpoint, "/usr") == 0) {
            usr_found = TRUE;
        } else if (strcmp(conf->mountpoint, "/boot/efi") == 0 &&
                  (strcmp(conf->fstype, "vfat") == 0 || strcmp(conf->fstype, "fat32") == 0)) {
            efi_partition_found = TRUE;
        }
        l = l->next;
    }

    if (!root_found) {
        log_to_ui(app, "ERROR: Root (/) partition not configured!", 0.0);
        return -1;
    }

    if (usr_found) {
        log_to_ui(app, "ERROR: /usr as separate partition is not supported!", 0.0);
        return -1;
    }

    // EFI requires EFI partition, but skip for now - bootloader step will handle
    return 0;
}

int step_partitioning(AppData *app, const char *disk_name) {
    if (app->install_mode == INSTALL_MODE_MANUAL) return 0; // Skip if manual

    log_to_ui(app, "Partitioning Disk...", 0.15);

    char disk_dev[64];
    snprintf(disk_dev, sizeof(disk_dev), "/dev/%s", disk_name);

    if (app->install_mode == INSTALL_MODE_ERASE) {
        // ERASE / CREATE NEW TABLE
        char cmd_buf[256];
        snprintf(cmd_buf, sizeof(cmd_buf), "sfdisk --wipe always %s", disk_dev);
        FILE *sf = popen(cmd_buf, "w");
        if (sf) {
             if (app->is_efi) fprintf(sf, "label: gpt\n");
             else fprintf(sf, "label: dos\n");

             if (app->is_efi) {
                 fprintf(sf, ",512M,U\n"); // ESP
                 fprintf(sf, ",,L\n"); // Root (Rest)
             } else {
                 fprintf(sf, ",,L,*\n"); // Root – MBR Boot Flag
             }

             pclose(sf);
             sleep(2);
             run_sync(app, "partprobe");
             sleep(1);
        }
} else if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
        // DUAL BOOT: Append new partitions to existing table (no wipe)
        char cmd_buf2[256];
        snprintf(cmd_buf2, sizeof(cmd_buf2), "sfdisk -a %s", disk_dev);
        FILE *sf = popen(cmd_buf2, "w");
        if (sf) {
              if (app->is_efi) {
                  fprintf(sf, ",512M,U\n"); // ESP in free space
                  fprintf(sf, ",,L\n"); // Root in free space
              } else {
                  fprintf(sf, ",,L,*\n"); // Root in free space – MBR Boot Flag
              }

              pclose(sf);
             sleep(2);
             run_sync(app, "partprobe");
             sleep(1);
        }
    }

    // RE-SCAN and UPDATE `part_config_list` with real device paths
    GSList *l = app->part_config_list;
    while(l) {
         PartitionConfig *cfg = (PartitionConfig*)l->data;
         g_free(cfg->device);

         const char *sep = "";
         int len = strlen(disk_name);
         if (g_ascii_isdigit(disk_name[len-1])) sep = "p";

         if (app->is_efi && app->install_mode == INSTALL_MODE_ERASE) {
             if (strcmp(cfg->mountpoint, "/boot/efi") == 0) {
                 cfg->device = g_strdup_printf("/dev/%s%s1", disk_name, sep);
             } else {
                 cfg->device = g_strdup_printf("/dev/%s%s2", disk_name, sep);
             }
} else if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
              // Find the last partition number on disk
              char cmd[256];
              snprintf(cmd, sizeof(cmd), "lsblk -rn -o NAME /dev/%s | tail -n1 | sed 's/[^0-9]*//g'", disk_name);
              FILE *fp = popen(cmd, "r");
              char last_num[16] = "1";
              if (fp) {
                  if (fgets(last_num, sizeof(last_num), fp)) {
                      last_num[strcspn(last_num, "\n")] = 0;
                  }
                  pclose(fp);
              }
              int base_num = atoi(last_num);

              // For EFI dual boot, we added 2 partitions (ESP + root)
              // For BIOS dual boot, we added 1 partition (root only)
              if (app->is_efi) {
                  // EFI dual boot:ESP is last_num+1, root is last_num+2
                  base_num = base_num - 1; // shift back so we assign right
              }

              // Assign based on mountpoint
              if (strcmp(cfg->mountpoint, "/boot/efi") == 0) {
                  cfg->device = g_strdup_printf("/dev/%s%s%d", disk_name, sep, base_num + 1);
              } else {
                  cfg->device = g_strdup_printf("/dev/%s%s%d", disk_name, sep, base_num + (app->is_efi ? 2 : 1));
              }
          } else {
             cfg->device = g_strdup_printf("/dev/%s%s1", disk_name, sep);
         }
         l = l->next;
    }
    return 0;
}

int step_format_and_mount(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "Configuring partitions...", 0.2);

    // SORT: Ensure proper mount order (Shortest mountpoint first: / before /home)
    app->part_config_list = g_slist_sort(app->part_config_list, sort_partitions);

    GSList *l = app->part_config_list;

// First pass: LUKS Format & Open all encrypted partitions
    GSList *l_luks = app->part_config_list;
    while(l_luks) {
        PartitionConfig *conf = (PartitionConfig*)l_luks->data;
        if (conf->encrypt && conf->luks_pass) {
             log_to_ui_printf(app, "Encrypting %s...", conf->device);

             char keyfile[64];
             snprintf(keyfile, sizeof(keyfile), "/tmp/luks_key_XXXXXX");
             int fd = mkstemp(keyfile);
             if (fd < 0) {
                 log_to_ui(app, "ERROR: Failed to create temp keyfile.", 0.0);
                 return -1;
             }
             write(fd, conf->luks_pass, strlen(conf->luks_pass));
             close(fd);

             // 1. Format LUKS (LUKS1 for GRUB compatibility)
             char *cmd_fmt = g_strdup_printf("chmod 600 %s && cryptsetup luksFormat --type luks1 -q --key-file=%s %s", keyfile, keyfile, conf->device);
             if (run_sync(app, cmd_fmt) != 0) {
                 log_to_ui(app, "ERROR: LUKS Format failed.", 0.0);
                 unlink(keyfile);
                 g_free(cmd_fmt);
                 return -1;
             }
             g_free(cmd_fmt);

             // Get LUKS UUID (header UUID, needed for GRUB)
             log_to_ui(app, "Getting LUKS UUID...", 0.215);
             char luks_uuid[64] = {0};
             char *cmd_uuid = g_strdup_printf("cryptsetup luksUUID %s", conf->device);
             FILE *fp = popen(cmd_uuid, "r");
             if (fp) {
                 if (fgets(luks_uuid, sizeof(luks_uuid), fp) != NULL) {
                     size_t len = strlen(luks_uuid);
                     if (len > 0 && luks_uuid[len-1] == '\n') luks_uuid[len-1] = '\0';
                     conf->luks_uuid = g_strdup(luks_uuid);
                     log_to_ui_printf(app, "LUKS UUID: %s", luks_uuid);
                 }
                 pclose(fp);
             } else {
                 log_to_ui(app, "ERROR: Failed to get LUKS UUID", 0.0);
                 g_free(cmd_uuid);
                 unlink(keyfile);
                 return -1;
             }
             g_free(cmd_uuid);

             // Verify UUID was captured
             if (!conf->luks_uuid || strlen(conf->luks_uuid) == 0) {
                 log_to_ui(app, "ERROR: LUKS UUID is empty!", 0.0);
                 unlink(keyfile);
                 return -1;
             }

             // 2. Open LUKS
             char *dev_base = g_path_get_basename(conf->device);
             char *mapper_name = g_strdup_printf("cryptroot"); // Use cryptroot for compatibility
             char *cmd_open = g_strdup_printf("cryptsetup open --key-file=%s %s %s", keyfile, conf->device, mapper_name);

             if (run_sync(app, cmd_open) != 0) {
                 log_to_ui(app, "ERROR: LUKS Open failed.", 0.0);
                 unlink(keyfile);
                 g_free(cmd_open);
                 g_free(mapper_name);
                 g_free(dev_base);
                 return -1;
             }
             g_free(cmd_open);

             unlink(keyfile);

// UPDATE DEVICE PATH to /dev/mapper/...
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

        if (strcmp(conf->fstype, "ntfs") == 0) { l = l->next; continue; }

        if (strcmp(conf->mountpoint, "/") == 0) {
            if (conf->format) {
                gchar *fs_cmd = NULL;
                log_to_ui_printf(app, "Formatting Root %s as %s...", conf->device, conf->fstype);

                if (strcmp(conf->fstype, "ext4") == 0) fs_cmd = "mkfs.ext4 -F";
                else if (strcmp(conf->fstype, "btrfs") == 0) fs_cmd = "mkfs.btrfs -f";
                else if (strcmp(conf->fstype, "xfs") == 0) fs_cmd = "mkfs.xfs -f";
                else if (strcmp(conf->fstype, "f2fs") == 0) fs_cmd = "mkfs.f2fs -f";

                if (fs_cmd) {
                     if (run_sync(app, "%s %s", fs_cmd, conf->device) != 0) {
                         log_to_ui(app, "ERROR: Formatting failed!", 0.0);
                         return -1;
                     }
                }
            }

            log_to_ui_printf(app, "Mounting Root %s...", conf->device);
            run_sync(app, "mkdir -p %s", TARGETDIR);

            if (run_sync(app, "mount %s %s", conf->device, TARGETDIR) != 0) {
                 log_to_ui_printf(app, "ERROR: Failed to mount %s to %s!", conf->device, TARGETDIR);
                 return -1;
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
                log_to_ui_printf(app, "Formatting %s...", conf->mountpoint);

                if (strcmp(conf->fstype, "ext4") == 0) fs_cmd = "mkfs.ext4 -F";
                else if (strcmp(conf->fstype, "btrfs") == 0) fs_cmd = "mkfs.btrfs -f";
                else if (strcmp(conf->fstype, "xfs") == 0) fs_cmd = "mkfs.xfs -f";
                else if (strcmp(conf->fstype, "f2fs") == 0) fs_cmd = "mkfs.f2fs -f";
                else if (strcmp(conf->fstype, "vfat") == 0) fs_cmd = "mkfs.vfat -F32";
                else if (strcmp(conf->fstype, "swap") == 0) fs_cmd = "mkswap";

                if (fs_cmd) {
                     if (run_sync(app, "%s %s", fs_cmd, conf->device) != 0) {
                         log_to_ui_printf(app, "ERROR: Formatting %s failed!", conf->mountpoint);
                         return -1;
                     }
                }
            }

            // 2.2 Mount
            if (strcmp(conf->fstype, "swap") == 0) {
                run_sync(app, "swapon %s", conf->device);
            } else {
                gchar *target_path = g_strdup_printf("%s%s", TARGETDIR, conf->mountpoint);
                run_sync(app, "mkdir -p %s", target_path);
                log_to_ui_printf(app, "Mounting %s...", conf->mountpoint);

                if (run_sync(app, "mount %s %s", conf->device, target_path) != 0) {
                     log_to_ui_printf(app, "ERROR: Failed to mount %s to %s!", conf->device, target_path);
                     g_free(target_path);
                     return -1;
                }
                g_free(target_path);
            }
        }
        l = l->next;
    }
    return 0;
}

int step_install_base_system(AppData *app, const char *TARGETDIR) {
    // COPY ROOTFS
    log_to_ui(app, "Copying Live Image to Target...", 0.3);
    char tar_cmd[512];
    snprintf(tar_cmd, sizeof(tar_cmd),
        "tar -cf - --one-file-system --xattrs / 2>/dev/null | "
        "tar --extract --xattrs --xattrs-include='*' --preserve-permissions -f - -C %s", TARGETDIR);
    int ret = system(tar_cmd);
    if (WEXITSTATUS(ret) != 0) {
        log_to_ui(app, "ERROR: Failed to copy filesystem.", 0.0);
        return -1;
    }

    // CLEANUP LIVE FILES
    log_to_ui(app, "Cleaning up live image files...", 0.4);

    run_sync(app, "rm -f %s/etc/motd", TARGETDIR);
    run_sync(app, "rm -f %s/etc/issue", TARGETDIR);
    run_sync(app, "rm -f %s/usr/sbin/void-installer", TARGETDIR);
    run_sync(app, "rm -f %s/etc/sddm.conf", TARGETDIR);
    run_sync(app, "rmdir %s/mnt/target 2>/dev/null", TARGETDIR);

    // MOUNT DEV/PROC/SYS
    log_to_ui(app, "Mounting virtual filesystems...", 0.5);
    run_sync(app, "mount --rbind /dev %s/dev", TARGETDIR);
    run_sync(app, "mount --rbind /proc %s/proc", TARGETDIR);
    run_sync(app, "mount --rbind /sys %s/sys", TARGETDIR);

    // INSTALL CRYPTSETUP IF NEEDED
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

    // COPY XBPS KEYS (needed for package verification post-install)
    log_to_ui(app, "Copying XBPS repository keys...", 0.56);
    run_sync(app, "mkdir -p %s/var/db/xbps/keys", TARGETDIR);
    run_sync(app, "cp /var/db/xbps/keys/*.plist %s/var/db/xbps/keys/", TARGETDIR);
    run_sync(app, "cp -a /usr/share/xbps.d %s/usr/share/ 2>/dev/null", TARGETDIR);

    // REBUILD INITRAMFS (generic, with AHCI driver for SATA support)
    log_to_ui(app, "Rebuilding initramfs...", 0.6);
    run_sync(app, "chroot %s dracut --no-hostonly --add-drivers \"ahci\" --force", TARGETDIR);

    // RECONFIGURE BASE PACKAGES
    log_to_ui(app, "Reconfiguring base system packages...", 0.63);
    run_sync(app, "xbps-reconfigure -r %s -f base-files 2>/dev/null", TARGETDIR);
    run_sync(app, "chroot %s xbps-reconfigure -a", TARGETDIR);

    return 0;
}

int step_configure_system(AppData *app, const char *TARGETDIR, const gchar *hostname, const gchar *locale, const gchar *root_pass, const gchar *user_login, const gchar *user_fullname, const gchar *user_pass, gboolean autologin) {
    // REMOVE TEMPORARY PACKAGES
    log_to_ui(app, "Removing temporary live packages...", 0.7);
    run_sync(app, "chroot %s xbps-remove -Ry dialog xtools-minimal xmirror espeakup brltty 2>/dev/null", TARGETDIR);

    // REMOVE LIVE USER FIRST (before creating new user to avoid UID conflicts)
    log_to_ui(app, "Removing live user (anon) from target system...", 0.72);
    run_sync(app, "chroot %s userdel -r anon 2>/dev/null", TARGETDIR);
    run_sync(app, "rm -f %s/etc/sudoers.d/99-void-live", TARGETDIR);
    run_sync(app, "sed -i 's|GETTY_ARGS=\"--noclear -a anon\"|GETTY_ARGS=\"--noclear\"|g' %s/etc/sv/agetty-tty1/conf", TARGETDIR);
    run_sync(app, "rm -f %s/etc/polkit-1/rules.d/void-live.rules", TARGETDIR);

    // CLEANUP CLONED LIVE STATE
    log_to_ui(app, "Cleaning up machine-id and network state...", 0.73);
    run_sync(app, "rm -f %s/etc/machine-id", TARGETDIR);
    run_sync(app, "rm -f %s/var/lib/dbus/machine-id", TARGETDIR);
    run_sync(app, "rm -f %s/etc/NetworkManager/system-connections/*", TARGETDIR);


    // CONFIGURATION (Hostname, Locale)
    log_to_ui(app, "Applying System Configuration...", 0.75);
    run_sync(app, "echo '%s' > %s/etc/hostname", hostname, TARGETDIR);

    // Enable locale in libc-locales
    run_sync(app, "sed -i 's|^#%s |%s |' %s/etc/default/libc-locales 2>/dev/null || true",
             locale, locale, TARGETDIR);
    run_sync(app, "echo 'LANG=%s' > %s/etc/locale.conf", locale, TARGETDIR);
    run_sync(app, "chroot %s xbps-reconfigure -f glibc-locales", TARGETDIR);

    // KEYMAP SETUP — copy from live system to target
    run_sync(app, "cp /etc/vconsole.conf %s/etc/vconsole.conf 2>/dev/null", TARGETDIR);

    // TIMEZONE SETUP
    char *tz_area = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->tz_area_combo));
    char *tz_city = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->tz_city_combo));

    if (tz_area && tz_city) {
        log_to_ui(app, "Setting Timezone...", 0.78);
        run_sync(app, "ln -sf /usr/share/zoneinfo/%s/%s %s/etc/localtime", tz_area, tz_city, TARGETDIR);
        g_free(tz_area);
        g_free(tz_city);
    } else {
        log_to_ui(app, "Timezone not selected, defaulting to UTC.", 0.78);
        run_sync(app, "ln -sf /usr/share/zoneinfo/UTC %s/etc/localtime", TARGETDIR);
    }

    // ROOT USER
    log_to_ui(app, "Setting Root Password (SHA512)...", 0.80);
    set_safe_password(app, "root", root_pass, TARGETDIR);

    // Copy /etc/skel files for root
    run_sync(app, "cp %s/etc/skel/.[bix]* %s/root/ 2>/dev/null", TARGETDIR, TARGETDIR);

    // CREATE USER ACCOUNT (with full group membership)
    if (strlen(user_login) > 0) {
        log_to_ui(app, "Creating user account...", 0.82);

        // Step 1: Ensure ALL required groups exist
        const char *all_groups[] = {
            "wheel", "floppy", "audio", "video", "cdrom", "optical",
            "storage", "network", "kvm", "input", "plugdev", "users",
            "xbuilder", "render", "fuse", NULL
        };

        for (int i = 0; all_groups[i] != NULL; i++) {
            log_to_ui_printf(app, "Ensuring group '%s' exists...", all_groups[i]);
            run_sync(app, "chroot %s getent group %s > /dev/null 2> /dev/null || chroot %s groupadd %s", TARGETDIR, all_groups[i], TARGETDIR, all_groups[i]);
        }

        // Step 2: Build list of groups that actually exist
        char valid_groups[1024] = {0};
        int first = 1;

        for (int i = 0; all_groups[i] != NULL; i++) {
            char check_cmd[256];
            snprintf(check_cmd, sizeof(check_cmd), "chroot %s getent group %s > /dev/null 2> /dev/null", TARGETDIR, all_groups[i]);
            if (system(check_cmd) == 0) {
                if (!first) {
                    strcat(valid_groups, ",");
                }
                strcat(valid_groups, all_groups[i]);
                first = 0;
            } else {
                log_to_ui_printf(app, "WARNING: Group '%s' does not exist, skipping", all_groups[i]);
            }
        }

        log_to_ui_printf(app, "Using groups: %s", valid_groups);

        // Step 3: Create the user
        char useradd_cmd[1024];
        if (user_fullname && strlen(user_fullname) > 0) {
            snprintf(useradd_cmd, sizeof(useradd_cmd),
                "chroot %s useradd -m -c \"%s\" -G %s -s /bin/bash %s",
                TARGETDIR, user_fullname, valid_groups, user_login);
        } else {
            snprintf(useradd_cmd, sizeof(useradd_cmd),
                "chroot %s useradd -m -G %s -s /bin/bash %s",
                TARGETDIR, valid_groups, user_login);
        }

        log_to_ui(app, useradd_cmd, -1);
        int add_result = system(useradd_cmd);
        if (WEXITSTATUS(add_result) != 0) {
            log_to_ui(app, "ERROR: useradd failed!", 0.0);
        }

        log_to_ui(app, "Setting User Password (SHA512)...", 0.84);
        set_safe_password(app, user_login, user_pass, TARGETDIR);

        // Verify user was created
        char verify_cmd[256];
        snprintf(verify_cmd, sizeof(verify_cmd), "chroot %s id %s", TARGETDIR, user_login);
        if (system(verify_cmd) == 0) {
            log_to_ui_printf(app, "User %s created successfully.", user_login);
        } else {
            log_to_ui_printf(app, "ERROR: User %s not found!", user_login);
        }

        // Neko Void customizations
        log_to_ui(app, "Applying Neko Void customizations...", 0.85);

        // Add rice_set as autostart entry (script is already in the distro)
        log_to_ui(app, "Creating rice_set autostart entry...", 0.86);
        run_sync(app, "mkdir -p %s/home/%s/.config/autostart", TARGETDIR, user_login);
        run_sync(app, "printf '[Desktop Entry]\\nType=Application\\nName=Rice Set\\nExec=/usr/bin/rice_set\\nX-MATE-Autostart-enabled=true\\n' > %s/home/%s/.config/autostart/rice_set.desktop", TARGETDIR, user_login);

        // Copy themes if exists
        run_sync(app, "mkdir -p %s/home/%s/.themes", TARGETDIR, user_login);
        run_sync(app, "cp -rf /home/anon/.themes/* %s/home/%s/.themes/ 2>/dev/null || true", TARGETDIR, user_login);

        // Copy flatpak if exists
        run_sync(app, "cp -rf /var/lib/flatpak %s/var/lib/ 2>/dev/null || true", TARGETDIR);

        // Copy xbps.d config
        run_sync(app, "mkdir -p %s/etc/xbps.d", TARGETDIR);
        run_sync(app, "cp -f /etc/xbps.d/* %s/etc/xbps.d/ 2>/dev/null || true", TARGETDIR);

        // Fix ownership of user home directory
        run_sync(app, "chroot %s chown -R %s:%s /home/%s 2>/dev/null || true", TARGETDIR, user_login, user_login, user_login);

        // Autologin
        if (autologin) {
            run_sync(app, "sed -i 's/^autologin-user=.*/autologin-user=%s/' %s/etc/lightdm/lightdm.conf", user_login, TARGETDIR);
            run_sync(app, "grep -q '^autologin-user=' %s/etc/lightdm/lightdm.conf || sed -i '/^\\[Seat:\\*\\]/a autologin-user=%s' %s/etc/lightdm/lightdm.conf", TARGETDIR, user_login, TARGETDIR);
        } else {
            // Ensure autologin is explicitly disabled if the user unchecked the box
            run_sync(app, "sed -i '/^autologin-user=/d' %s/etc/lightdm/lightdm.conf", TARGETDIR);
        }

        // Sudoers
        run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > %s/etc/sudoers.d/wheel", TARGETDIR);
        run_sync(app, "chmod 0440 %s/etc/sudoers.d/wheel", TARGETDIR);
    }

    generate_fstab(app, TARGETDIR);
    generate_crypttab(app, TARGETDIR);
    return 0;
}

int step_install_bootloader(AppData *app, const char *TARGETDIR, const char *disk_name) {
    log_to_ui(app, "Installing GRUB Bootloader...", 0.9);
    char disk_path[64];
    snprintf(disk_path, sizeof(disk_path), "/dev/%s", disk_name);

    // CONFIGURE GRUB FOR LUKS
    gboolean has_crypto = FALSE;
    GSList *chk = app->part_config_list;
    while(chk) {
      if(((PartitionConfig*)chk->data)->encrypt) has_crypto = TRUE;
      chk = chk->next;
    }

if (has_crypto) {
        log_to_ui(app, "Configuring GRUB for LUKS...", 0.91);

        GSList *f = app->part_config_list;
        while(f) {
            PartitionConfig *c = (PartitionConfig*)f->data;
            if (c->encrypt && strcmp(c->mountpoint, "/") == 0 && c->luks_uuid) {
                log_to_ui_printf(app, "Setting GRUB LUKS UUID: %s", c->luks_uuid);

                // Enable cryptodisk in GRUB
                run_sync(app, "sed -i 's/GRUB_ENABLE_CRYPTODISK=.*//' %s/etc/default/grub", TARGETDIR);
                run_sync(app, "echo 'GRUB_ENABLE_CRYPTODISK=y' >> %s/etc/default/grub", TARGETDIR);

                // Set rd.luks.name for boot
                run_sync(app, "sed -i 's|GRUB_CMDLINE_LINUX_DEFAULT=\"|GRUB_CMDLINE_LINUX_DEFAULT=\"rd.luks.name=%s=cryptroot |' %s/etc/default/grub", c->luks_uuid, TARGETDIR);
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

        run_sync(app, "chroot %s grub-install --target=%s --efi-directory=/boot/efi --bootloader-id=BOOT --recheck --removable", TARGETDIR, app->efi_target);
    } else {
        run_sync(app, "chroot %s grub-install --recheck %s", TARGETDIR, disk_path);
    }

    run_sync(app, "mkdir -p %s/boot/grub", TARGETDIR);
    run_sync(app, "chroot %s grub-mkconfig -o /boot/grub/grub.cfg", TARGETDIR);

    if (has_crypto) {
        log_to_ui(app, "Configuring dracut for LUKS...", 0.93);
        run_sync(app, "mkdir -p %s/etc/dracut.conf.d", TARGETDIR);
        run_sync(app, "echo 'hostonly=yes' > %s/etc/dracut.conf.d/10-crypt.conf", TARGETDIR);
        run_sync(app, "echo 'add_dracutmodules+=\" crypt \"' >> %s/etc/dracut.conf.d/10-crypt.conf", TARGETDIR);

        log_to_ui(app, "Regenerating initramfs with LUKS support...", 0.935);
        run_sync(app, "chroot %s xbps-install -y base-system-dracut 2>/dev/null || true", TARGETDIR);
        run_sync(app, "chroot %s xbps-reconfigure -fa 2>/dev/null || chroot %s dracut --force 2>/dev/null || true", TARGETDIR, TARGETDIR);
    }
    return 0;
}

int step_finalize(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "Syncing...", 0.95);
    system("sync");

    // Disable swap partitions before unmounting
    GSList *sw = app->part_config_list;
    while(sw) {
        PartitionConfig *pc = (PartitionConfig*)sw->data;
        if (strcmp(pc->fstype, "swap") == 0) {
            run_sync(app, "swapoff %s 2>/dev/null", pc->device);
        }
        sw = sw->next;
    }

    run_sync(app, "umount -R %s", TARGETDIR);

    // CLOSE LUKS DEVICES
    gboolean has_crypto = FALSE;
    GSList *chk = app->part_config_list;
    while(chk) {
      if(((PartitionConfig*)chk->data)->encrypt) has_crypto = TRUE;
      chk = chk->next;
    }

    if (has_crypto) {
        log_to_ui(app, "Closing encrypted devices...", 0.98);
        run_sync(app, "cryptsetup close cryptroot 2>/dev/null || true");
    }
    return 0;
}
