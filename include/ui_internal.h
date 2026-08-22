#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "neko_installer.h"
#include "country_data.h"

/* ui_css.c */
void load_custom_css(void);

/* ui_i18n.c */
const char* get_loc(const char *key, int lang);
void update_ui_language(AppData *app);
void on_lang_changed(GtkComboBox *widget, AppData *app);

/* ui_welcome.c */
GtkWidget* create_welcome_page(AppData *app);

/* ui_widgets.c */
GtkWidget* create_form_row(const gchar *label_text, GtkWidget **entry_ptr, GtkWidget **label_ptr);
GtkWidget* create_vertical_input(const gchar *label_text, GtkWidget **entry_ptr, GtkWidget **label_ptr);

/* ui_desktop.c */
#ifdef HAS_DESKTOP_TAB
GtkWidget* create_desktop_page(AppData *app);
#endif

/* ui_finished.c */
void set_ui_finished(AppData *app);

/* keyboard layouts (si están en country_data.h o similar, verifica) */
const KbdLayout* get_keyboard_layouts(int *count);

#endif
