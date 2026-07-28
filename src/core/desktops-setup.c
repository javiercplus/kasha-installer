/*
 * Logic for install desktops
 */

#ifndef UNIVERSAL_BUILD   /* Guard in case it is included manually */
#include "neko_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void install_desktop(AppData *app, const char *TARGETDIR, const char *desktop_type) {
    log_to_ui(app, "INSTALLING DESKTOP", 0.935);
    run_sync(app, "chroot %s bash -c 'xbps-install -S git bash'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'git clone https://codeberg.org/Neko-Void/neko-desktops.git /tmp/desktops'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'cd /tmp/desktops && chmod +x desktop-set.sh && ./desktop-set.sh %s'", TARGETDIR, desktop_type);
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
