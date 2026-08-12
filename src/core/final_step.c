/*
 * FINAL STEPS THE FINAL BOSS BABY
 */
#include "neko_installer.h"
#include <stdlib.h>
#include <string.h>

int step_finalize(AppData *app, const char *TARGETDIR) {
    log_to_ui(app, "Syncing...", 0.95);
    system("sync");

    // Disable swap partitions before unmounting
    GSList *sw = app->part_config_list;
    while(sw) {
        PartitionConfig *pc = (PartitionConfig*)sw->data;
        if (strcmp(pc->fstype, "swap") == 0) {
            run_sync(app, "swapoff %s 2>/dev/null", pc->device);
        }
        sw = sw->next;
    }

    run_sync(app, "umount -R %s", TARGETDIR);

    // CLOSE LUKS DEVICES
    gboolean has_crypto = FALSE;
    GSList *chk = app->part_config_list;
    while(chk) {
        if(((PartitionConfig*)chk->data)->encrypt) has_crypto = TRUE;
        chk = chk->next;
    }

    if (has_crypto) {
        log_to_ui(app, "Closing encrypted devices...", 0.98);
        run_sync(app, "cryptsetup close cryptroot 2>/dev/null || true");
    }
    return 0;
}
