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
    
    // Tabs
    .tab_welcome = "Welcome",
    .tab_install_type = "Installation Type",
    .tab_partitions = "Partitions",
    .tab_bootloader = "Bootloader",
    .tab_system = "System",
    .tab_users = "Users",
    .tab_install = "Install",
    
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
};

const LangInfo* lang_en_module(void) {
    return &lang_en;
}