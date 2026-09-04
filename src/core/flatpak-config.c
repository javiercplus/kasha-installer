/*
 * flatpak-config.c
 * Initialize the Flatpak repository structure inside the target system.
 *
 * Only runs if /var/lib/flatpak was copied from the live image.
 * Sets up a bare-user-only OSTree repo so Flatpak works out of the box
 * on the installed system without needing network access at first boot.
 */
#include "neko_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ *
 *  configure_flatpak_repo                                             *
 *  Create the OSTree repo skeleton for system-wide Flatpak.          *
 *  Only executes if TARGETDIR/var/lib/flatpak exists.                *
 * ------------------------------------------------------------------ */
void configure_flatpak_repo(AppData *app, const char *TARGETDIR) {
    /* Guard: only proceed if the live Flatpak data was actually copied */
    char flatpak_path[512];
    snprintf(flatpak_path, sizeof(flatpak_path), "%s/var/lib/flatpak", TARGETDIR);

    struct stat st;
    if (stat(flatpak_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        log_to_ui(app, "[Flatpak] /var/lib/flatpak not found in target — skipping repo init.", -1);
        return;
    }

    log_to_ui(app, "Initializing Flatpak system repo in target...", -1);

    /* Create the required OSTree repo subdirectories */
    run_sync(app, "mkdir -p %s/var/lib/flatpak/repo/objects", TARGETDIR);
    run_sync(app, "mkdir -p %s/var/lib/flatpak/repo/tmp",     TARGETDIR);


    /* Configure Flatpak remotes inside the target system */
    log_to_ui(app, "[Flatpak] Configuring remotes (flathub + trinity)...", -1);

    /* Flathub: remove stale entry (if any) and re-add from official URL */
    run_sync(app, "chroot %s flatpak remote-delete flathub 2>/dev/null || true", TARGETDIR);
    run_sync(app, "chroot %s flatpak remote-add --if-not-exists flathub "
                  "https://dl.flathub.org/repo/flathub.flatpakrepo", TARGETDIR);

    /* Trinity Launcher: remove stale entry (if any) and add from GitHub */
    run_sync(app, "chroot %s flatpak remote-delete trinity 2>/dev/null || true", TARGETDIR);
    run_sync(app, "chroot %s flatpak remote-add trinity "
                  "https://github.com/Trinity-LA/Trinity-Launcher/releases/download/flatpak/com.trench.trinity.launcher.flatpakrepo", TARGETDIR);

    /* Write the OSTree repo config (bare-user-only, matches Flatpak's default) */
    run_sync(app,
             "tee %s/var/lib/flatpak/repo/config > /dev/null << 'EOF'\n"
             "[core]\n"
             "repo_version=1\n"
             "mode=bare-user-only\n"
             "min-free-space-size=500MB\n"
             "EOF",
             TARGETDIR);
    run_sync(app, "chroot %s flatpak repair 2>/dev/null || true", TARGETDIR);

    log_to_ui(app, "[Flatpak] System repo initialized (bare-user-only).", -1);

    log_to_ui(app, "[Flatpak] Remotes configured.", -1);
}
