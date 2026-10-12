/*
 * utils.c
 */
#include "neko_installer.h"
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

gboolean check_efi() {
    if (access("/sys/firmware/efi/efivars", F_OK) == 0) {
        return TRUE;
    }
    return FALSE;
}

void read_efi_bits(AppData *app) {
    if (!app->is_efi) {
        app->efi_target = NULL;
        log_to_ui(app, "BIOS/Legacy System Detected.", -1.0);
        return;
    }

    char fw_size_str[16];
    FILE *f = fopen("/sys/firmware/efi/fw_platform_size", "r");

    if (f) {
        if (fgets(fw_size_str, sizeof(fw_size_str), f) != NULL) {
            fw_size_str[strcspn(fw_size_str, "\n")] = 0;

            if (strcmp(fw_size_str, "32") == 0) {
                app->efi_target = "i386-efi";
                log_to_ui(app, "EFI System Detected (32-bit).", -1.0);
            } else {
                app->efi_target = "x86_64-efi";
                log_to_ui(app, "EFI System Detected (64-bit).", -1.0);
            }
        }
        fclose(f);
    } else {
        app->efi_target = "x86_64-efi";
        log_to_ui(app, "EFI System Detected (Assuming 64-bit).", -1.0);
    }
}

void core_detect_hardware(AppData *app) {
    app->is_efi = check_efi();
    read_efi_bits(app);
}
