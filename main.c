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
    
    // 1. Build Interface
    build_ui(app);
    
    // 2. Hardware Detection and List Synchronization
    app->is_efi = check_efi();
    init_utils(app); // This scans disks AND fills the Grub list
    
    // 3. Main Loop
    gtk_main();
    
    g_free(app);
    return 0;
}
