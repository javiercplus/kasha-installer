/*
 * lang_es.c
 * Spanish translations
 */
#include "lang.h"
#include <string.h>
#include <stddef.h>

static const LangInfo lang_es = {
    .code = "es",
    .name = "Español",
    .language = "Idioma:",
    
    // Welcome
    .welcome_title = "Bienvenido a Neko-Void",
    .welcome_body = "Un respin no oficial de Void Linux, ligero, rápido y listo para gaming.",
    .welcome_title_markup = "<span size='xx-large' weight='bold'>Bienvenido a Neko-Void</span>",
    .welcome_body_markup = "<span size='large'>Un respin no oficial de Void Linux, ligero, rápido y listo para gaming.</span>",
    
    // Install Type
    .install_type_title = "Seleccione el tipo de instalación:",
    .clean_install = "Instalación Limpia (borrar todo el disco)",
    .install_alongside = "Instalar junto a otro SO (redimensionar)",
    .clean_install_desc = "Se borrará todo el contenido del disco y se crearán las particiones automáticamente.",
    .alongside_desc = "Se redimensionará la partición existente para crear espacio libre.",
    
    // Partitions
    .disk = "Disco:",
    .disk_title = "Seleccionar Disco:",
    .disk_note = "Nota: Puede modificar las particiones con GParted y luego configurarlas abajo.",
    .existing_parts = "Particiones Existentes",
    .install_parts = "Particiones de Instalación",
    .manual_partitioning = "Configurar particiones manualmente",
    .btn_add = "Añadir",
    .btn_edit = "Editar",
    .btn_delete = "Borrar",
    .btn_reset = "Resetear",
    .btn_gparted = "Particionar (GParted)",
    .part_mount = "Puntos de Montaje:",
    
    // Bootloader
    .boot_detect = "Detectando firmware...",
    .grub_install = "Instalar GRUB en:",
    
    // System
    .hostname = "Nombre equipo:",
    .country = "País:",
    .locale = "Idioma (Locale):",
    .region = "Región:",
    .city = "Ciudad:",
    .timezone = "Zona horaria:",
    
    // Users
    .root_pass = "Contraseña Root:",
    .user_account = "Cuenta de Usuario:",
    .fullname = "Nombre Completo:",
    .username = "Usuario:",
    .password = "Contraseña:",
    .confirm = "Confirmar:",
    .autologin = "Activar Auto-Login",
    
    // Install
    .install_btn = "Iniciar Instalación",
    .reboot_btn = "Reiniciar Sistema",
    .back = "Atrás",
    .next = "Siguiente",
    
    // Privilege Manager
    .tab_privilege = "Seguridad",
    .priv_title = "Escalada de Privilegios",
    .priv_desc = "Elija entre sudo tradicional o el ligero doas:",
    .priv_sudo_label = "sudo",
    .priv_sudo_desc = "Herramienta estándar. Ampliamente compatible, código complejo.",
    .priv_doas_label = "doas",
    .priv_doas_desc = "Alternativa minimalista. Más simple, menos vectores de ataque. Recomendado.",
    
    // Tabs
    .tab_welcome = "Bienvenido",
    .tab_install_type = "Tipo Inst.",
    .tab_partitions = "Particiones",
    .tab_bootloader = "Arranque",
    .tab_system = "Sistema",
    .tab_desktop = "Escritorio",
    .tab_users = "Usuarios",
    .tab_install = "Instalar",
    
    // Desktop
    .desktop_title = "Entorno de Escritorio",
    .desktop_desc = "Seleccione cómo instalar el escritorio:",
    .d_local_label = "Local (predeterminado)",
    .d_default_desc = "Copiar la imagen en vivo tal cual. No se instala otro escritorio.",
    .d_xfce_desc = "Ligero y estable. Ideal para hardware antiguo.",
    .d_niri_desc = "Compositor Wayland moderno con mosaico desplazable.",
    .d_kde_desc = "Completo y personalizable. Herramientas potentes.",
    .d_icewm_desc = "Gestor de ventanas muy ligero, rápido y austero.",
    .d_i3_desc = "Gestor de ventanas en mosaico, mínimo y para usuarios avanzados.",
    .d_jwm_desc = "Gestor de ventanas ultraligero con estética retro.",
    .d_mate_desc = "Escritorio tradicional, continuación de GNOME 2.",
    .d_labwc_desc = "Compositor Wayland apilado, inspirado en Openbox.",
    .d_lxqt_desc = "Entorno de escritorio ligero basado en Qt.",
    
    // Partition dialog
    .select_partition = "Seleccionar partición:",
    .filesystem = "Sistema de archivos:",
    .mount_point = "Punto de montaje:",
    .format_partition = "¿Formatear partición?",
    .encrypt_luks = "¿Cifrar (LUKS)?",
    .general = "General",
    .encryption = "Cifrado",
    .dialog_add = "Añadir partición",
    .dialog_edit = "Editar partición",
    .dialog_update = "Actualizar",
    .save = "_Añadir",
    .cancel = "_Cancelar",
    
    // Messages
    .error_root_password = "Error: Falta contraseña de root.",
    .error_no_partitions = "Error: No hay particiones configuradas.",
    .error_no_root = "Error: Partición raíz (/) no configurada.",
    .error_usr_not_supported = "Error: /usr como partición separada no está soportado!",
    .error_efi_partition = "Error: Se requiere partición EFI (/boot/efi) para EFI!",
    
    // Window title
    .window_title = "Kasha Installer - Neko Void",
    
    // Success
    .success_title = "NEKO-VOID está LISTO!!!",
    .success_body = "Instalación completada exitosamente.\nEl sistema está listo para reiniciar.",
    .success_reboot = "REINICIAR AHORA",

    // Validation messages
    .val_select_disk    = "Por favor seleccione un disco antes de continuar.",
    .val_no_root        = "No hay partición raíz (/) configurada. Añada al menos una partición raíz.",
    .val_grub_disk      = "Por favor seleccione un disco para instalar GRUB.",
    .val_hostname       = "Por favor ingrese un nombre de equipo.",
    .val_username       = "Por favor ingrese un nombre de usuario.",
    .val_password       = "Por favor ingrese una contraseña de usuario.",
    .val_password_match = "Las contraseñas de usuario no coinciden.",
    .val_root_pass      = "Por favor ingrese una contraseña de root.",
    .val_incomplete     = "Configuración Incompleta",
};

const LangInfo* lang_es_module(void) {
    return &lang_es;
}