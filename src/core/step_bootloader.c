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

    /* Separate plain /boot: GRUB reads kernel + initramfs straight from it,
     * so it never has to unlock anything and no keyfile must be created
     * (it would live unencrypted on /boot and auto-unlock root). dracut
     * asks for the passphrase instead (rd.luks.uuid + crypttab "none"). */
    gboolean unencrypted_boot = has_separate_unencrypted_boot(app);
    if (unencrypted_boot) {
        log_to_ui(app, "Separate unencrypted /boot detected: passphrase will be asked by the initramfs.", 0.911);
    }

    GSList *f = app->part_config_list;
    while(f) {
        PartitionConfig *c = (PartitionConfig*)f->data;
        if (c->encrypt && strcmp(c->mountpoint, "/") == 0 && c->luks_uuid) {
            log_to_ui_printf(app, "Setting GRUB LUKS UUID: %s", c->luks_uuid);

            run_sync(app, "sed -i '/GRUB_ENABLE_CRYPTODISK/d' %s/etc/default/grub", TARGETDIR);
            if (!unencrypted_boot) {
                // /boot lives inside the encrypted root: GRUB must unlock it
                run_sync(app, "echo 'GRUB_ENABLE_CRYPTODISK=y' >> %s/etc/default/grub", TARGETDIR);
            }

            // Set rd.luks.uuid for dracut to find and unlock the LUKS device.
            // Per the Void handbook: rd.luks.uuid=<UUID> tells the initramfs
            // which device to unlock at boot.
            run_sync(app, "sed -i 's|GRUB_CMDLINE_LINUX_DEFAULT=\"|GRUB_CMDLINE_LINUX_DEFAULT=\"rd.luks.uuid=%s |' %s/etc/default/grub", c->luks_uuid, TARGETDIR);

            if (unencrypted_boot) {
                // Make sure no stale keyfile is left on the plain /boot
                run_sync(app, "rm -f %s/boot/volume.key", TARGETDIR);
                f = f->next;
                continue;
            }

            // Generate a random keyfile to avoid entering the passphrase twice
            // (once for GRUB, once for the initramfs).  Per the Void handbook:
            //   dd bs=1 count=64 if=/dev/urandom of=/boot/volume.key
            //   cryptsetup luksAddKey /dev/sdXn /boot/volume.key
            log_to_ui(app, "Generating LUKS volume key for auto-unlock...", 0.912);
            run_sync(app, "dd bs=1 count=64 if=/dev/urandom of=%s/boot/volume.key 2>/dev/null", TARGETDIR);

            // Add the key to the LUKS volume using a tempfile with the passphrase
            if (c->luks_pass && c->original_device) {
                char keyfile[64];
                snprintf(keyfile, sizeof(keyfile), "/tmp/luks_addkey_XXXXXX");
                int kfd = mkstemp(keyfile);
                if (kfd >= 0) {
                    write(kfd, c->luks_pass, strlen(c->luks_pass));
                    close(kfd);
                    run_sync(app, "cryptsetup luksAddKey --key-file=%s %s %s/boot/volume.key",
                             keyfile, c->original_device, TARGETDIR);
                    unlink(keyfile);
                }
            }

            // Protect the keyfile and /boot directory
            run_sync(app, "chmod 000 %s/boot/volume.key", TARGETDIR);
            run_sync(app, "chmod -R g-rwx,o-rwx %s/boot", TARGETDIR);
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
        /* Install GRUB into a named EFI directory (EFI/NekoVoid) so a proper
         * "NekoVoid" NVRAM boot entry can be registered below. */
        if (run_sync(app, "chroot %s grub-install --target=%s --efi-directory=/boot/efi --bootloader-id=NekoVoid --recheck", TARGETDIR, app->efi_target) != 0) {
            log_to_ui(app, "ERROR: grub-install (EFI) failed! Check that the EFI partition is mounted at /boot/efi.", 0.0);
            return -1;
        }

        /* Also install the removable fallback (EFI/BOOT/BOOTX64.EFI) so the
         * machine still boots on firmware with a full or buggy NVRAM. */
        run_sync(app, "chroot %s grub-install --target=%s --efi-directory=/boot/efi --bootloader-id=BOOT --recheck --removable", TARGETDIR, app->efi_target);

        /* After grub-install, ensure the UEFI boot entry exists. grub-install
         * usually registers it, but that is unreliable inside a chroot, so we
         * create it explicitly with efibootmgr. Existing duplicate entries
         * with the same label are removed first. */
        log_to_ui(app, "Creating UEFI boot entry with efibootmgr...", 0.915);

        /* The GRUB EFI filename depends on the firmware architecture. */
        const char *grub_efi_file =
            (app->efi_target && strcmp(app->efi_target, "i386-efi") == 0)
                ? "grubia32.efi" : "grubx64.efi";

        char efiboot_cmd[1024];
        snprintf(efiboot_cmd, sizeof(efiboot_cmd),
            "chroot %s sh -c '"
            "EFI_DEV=$(findmnt -n -o SOURCE /boot/efi 2>/dev/null || echo \"\"); "
            "if [ -z \"$EFI_DEV\" ]; then "
            "  echo \"Error: EFI partition not mounted\"; exit 1; "
            "fi; "
            "EFI_PART=$(echo \"$EFI_DEV\" | grep -oP 'nvme\\d+n\\d+p\\K\\d+$|sd\\w\\K\\d+$'); "
            "EFI_DISK=$(echo \"$EFI_DEV\" | grep -oP '^/dev/(nvme\\d+n\\d+|sd[a-z])'); "
            "for n in $(efibootmgr 2>/dev/null | sed -n \"s/^Boot\\([0-9A-Fa-f]\\{4\\}\\)[^A-Za-z]*NekoVoid.*/\\1/p\"); do "
            "  efibootmgr -b \"$n\" -B >/dev/null 2>&1; "
            "done; "
            "efibootmgr --create --disk \"$EFI_DISK\" --part \"$EFI_PART\" "
            "--label \"NekoVoid\" --loader \"\\\\EFI\\\\NekoVoid\\\\%s\"'",
            TARGETDIR, grub_efi_file);

        /* Non-fatal: the removable fallback still boots if this fails. */
        if (run_sync(app, "%s", efiboot_cmd) != 0)
            log_to_ui(app, "Warning: could not create the UEFI boot entry; the fallback bootloader will be used instead.", 0.0);
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
 *  bootloader_ensure_os_prober                                        *
 *  Ensure os-prober is enabled (GRUB_DISABLE_OS_PROBER=false)        *
 *  with a strict check so the line is mandatory and never duplicated.*
 * ------------------------------------------------------------------ */
void bootloader_ensure_os_prober(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "Checking GRUB OS-prober configuration...", 0.92);

#ifndef UNIVERSAL_BUILD
    /* Void Linux: make sure os-prober and ntfs-3g packages are installed */
    void_install_osprober(app, TARGETDIR);
#else
    log_to_ui(app, "[Universal] os-prober/ntfs-3g must be pre-installed in the base image.", 0.92);
#endif

    /* Check if GRUB_DISABLE_OS_PROBER=false is already uniquely configured.
     * If missing, commented out, set to true, or duplicated, remove all existing
     * occurrences and append exactly one GRUB_DISABLE_OS_PROBER=false line. */
    char check_cmd[1024];
    snprintf(check_cmd, sizeof(check_cmd),
        "sh -c '"
        "GRUB_FILE=\"%s/etc/default/grub\"; "
        "mkdir -p \"%s/etc/default\"; "
        "touch \"$GRUB_FILE\"; "
        "TOT=$(grep -c \"GRUB_DISABLE_OS_PROBER\" \"$GRUB_FILE\" 2>/dev/null) || TOT=0; "
        "COR=$(grep -c \"^[[:space:]]*GRUB_DISABLE_OS_PROBER=false[[:space:]]*$\" \"$GRUB_FILE\" 2>/dev/null) || COR=0; "
        "if [ \"$TOT\" -ne 1 ] || [ \"$COR\" -ne 1 ]; then "
        "  sed -i -E \"/^[#[:space:]]*GRUB_DISABLE_OS_PROBER/d\" \"$GRUB_FILE\"; "
        "  [ -s \"$GRUB_FILE\" ] && [ -n \"$(tail -c 1 \"$GRUB_FILE\")\" ] && echo \"\" >> \"$GRUB_FILE\"; "
        "  echo \"GRUB_DISABLE_OS_PROBER=false\" >> \"$GRUB_FILE\"; "
        "fi'",
        TARGETDIR, TARGETDIR);
    run_sync(app, "%s", check_cmd);
}

/* ------------------------------------------------------------------ *
 *  bootloader_setup_os_prober                                         *
 *  Mount other partitions so os-prober can detect them.               *
 * ------------------------------------------------------------------ */
void bootloader_setup_os_prober(AppData *app, const char *TARGETDIR, const char *disk_name) {
    (void)TARGETDIR;

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

    /* Per the Void handbook, dracut needs:
     *   add_dracutmodules+=" crypt dm lvm "   — to handle LUKS + LVM
     *   install_items+=" /boot/volume.key /etc/crypttab "
     *                                         — so the initramfs can auto-unlock
     *                                           the LUKS volume without asking
     *                                           the passphrase a second time.
     */
    run_sync(app, "echo 'add_dracutmodules+=\" crypt dm lvm \"' > %s/etc/dracut.conf.d/10-crypt.conf", TARGETDIR);
    if (has_separate_unencrypted_boot(app)) {
        /* Plain /boot: never embed a keyfile — the initramfs sits unencrypted
         * on /boot, so the key would unlock root for anyone. Ship only
         * crypttab (key "none") and let dracut prompt for the passphrase. */
        run_sync(app, "echo 'install_items+=\" /etc/crypttab \"' >> %s/etc/dracut.conf.d/10-crypt.conf", TARGETDIR);
    } else {
        run_sync(app, "echo 'install_items+=\" /boot/volume.key /etc/crypttab \"' >> %s/etc/dracut.conf.d/10-crypt.conf", TARGETDIR);
    }

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
