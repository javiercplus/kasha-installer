/*
 * hard_steps.c
 * Hard Steps Implementation
 * It's really tryhard
 * only for genius and nerds
 */
#include "neko_installer.h"
#include "country_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <ctype.h>

int step_install_base_system(AppData *app, const char *TARGETDIR) {
#ifdef HAS_DESKTOP_TAB
    /*
     * When "local (default)" is UNCHECKED the user chose to install one of
     * the other desktops: instead of mass-copying the live image we
     * bootstrap the base system from the downloadable rootfs
     * (src/core/rootfs-base.c) and install everything on top of it.
     */
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_local))) {
        return void_install_rootfs_base(app, TARGETDIR);
    }
#endif
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

    /*
     * FIX OWNERSHIP OF SYSTEM DIRECTORIES
     * The live rootfs may have been built with uid/gid 1000 owning critical
     * system directories (e.g. /etc, /usr, /usr/bin, /etc/default).
     * When the tar is extracted with --preserve-permissions those wrong owners
     * are carried to the target, causing tools like xbps-install, grub-install
     * and dracut to emit:
     *   WARN: uid is 0 but '/etc/default' is owned by 1000
     * Fix this immediately after the tar, before any chroot operation.
     */
    log_to_ui(app, "Fixing system directory ownership...", 0.35);
    run_sync(app, "chown 0:0 %s %s/bin %s/sbin %s/lib %s/lib64 %s/usr "
                  "%s/usr/bin %s/usr/sbin %s/usr/lib %s/usr/lib64 "
                  "%s/etc %s/etc/default %s/etc/X11 %s/etc/profile.d "
                  "%s/var %s/var/lib %s/var/log %s/tmp %s/root "
                  "2>/dev/null || true",
             TARGETDIR,
             TARGETDIR, TARGETDIR, TARGETDIR, TARGETDIR, TARGETDIR,
             TARGETDIR, TARGETDIR, TARGETDIR, TARGETDIR,
             TARGETDIR, TARGETDIR, TARGETDIR, TARGETDIR,
             TARGETDIR, TARGETDIR, TARGETDIR, TARGETDIR, TARGETDIR);
    /* Also restore any setuid/setgid bits that tar may have stripped */
    run_sync(app, "chmod 755 %s %s/bin %s/sbin %s/usr %s/usr/bin %s/usr/sbin "
                  "%s/etc %s/var 2>/dev/null || true",
             TARGETDIR,
             TARGETDIR, TARGETDIR, TARGETDIR, TARGETDIR, TARGETDIR,
             TARGETDIR, TARGETDIR);
    run_sync(app, "chmod 1777 %s/tmp 2>/dev/null || true", TARGETDIR);
    run_sync(app, "chmod 700  %s/root 2>/dev/null || true", TARGETDIR);

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

#ifndef UNIVERSAL_BUILD
    /* --- Void Linux: xbps-install cryptsetup + copy XBPS keys --- */
    if (has_crypto) {
        void_install_crypto_packages(app, TARGETDIR);
    }
    void_copy_xbps_keys(app, TARGETDIR);
#else
    /* --- Universal: cryptsetup must already be in the base image --- */
    if (has_crypto) {
        log_to_ui(app, "[Universal] cryptsetup required – ensure it is pre-installed in the base image.", 0.55);
    }
#endif

    /* REBUILD INITRAMFS (generic, with AHCI driver for SATA support).
     * Mirrors extra/installer.sh: chroot dracut --no-hostonly --add-drivers "ahci" --force.
     * First drop 01-neko.conf so the SAME generic settings also apply to any
     * dracut run later triggered by xbps-reconfigure -fa (kernel INSTALL hook).
     * Without it, that second pass builds a HOSTONLY initramfs from the
     * chroot's host /proc/cmdline (the live USB root device) and overwrites
     * the good generic one → the installed system can't find its root
     * ("no carga initramfs"). Same approach as the rootfs path (rootfs-base.c). */
    run_sync(app, "mkdir -p %s/etc/dracut.conf.d", TARGETDIR);
    run_sync(app, "echo 'hostonly=no' > %s/etc/dracut.conf.d/01-neko.conf", TARGETDIR);
    run_sync(app, "echo 'add_drivers+=\" ahci \"' >> %s/etc/dracut.conf.d/01-neko.conf", TARGETDIR);
    log_to_ui(app, "Rebuilding initramfs (generic, this can take a few minutes)...", 0.6);
    run_sync(app, "chroot %s dracut --no-hostonly --add-drivers \"ahci\" --force", TARGETDIR);

#ifndef UNIVERSAL_BUILD
    /* --- Void Linux: reconfigure base packages with xbps-reconfigure --- */
    void_reconfigure_base(app, TARGETDIR);
#else
    log_to_ui(app, "[Universal] Rebuilding done — initramfs is generic (ahci).", 0.63);
#endif

    return 0;
}

int step_configure_system(AppData *app, const char *TARGETDIR, const gchar *hostname, const gchar *locale, const gchar *root_pass, const gchar *user_login, const gchar *user_fullname, const gchar *user_pass, gboolean autologin) {
#ifndef UNIVERSAL_BUILD
    /* --- Void Linux: remove live packages with xbps-remove --- */
    void_remove_live_packages(app, TARGETDIR);
#else
    log_to_ui(app, "[Universal] Skipping xbps-remove of live packages (not a Void system).", 0.70);
#endif

    // REMOVE LIVE USER FIRST (before creating new user to avoid UID conflicts)
    log_to_ui(app, "Removing live user (anon) from target system...", 0.72);
    if (run_sync(app, "chroot %s userdel anon 2>/dev/null", TARGETDIR) != 0) {
        log_to_ui(app, "WARNING: userdel anon failed, forcing removal...", 0.72);
    }
    run_sync(app, "rm -rf %s/home/anon 2>/dev/null || true", TARGETDIR);
    run_sync(app, "rm -f %s/var/spool/mail/anon 2>/dev/null || true", TARGETDIR);
    run_sync(app, "rm -f %s/etc/sudoers.d/99-void-live 2>/dev/null || true", TARGETDIR);
    run_sync(app, "sed -i 's|GETTY_ARGS=\"--noclear -a anon\"|GETTY_ARGS=\"--noclear\"|g' %s/etc/sv/agetty-tty1/conf", TARGETDIR);
    run_sync(app, "rm -f %s/etc/polkit-1/rules.d/void-live.rules 2>/dev/null || true", TARGETDIR);

    // CLEANUP CLONED LIVE STATE
    log_to_ui(app, "Cleaning up machine-id and network state...", 0.73);
    run_sync(app, "rm -f %s/etc/machine-id", TARGETDIR);
    run_sync(app, "rm -f %s/var/lib/dbus/machine-id", TARGETDIR);

    // Copy NetworkManager WiFi/network connections from live system
    run_sync(app, "mkdir -p %s/etc/NetworkManager/system-connections", TARGETDIR);
    run_sync(app, "cp -rf /etc/NetworkManager/system-connections/* %s/etc/NetworkManager/system-connections/ 2>/dev/null || true", TARGETDIR);


    // CONFIGURATION (Hostname, Locale)
    log_to_ui(app, "Applying System Configuration...", 0.75);
    run_sync(app, "echo '%s' > %s/etc/hostname", hostname, TARGETDIR);

    // Enable locale
    run_sync(app, "echo 'LANG=%s' > %s/etc/locale.conf", locale, TARGETDIR);
#ifndef UNIVERSAL_BUILD
    /* --- Void Linux: enable locale in libc-locales and reconfigure with xbps --- */
    void_reconfigure_locales(app, TARGETDIR, locale);
#else
    /* --- Universal: use locale-gen or another base distro mechanism --- */
    run_sync(app, "chroot %s locale-gen 2>/dev/null || true", TARGETDIR);
#endif

    // KEYMAP SETUP — configure keyboard layout for console, X11 and Wayland
    {
        const char *console_kmap = "us";
        const char *x11_layout = "us";
        const char *x11_variant = "";

        // 1st priority: user-selected keyboard layout from UI
        int kbd_active = gtk_combo_box_get_active(GTK_COMBO_BOX(app->kbd_layout_combo));
        if (kbd_active >= 0) {
            int kbd_count = 0;
            const KbdLayout *layouts = get_keyboard_layouts(&kbd_count);
            if (kbd_active < kbd_count) {
                console_kmap = layouts[kbd_active].console_kmap;
                x11_layout   = layouts[kbd_active].layout;
            }

            // Get selected variant
            int var_active = gtk_combo_box_get_active(GTK_COMBO_BOX(app->kbd_variant_combo));
            if (var_active > 0 && kbd_active < kbd_count) {
                // variant index 0 = "Default" = empty string
                const KbdVariant *vars = layouts[kbd_active].variants;
                for (int i = 0; vars[i].name != NULL; i++) {
                    if (i == var_active) {
                        x11_variant = vars[i].code;
                        break;
                    }
                }
            }
        } else {
            // 2nd priority: derive from country selection
            gchar *country_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->country_combo));
            if (country_name && strlen(country_name) > 0) {
                const CountryInfo *ci = find_country_by_name(country_name);
                if (ci && ci->console_kmap && ci->x11_layout) {
                    console_kmap = ci->console_kmap;
                    x11_layout   = ci->x11_layout;
                }
                g_free(country_name);
            }
        }

        log_to_ui_printf(app, "Keyboard: console=%s, X11=%s, variant=%s",
                         console_kmap, x11_layout,
                         strlen(x11_variant) ? x11_variant : "none");

        // 1. Console TTY keymap → /etc/rc.conf (Runit/Void Linux)
        run_sync(app, "mkdir -p %s/etc", TARGETDIR);
        run_sync(app, "if grep -q '^KEYMAP=' %s/etc/rc.conf 2>/dev/null; then "
                       "sed -i 's/^KEYMAP=.*/KEYMAP=\"%s\"/' %s/etc/rc.conf; "
                       "else echo 'KEYMAP=\"%s\"' >> %s/etc/rc.conf; fi",
                 TARGETDIR, console_kmap, TARGETDIR, console_kmap, TARGETDIR);

        // 2. X11 keyboard → /etc/X11/xorg.conf.d/00-keyboard.conf
        run_sync(app, "mkdir -p %s/etc/X11/xorg.conf.d", TARGETDIR);
        if (strlen(x11_variant) > 0) {
            run_sync(app, "printf 'Section \"InputClass\"\\n"
                           "        Identifier \"system-keyboard\"\\n"
                           "        MatchIsKeyboard \"on\"\\n"
                           "        Option \"XkbLayout\" \"%s\"\\n"
                           "        Option \"XkbVariant\" \"%s\"\\n"
                           "EndSection\\n' > %s/etc/X11/xorg.conf.d/00-keyboard.conf",
                     x11_layout, x11_variant, TARGETDIR);
        } else {
            run_sync(app, "printf 'Section \"InputClass\"\\n"
                           "        Identifier \"system-keyboard\"\\n"
                           "        MatchIsKeyboard \"on\"\\n"
                           "        Option \"XkbLayout\" \"%s\"\\n"
                           "EndSection\\n' > %s/etc/X11/xorg.conf.d/00-keyboard.conf",
                     x11_layout, TARGETDIR);
        }

        // 3. Environment variables for Wayland / XKB → /etc/profile.d/keyboard.sh
        run_sync(app, "mkdir -p %s/etc/profile.d", TARGETDIR);
        if (strlen(x11_variant) > 0) {
            run_sync(app, "printf 'export XKB_DEFAULT_LAYOUT=\"%s\"\\n"
                           "export XKB_DEFAULT_VARIANT=\"%s\"\\n' > %s/etc/profile.d/keyboard.sh",
                     x11_layout, x11_variant, TARGETDIR);
        } else {
            run_sync(app, "printf 'export XKB_DEFAULT_LAYOUT=\"%s\"\\n' > %s/etc/profile.d/keyboard.sh",
                     x11_layout, TARGETDIR);
        }
        run_sync(app, "chmod 0644 %s/etc/profile.d/keyboard.sh", TARGETDIR);
    }

    // TIMEZONE SETUP
    char *tz_area = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->tz_area_combo));
    char *tz_city = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->tz_city_combo));

    if (tz_area && tz_city) {
        log_to_ui(app, "Setting Timezone...", 0.78);
        run_sync(app, "ln -sf /usr/share/zoneinfo/%s/%s %s/etc/localtime", tz_area, tz_city, TARGETDIR);
        g_free(tz_area);
        g_free(tz_city);
    } else {
        /* Ensure we free whichever pointer was allocated before defaulting to UTC */
        if (tz_area) g_free(tz_area);
        if (tz_city) g_free(tz_city);
        log_to_ui(app, "Timezone not selected, defaulting to UTC.", 0.78);
        run_sync(app, "ln -sf /usr/share/zoneinfo/UTC %s/etc/localtime", TARGETDIR);
    }

#ifdef HAS_DESKTOP_TAB
    /*
     * INSTALL DESKTOP ENVIRONMENT — must run BEFORE user creation so that
     * the desktop config copy can populate /etc/skel with the desktop
     * dotfiles. useradd -m (below) will then copy those skel files into the
     * new user's home directory automatically.
     *
     * When "local (default)" is checked nothing runs here: the live image
     * is copied as-is (see step_install_base_system).
     *
     * The autologin configuration happens INSIDE the user-creation block
     * further below, which runs AFTER this desktop install, so there is
     * no risk of overwriting the autologin settings.
     */
    g_free(app->selected_desktop);
    app->selected_desktop = NULL;
    /* "local (default)" checked → copy the live image as-is, no desktop
     * install and no neko-desktops logic at all. */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_local)))
        app->selected_desktop = g_strdup("local");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_xfce)))
        app->selected_desktop = g_strdup("xfce");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_niri)))
        app->selected_desktop = g_strdup("niri");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_kde)))
        app->selected_desktop = g_strdup("kde");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_icejwm)))
        app->selected_desktop = g_strdup("icejwm");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_mate)))
        app->selected_desktop = g_strdup("mate");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_labwc)))
        app->selected_desktop = g_strdup("labwc");
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_desktop_lxqt)))
        app->selected_desktop = g_strdup("lxqt");

    if (app->selected_desktop && strcmp(app->selected_desktop, "local") != 0) {
        log_to_ui_printf(app, "Installing desktop environment: %s", app->selected_desktop);
        if (strcmp(app->selected_desktop, "xfce") == 0)
            void_xfce(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "niri") == 0)
            void_niri(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "kde") == 0)
            void_kde(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "icejwm") == 0)
            void_icejwm(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "mate") == 0)
            void_mate(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "labwc") == 0)
            void_labwc(app, TARGETDIR);
        else if (strcmp(app->selected_desktop, "lxqt") == 0)
            void_lxqt(app, TARGETDIR);
    }
#endif /* HAS_DESKTOP_TAB */

    // ROOT USER
    log_to_ui(app, "Setting Root Password (SHA512)...", 0.80);
    if (!set_safe_password(app, "root", root_pass, TARGETDIR)) {
        return -1;
    }

    // Copy /etc/skel files for root
    run_sync(app, "cp %s/etc/skel/.[bix]* %s/root/ 2>/dev/null", TARGETDIR, TARGETDIR);

    // CREATE USER ACCOUNT (with full group membership)
    if (strlen(user_login) > 0) {
        log_to_ui(app, "Creating user account...", 0.82);

        // Step 1: Ensure ALL required groups exist
        // Note: nopasswdlogin is NOT included here — it is only added conditionally
        // when autologin is enabled (it grants password-bypass via PAM for emptty)
        const char *all_groups[] = {
            "wheel", "floppy", "audio", "video", "cdrom", "optical",
            "storage", "network", "kvm", "input", "plugdev", "users",
            "xbuilder", "render", "fuse", "disk", NULL
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
        gchar *safe_fullname = NULL;
        if (user_fullname && strlen(user_fullname) > 0) {
            safe_fullname = g_shell_quote(user_fullname);
        }

        gchar *useradd_cmd = NULL;
        if (safe_fullname) {
            useradd_cmd = g_strdup_printf(
                "chroot %s useradd -m -c %s -G %s -s /bin/bash %s",
                TARGETDIR, safe_fullname, valid_groups, user_login);
        } else {
            useradd_cmd = g_strdup_printf(
                "chroot %s useradd -m -G %s -s /bin/bash %s",
                TARGETDIR, valid_groups, user_login);
        }

        g_free(safe_fullname);

        log_to_ui(app, useradd_cmd, -1);
        int add_result = system(useradd_cmd);
        g_free(useradd_cmd);
        if (WEXITSTATUS(add_result) != 0) {
            log_to_ui(app, "ERROR: useradd failed! Aborting configuration.", 0.0);
            return -1;
        }

        log_to_ui(app, "Setting User Password (SHA512)...", 0.84);
        if (!set_safe_password(app, user_login, user_pass, TARGETDIR)) {
            return -1;
        }

        // Verify user was created
        if (run_sync(app, "chroot %s id %s", TARGETDIR, user_login) != 0) {
            log_to_ui_printf(app, "ERROR: User %s not found after creation!", user_login);
            return -1;
        }
        log_to_ui_printf(app, "User %s created successfully.", user_login);

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

#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: copy XBPS repository configuration --- */
        void_copy_xbpsd_config(app, TARGETDIR);
#endif

        // Fix ownership of user home directory
        run_sync(app, "chroot %s chown -R %s:%s /home/%s 2>/dev/null || true", TARGETDIR, user_login, user_login, user_login);

        // Privilege Manager: doas or sudo
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->radio_priv_doas))) {
            log_to_ui(app, "Configuring doas (lightweight privilege manager)...", 0.88);

            // 1. Prevent sudo from being pulled as a dependency
            run_sync(app, "mkdir -p %s/etc/xbps.d", TARGETDIR);
            run_sync(app, "if ! grep -q 'ignorepkg=sudo' %s/etc/xbps.d/10-ignore.conf 2>/dev/null; then "
                           "echo 'ignorepkg=sudo' >> %s/etc/xbps.d/10-ignore.conf; fi",
                     TARGETDIR, TARGETDIR);

            // 2. Install opendoas
            run_sync(app, "chroot %s xbps-install -Sy --repository=https://repo-de.voidlinux.org/current/ opendoas", TARGETDIR);

            // 3. Configure doas.conf
            run_sync(app, "printf '# doas configuration\npermit persist keepenv :wheel\n' > %s/etc/doas.conf", TARGETDIR);
            run_sync(app, "chmod 0400 %s/etc/doas.conf", TARGETDIR);

            // 4. Remove sudo (will be ignored by xbps from now on)
            run_sync(app, "chroot %s xbps-remove -Ry sudo", TARGETDIR);

            // Still write sudoers as fallback
            run_sync(app, "mkdir -p %s/etc/sudoers.d", TARGETDIR);
            run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > %s/etc/sudoers.d/wheel", TARGETDIR);
            run_sync(app, "chmod 0440 %s/etc/sudoers.d/wheel", TARGETDIR);
        } else {
            // Traditional sudo
            log_to_ui(app, "Configuring sudo...", 0.88);
            run_sync(app, "chroot %s xbps-install -Sy --repository=https://repo-de.voidlinux.org/current/ sudo", TARGETDIR);
            run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > %s/etc/sudoers.d/wheel", TARGETDIR);
            run_sync(app, "chmod 0440 %s/etc/sudoers.d/wheel", TARGETDIR);
        }
    }

    generate_fstab(app, TARGETDIR);
    generate_crypttab(app, TARGETDIR);


    // ================================================================
    // AUTOLOGIN — must be the absolute LAST step.  Nothing below
    // this point touches DM config, group membership or PAM data.
    // ================================================================
    if (strlen(user_login) > 0) {
        char emptty_conf_path[512];
        snprintf(emptty_conf_path, sizeof(emptty_conf_path), "%s/etc/emptty/conf", TARGETDIR);

        if (autologin) {
            /* The 'autologin' group is required by the PAM config shipped with
             * SDDM and LightDM on Void (pam_succeed_if.so user ingroup autologin).
             * Without it the DM silently blocks autologin and falls back to the
             * password prompt. Ensure the group exists and the user is in it. */
            log_to_ui(app, "Configuring autologin...", 0.87);
            run_sync(app, "chroot %s getent group autologin > /dev/null 2>/dev/null || chroot %s groupadd -r autologin", TARGETDIR, TARGETDIR);
            run_sync(app, "chroot %s gpasswd -a %s autologin 2>/dev/null || chroot %s usermod -aG autologin %s", TARGETDIR, user_login, TARGETDIR, user_login);

            /* Pick the display manager that is ACTUALLY enabled in runit
             * (desktop-set.sh enables exactly one via /var/service). Checking the
             * binary presence is unreliable because several DMs may coexist. */
            char lightdm_conf_path[512];
            snprintf(lightdm_conf_path, sizeof(lightdm_conf_path), "%s/etc/lightdm/lightdm.conf", TARGETDIR);

            /* Check which display manager desktop-set.sh enabled.
             * desktop-set.sh creates symlinks in BOTH /var/service (active,
             * inside the chroot it is a real dir) AND /etc/runit/runsvdir/default
             * (persistent, survives reboot).  Inside the chroot /var/service is
             * replaced with a real directory by the script, so test -L would
             * return false.  The /etc/runit/runsvdir/default path is always a
             * real directory and its entries are the ground truth.
             *
             * IMPORTANT: use test -L, NOT test -e.  The symlinks point to
             * absolute paths (/etc/sv/sddm).  test -e follows symlinks and
             * from the live system resolves to /etc/sv/sddm on the LIVE host,
             * which may not have SDDM installed → false negative → autologin
             * silently skipped.  test -L only checks the symlink itself. */
            int sddm_enabled    = (run_sync(app, "test -L %s/etc/runit/runsvdir/default/sddm",    TARGETDIR) == 0);
            int lightdm_enabled = (run_sync(app, "test -L %s/etc/runit/runsvdir/default/lightdm", TARGETDIR) == 0);

            if (sddm_enabled) {
                log_to_ui(app, "Configuring SDDM autologin...", 0.87);
                run_sync(app, "mkdir -p %s/etc/sddm.conf.d", TARGETDIR);
                /* SDDM requires Session= to avoid falling back to the password
                 * prompt.  KDE Plasma always registers its session as 'plasma'. */
                run_sync(app, "printf '[Autologin]\\nUser=%s\\nSession=plasma\\n' > %s/etc/sddm.conf.d/autologin.conf",
                         user_login, TARGETDIR);
                run_sync(app, "rm -f %s/etc/sddm.conf", TARGETDIR);
            } else if (lightdm_enabled) {
                log_to_ui(app, "Configuring LightDM autologin...", 0.87);
                run_sync(app, "sed -i 's/^autologin-user=.*/autologin-user=%s/' %s/etc/lightdm/lightdm.conf",
                         user_login, TARGETDIR);
                run_sync(app, "grep -q '^autologin-user=' %s/etc/lightdm/lightdm.conf || "
                              "sed -i '/^\\[Seat:\\*\\]/a autologin-user=%s' %s/etc/lightdm/lightdm.conf",
                         TARGETDIR, user_login, TARGETDIR);
            }

            // emptty (independent - can coexist with graphical DMs)
            // Add user to nopasswdlogin group (required by emptty/PAM for password bypass)
            run_sync(app, "chroot %s getent group nopasswdlogin > /dev/null 2>/dev/null || chroot %s groupadd -r nopasswdlogin", TARGETDIR, TARGETDIR);
            run_sync(app, "chroot %s usermod -aG nopasswdlogin %s", TARGETDIR, user_login);
            if (access(emptty_conf_path, F_OK) == 0) {
                log_to_ui(app, "Configuring emptty autologin...", 0.87);
                run_sync(app, "sed -i 's/^#\\?DEFAULT_USER=.*/DEFAULT_USER=%s/' %s/etc/emptty/conf", user_login, TARGETDIR);
                run_sync(app, "sed -i 's/^#\\?AUTOLOGIN=.*/AUTOLOGIN=true/' %s/etc/emptty/conf", TARGETDIR);
            }
        } else {
            // Ensure autologin is explicitly disabled if the user unchecked the box
            // SDDM
            run_sync(app, "rm -f %s/etc/sddm.conf.d/autologin.conf", TARGETDIR);
            // LightDM
            run_sync(app, "sed -i '/^autologin-user=/d' %s/etc/lightdm/lightdm.conf 2>/dev/null || true", TARGETDIR);
            // emptty: disable autologin in config AND ensure user is NOT in nopasswdlogin group
            if (access(emptty_conf_path, F_OK) == 0) {
                run_sync(app, "sed -i 's/^#\\?DEFAULT_USER=.*/#DEFAULT_USER=/' %s/etc/emptty/conf", TARGETDIR);
                run_sync(app, "sed -i 's/^#\\?AUTOLOGIN=.*/AUTOLOGIN=false/' %s/etc/emptty/conf", TARGETDIR);
            }
            // Remove user from nopasswdlogin group and delete the group entirely.
            // This prevents emptty/PAM from bypassing the password prompt.
            run_sync(app, "chroot %s gpasswd -d %s nopasswdlogin 2>/dev/null || true", TARGETDIR, user_login);
            run_sync(app, "chroot %s groupdel nopasswdlogin 2>/dev/null || true", TARGETDIR);
        }
    }

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
#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: install grub-*-efi via xbps --- */
        void_install_grub_efi_pkg(app, TARGETDIR);
#else
        log_to_ui(app, "[Universal] grub EFI package must be pre-installed in the base image.", 0.91);
#endif
        if (run_sync(app, "chroot %s grub-install --target=%s --efi-directory=/boot/efi --bootloader-id=BOOT --recheck --removable", TARGETDIR, app->efi_target) != 0) {
            log_to_ui(app, "ERROR: grub-install (EFI) failed! Check that the EFI partition is mounted at /boot/efi.", 0.0);
            return -1;
        }
    } else {
#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: install grub-i386-pc for MBR/Legacy BIOS --- */
        void_install_grub_bios_pkg(app, TARGETDIR);
#else
        log_to_ui(app, "[Universal] grub-i386-pc package must be pre-installed in the base image.", 0.91);
#endif
        /* Ensure /dev is fully accessible inside chroot for grub-install to probe the disk */
        run_sync(app, "mount --bind /dev %s/dev 2>/dev/null || true", TARGETDIR);

        if (run_sync(app, "chroot %s grub-install --target=i386-pc --recheck --force %s", TARGETDIR, disk_path) != 0) {
            log_to_ui(app, "ERROR: grub-install (BIOS/MBR) failed! The disk may be busy or GRUB i386-pc modules are missing.", 0.0);
            return -1;
        }
    }

    run_sync(app, "mkdir -p %s/boot/grub", TARGETDIR);

    // DUAL BOOT: Install and enable os-prober to detect other operating systems
    if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: install os-prober and ntfs-3g via xbps --- */
        void_install_osprober(app, TARGETDIR);
#else
        log_to_ui(app, "[Universal] os-prober/ntfs-3g must be pre-installed in the base image.", 0.92);
#endif

        // Ensure GRUB_DISABLE_OS_PROBER is not set to true
        run_sync(app, "sed -i '/GRUB_DISABLE_OS_PROBER/d' %s/etc/default/grub", TARGETDIR);
        run_sync(app, "echo 'GRUB_DISABLE_OS_PROBER=false' >> %s/etc/default/grub", TARGETDIR);

        // Mount other partitions so os-prober can detect them
        log_to_ui(app, "Scanning for other operating systems...", 0.925);
        run_sync(app, "mkdir -p /tmp/kasha_osprobe");

        // Scan all partitions on the system for other OS
        char probe_cmd[512];
        snprintf(probe_cmd, sizeof(probe_cmd),
            "lsblk -rn -o NAME,FSTYPE /dev/%s 2>/dev/null | while read name fstype; do "
            "  case \"$fstype\" in "
            "    ntfs|ext4|ext3|btrfs|xfs) "
            "      dev=\"/dev/$name\"; "
            "      mp=\"/tmp/kasha_osprobe/$name\"; "
            "      mkdir -p \"$mp\"; "
            "      mount -o ro \"$dev\" \"$mp\" 2>/dev/null || true; "
            "    ;; "
            "  esac; "
            "done", disk_name);
        system(probe_cmd);
    }

    /* Finalization: the initramfs was already regenerated (generically, per
     * /etc/dracut.conf.d/01-neko.conf) — during step_install_base_system for
     * the live-copy path, and by xbps-reconfigure -fa during the rootfs
     * install. So grub-mkconfig always finds a valid initramfs. */
    run_sync(app, "chroot %s grub-mkconfig -o /boot/grub/grub.cfg", TARGETDIR);

    // Cleanup os-prober mounts
    if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
        run_sync(app, "umount -R /tmp/kasha_osprobe 2>/dev/null || true");
        run_sync(app, "rm -rf /tmp/kasha_osprobe 2>/dev/null || true");
    }


    if (has_crypto) {
        log_to_ui(app, "Configuring dracut for LUKS...", 0.93);
        run_sync(app, "mkdir -p %s/etc/dracut.conf.d", TARGETDIR);
        run_sync(app, "echo 'hostonly=yes' > %s/etc/dracut.conf.d/10-crypt.conf", TARGETDIR);
        run_sync(app, "echo 'add_dracutmodules+=\" crypt \"' >> %s/etc/dracut.conf.d/10-crypt.conf", TARGETDIR);

        log_to_ui(app, "Regenerating initramfs with LUKS support...", 0.935);
#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: install base-system-dracut and reconfigure with xbps --- */
        void_install_dracut_luks(app, TARGETDIR);
#else
        /* --- Universal: regenerate initramfs directly with dracut --- */
        run_sync(app, "chroot %s dracut --force 2>/dev/null || true", TARGETDIR);
#endif
    }
    return 0;
}

