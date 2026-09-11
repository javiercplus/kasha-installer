/*
 * step_base_system.c
 * Helper functions for step_install_base_system (extracted from hard_steps.c)
 * Handles: rootfs copy, permission fixing, VFS mounting, and initramfs rebuild.
 */
#include "neko_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ *
 *  install_base_copy_rootfs                                           *
 *  Copy the live rootfs to the target via tar.                       *
 * ------------------------------------------------------------------ */
int install_base_copy_rootfs(AppData *app, const char *TARGETDIR) {
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
    return 0;
}

/* ------------------------------------------------------------------ *
 *  install_base_fix_permissions                                       *
 *  Fix ownership of system directories after tar extraction.         *
 *  The live rootfs may have been built with uid/gid 1000 owning      *
 *  critical system directories (e.g. /etc, /usr, /usr/bin,           *
 *  /etc/default). When the tar is extracted with                     *
 *  --preserve-permissions those wrong owners are carried to the      *
 *  target, causing tools like xbps-install, grub-install and dracut  *
 *  to emit: WARN: uid is 0 but '/etc/default' is owned by 1000      *
 *  Fix this immediately after the tar, before any chroot operation.  *
 * ------------------------------------------------------------------ */
void install_base_fix_permissions(AppData *app, const char *TARGETDIR) {
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
}

/* ------------------------------------------------------------------ *
 *  install_base_cleanup_live                                          *
 *  Remove live-only files from the target.                           *
 * ------------------------------------------------------------------ */
void install_base_cleanup_live(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "Cleaning up live image files...", 0.4);
    run_sync(app, "rm -f %s/etc/motd", TARGETDIR);
    run_sync(app, "rm -f %s/etc/issue", TARGETDIR);
    run_sync(app, "rm -f %s/usr/sbin/void-installer", TARGETDIR);
    run_sync(app, "rm -f %s/etc/sddm.conf", TARGETDIR);
    run_sync(app, "rmdir %s/mnt/target 2>/dev/null", TARGETDIR);
}

/* ------------------------------------------------------------------ *
 *  install_base_mount_vfs                                             *
 *  Bind-mount /dev, /proc and /sys into the target.                  *
 * ------------------------------------------------------------------ */
void install_base_mount_vfs(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "Mounting virtual filesystems...", 0.5);
    run_sync(app, "mount --rbind /dev %s/dev", TARGETDIR);
    run_sync(app, "mount --rbind /proc %s/proc", TARGETDIR);
    run_sync(app, "mount --rbind /sys %s/sys", TARGETDIR);
}

/* ------------------------------------------------------------------ *
 *  install_base_crypto                                                *
 *  Install cryptsetup and copy XBPS keys if encryption is needed.    *
 * ------------------------------------------------------------------ */
void install_base_crypto(AppData *app, const char *TARGETDIR) {
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
        log_to_ui(app, "[Universal] cryptsetup required \u2013 ensure it is pre-installed in the base image.", 0.55);
    }
#endif
}

/* ------------------------------------------------------------------ *
 *  install_base_initramfs                                             *
 *  Rebuild the initramfs (generic, with AHCI driver for SATA).       *
 *  Mirrors extra/installer.sh: chroot dracut --no-hostonly            *
 *  --add-drivers "ahci" --force.                                     *
 *  First drop 01-neko.conf so the SAME generic settings also apply   *
 *  to any dracut run later triggered by xbps-reconfigure -fa         *
 *  (kernel INSTALL hook).                                            *
 *  Without it, that second pass builds a HOSTONLY initramfs from the *
 *  chroot's host /proc/cmdline (the live USB root device) and        *
 *  overwrites the good generic one -> the installed system can't     *
 *  find its root ("no carga initramfs"). Same approach as the rootfs *
 *  path (rootfs-base.c).                                             *
 * ------------------------------------------------------------------ */
void install_base_initramfs(AppData *app, const char *TARGETDIR) {
    run_sync(app, "mkdir -p %s/etc/dracut.conf.d", TARGETDIR);
    run_sync(app, "echo 'hostonly=no' > %s/etc/dracut.conf.d/01-neko.conf", TARGETDIR);
    run_sync(app, "echo 'add_drivers+=\" ahci \"' >> %s/etc/dracut.conf.d/01-neko.conf", TARGETDIR);

    /* If any partition uses LUKS, include the crypt and dm modules from the
     * very first initramfs build so the kernel can unlock the root device. */
    gboolean has_crypto = FALSE;
    GSList *chk = app->part_config_list;
    while(chk) {
        if(((PartitionConfig*)chk->data)->encrypt) has_crypto = TRUE;
        chk = chk->next;
    }
    if (has_crypto) {
        run_sync(app, "echo 'add_dracutmodules+=\" crypt dm \"' >> %s/etc/dracut.conf.d/01-neko.conf", TARGETDIR);
    }

    log_to_ui(app, "Rebuilding initramfs (generic, this can take a few minutes)...", 0.6);

    /* Run depmod first so dracut can find kernel modules (modules.dep). */
    run_sync(app, "chroot %s sh -c 'for kver in $(ls /usr/lib/modules/); do depmod -a \"$kver\"; done'", TARGETDIR);

    /* Build initramfs for each installed kernel, specifying output path and
     * kernel version explicitly to avoid the /boot/efi/Default/ path issue. */
    run_sync(app, "chroot %s sh -c 'for kver in $(ls /usr/lib/modules/); do "
             "dracut --no-hostonly --add-drivers \"ahci\" --force "
             "/boot/initramfs-${kver}.img ${kver}; done'", TARGETDIR);

#ifndef UNIVERSAL_BUILD
    /* --- Void Linux: reconfigure base packages with xbps-reconfigure --- */
    void_reconfigure_base(app, TARGETDIR);
#else
    log_to_ui(app, "[Universal] Rebuilding done \u2014 initramfs is generic (ahci).", 0.63);
#endif
}
