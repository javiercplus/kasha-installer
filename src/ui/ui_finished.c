/*
 * ui_finished.c
 * Installation finished state
 */
#include "neko_installer.h"
#include "ui_internal.h"

static void set_ui_finished_safe(gpointer data) {
    AppData *app = (AppData *)data;

    gtk_widget_set_sensitive(app->btn_back, FALSE);
    gtk_widget_set_sensitive(app->btn_next, FALSE);

    /* Disable every notebook page EXCEPT the install page (last page)
     * so the user can still scroll through the log after installation. */
    {
        gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
        for (gint i = 0; i < n - 1; i++) {
            GtkWidget *pg = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
            gtk_widget_set_sensitive(pg, FALSE);
        }
    }

    gtk_button_set_label(GTK_BUTTON(app->btn_install), get_loc("reboot_btn", app->current_lang));
    g_signal_handlers_disconnect_by_func(app->btn_install, G_CALLBACK(start_installation), app);
    g_signal_connect(app->btn_install, "clicked", G_CALLBACK(on_reboot_clicked), app);
    gtk_widget_set_sensitive(app->btn_install, TRUE);

    GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE,
        get_loc("success_title", app->current_lang));

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(success_dialog),
        get_loc("success_body", app->current_lang));

    GtkWidget *btn_reboot_popup = gtk_dialog_add_button(GTK_DIALOG(success_dialog), get_loc("success_reboot", app->current_lang), GTK_RESPONSE_ACCEPT);
    GtkStyleContext *context = gtk_widget_get_style_context(btn_reboot_popup);
    gtk_style_context_add_class(context, "suggested-action");
    /* The title-bar X / Escape only closes the dialog (no reboot). */
    g_signal_connect(success_dialog, "response", G_CALLBACK(on_popup_reboot), NULL);
    gtk_widget_show_all(success_dialog);
    // return FALSE; // void function signature mismatch if we return FALSE here? No, g_idle_add expects FALSE
}

// wait wrapper for g_idle_add
static gboolean set_ui_finished_wrapper(gpointer data) {
    set_ui_finished_safe(data);
    return FALSE;
}

void set_ui_finished(AppData *app) { g_idle_add(set_ui_finished_wrapper, app); }