/*
 * lang_en.c
 * English translations
 */
#include "lang.h"
#include <string.h>
#include <stddef.h>

static const LangInfo lang_en = {
    .code = "en",
    .name = "English",
    .language = "Language:",
    
    // Welcome
    .welcome_title = "Welcome to Neko-Void",
    .welcome_body = "An unofficial Void Linux respin, lightweight, fast, and gaming-ready experience.",
    .welcome_title_markup = "<span size='xx-large' weight='bold'>Welcome to Neko-Void</span>",
    .welcome_body_markup = "<span size='large'>An unofficial Void Linux respin, lightweight, fast, and gaming-ready experience.</span>",
    
    // Install Type
    .install_type_title = "Select installation type:",
    .clean_install = "Clean Install (erase entire disk)",
    .install_alongside = "Install alongside another OS (resize)",
    .clean_install_desc = "All data on the selected disk will be erased and partitions will be created automatically.",
    .alongside_desc = "The existing partition will be resized to create free space for the new system.",
    
    // Partitions
    .disk = "Disk:",
    .disk_title = "Select Disk:",
    .disk_note = "Note: You can modify the partitions with GParted and then configure them below.",
    .existing_parts = "Existing Partitions",
    .install_parts = "Installation Partitions",
    .manual_partitioning = "Configure partitions manually",
    .btn_add = "Add",
    .btn_edit = "Edit",
    .btn_delete = "Delete",
    .btn_reset = "Reset",
    .btn_gparted = "Partition (GParted)",
    .part_mount = "Mount Points:",
    
    // Bootloader
    .boot_detect = "Detecting firmware...",
    .grub_install = "Install GRUB to:",
    
    // System
    .hostname = "Hostname:",
    .country = "Country:",
    .locale = "Locale:",
    .region = "Region:",
    .city = "City:",
    .timezone = "Timezone:",
    
    // Users
    .root_pass = "Root Password:",
    .user_account = "User Account:",
    .fullname = "Full Name:",
    .username = "Username:",
    .password = "Password:",
    .confirm = "Confirm:",
    .autologin = "Enable Auto-Login",
    
    // Install
    .install_btn = "Start Installation",
    .reboot_btn = "Reboot System",
    .back = "Back",
    .next = "Next",
    
    // Privilege Manager
    .tab_privilege = "Security",
    .priv_title = "Privilege Escalation",
    .priv_desc = "Choose between traditional sudo or the lightweight doas:",
    .priv_sudo_label = "sudo",
    .priv_sudo_desc = "Standard tool. Widely compatible, complex codebase.",
    .priv_doas_label = "doas",
    .priv_doas_desc = "Minimal alternative. Simpler, fewer attack vectors. Recommended.",
    
    // Tabs
    .tab_welcome = "Welcome",
    .tab_install_type = "Installation Type",
    .tab_partitions = "Partitions",
    .tab_bootloader = "Bootloader",
    .tab_system = "System",
    .tab_desktop = "Desktop",
    .tab_users = "Users",
    .tab_install = "Install",
    
    // Desktop
    .desktop_title = "Desktop Environment",
    .desktop_desc = "Select how to install the desktop:",
    .d_local_label = "Local (default)",
    .d_default_desc = "Copy the live image as-is. No other desktop is installed.",
    .d_xfce_desc = "Lightweight and stable. Ideal for older hardware.",
    .d_niri_desc = "Modern scrollable-tiling Wayland compositor.",
    .d_kde_desc = "Full-featured and customizable. Powerful tools.",
    .d_icejwm_desc = "Very light, fast and low-resource window manager.",
    .d_mate_desc = "Traditional desktop, GNOME 2 continuation.",
    .d_labwc_desc = "Stacking Wayland compositor, Openbox-inspired.",
    .d_lxqt_desc = "Lightweight Qt-based desktop environment.",
    
    // Partition dialog
    .select_partition = "Select Partition:",
    .filesystem = "Filesystem:",
    .mount_point = "Mount Point:",
    .format_partition = "Format Partition?",
    .encrypt_luks = "Encrypt (LUKS)?",
    .general = "General",
    .encryption = "Encryption",
    .dialog_add = "Add Partition",
    .dialog_edit = "Edit Partition",
    .dialog_update = "Update",
    .save = "_Add",
    .cancel = "_Cancel",
    
    // Messages
    .error_root_password = "Error: Root password missing.",
    .error_no_partitions = "Error: No partitions configured.",
    .error_no_root = "Error: Root (/) partition not configured.",
    .error_usr_not_supported = "Error: /usr as separate partition is not supported!",
    .error_efi_partition = "Error: EFI System Partition (/boot/efi) required for EFI!",
    
    // Window title
    .window_title = "Kasha Installer - Neko Void",
    
    // Success
    .success_title = "NEKO-VOID is READY!!!",
    .success_body = "Installation completed successfully.\nThe system is ready to restart.",
    .success_reboot = "REBOOT NOW",

    // Validation messages
    .val_select_disk    = "Please select a disk before continuing.",
    .val_no_root        = "No root (/) partition configured. Add at least one root partition.",
    .val_grub_disk      = "Please select a disk for GRUB installation.",
    .val_hostname       = "Please enter a hostname.",
    .val_username       = "Please enter a username.",
    .val_password       = "Please enter a user password.",
    .val_password_match = "User passwords do not match.",
    .val_root_pass      = "Please enter a root password.",
    .val_incomplete     = "Incomplete Configuration",
};

const LangInfo* lang_en_module(void) {
    return &lang_en;
}