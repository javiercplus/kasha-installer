/*
 * first step check filesystemssss
 */
#include "neko_installer.h"
#include <string.h>

int check_filesystems(AppData *app) {
    gboolean root_found = FALSE;
    gboolean usr_found = FALSE;
    gboolean efi_partition_found = FALSE;

    GSList *l = app->part_config_list;
    while (l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;

        if (strcmp(conf->mountpoint, "/") == 0) {
            root_found = TRUE;
        } else if (strcmp(conf->mountpoint, "/usr") == 0) {
            usr_found = TRUE;
        } else if (strcmp(conf->mountpoint, "/boot/efi") == 0 &&
            (strcmp(conf->fstype, "vfat") == 0 || strcmp(conf->fstype, "fat32") == 0)) {
            efi_partition_found = TRUE;
            }
            l = l->next;
    }

    if (!root_found) {
        log_to_ui(app, "ERROR: Root (/) partition not configured!", 0.0);
        return -1;
    }

    if (usr_found) {
        log_to_ui(app, "ERROR: /usr as separate partition is not supported!", 0.0);
        return -1;
    }

    // EFI requires EFI partition, but skip for now - bootloader step will handle
    return 0;
}
