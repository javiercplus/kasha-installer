/*
 * installer_steps_void.c
 * EXCLUSIVE Void Linux steps (xbps, native dracut, etc.)
 * This file is ONLY compiled in the normal build (without -DUNIVERSAL_BUILD).
 *
 * In the universal build (cmake -DUNIVERSAL_BUILD=ON or make universal)
 * this module is NOT included and stubs in installer_steps.c are used instead.
 */

#ifndef UNIVERSAL_BUILD   /* Guard in case it is included manually */

#include "neko_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ *
 *  void_install_crypto_packages                                       *
 *  Install cryptsetup inside the chroot via xbps-install.            *
 * ------------------------------------------------------------------ */
void void_install_crypto_packages(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "[Void] Installing cryptsetup via xbps...", 0.55);
    run_sync(app, "chroot %s xbps-install -y cryptsetup", TARGETDIR);
}

/* ------------------------------------------------------------------ *
 *  void_copy_xbps_keys                                               *
 *  Copy XBPS repository keys and xbps.d to the target.         *
 * ------------------------------------------------------------------ */
void void_copy_xbps_keys(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "[Void] Copying XBPS keys and xbps.d...", 0.56);
    run_sync(app, "mkdir -p %s/var/db/xbps/keys", TARGETDIR);
    run_sync(app, "cp /var/db/xbps/keys/*.plist %s/var/db/xbps/keys/", TARGETDIR);
    run_sync(app, "wget -O %s/var/db/xbps/keys/3a:23:f2:2d:5e:d1:ab:f5:3f:01:6f:a6:50:9f:15:64.plist https://codeberg.org/javiercplus/repo-neko-up/releases/download/key/xbpspass.plist", TARGETDIR);
    run_sync(app, "cp -a /usr/share/xbps.d %s/usr/share/ 2>/dev/null", TARGETDIR);
}

/* ------------------------------------------------------------------ *
 *  void_reconfigure_base                                              *
 *  Reconfigure base packages with xbps-reconfigure.               *
 * ------------------------------------------------------------------ */
void void_reconfigure_base(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "[Void] Reconfiguring base packages (xbps-reconfigure)...", 0.63);
    run_sync(app, "xbps-reconfigure -r %s -f base-files 2>/dev/null", TARGETDIR);
    run_sync(app, "chroot %s xbps-reconfigure -a", TARGETDIR);
    run_sync(app, "chroot %s xbps-install -S", TARGETDIR);
    run_sync(app, "chroot %s xbps-install -yu xbps", TARGETDIR);
    run_sync(app, "chroot %s xbps-install -Sy Neko-Wizard kpm", TARGETDIR);
}

/* ------------------------------------------------------------------ *
 *  void_remove_live_packages                                          *
 *  Remove temporary live packages with xbps-remove.             *
 * ------------------------------------------------------------------ */
void void_remove_live_packages(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "[Void] Removing temporary live packages (xbps-remove)...", 0.70);
    run_sync(app, "chroot %s xbps-remove -Ry dialog xtools-minimal xmirror espeakup brltty 2>/dev/null", TARGETDIR);
}

/* ------------------------------------------------------------------ *
 *  void_reconfigure_locales                                           *
 *  Enable locale in libc-locales and reconfigure via xbps.        *
 * ------------------------------------------------------------------ */
void void_reconfigure_locales(AppData *app, const char *TARGETDIR, const char *locale) {
    log_to_ui(app, "[Void] Enabling locale in libc-locales...", 0.76);
    run_sync(app, "sed -i 's|^#%s |%s |' %s/etc/default/libc-locales 2>/dev/null || true",
             locale, locale, TARGETDIR);
    run_sync(app, "chroot %s xbps-reconfigure -f glibc-locales", TARGETDIR);
}

/* ------------------------------------------------------------------ *
 *  void_copy_xbpsd_config                                             *
 *  Copy /etc/xbps.d from live to target (mirrors, etc.).             *
 * ------------------------------------------------------------------ */
void void_copy_xbpsd_config(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "[Void] Copying xbps.d configuration...", 0.85);
    run_sync(app, "mkdir -p %s/etc/xbps.d", TARGETDIR);
    run_sync(app, "cp -f /etc/xbps.d/* %s/etc/xbps.d/ 2>/dev/null || true", TARGETDIR);
}

/* ------------------------------------------------------------------ *
 *  void_install_grub_efi_pkg                                          *
 *  Install grub-{arch}-efi package via xbps-install.              *
 * ------------------------------------------------------------------ */
void void_install_grub_efi_pkg(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "[Void] Downloading GRUB EFI support via xbps...", 0.91);
    if (strcmp(app->efi_target, "x86_64-efi") == 0) {
        run_sync(app, "chroot %s xbps-install -y grub-x86_64-efi", TARGETDIR);
    } else {
        run_sync(app, "chroot %s xbps-install -y grub-i386-efi", TARGETDIR);
    }
}

/* ------------------------------------------------------------------ *
 *  void_install_osprober                                              *
 *  Install os-prober and ntfs-3g for dual boot detection.          *
 * ------------------------------------------------------------------ */
void void_install_osprober(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "[Void] Installing os-prober and ntfs-3g via xbps...", 0.92);
    run_sync(app, "chroot %s xbps-install -y os-prober ntfs-3g 2>/dev/null || true", TARGETDIR);
}

/* ------------------------------------------------------------------ *
 *  void_install_dracut_luks                                           *
 *  Install base-system-dracut and regenerate initramfs for LUKS.     *
 * ------------------------------------------------------------------ */
void void_install_dracut_luks(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "[Void] Installing base-system-dracut via xbps...", 0.935);
    run_sync(app, "chroot %s xbps-install -y base-system-dracut 2>/dev/null || true", TARGETDIR);
    run_sync(app, "chroot %s xbps-reconfigure -fa 2>/dev/null || chroot %s dracut --force 2>/dev/null || true",
             TARGETDIR, TARGETDIR);
}

#endif /* !UNIVERSAL_BUILD */
