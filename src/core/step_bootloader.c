/*
 * step_bootloader.c
 * Helper functions for step_install_bootloader (extracted from hard_steps.c)
 * Handles: LUKS GRUB config, GRUB install (EFI/BIOS), os-prober, dracut LUKS.
 */
#include "neko_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 *  bootloader_config_luks                                             *
 *  Configure GRUB defaults for LUKS-encrypted root.                  *
 * ------------------------------------------------------------------ */
void bootloader_config_luks(AppData *app, const char *TARGETDIR) {
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

/* ------------------------------------------------------------------ *
 *  bootloader_install_grub                                            *
 *  Install GRUB for EFI or BIOS/MBR depending on system type.        *
 * ------------------------------------------------------------------ */
int bootloader_install_grub(AppData *app, const char *TARGETDIR, const char *disk_path) {
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
    return 0;
}

/* ------------------------------------------------------------------ *
 *  bootloader_setup_os_prober                                         *
 *  Install os-prober, mount other partitions for detection.          *
 * ------------------------------------------------------------------ */
void bootloader_setup_os_prober(AppData *app, const char *TARGETDIR, const char *disk_name) {
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
    log_to_ui(app, "Scanning for other operating systems (all disks)...", 0.925);
    run_sync(app, "mkdir -p /tmp/kasha_osprobe");

    /* Scan EVERY disk on the system, not just the target disk: the other OS
     * (Windows, CachyOS, etc.) is frequently installed on a separate drive.
     * Mounting all matching partitions read-only lets os-prober see them and
     * add proper GRUB entries.
     *
     * btrfs is handled specially: we mount the default subvolume (subvol=@
     * or whatever btrfs stamped as default) read-only so os-prober can read
     * the kernel/initramfs paths inside it. */
    char probe_cmd[1024];
    snprintf(probe_cmd, sizeof(probe_cmd),
        "lsblk -rn -o NAME,FSTYPE,TYPE 2>/dev/null | "
        "awk -v self=\"%s\" '$3 == \"part\" && $1 !~ (\"^\" self \"[0-9p]\") && "
        "  $2 ~ /^(ntfs|ext4|ext3|btrfs|xfs)$/ {print $1, $2}' | "
        "while read name fstype; do "
        "  dev=\"/dev/$name\"; "
        "  mp=\"/tmp/kasha_osprobe/$name\"; "
        "  mkdir -p \"$mp\"; "
        "  if [ \"$fstype\" = \"btrfs\" ]; then "
        "    mount -o ro,subvol=@ \"$dev\" \"$mp\" 2>/dev/null || "
        "    mount -o ro \"$dev\" \"$mp\" 2>/dev/null || true; "
        "  else "
        "    mount -o ro \"$dev\" \"$mp\" 2>/dev/null || true; "
        "  fi; "
        "done", disk_name);
    system(probe_cmd);
}

/* ------------------------------------------------------------------ *
 *  bootloader_cleanup_os_prober                                       *
 *  Unmount and remove temporary os-prober mount points.              *
 * ------------------------------------------------------------------ */
void bootloader_cleanup_os_prober(AppData *app) {
    log_to_ui(app, "Cleaning up os-prober mount points...", 0.94);
    /* Kill any lingering processes before unmounting */
    run_sync(app, "fuser -km /tmp/kasha_osprobe 2>/dev/null || true");
    run_sync(app, "umount -R /tmp/kasha_osprobe 2>/dev/null || true");
    /* Verify cleanup succeeded — retry if needed */
    run_sync(app, "grep -q '/tmp/kasha_osprobe' /proc/mounts && "
             "{ sleep 1; umount -Rf /tmp/kasha_osprobe 2>/dev/null || true; } || true");
    run_sync(app, "rm -rf /tmp/kasha_osprobe 2>/dev/null || true");
}

/* ------------------------------------------------------------------ *
 *  bootloader_dracut_luks                                             *
 *  Configure dracut for LUKS and regenerate initramfs.               *
 * ------------------------------------------------------------------ */
void bootloader_dracut_luks(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "Configuring dracut for LUKS...", 0.93);
    run_sync(app, "mkdir -p %s/etc/dracut.conf.d", TARGETDIR);
    /* Do NOT set hostonly=yes here — it conflicts with the hostonly=no in
     * 01-neko.conf and produces a broken initramfs inside a chroot where
     * /proc/cmdline refers to the live USB, not the installed system. */
    run_sync(app, "echo 'add_dracutmodules+=\" crypt dm \"' > %s/etc/dracut.conf.d/10-crypt.conf", TARGETDIR);

    log_to_ui(app, "Regenerating initramfs with LUKS support...", 0.935);

    /* Ensure modules.dep is up-to-date before dracut runs */
    run_sync(app, "chroot %s sh -c 'for kver in $(ls /usr/lib/modules/); do depmod -a \"$kver\"; done'", TARGETDIR);

#ifndef UNIVERSAL_BUILD
    /* --- Void Linux: install base-system-dracut and reconfigure with xbps --- */
    void_install_dracut_luks(app, TARGETDIR);
#else
    /* --- Universal: regenerate initramfs directly with dracut --- */
    run_sync(app, "chroot %s sh -c 'for kver in $(ls /usr/lib/modules/); do "
             "dracut --no-hostonly --force /boot/initramfs-${kver}.img ${kver}; done'", TARGETDIR);
#endif
}
