/*
 * main.c
 * Main entry point.
 */
#include "neko_installer.h"

AppData *global_app_data;

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    AppData *app = g_new0(AppData, 1);
    global_app_data = app;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug-ui") == 0) {
            app->debug_mode = TRUE;
            g_print("[DEBUG] UI-only mode enabled - commands will be logged only\n");
        }
    }
    
    // 1. Build Interface
    build_ui(app);
    
    if (app->debug_mode) {
        g_print("[DEBUG] Running in UI-only mode. Installation commands will not be executed.\n");
        gtk_main();
        g_free(app);
        return 0;
    }
    
    // 2. Hardware Detection and List Synchronization
    app->is_efi = check_efi();
    init_utils(app); // This scans disks AND fills the Grub list
    
    // 3. Main Loop
    gtk_main();
    
    g_free(app);
    return 0;
}
