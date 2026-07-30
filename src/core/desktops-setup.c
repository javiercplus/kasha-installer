/*
 * Logic for install desktops
 */

#ifdef HAS_DESKTOP_TAB
#include "neko_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/*
 * Ensure name resolution works inside the chroot before any network
 * operation (xbps-install / git). The live system normally has a working
 * /etc/resolv.conf that is NOT copied by the tar-based rootfs copy, so the
 * target would otherwise have no DNS and every download silently fails.
 *
 * Returns 0 on success, -1 if a usable resolv.conf could not be provided.
 */
static int ensure_chroot_dns(AppData *app, const char *TARGETDIR) {
    char target_resolv[512];
    snprintf(target_resolv, sizeof(target_resolv), "%s/etc/resolv.conf", TARGETDIR);

    /* If the target already resolves something, leave it alone. */
    if (run_sync(app, "grep -qE '^[[:space:]]*nameserver[[:space:]]+' %s", target_resolv) == 0) {
        return 0;
    }

    log_to_ui(app, "Configuring DNS in target (resolv.conf)...", -1.0);

    /* Try the host's live /etc/resolv.conf first. */
    if (access("/etc/resolv.conf", F_OK) == 0) {
        if (run_sync(app, "cp -f /etc/resolv.conf %s", target_resolv) == 0 &&
            run_sync(app, "grep -qE '^[[:space:]]*nameserver[[:space:]]+' %s", target_resolv) == 0) {
            return 0;
        }
    }

    /* Fallback: a couple of public resolvers so the install can still proceed. */
    FILE *fp = fopen(target_resolv, "w");
    if (!fp) {
        log_to_ui(app, "ERROR: cannot write /etc/resolv.conf in target — desktop install will likely fail.", 0.0);
        return -1;
    }
    fprintf(fp, "# Written by neko-installer (DNS fallback)\n");
    fprintf(fp, "nameserver 1.1.1.1\n");
    fprintf(fp, "nameserver 9.9.9.9\n");
    fclose(fp);
    chmod(target_resolv, 0644);
    return 0;
}

/*
 * Install a desktop environment into the target via the neko-desktops
 * repository. DNS is ensured first so xbps-install / git work inside the
 * chroot, and the return code of the setup script is checked.
 */
void install_desktop(AppData *app, const char *TARGETDIR, const char *desktop_type) {
    log_to_ui_printf(app, "Installing desktop environment: %s", desktop_type);

    /* Network/DNS must work inside the chroot for xbps-install and git. */
    ensure_chroot_dns(app, TARGETDIR);

    /* xbps + git are required by the setup script. */
    if (run_sync(app, "chroot %s bash -c 'xbps-install -Sy git bash xbps'", TARGETDIR) != 0) {
        log_to_ui(app, "WARNING: xbps-install of git/bash/xbps returned an error — continuing anyway.", -1.0);
    }

    /* Clone the neko-desktops repository into the target. */
    log_to_ui(app, "Downloading neko-desktops from git...", 0.93);
    run_sync(app, "rm -rf %s/tmp/desktops", TARGETDIR);
    if (run_sync(app,
            "chroot %s bash -c 'git clone --depth 1 -b master "
            "https://codeberg.org/Neko-Void/neko-desktops.git /tmp/desktops'",
            TARGETDIR) != 0) {
        log_to_ui(app, "ERROR: failed to clone neko-desktops. Desktop will NOT be installed.", 0.0);
        return;
    }

    /* Make sure the script is executable, then run it with the chosen desktop. */
    run_sync(app, "chmod +x %s/tmp/desktops/desktop-set.sh", TARGETDIR);

    int rc = run_sync(app,
        "chroot %s bash -c 'cd /tmp/desktops && ./desktop-set.sh %s'",
        TARGETDIR, desktop_type);

    if (rc != 0) {
        log_to_ui_printf(app,
            "ERROR: desktop-set.sh %s exited with code %d. The desktop may be partially installed.",
            desktop_type, rc);
        return;
    }

    log_to_ui_printf(app, "Desktop '%s' installed successfully.", desktop_type);
}

void void_xfce(AppData *app, const char *TARGETDIR) {
    install_desktop(app, TARGETDIR, "xfce");
}

void void_niri(AppData *app, const char *TARGETDIR) {
    install_desktop(app, TARGETDIR, "niri");
}

void void_kde(AppData *app, const char *TARGETDIR) {
    install_desktop(app, TARGETDIR, "kde");
}

void void_icejwm(AppData *app, const char *TARGETDIR) {
    install_desktop(app, TARGETDIR, "icejwm");
}

void void_mate(AppData *app, const char *TARGETDIR) {
    install_desktop(app, TARGETDIR, "mate");
}

void void_labwc(AppData *app, const char *TARGETDIR) {
    install_desktop(app, TARGETDIR, "labwc");
}

void void_lxqt(AppData *app, const char *TARGETDIR) {
    install_desktop(app, TARGETDIR, "lxqt");
}
#endif /* HAS_DESKTOP_TAB */
