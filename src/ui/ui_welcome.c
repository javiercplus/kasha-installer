/*
 * ui_welcome.c
 * Welcome page construction
 */
#include "neko_installer.h"
#include "ui_internal.h"
#include "logo.h"

GtkWidget* create_welcome_page(AppData *app) {
    GtkWidget *align_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(align_box, 10);
    gtk_widget_set_margin_bottom(align_box, 10);
    gtk_widget_set_margin_start(align_box, 10);
    gtk_widget_set_margin_end(align_box, 10);

    GtkWidget *vbox_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(vbox_main, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(vbox_main, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(vbox_main, TRUE);

    // Picture - smaller for low res
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    if (gdk_pixbuf_loader_write(loader, logo_png, logo_png_len, NULL)) {
        gdk_pixbuf_loader_close(loader, NULL);
        GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);

        if (pixbuf) {
            int width = gdk_pixbuf_get_width(pixbuf);
            int height = gdk_pixbuf_get_height(pixbuf);
            int target_width = 180;
            int target_height = target_width * height / width;
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, target_width, target_height, GDK_INTERP_BILINEAR);
            GtkWidget *image = gtk_image_new_from_pixbuf(scaled);
            gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
            gtk_widget_set_margin_bottom(image, 10);
            gtk_box_pack_start(GTK_BOX(vbox_main), image, FALSE, FALSE, 0);

            g_object_unref(scaled);
        }
    }
    g_object_unref(loader);

    app->lbl_welcome_title = gtk_label_new(NULL);
    gtk_label_set_justify(GTK_LABEL(app->lbl_welcome_title), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(app->lbl_welcome_title), 0.5);
    gtk_box_pack_start(GTK_BOX(vbox_main), app->lbl_welcome_title, FALSE, FALSE, 0);

    app->lbl_welcome_body = gtk_label_new(NULL);
    gtk_label_set_justify(GTK_LABEL(app->lbl_welcome_body), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(app->lbl_welcome_body), 0.5);
    gtk_label_set_yalign(GTK_LABEL(app->lbl_welcome_body), 0.5);
    gtk_label_set_line_wrap(GTK_LABEL(app->lbl_welcome_body), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(app->lbl_welcome_body), 50);
    gtk_label_set_selectable(GTK_LABEL(app->lbl_welcome_body), TRUE);
    gtk_widget_set_margin_start(app->lbl_welcome_body, 10);
    gtk_widget_set_margin_end(app->lbl_welcome_body, 10);
    gtk_box_pack_start(GTK_BOX(vbox_main), app->lbl_welcome_body, FALSE, FALSE, 5);

    // Lang Dropdown
    GtkWidget *hbox_lang = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(hbox_lang, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(hbox_lang, 5);

    app->lbl_lang_selection = gtk_label_new(get_loc("language", app->current_lang));
    gtk_box_pack_start(GTK_BOX(hbox_lang), app->lbl_lang_selection, FALSE, FALSE, 0);

    GtkWidget *combo_lang = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_lang), "English");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_lang), "Español");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_lang), "日本語");
    
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_lang), 0);
    g_signal_connect(combo_lang, "changed", G_CALLBACK(on_lang_changed), app);
    gtk_box_pack_start(GTK_BOX(hbox_lang), combo_lang, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox_main), hbox_lang, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(align_box), vbox_main, TRUE, TRUE, 0);
    gtk_widget_show_all(align_box);

    return align_box;
}