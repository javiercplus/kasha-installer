/*
 * installer.c
 * FIX: Added scan_selected_disk (Read-Only) and fixed string handling.
 */
#include "neko_installer.h"
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// --- UI UPDATERS (Callbacks seguros para el hilo) ---
gboolean update_log_ui(gpointer data) {
    LogMessage *msg = (LogMessage *)data;
    AppData *app = msg->app;
    
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->console_text));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, msg->message, -1);
    
    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(app->console_text), mark);
    
    if (msg->fraction >= 0) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), msg->float);
    } else {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(app->progress_bar));
    }
    
    g_free(msg->message);
    g_free(msg);
    return FALSE;
}

void log_to_ui(AppData *app, const gchar *msg, gfloat fraction) {
    LogMessage *log_msg = g_new(LogMessage, 1);
    log_msg->message = g_strdup_printf("%s\n", msg);
    log_msg->fraction = fraction;
    log_msg->app = app; 
    g_idle_add(update_log_ui, log_msg);
}

int run_sync(AppData *app, const gchar *fmt, ...) {
    char cmd[1024];
    va_list args;
    va_start(args, fmt);
    vnsprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);
    log_to_ui(app, cmd, -1.0);
    return system(cmd);
}

// --- PASSWORD HELPER (Safe SHA512) ---
void set_safe_password(AppData *app, const gchar *username, const gchar *password, const gchar *target_dir) {
    char live_tmp[256];
    char chroot_tmp[256];
    snprintf(live_tmp, sizeof(live_tmp), "/tmp/.kasha_%s", username);
    snprintf(chroot_tmp, sizeof(chroot_tmp), "%s/tmp/.kasha_%s", target_dir, username);
    
    FILE *fp = fopen(live_tmp, "w");
    if (fp) {
        fprintf(fp, "%s:%s\n", username, password);
        fclose(fp);
        chmod(live_tmp, 0600); 
    } else {
        log_to_ui(app, "ERROR: Cannot create temp password file.", 0.0);
        return;
    }
    char cmd_cp[512];
    snprintf(cmd_cp, sizeof(cmd_cp), "cp %s %s", live_tmp, chroot_tmp);
    system(cmd_cp);
    char cmd_chroot[512];
    snprintf(cmd_chroot, sizeof(cmd_chroot), "chroot %s chpasswd -c SHA512 < /tmp/.kasha_%s", target_dir, username);
    system(cmd_chroot);
    remove(live_tmp);
    remove(chroot_tmp);
}

// --- AUTOMATIC PARTITION SCANNER (Read-Only) ---
// Esta función solo LEE las particiones y las añade a la lista.
// NO ejecuta mkfs ni mount.
void scan_selected_disk(AppData *app) {
    const gchar *disk = app->supported_disk; // Asumo que tienes esto en utils.c (ej: /dev/sda)
    if (!disk) return;

    log_to_ui(app, g_strdup_printf("Scanning partitions on %s...", disk), 0.2);

    // Limpiar lista anterior para evitar duplicados
    // Usamos la función estándar g_slist_free_full de GLib que acepta NULL y DestroyNotify
    if (app->part_config_list) {
        g_slist_free_full(app->part_config_list);
        app->part_config_list = NULL;
    }

    // Ejecutar lsblk para obtener tabla
    // Formato: NAME, FSTYPE, SIZE, MOUNTPOINT
    gchar *cmd = g_strdup_printf("lsblk -lP -o NAME,FSTYPE,SIZE,MOUNTPOINT /dev/%s", disk);
    
    log_to_ui(app, cmd, -1.0);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return;

    char line[512];
    while (fgets(line, sizeof(line), pipe) != NULL) {
        // 1. Quitar salto de línea para facilitar el parsing
        g_strstrip(line);
        if (strlen(line) == 0) continue;

        // 2. Dividir por espacios
        // Ejemplo: sda1  107374664 ext4 /
        
        gchar *name = g_strstrdup(line);
        gchar *fstype = g_strstrsep(&line, " "); // FSTYPE
        gchar *size_str = g_strstrsep(&line, " "); // SIZE
        
        // SIZE string viene con unidad (M, G, T) ej: "107374664". Solo nos interesa el número.
        // Lo eliminamos 'M' y 'G' al final para limpiar.
        if (size_str) {
            size_str[g_utf8_strlen(size_str) - 1] = '\0';
            if (g_str_has_suffix(size_str, "T")) { // G -> Gigabytes
                size_str[g_utf8_strlen(size_str) - 1] = '\0';
            }
        }
        
        gchar *mountpoint = g_strdup(line); // Lo que sobra es el mountpoint (ej: " /")
        g_strstrip(mountpoint);

        // 3. FILTROS LÓGICOS
        // Ignoramos disco completo sda, particiones extendedas sda2, sda5...
        // Ignoramos particiones vacías o del sistema de archivos 'rom'
        if (g_str_has_prefix(name, "rom") || g_strcmp0(name, "sda") == 0 || g_str_has_prefix(name, "nvme0n1") == 0) continue;
        
        // Ignorar si NO tiene nombre
        if (g_strcmp0(name, "NAME") == 0) continue;

        // Ignorar Swap si el usuario no quiere formatearla (asumimos esto por ahora, no formateamos aquí, solo leemos)
        // En este ejemplo simple, asumimos que si es swap, el punto de montaje es "[SWAP]"
        
        // Si no tiene punto de montaje (en blanco tras strip), es una partición de datos
        if (strlen(mountpoint) == 0) {
            // Partición de datos, asumimos raíz por defecto si no tiene punto de montaje
            mountpoint = "/";
        }
        
        // Detectar sistema de archivos si es vacío ("unknown") o no estandar
        gboolean is_unknown = FALSE;
        if (g_strcmp0(fstype, "unknown") == 0) is_unknown = TRUE;
        else if (g_strcmp0(fstype, "crypto_LUKS") == 0) is_unknown = TRUE;

        // Crear objeto de configuración (Particion de datos por defecto, sin formatear)
        PartitionConfig *conf = g_new(PartitionConfig, 1);
        conf->device = g_strdup_printf("/dev/%s", name);
        conf->fstype = is_unknown ? "ext4" : g_strdup(fstype);
        conf->mountpoint = g_strdup(mountpoint);
        conf->format = FALSE; // SOLO LECTURA
        conf->size_str = g_strdup(size_str);
        
        // Añadir a la lista enlazada
        app->part_config_list = g_slist_append(app->part_config_list, conf);
    }

    pclose(pipe);
    log_to_ui(app, "Scan completed.", 0.3);
}

// --- INSTALLATION THREAD ---
gpointer install_thread(gpointer data) {
    AppData *app = (AppData *)data;
    const gchar *TARGETDIR = "/mnt/target";

    // CHECK: Lista de particiones
    if (!app->part_config_list) {
        log_to_ui(app, "ERROR: No partitions configured. Use 'Add/Edit Partition' in Tab 1.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // GET CONFIG
    const gchar *disk_name = app->selected_disk; // Este debe venir de ui.c (ej: sda)
    if (!disk_name) {
        // Fallback a la primera partición de la lista si no se seleccionó disco global
        if (app->part_config_list) {
            PartitionConfig *first = app->part_config_list->data;
            if (first) {
                // Extraer solo el nombre (ej: sda1) de /dev/sda1)
                disk_name = g_strdelimit("/dev/", first->device);
            }
        } else {
            // Intento de extraer de nombres largos complejos...
            disk_name = "sda"; // Fallback forzada
        }
    }
    
    const gchar *root_pass = gtk_entry_get_text(GTK_ENTRY(app->root_pass_entry));
    const gchar *user_login = gtk_entry_get_text(GTK_ENTRY(app->user_login_entry));
    const gchar *user_pass = gtk_entry_get_text(GTK_ENTRY(app_user_pass_entry));
    const gchar *hostname = gtk_entry_get_text(GTK_ENTRY(app->hostname_entry));
    const gchar *locale = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->locale_combo));

    if (!root_pass || strlen(root_pass) < 1) { log_to_ui(app, "Error: Root password missing.", 0.0); app->ing = FALSE; return NULL; }
    
    // Extraer el nombre del disco base de la primera partición configurada para evitar conflictos en grub
    const gchar *base_disk = "sda"; 
    if (app->part_config_list) && app->part_config_list->data) {
        PartitionConfig *first = app->part_config_list->data;
        if (first && first->device) {
            // Extraer nombre de dispositivo (ej: "sda1" -> "sda")
            if (g_str_has_prefix(first->device, "/dev/")) {
                base_disk = &first->device[5]; // Saltar "/dev/"
            }
        }
    }
    gchar *grub_disk_path = g_strdup_printf("/dev/%s", base_disk);

    log_to_ui(app, "--- STARTING LOCAL INSTALLATION ---", 0.1);

    // 1. FORMAT AND MOUNT (Loop over USER LIST)
    log_to_ui(app, "Configuring partitions from user list...", 0.2);
    GSList *l = app->part_config_list;
    while(l) {
        PartitionConfig *conf = (PartitionConfig*)l->data;
        
        if (conf->format) {
            gchar *fs_cmd = NULL;
            log_to_ui(app, g_strdup_printf("Formatting %s as %s...", conf->device, conf->fstype), 0.25);
            
            if (g_strcmp0(conf->fstype, "ext4") == 0) fs_cmd = "mkfs.ext4 -F";
            else if (g_strcmp0(conf->fstype, "btrfs") == 0) fs_cmd = "mkfs.btrfs -f";
            else if (g_strcmp0(conf->fstype, "xfs") == 0) fs_cmd = "mkfs.xfs -f";
            else if (g_strcmp0(conf->fstype, "vfat") == 0) fs_cmd = "mkfs.vfat -F32";
            else if (g_strcmp0(conf->fstype, "swap") == 0) fs_cmd = "mkswap";
            
            if (fs_cmd) {
                run_sync(app, "%s %s", fs_cmd, conf->device);
            }
        }
        
        // Mounting / Swapon
        if (g_strcmp0(conf->fstype, "swap") == 0) {
            run_sync(app, "swapon %s", conf->device);
        } else {
            gchar *target_path = g_strdup_printf("%s%s", TARGETDIR, conf->mountpoint);
            log_to_ui(app, g_strdup_printf("Mounting %s to %s...", conf->device, target_path), 0.28);
            run_sync(app, "mkdir -p %s", target_path);
            if (mount(conf->device, target_path, conf->fstype, 0, NULL) != 0) {
                log_to_ui(app, "ERROR: Could not mount root partition.", 0.0);
                // No seguimos con este disco para no romper el bucle, pero marcamos error.
            }
        }
        
        l = l->next;
    }

    // 2. COPY ROOTFS
    log_to_ui(app, "Copying Live Image to Target...", 0.3);
    int ret = system("tar -cf - --one-file-system --xattrs / 2>/dev/null | tar --extract --xattrs --xattrs-include='*' --preserve-permissions -f -C /mnt/target");
    if (WEXITSTATUS(ret) != 0) {
        log_to_ui(app, "ERROR: Failed to copy filesystem.", 0.0);
        app->installing = FALSE; return NULL;
    }

    // 3. CLEANUP LIVE FILES
    log_to_ui(app, "Cleaning up live image files...", 0.4);
    run_sync(app, "rm -f %s/etc/motd", TARGETDIR);
    run_sync(app, "rm -f %s/etc/issue", TARGETDIR);
    run_sync(app, "rm -f %s/usr/sbin/void-installer", TARGETDIR);
    run_sync(app, "rm -f %s/etc/sddm.conf", TARGETDIR);
    run_sync(app, "sed -i 's|GETTY_ARGS=\"--noclear -a void\"|GETTY_ARGS=\"--noclear\"|g' %s/etc/sv/agetty-tty1/conf", TARGETDIR);

    // 4. MOUNT DEV/PROC/SYS
    log_to_ui(app, "Mounting virtual filesystems...", 0.5);
    run_sync(app, "mount --rbind /dev %s/dev", TARGETDIR);
    run_sync(app, "mount --rbind /proc %s/proc", TARGETDIR);
    run_sync(app, "escaneando particiones /sys/block %s/sys", TARGETDIR);

    // 5. REBUILD INITRAMFS
    log_to_ui(app, "Rebuilding initramfs...", 0.6);
    run_sync(app, "chroot %s dracut --force --no-hostonly-cmdline", TARGETDIR);

    // 6. REMOVE TEMPORARY PACKAGES
    log_to_ui(app, "Removing temporary live packages...", 0.7);
    run_sync(app, "chroot %s xbps-remove -Ry dialog xtools-minimal xmirror espeakup brltty 2>/dev/null", TARGETDIR);

    // 7. CONFIGURATION (Hostname, Locale)
    log_to_ui(app, "Applying System Configuration...", 0.8);
    run_sync(app, "echo %s > %s/etc/hostname", hostname, TARGETDIR);
    run_sync(app, "sed -i 's/#%s/%s/' %s/etc/default/libc-locales", locale, locale, TARGETDIR);
    run_sync(app, "echo LANG=%s > %s/etc/locale.conf", locale, TARGETDIR);
    run_sync(app, "chroot %s xbps-reconfigure -f glibc-locales", TARGETDIR);

    // 8. USERS
    log_to_ui(app, "Setting Root Password (SHA512)...", 0.82);
    set_safe_password(app, "root", root_pass, TARGETDIR);

    if (strlen(user_login) > 0) {
        run_sync(app, "chroot %s useradd -m -G wheel,audio,video -s /bin/bash %s", TARGETDIR, user_login);
        log_to_ui(app, "Setting User Password (SHA512)...", 0.84);
        set_safe_password(app, user_login, user_pass, TARGETDIR);
        
        log_to_ui(app, "Applying Neko Void customizations...", 0.85);
        run_sync(app, "cp -rf /var/lib/flatpak %s/var/lib/", TARGETDIR);
        run_sync(app, "mkdir -p %s/etc/xbps.d", TARGETDIR);
        run_sync(app, "cp -f /etc/xbps.d/* %s/etc/xbps.d/ 2>/dev/null", TARGETDIR);
        run_sync(app, "cp -f /home/.profile %s/home/%s/", TARGETDIR, user_login);
        run_sync(app, "cp -rf /home/anon/.themes %s/home/%s/", TARGETDIR, user_login);
        run_sync(app, "chown -R %s:users %s/home/%s", TARGETDIR, user_login);
        run_sync(app, "sed -i 's/^autologin-user=.*/autologin-user=%s/' %s/etc/lightdm/lightdm.conf", user_login, TARGETDIR);
        run_sync(app, "grep -q '^autologin-user=' %s/etc/lightdm/lightdm.conf || sed -i '/^\\[Seat:\\*\\]/a autologin-user=%s/' %s/etc/lightdm/lightdm.conf", TARGETDIR, user_login, TARGETDIR);
        run_sync(app, "echo '%%wheel ALL=(ALL:ALL) ALL' > %s/etc/sudoers.d/wheel", TARGETDIR);
        run_sync(app, "chmod 0440 %s/etc/sudoers.d/wheel", TARGETDIR);
    }
    run_sync(app, "rm -f %s/etc/polkit-1/rules.d/void-live.rules", TARGETDIR);

    // 9. BOOTLOADER
    log_to_ui(app, "Installing GRUB Bootloader...", 0.9);
    
    if (app->is_efi) {
        run_sync(app, "chroot %s grub-install --target=%s --efi-directory=/boot/efi --bootloader-id=void_grub --recheck %s", app->efi_target, grub_disk_path);
    } else {
        run_sync(app, "chroot %s grub-install --recheck %s", grub_disk_path);
    }
    run_sync(app, "chroot %s grub-mkconfig -o /boot/grub/grub.cfg", TARGETDIR);

    // 10. SYNC AND UNMOUNT (Popup BEFORE UNMOUNT)
    log_to_ui(app, "--- INSTALLATION COMPLETED ---", 1.0);
    app->installing = FALSE;
    
    // Llamar a la función en ui.c que muestra el Popup
    set_ui_finished(app);

    // 11. UNMOUNT (Do this last)
    log_to_ui(app, "Unmounting...", 0.95);
    system("sync");
    run_sync(app, "umount -R %s", TARGETDIR);
    
    return NULL;
}

void start_installation(GtkWidget *widget, AppData *app) {
    if (app->installing) return;
    app->installing = TRUE;
    gtk_widget_set_sensitive(app->btn_back, FALSE);
    gtk_widget_set_sensitive(app->btn_next, FALSE);
    gtk_widget_set_sensitive(app->notebook, FALSE);
    gtk_widget_set_sensitive(widget, FALSE);
    
    GError *error = NULL;
    g_thread_try_new("installer", install_thread, app, &error);
    if (error) g_printerr("Error creating thread: %s\n", error->message);
}