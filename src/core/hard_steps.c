/*
 * hard_steps.c
 * High-level orchestrator for the three main installation steps.
 *
 * The actual work is delegated to helper functions in:
 *   step_base_system.c   — rootfs copy, permissions, VFS, initramfs
 *   step_config_system.c — locale, keyboard, users, desktop, autologin
 *   step_bootloader.c    — GRUB install, LUKS config, os-prober
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

    /* 1. Copy live rootfs via tar */
    if (install_base_copy_rootfs(app, TARGETDIR) != 0)
        return -1;

    /* 2. Fix ownership/permissions of system directories */
    install_base_fix_permissions(app, TARGETDIR);

    /* 3. Remove live-only files */
    install_base_cleanup_live(app, TARGETDIR);

    /* 4. Bind-mount /dev, /proc, /sys */
    install_base_mount_vfs(app, TARGETDIR);

    /* 5. Install cryptsetup + XBPS keys if encryption is needed */
    install_base_crypto(app, TARGETDIR);

    /* 6. Rebuild initramfs (generic, with AHCI) */
    install_base_initramfs(app, TARGETDIR);

    return 0;
}

int step_configure_system(AppData *app, const char *TARGETDIR, const gchar *hostname, const gchar *locale, const gchar *root_pass, const gchar *user_login, const gchar *user_fullname, const gchar *user_pass, gboolean autologin) {

    /* 1. Remove live user, clean machine-id, copy NM connections */
    config_sys_remove_live_user(app, TARGETDIR);

    /* 2. Set hostname, locale, patch regional translations */
    config_sys_locale_and_hostname(app, TARGETDIR, hostname, locale);

    /* 3. Configure keyboard for console, X11, Wayland */
    config_sys_keyboard(app, TARGETDIR);

    /* 4. Set timezone */
    config_sys_timezone(app, TARGETDIR);

#ifdef HAS_DESKTOP_TAB
    /* 5. Install desktop environment (before user creation for /etc/skel) */
    config_sys_desktop_env(app, TARGETDIR);
#endif

    /* 6. Create root password, user account, groups, XDG dirs */
    if (config_sys_create_user(app, TARGETDIR, locale, root_pass,
                               user_login, user_fullname, user_pass) != 0)
        return -1;

    /* 7. Configure privilege manager (doas or sudo) */
    config_sys_privileges(app, TARGETDIR, user_login);

    /* 8. Generate fstab and crypttab */
    generate_fstab(app, TARGETDIR);
    generate_crypttab(app, TARGETDIR);

    /* 9. Configure autologin (MUST be last — nothing below touches DM config) */
    config_sys_autologin(app, TARGETDIR, user_login, autologin);

    return 0;
}

int step_install_bootloader(AppData *app, const char *TARGETDIR, const char *disk_name) {
    log_to_ui(app, "Installing GRUB Bootloader...", 0.9);
    char disk_path[64];
    snprintf(disk_path, sizeof(disk_path), "/dev/%s", disk_name);

    /* 1. Check for LUKS encryption */
    gboolean has_crypto = FALSE;
    GSList *chk = app->part_config_list;
    while(chk) {
      if(((PartitionConfig*)chk->data)->encrypt) has_crypto = TRUE;
      chk = chk->next;
    }

    /* 2. Configure GRUB for LUKS if needed (sets GRUB_ENABLE_CRYPTODISK,
     *    rd.luks.uuid, generates volume.key) */
    if (has_crypto) {
        bootloader_config_luks(app, TARGETDIR);
    }

    /* 3. Install GRUB (EFI or BIOS) */
    if (bootloader_install_grub(app, TARGETDIR, disk_path) != 0)
        return -1;

    /* 4. Configure dracut for LUKS and regenerate initramfs BEFORE
     *    grub-mkconfig.  This is critical: grub-mkconfig reads the
     *    initramfs to build menu entries; if the initramfs doesn't
     *    contain the crypt/dm/lvm modules + volume.key + crypttab,
     *    the generated GRUB config will reference a broken initramfs
     *    and the system won't boot. */
    if (has_crypto) {
        bootloader_dracut_luks(app, TARGETDIR);
    }

    /* 5. Set up os-prober for dual boot */
    if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
        bootloader_setup_os_prober(app, TARGETDIR, disk_name);
    }

    /* 6. Generate GRUB config — AFTER dracut has rebuilt the initramfs
     *    with LUKS support, so grub-mkconfig picks up the correct one. */
    run_sync(app, "chroot %s grub-mkconfig -o /boot/grub/grub.cfg", TARGETDIR);

    /* 7. Cleanup os-prober mounts */
    if (app->install_mode == INSTALL_MODE_DUAL_BOOT) {
        bootloader_cleanup_os_prober(app);
    }

    return 0;
}
