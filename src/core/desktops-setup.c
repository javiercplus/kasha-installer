/*
 * Logic for install desktops
 */

#ifndef UNIVERSAL_BUILD   /* Guard in case it is included manually */
#include "neko_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void void_xfce(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "INSTALLING DESKTOP", 0.935);
    run_sync(app, "chroot %s bash -c 'xbps-install -S git bash'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'git clone https://codeberg.org/Neko-Void/neko-desktops.git /tmp/desktops'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'cd /tmp/desktops && chmod +x desktop-set.sh && ./desktop-set.sh xfce'", TARGETDIR);
}
void void_niri(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "INSTALLING DESKTOP", 0.935);
    run_sync(app, "chroot %s bash -c 'xbps-install -S git bash'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'git clone https://codeberg.org/Neko-Void/neko-desktops.git /tmp/desktops'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'cd /tmp/desktops && chmod +x desktop-set.sh && ./desktop-set.sh niri'", TARGETDIR);
}
void void_kde(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "INSTALLING DESKTOP", 0.935);
    run_sync(app, "chroot %s bash -c 'xbps-install -S git bash'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'git clone https://codeberg.org/Neko-Void/neko-desktops.git /tmp/desktops'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'cd /tmp/desktops && chmod +x desktop-set.sh && ./desktop-set.sh kde'", TARGETDIR);
}
void void_icejwm(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "INSTALLING DESKTOP", 0.935);
    run_sync(app, "chroot %s bash -c 'xbps-install -S git bash'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'git clone https://codeberg.org/Neko-Void/neko-desktops.git /tmp/desktops'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'cd /tmp/desktops && chmod +x desktop-set.sh && ./desktop-set.sh icejwm'", TARGETDIR);
}
void void_mate(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "INSTALLING DESKTOP", 0.935);
    run_sync(app, "chroot %s bash -c 'xbps-install -S git bash'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'git clone https://codeberg.org/Neko-Void/neko-desktops.git /tmp/desktops'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'cd /tmp/desktops && chmod +x desktop-set.sh && ./desktop-set.sh mate'", TARGETDIR);
}
void void_labwc(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "INSTALLING DESKTOP", 0.935);
    run_sync(app, "chroot %s bash -c 'xbps-install -S git bash'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'git clone https://codeberg.org/Neko-Void/neko-desktops.git /tmp/desktops'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'cd /tmp/desktops && chmod +x desktop-set.sh && ./desktop-set.sh labwc'", TARGETDIR);
}
void void_lxqt(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "INSTALLING DESKTOP", 0.935);
    run_sync(app, "chroot %s bash -c 'xbps-install -S git bash'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'git clone https://codeberg.org/Neko-Void/neko-desktops.git /tmp/desktops'", TARGETDIR);
    run_sync(app, "chroot %s bash -c 'cd /tmp/desktops && chmod +x desktop-set.sh && ./desktop-set.sh lxqt'", TARGETDIR);
}

#endif /* !UNIVERSAL_BUILD */
