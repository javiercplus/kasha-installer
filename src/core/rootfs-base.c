/*
 * rootfs-base.c
 * Rootfs-based base system installation.
 *
 * Used when the "local (default)" checkbox in the Desktop tab is UNCHECKED:
 * instead of mass-copying the live image we bootstrap the base system from
 * the official rootfs-custom tarball and install/configure everything on top
 * of it (base packages + chosen desktop + core services).
 *
 * The default instructions for the installed system live HERE (package lists
 * mirror live-maker/base-neko-pkgs.sh; services mirror neko-builder.sh).
 * The neko-desktops script is only used to COPY the desktop config bundle.
 *
 * This module is ONLY compiled in the normal (non-universal) build.
 */

#ifndef UNIVERSAL_BUILD

#include "neko_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* Official Neko-Void rootfs used to bootstrap the base system. */
#define ROOTFS_URL \
    "https://github.com/Neko-Void-Linux/rootfs-custom/releases/download/stable/neko-void.tar.xz"
#define ROOTFS_TARBALL "/tmp/void-rootfs.tar.xz"
#define VOID_REPO "https://repo-de.voidlinux.org/current/"

/* ─────────────────────────────────────────────
 * Core services that must ALWAYS run, regardless of the chosen desktop.
 * Mirrors SERVICES_BASE from live-maker/neko-builder.sh:
 *   dbus NetworkManager polkitd rtkit sshd chronyd zramen tlp tlp-pd
 * ───────────────────────────────────────────── */
static const char *CORE_SERVICES[] = {
    "dbus", "NetworkManager", "polkitd", "rtkit", "sshd",
    "chronyd", "zramen", "tlp", "tlp-pd", NULL
};

/* Packages providing the core services above. */
static const char *CORE_PACKAGES[] = {
    "dbus", "NetworkManager", "polkit", "rtkit", "openssh",
    "chrony", "zramen", "tlp", "tlp-pd", NULL
};

/* ─────────────────────────────────────────────
 * Base packages — mirrors the DEFAULT set from
 * live-maker/base-neko-pkgs.sh.
 * ───────────────────────────────────────────── */
static const char BASE_PACKAGES[] =
    "void-repo-nonfree void-repo-multilib void-repo-multilib-nonfree "
    "base-system linux-firmware linux-firmware-amd linux-firmware-intel "
    "linux-mainline linux-mainline-headers dkms dnsmasq kasha-installer iptables tlp tlp-pd tlp-rdw "
    "intel-ucode at-spi2-core yad git inetutils qemu-ga upower open-vm-tools "
    "spice-vdagent elogind bash-completion cryptsetup dbus dialog grub mdadm nano rtkit "
    "xdo xsetroot xinit Neko-Wizard neko-icons neko-themes neko-backgrounds xtools tmux xmirror "
    "7zip p7zip unrar zip xxd xz btop fastfetch curl wget xdg-user-dirs xdg-utils ethtool "
    "iproute2 lvm2 polkit udisks2 eudev void-docs-browse xtools-minimal openssh chrony "
    "NetworkManager network-manager-applet wpa_supplicant iw bluez "
    "pipewire wireplumber alsa-lib alsa-utils alsa-pipewire libjack-pipewire pavucontrol "
    "pulsemixer rsync volumeicon "
    "xorg libva-intel-driver intel-media-driver orca "
    "mesa mesa-dri mesa-vaapi vulkan-loader Vulkan-Tools libglvnd "
    "linux-firmware-intel linux-firmware-nvidia linux-firmware-amd "
    "gparted iruka-xbps Neko-Kernel-Manager "
    "flatpak xdg-desktop-portal xdg-desktop-portal-gtk "
    "noto-fonts-emoji noto-fonts-cjk noto-fonts-ttf font-JetBrainsMono font-awesome "
    "dejavu-fonts-ttf liberation-fonts-ttf font-misc-misc terminus-font "
    "ffmpeg gstreamer1 gst-plugins-base1 gst-plugins-good1 gst-plugins-bad1 gst-plugins-ugly1 "
    "gamemode MangoHud "
    "ntp zramen "
    "espeakup void-live-audio brltty";

/* ─────────────────────────────────────────────
 * Desktop-specific packages — mirrors live-maker/base-neko-pkgs.sh.
 * The base set above is always installed first.
 * ───────────────────────────────────────────── */
#define MATE_PACKAGES \
    "engrampa firefox mate mate-extra mate-tweak mate-polkit mate-terminal mpv pluma " \
    "caja-wallpaper caja-sendto caja-open-terminal caja-extensions atril gnome-screenshot " \
    "gnome-keyring gvfs-afc gvfs-mtp gvfs-smb lightdm lightdm-webkit2-greeter " \
    "lightdm-gtk-greeter-settings libnotify numlockx picom nwg-look"

#define XFCE_PACKAGES \
    "xfce4 xfce4-whiskermenu-plugin gnome-themes-standard xfce4-pulseaudio-plugin " \
    "xfce4-screenshooter atril gvfs-afc gvfs-mtp firefox gvfs-smb udisks2 lightdm " \
    "lightdm-webkit2-greeter lightdm-gtk-greeter-settings libnotify numlockx"

#define KDE_PACKAGES \
    "kde-plasma konsole kate firefox dolphin gvfs-afc gvfs-mtp gvfs-smb mpv sddm " \
    "plasma-framework kdeconnect kdegraphics-thumbnailers kde-baseapps " \
    "qt6-virtualkeyboard qt6-svg qt6-multimedia gum okular spectacle gwenview ark"

#define LXQT_PACKAGES \
    "kate mpv lxqt xfwm4 xfwm4-themes lightdm lightdm-webkit2-greeter " \
    "lightdm-gtk-greeter-settings gvfs-afc gvfs-mtp gvfs-smb udisks2 firefox " \
    "qt6-virtualkeyboard qt6-svg qt6-multimedia gum"

#define ICEJWM_PACKAGES \
    "ristretto xarchiver arandr jwm jwmkit-neko icewm mpv pcmanfm alacritty lxappearance " \
    "atril lightdm lightdm-gtk-greeter gvfs-afc gvfs-mtp gvfs-smb udisks2 firefox " \
    "xdg-desktop-portal xdg-desktop-portal-gtk mate-polkit xfce4-screenshooter"

#define LABWC_PACKAGES \
    "ristretto xarchiver lightdm gvfs-afc gvfs-mtp gvfs-smb wlr-randr " \
    "xwayland-satellite swaylock labwc alacritty kanshi noctalia " \
    "xdg-desktop-portal xdg-desktop-portal-wlr playerctl nwg-look gtk-update-icon-cache " \
    "wl-clipboard wlopm mpv geany grim slurp gtksourceview json-c yad waterfox " \
    "gtk-layer-shell gtkmm pcmanfm wdisplays"

#define NIRI_PACKAGES \
    "qt6-wayland-client ristretto xarchiver gvfs-afc gvfs-mtp gvfs-smb wlr-randr wdisplays " \
    "mate-polkit caja xwayland-satellite emptty niri noctalia foot xdg-desktop-portal " \
    "xdg-desktop-portal-gnome xdg-desktop-portal-wlr wl-clipboard mako wlsunset nwg-look " \
    "gtk-update-icon-cache wlopm mpv geany grim slurp gtksourceview json-c yad waterfox " \
    "gtk-layer-shell gtkmm"

/* ------------------------------------------------------------------ *
 *  rootfs_ensure_dns                                                  *
 *  Make sure name resolution works inside the chroot before any        *
 *  network operation (xbps-install).                                  *
 * ------------------------------------------------------------------ */
static int rootfs_ensure_dns(AppData *app, const char *TARGETDIR) {
    char target_resolv[512];
    snprintf(target_resolv, sizeof(target_resolv), "%s/etc/resolv.conf", TARGETDIR);

    if (run_sync(app, "grep -qE '^[[:space:]]*nameserver[[:space:]]+' %s 2>/dev/null", target_resolv) == 0)
        return 0;

    log_to_ui(app, "Configuring DNS in target (resolv.conf)...", -1.0);

    if (access("/etc/resolv.conf", F_OK) == 0 &&
        run_sync(app, "cp -f /etc/resolv.conf %s", target_resolv) == 0 &&
        run_sync(app, "grep -qE '^[[:space:]]*nameserver[[:space:]]+' %s 2>/dev/null", target_resolv) == 0)
        return 0;

    FILE *fp = fopen(target_resolv, "w");
    if (!fp) {
        log_to_ui(app, "ERROR: cannot write /etc/resolv.conf in target.", 0.0);
        return -1;
    }
    fprintf(fp, "# Written by neko-installer (DNS fallback)\n");
    fprintf(fp, "nameserver 1.1.1.1\n");
    fprintf(fp, "nameserver 9.9.9.9\n");
    fclose(fp);
    chmod(target_resolv, 0644);
    return 0;
}

/* ------------------------------------------------------------------ *
 *  enable_service / disable_service                                   *
 *  Void Linux runit services live in /etc/sv/<name> and are enabled   *
 *  by symlinking them into /var/service (active) and                  *
 *  /etc/runit/runsvdir/default (persistent across reboots).           *
 * ------------------------------------------------------------------ */
static void enable_service(AppData *app, const char *TARGETDIR, const char *svc) {
    char svc_path[600];
    snprintf(svc_path, sizeof(svc_path), "%s/etc/sv/%s", TARGETDIR, svc);

    if (access(svc_path, F_OK) != 0) {
        log_to_ui_printf(app, "WARNING: service %s not found — skipping.", svc);
        return;
    }

    /* Inside a chroot /var/service is a broken symlink to the running
     * runsvdir; replace it with a real directory before linking. */
    run_sync(app, "if [ -L %s/var/service ] && [ ! -e %s/var/service ]; then rm -f %s/var/service; fi",
             TARGETDIR, TARGETDIR, TARGETDIR);
    run_sync(app, "mkdir -p %s/var/service", TARGETDIR);
    run_sync(app, "mkdir -p %s/etc/runit/runsvdir/default", TARGETDIR);

    run_sync(app, "ln -sf /etc/sv/%s %s/var/service/%s", svc, TARGETDIR, svc);
    run_sync(app, "ln -sf /etc/sv/%s %s/etc/runit/runsvdir/default/%s", svc, TARGETDIR, svc);
    log_to_ui_printf(app, "Enabled service: %s", svc);
}

static void disable_service(AppData *app, const char *TARGETDIR, const char *svc) {
    run_sync(app, "rm -f %s/var/service/%s 2>/dev/null || true", TARGETDIR, svc);
    run_sync(app, "rm -f %s/etc/runit/runsvdir/default/%s 2>/dev/null || true", TARGETDIR, svc);
    run_sync(app, "rm -rf %s/etc/runit/runsvdir/default/%s 2>/dev/null || true", TARGETDIR, svc);
}

static void enable_core_services(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "Enabling core services...", -1.0);
    int i;
    for (i = 0; CORE_SERVICES[i] != NULL; i++)
        enable_service(app, TARGETDIR, CORE_SERVICES[i]);
}

/* ------------------------------------------------------------------ *
 *  void_install_rootfs_base                                           *
 *  Bootstrap the base system from the rootfs tarball: download,        *
 *  extract, install base packages and enable the core services.        *
 *  Returns 0 on success, -1 on fatal error.                           *
 * ------------------------------------------------------------------ */
int void_install_rootfs_base(AppData *app, const char *TARGETDIR) {
    /* 1. Download + extract the rootfs tarball. */
    log_to_ui(app, "Downloading rootfs (void-x86_64-ROOTFS)...", 0.30);
    if (run_sync(app, "wget -4 -q -O %s %s", ROOTFS_TARBALL, ROOTFS_URL) != 0) {
        log_to_ui(app, "ERROR: Failed to download the rootfs. Check the network connection.", 0.0);
        return -1;
    }

    log_to_ui(app, "Extracting rootfs into target...", 0.33);
    if (run_sync(app, "tar -xJf %s -C %s", ROOTFS_TARBALL, TARGETDIR) != 0) {
        log_to_ui(app, "ERROR: Failed to extract the rootfs.", 0.0);
        return -1;
    }
    run_sync(app, "rm -f %s", ROOTFS_TARBALL);

    /* 2. Mount virtual filesystems (needed by every chroot command). */
    log_to_ui(app, "Mounting virtual filesystems...", 0.35);
    run_sync(app, "mount --rbind /dev %s/dev", TARGETDIR);
    run_sync(app, "mount --rbind /proc %s/proc", TARGETDIR);
    run_sync(app, "mount --rbind /sys %s/sys", TARGETDIR);

    /* 3. DNS must work inside the chroot for xbps-install. */
    rootfs_ensure_dns(app, TARGETDIR);

    /* 4. Copy XBPS keys + xbps.d config so package installs can verify. */
    void_copy_xbps_keys(app, TARGETDIR);

    /* 5. The rootfs ships an outdated xbps that refuses to run new repo
     * transactions ("The 'xbps' package must be updated"). Update it first. */
    log_to_ui(app, "Updating xbps in target...", -1.0);
    if (run_sync(app, "chroot %s bash -c 'xbps-install -Sy -u xbps'", TARGETDIR) != 0) {
        log_to_ui(app, "ERROR: failed to update xbps inside the target.", 0.0);
        return -1;
    }

    /* 5b. Full system update — the rootfs tarball is a frozen snapshot with
     * outdated packages (kmod, glibc, etc.).  New dracut from the repo
     * requires LIBKMOD_33, but the rootfs kmod is too old → version mismatch
     * → dracut-install crashes → initramfs never generated.
     * xbps-install -u (per Void Handbook) brings EVERYTHING up to date. */
    log_to_ui(app, "Updating all packages (rootfs is outdated, this can take a while)...", -1.0);
    if (run_sync(app, "chroot %s bash -c 'xbps-install -Syu'", TARGETDIR) != 0) {
        log_to_ui(app, "WARNING: full system update reported errors — continuing.", -1.0);
    }

    /* 6. Force a GENERIC (non-hostonly) dracut for ALL invocations — even
     * the ones triggered by kernel post-install during xbps-install below —
     * so no initramfs captures the live system's root device or hardware
     * (chroot has the host's /proc, /sys, /dev mounted). Per Void handbook.
     * File name "01-neko" sorts BEFORE the LUKS "10-crypt.conf" so the LUKS
     * path (hostonly=yes) still wins when encryption is used. */
    run_sync(app, "mkdir -p %s/etc/dracut.conf.d", TARGETDIR);
    run_sync(app, "echo 'hostonly=no' > %s/etc/dracut.conf.d/01-neko.conf", TARGETDIR);
    run_sync(app, "echo 'add_drivers+=\" ahci \"' >> %s/etc/dracut.conf.d/01-neko.conf", TARGETDIR);

    /* 7. Install the base packages (mirrors live-maker DEFAULT). */
    log_to_ui(app, "Installing base packages...", 0.45);
    if (run_sync(app, "chroot %s bash -c 'xbps-install -Sy --repository=%s %s'",
                 TARGETDIR, VOID_REPO, BASE_PACKAGES) != 0) {
        log_to_ui(app, "ERROR: base package installation failed.", 0.0);
        return -1;
    }

    /* 8. The ROOTFS tarball is a container image: drop the container
     * metapackage so the installed system boots as a real host
     * (xbps-remove -R base-container-full, per the Void handbook).
     *
     * WARNING: -R removes orphaned dependencies, including dracut that
     * was only pulled in by base-container-full. */
    log_to_ui(app, "Removing container metapackage (base-container-full)...", -1.0);
    run_sync(app, "chroot %s xbps-remove -R base-container-full 2>/dev/null || true", TARGETDIR);

    /* 8b. Reinstall dracut — stripped by the -R above. */
    log_to_ui(app, "Reinstalling dracut (removed by base-container-full cleanup)...", -1.0);
    run_sync(app, "chroot %s bash -c 'xbps-install -Sy --repository=%s dracut'",
             TARGETDIR, VOID_REPO);

    /* 8c. MANDATORY full sync after the metapackage swap.  Removing
     * base-container-full shuffles the dependency tree; xbps-install -Syu
     * reconciles everything (kmod ↔ dracut version match, broken deps,
     * orphan cleanup) so the reconfigure in step 10 inherits a consistent
     * package set and dracut can actually build a working initramfs. */
    log_to_ui(app, "Syncing package state (xbps-install -Syu)...", -1.0);
    if (run_sync(app, "chroot %s bash -c 'xbps-install -Syu'", TARGETDIR) != 0) {
        log_to_ui(app, "WARNING: post-cleanup sync reported errors — continuing.", -1.0);
    }

    /* 9. Enable the core services. */
    enable_core_services(app, TARGETDIR);

    /* 10. Reconfigure ALL base packages (xbps-reconfigure -fa).  The -f flag
     * is essential: without it only unpacked packages are touched, and the
     * kernel (fully installed) is skipped → its INSTALL hook (dracut) never
     * fires → no initramfs.  -f forces reconfigure of every package so the
     * kernel's post-install hook regenerates the initramfs with the target's
     * /etc/dracut.conf.d settings (01-neko.conf above: generic + ahci). */
    void_reconfigure_base(app, TARGETDIR);

    return 0;
}

/* ------------------------------------------------------------------ *
 *  void_install_desktop_packages                                      *
 *  Install the packages specific to the chosen desktop.              *
 *  The base set (BASE_PACKAGES) is installed by the rootfs bootstrap. *
 * ------------------------------------------------------------------ */
void void_install_desktop_packages(AppData *app, const char *TARGETDIR, const char *desktop) {
    const char *pkgs = NULL;
    if      (desktop && strcmp(desktop, "xfce")   == 0) pkgs = XFCE_PACKAGES;
    else if (desktop && strcmp(desktop, "niri")   == 0) pkgs = NIRI_PACKAGES;
    else if (desktop && strcmp(desktop, "kde")    == 0) pkgs = KDE_PACKAGES;
    else if (desktop && strcmp(desktop, "icejwm") == 0) pkgs = ICEJWM_PACKAGES;
    else if (desktop && strcmp(desktop, "mate")   == 0) pkgs = MATE_PACKAGES;
    else if (desktop && strcmp(desktop, "labwc")  == 0) pkgs = LABWC_PACKAGES;
    else if (desktop && strcmp(desktop, "lxqt")   == 0) pkgs = LXQT_PACKAGES;
    else {
        log_to_ui_printf(app, "ERROR: unknown desktop '%s' — skipping package install.",
                         desktop ? desktop : "(null)");
        return;
    }

    log_to_ui_printf(app, "Installing packages for desktop: %s", desktop);
    if (run_sync(app, "chroot %s bash -c 'xbps-install -Sy --repository=%s %s'",
                 TARGETDIR, VOID_REPO, pkgs) != 0) {
        log_to_ui(app, "WARNING: desktop package installation reported errors — continuing.", -1.0);
    }
}

/* ------------------------------------------------------------------ *
 *  void_enable_desktop_services                                       *
 *  Enable the display manager for the chosen desktop (exactly one)    *
 *  and re-enable the core services.                                   *
 * ------------------------------------------------------------------ */
void void_enable_desktop_services(AppData *app, const char *TARGETDIR, const char *desktop) {
    const char *dm = "lightdm";
    if (desktop && strcmp(desktop, "kde")  == 0) dm = "sddm";
    if (desktop && strcmp(desktop, "niri") == 0) dm = "emptty";

    void_ensure_core_packages(app, TARGETDIR);

    log_to_ui_printf(app, "Enabling display manager: %s", dm);
    disable_service(app, TARGETDIR, "lightdm");
    disable_service(app, TARGETDIR, "sddm");
    disable_service(app, TARGETDIR, "emptty");
    enable_service(app, TARGETDIR, dm);

    enable_core_services(app, TARGETDIR);
}

/* ------------------------------------------------------------------ *
 *  void_ensure_core_packages                                         *
 *  Re-install the core service packages as a safety net after any      *
 *  desktop install (mirrors the old script's ensure_core_services).   *
 * ------------------------------------------------------------------ */
void void_ensure_core_packages(AppData *app, const char *TARGETDIR) {
    char pkgs[512] = "";
    int i;
    for (i = 0; CORE_PACKAGES[i] != NULL; i++) {
        if (i > 0) strcat(pkgs, " ");
        strcat(pkgs, CORE_PACKAGES[i]);
    }
    log_to_ui(app, "Ensuring core packages...", -1.0);
    if (run_sync(app, "chroot %s bash -c 'xbps-install -Sy --repository=%s %s'",
                 TARGETDIR, VOID_REPO, pkgs) != 0) {
        log_to_ui(app, "WARNING: core package re-install reported errors — continuing.", -1.0);
    }
}

#endif /* !UNIVERSAL_BUILD */
