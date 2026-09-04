/*
 * step_config_system.c
 * Helper functions for step_configure_system (extracted from hard_steps.c)
 * Handles: live user cleanup, locale/hostname, keyboard, timezone,
 *          desktop env selection, user creation, privilege manager, autologin.
 */
#include "neko_installer.h"
#include "country_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ *
 *  config_sys_remove_live_user                                        *
 *  Remove the live user (anon), clean machine-id and copy NM conns.  *
 * ------------------------------------------------------------------ */
void config_sys_remove_live_user(AppData *app, const char *TARGETDIR) {
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
}

/* ------------------------------------------------------------------ *
 *  config_sys_locale_and_hostname                                     *
 *  Set hostname, LANG and patch regional translations.               *
 * ------------------------------------------------------------------ */
void config_sys_locale_and_hostname(AppData *app, const char *TARGETDIR, const gchar *hostname, const gchar *locale) {
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

    /* --- Set LC_ALL in /etc/skel/.bashrc so every new user inherits it --- */
    run_sync(app, "if grep -q '^export LC_ALL=' %s/etc/skel/.bashrc 2>/dev/null; then "
                  "sed -i 's|^export LC_ALL=.*|export LC_ALL=%s|' %s/etc/skel/.bashrc; "
                  "else printf '\\nexport LC_ALL=%s\\n' >> %s/etc/skel/.bashrc; fi",
             TARGETDIR, locale, TARGETDIR, locale, TARGETDIR);

    /* --- Regional translation fallback patch ---
     * Upstream often ships broken/empty .mo files for regional variants.
     * Force a copy of ALL base-language translations (e.g. 'es') over the
     * regional ones (e.g. 'es_VE') to guarantee a fully translated system
     * across all applications. */
    log_to_ui(app, "Patching regional translations for all packages...", 0.76);
    run_sync(app,
        "chroot %s sh -c '"
        "BASE_LANG=$(echo \"%s\" | cut -d\"_\" -f1); "
        "REGIONAL_LANG=$(echo \"%s\" | cut -d\".\" -f1); "
        "if [ \"$BASE_LANG\" != \"$REGIONAL_LANG\" ] && [ -d \"/usr/share/locale/$BASE_LANG/LC_MESSAGES\" ]; then "
        "  mkdir -p \"/usr/share/locale/$REGIONAL_LANG/LC_MESSAGES\"; "
        "  cp -f /usr/share/locale/$BASE_LANG/LC_MESSAGES/*.mo \"/usr/share/locale/$REGIONAL_LANG/LC_MESSAGES/\" 2>/dev/null || true; "
        "fi'",
        TARGETDIR, locale, locale);
}

/* ------------------------------------------------------------------ *
 *  config_sys_keyboard                                                *
 *  Configure keyboard layout for console, X11 and Wayland.           *
 * ------------------------------------------------------------------ */
void config_sys_keyboard(AppData *app, const char *TARGETDIR) {
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

    // 1. Console TTY keymap -> /etc/rc.conf (Runit/Void Linux)
    run_sync(app, "mkdir -p %s/etc", TARGETDIR);
    run_sync(app, "if grep -q '^KEYMAP=' %s/etc/rc.conf 2>/dev/null; then "
                   "sed -i 's/^KEYMAP=.*/KEYMAP=\"%s\"/' %s/etc/rc.conf; "
                   "else echo 'KEYMAP=\"%s\"' >> %s/etc/rc.conf; fi",
             TARGETDIR, console_kmap, TARGETDIR, console_kmap, TARGETDIR);

    // 2. X11 keyboard -> /etc/X11/xorg.conf.d/00-keyboard.conf
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

    // 3. Environment variables for Wayland / XKB -> /etc/profile.d/keyboard.sh
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

/* ------------------------------------------------------------------ *
 *  config_sys_timezone                                                *
 *  Set /etc/localtime from the selected timezone area and city.      *
 * ------------------------------------------------------------------ */
void config_sys_timezone(AppData *app, const char *TARGETDIR) {
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
}

/* ------------------------------------------------------------------ *
 *  config_sys_desktop_env                                             *
 *  Detect selected desktop and install it via neko-desktops.         *
 *  Must run BEFORE user creation so /etc/skel is populated.          *
 * ------------------------------------------------------------------ */
#ifdef HAS_DESKTOP_TAB
void config_sys_desktop_env(AppData *app, const char *TARGETDIR) {
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
    /* "local (default)" checked -> copy the live image as-is, no desktop
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
}
#endif /* HAS_DESKTOP_TAB */

/* ------------------------------------------------------------------ *
 *  config_sys_create_user                                             *
 *  Create root password, user account with groups, XDG dirs,         *
 *  and Neko Void customizations.                                     *
 * ------------------------------------------------------------------ */
int config_sys_create_user(AppData *app, const char *TARGETDIR, const gchar *locale,
                           const gchar *root_pass, const gchar *user_login,
                           const gchar *user_fullname, const gchar *user_pass) {
    // ROOT USER
    log_to_ui(app, "Setting Root Password (SHA512)...", 0.80);
    if (!set_safe_password(app, "root", root_pass, TARGETDIR)) {
        return -1;
    }

    // Ensure Flatpak exports are visible to every new user via XDG_DATA_DIRS
    run_sync(app, "grep -q 'flatpak/exports/share' %s/etc/skel/.profile 2>/dev/null || "
                  "printf '\\nexport XDG_DATA_DIRS=\"/var/lib/flatpak/exports/share:$HOME/.local/share/flatpak/exports/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}:/usr/local/share:/usr/share\"\\n' "
                  ">> %s/etc/skel/.profile", TARGETDIR, TARGETDIR);
    run_sync(app, "grep -q 'flatpak/exports/share' %s/etc/skel/.bash_profile 2>/dev/null || "
                  "printf '\\nexport XDG_DATA_DIRS=\"/var/lib/flatpak/exports/share:$HOME/.local/share/flatpak/exports/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}:/usr/local/share:/usr/share\"\\n' "
                  ">> %s/etc/skel/.bash_profile", TARGETDIR, TARGETDIR);

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

        // Copy flatpak if exists and initialize the system repo
        run_sync(app, "cp -rf /var/lib/flatpak %s/var/lib/ 2>/dev/null || true", TARGETDIR);
        configure_flatpak_repo(app, TARGETDIR);

#ifndef UNIVERSAL_BUILD
        /* --- Void Linux: copy XBPS repository configuration --- */
        void_copy_xbpsd_config(app, TARGETDIR);
#endif

        // ================================================================
        // XDG USER DIRECTORIES (localized per base language)
        // ================================================================
        log_to_ui(app, "Generating localized user directories...", 0.85);
        run_sync(app,
            "chroot %s sh -c '"
            "BASE_LANG=$(echo \"%s\" | cut -d\"_\" -f1); "
            "USER_HOME=\"/home/%s\"; "
            "case \"$BASE_LANG\" in "
            "  es) D_DESK=\"Escritorio\"; D_DOCS=\"Documentos\"; D_DOWN=\"Descargas\"; D_MUS=\"M\u00fasica\"; D_PIC=\"Im\u00e1genes\"; D_PUB=\"P\u00fablico\"; D_TPL=\"Plantillas\"; D_VID=\"V\u00eddeos\"; D_PROJ=\"Proyectos\" ;; "
            "  fr) D_DESK=\"Bureau\"; D_DOCS=\"Documents\"; D_DOWN=\"T\u00e9l\u00e9chargements\"; D_MUS=\"Musique\"; D_PIC=\"Images\"; D_PUB=\"Public\"; D_TPL=\"Mod\u00e8les\"; D_VID=\"Vid\u00e9os\"; D_PROJ=\"Projets\" ;; "
            "  ja) D_DESK=\"\u30c7\u30b9\u30af\u30c8\u30c3\u30d7\"; D_DOCS=\"\u30c9\u30ad\u30e5\u30e1\u30f3\u30c8\"; D_DOWN=\"\u30c0\u30a6\u30f3\u30ed\u30fc\u30c9\"; D_MUS=\"\u30df\u30e5\u30fc\u30b8\u30c3\u30af\"; D_PIC=\"\u30d4\u30af\u30c1\u30e3\"; D_PUB=\"\u516c\u958b\"; D_TPL=\"\u30c6\u30f3\u30d7\u30ec\u30fc\u30c8\"; D_VID=\"\u30d3\u30c7\u30aa\"; D_PROJ=\"\u30d7\u30ed\u30b8\u30a7\u30af\u30c8\" ;; "
            "  pt) D_DESK=\"\u00c1rea de Trabalho\"; D_DOCS=\"Documentos\"; D_DOWN=\"Downloads\"; D_MUS=\"M\u00fasica\"; D_PIC=\"Imagens\"; D_PUB=\"P\u00fablico\"; D_TPL=\"Modelos\"; D_VID=\"V\u00eddeos\"; D_PROJ=\"Projetos\" ;; "
            "  de) D_DESK=\"Schreibtisch\"; D_DOCS=\"Dokumente\"; D_DOWN=\"Downloads\"; D_MUS=\"Musik\"; D_PIC=\"Bilder\"; D_PUB=\"\u00d6ffentlich\"; D_TPL=\"Vorlagen\"; D_VID=\"Videos\"; D_PROJ=\"Projekte\" ;; "
            "  ru) D_DESK=\"\u0420\u0430\u0431\u043e\u0447\u0438\u0439 \u0441\u0442\u043e\u043b\"; D_DOCS=\"\u0414\u043e\u043a\u0443\u043c\u0435\u043d\u0442\u044b\"; D_DOWN=\"\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0438\"; D_MUS=\"\u041c\u0443\u0437\u044b\u043a\u0430\"; D_PIC=\"\u0418\u0437\u043e\u0431\u0440\u0430\u0436\u0435\u043d\u0438\u044f\"; D_PUB=\"\u041e\u0431\u0449\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u043d\u044b\u0435\"; D_TPL=\"\u0428\u0430\u0431\u043b\u043e\u043d\u044b\"; D_VID=\"\u0412\u0438\u0434\u0435\u043e\"; D_PROJ=\"\u041f\u0440\u043e\u0435\u043a\u0442\u044b\" ;; "
            "  uk) D_DESK=\"\u0420\u043e\u0431\u043e\u0447\u0438\u0439 \u0441\u0442\u0456\u043b\"; D_DOCS=\"\u0414\u043e\u043a\u0443\u043c\u0435\u043d\u0442\u0438\"; D_DOWN=\"\u0417\u0430\u0432\u0430\u043d\u0442\u0430\u0436\u0435\u043d\u043d\u044f\"; D_MUS=\"\u041c\u0443\u0437\u0438\u043a\u0430\"; D_PIC=\"\u0417\u043e\u0431\u0440\u0430\u0436\u0435\u043d\u043d\u044f\"; D_PUB=\"\u0417\u0430\u0433\u0430\u043b\u044c\u043d\u043e\u0434\u043e\u0441\u0442\u0443\u043f\u043d\u0456\"; D_TPL=\"\u0428\u0430\u0431\u043b\u043e\u043d\u0438\"; D_VID=\"\u0412\u0456\u0434\u0435\u043e\"; D_PROJ=\"\u041f\u0440\u043e\u0454\u043a\u0442\u0438\" ;; "
            "  it) D_DESK=\"Scrivania\"; D_DOCS=\"Documenti\"; D_DOWN=\"Download\"; D_MUS=\"Musica\"; D_PIC=\"Immagini\"; D_PUB=\"Pubblici\"; D_TPL=\"Modelli\"; D_VID=\"Video\"; D_PROJ=\"Progetti\" ;; "
            "  *)  D_DESK=\"Desktop\"; D_DOCS=\"Documents\"; D_DOWN=\"Downloads\"; D_MUS=\"Music\"; D_PIC=\"Pictures\"; D_PUB=\"Public\"; D_TPL=\"Templates\"; D_VID=\"Videos\"; D_PROJ=\"Projects\" ;; "
            "esac; "
            "mkdir -p \"$USER_HOME/$D_DESK\" \"$USER_HOME/$D_DOCS\" \"$USER_HOME/$D_DOWN\" "
            "         \"$USER_HOME/$D_MUS\" \"$USER_HOME/$D_PIC\" \"$USER_HOME/$D_PUB\" "
            "         \"$USER_HOME/$D_TPL\" \"$USER_HOME/$D_VID\" \"$USER_HOME/$D_PROJ\" \"$USER_HOME/.config\"; "
            "cat <<EOF > \"$USER_HOME/.config/user-dirs.dirs\"\n"
            "XDG_DESKTOP_DIR=\"$USER_HOME/$D_DESK/\"\n"
            "XDG_DOCUMENTS_DIR=\"$USER_HOME/$D_DOCS/\"\n"
            "XDG_DOWNLOAD_DIR=\"$USER_HOME/$D_DOWN/\"\n"
            "XDG_MUSIC_DIR=\"$USER_HOME/$D_MUS/\"\n"
            "XDG_PICTURES_DIR=\"$USER_HOME/$D_PIC/\"\n"
            "XDG_PROJECTS_DIR=\"\\$HOME/$D_PROJ\"\n"
            "XDG_PUBLICSHARE_DIR=\"$USER_HOME/$D_PUB/\"\n"
            "XDG_TEMPLATES_DIR=\"$USER_HOME/$D_TPL/\"\n"
            "XDG_VIDEOS_DIR=\"$USER_HOME/$D_VID/\"\n"
            "EOF\n"
            "chown -R %s:%s \"$USER_HOME\"'",
            TARGETDIR, locale, user_login, user_login, user_login);

        // Comment out the xdg-user-dirs-update call inside rice_set so it
        // does not overwrite the localized directory names just created.
        log_to_ui(app, "Patching rice_set (disable xdg-user-dirs-update)...", 0.85);
        run_sync(app,
            "sed -i 's|^\\([[:space:]]*xdg-user-dirs-update.*\\)$|# \\1|' "
            "%s/usr/bin/rice_set 2>/dev/null || true", TARGETDIR);

        // Fix ownership of user home directory
        run_sync(app, "chroot %s chown -R %s:%s /home/%s 2>/dev/null || true", TARGETDIR, user_login, user_login, user_login);
    }

    return 0;
}

/* ------------------------------------------------------------------ *
 *  config_sys_privileges                                              *
 *  Configure doas or sudo as privilege manager.                      *
 * ------------------------------------------------------------------ */
void config_sys_privileges(AppData *app, const char *TARGETDIR, const gchar *user_login) {
    if (strlen(user_login) == 0) return;

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

/* ------------------------------------------------------------------ *
 *  config_sys_autologin                                               *
 *  Configure autologin for SDDM, LightDM and emptty.                *
 *  Must be the absolute LAST step. Nothing after this touches        *
 *  DM config, group membership or PAM data.                          *
 * ------------------------------------------------------------------ */
void config_sys_autologin(AppData *app, const char *TARGETDIR, const gchar *user_login, gboolean autologin) {
    if (strlen(user_login) == 0) return;

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
         * which may not have SDDM installed -> false negative -> autologin
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
