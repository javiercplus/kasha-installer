/*
 * ui_widgets.c
 * Reusable input widgets
 */
#include "neko_installer.h"
#include "ui_internal.h"

GtkWidget* create_form_row(const gchar *label_text, GtkWidget **entry_ptr, GtkWidget **label_ptr) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_bottom(hbox, 5);

    GtkWidget *label = gtk_label_new(label_text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);

    if(label_ptr) *label_ptr = label;

    *entry_ptr = gtk_entry_new();
    gtk_widget_set_hexpand(*entry_ptr, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), *entry_ptr, TRUE, TRUE, 0);
    return hbox;
}

GtkWidget* create_vertical_input(const gchar *label_text, GtkWidget **entry_ptr, GtkWidget **label_ptr) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_margin_bottom(vbox, 5);

    GtkWidget *label = gtk_label_new(label_text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    if(label_ptr) *label_ptr = label;

    *entry_ptr = gtk_entry_new();

    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), *entry_ptr, FALSE, FALSE, 0);
    return vbox;
}

static GdkPixbuf* create_eye_fallback_pixbuf(gboolean hidden) {
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
    cairo_t *cr = cairo_create(surface);

    cairo_set_source_rgba(cr, 0.45, 0.45, 0.45, 0.95);
    cairo_set_line_width(cr, 1.5);

    /* Eye outline */
    cairo_move_to(cr, 1.5, 8.0);
    cairo_curve_to(cr, 5.0, 3.0, 11.0, 3.0, 14.5, 8.0);
    cairo_curve_to(cr, 11.0, 13.0, 5.0, 13.0, 1.5, 8.0);
    cairo_stroke(cr);

    /* Pupil */
    cairo_arc(cr, 8.0, 8.0, 2.2, 0, 2 * G_PI);
    cairo_fill(cr);

    /* Slash when password is shown (conceal action) */
    if (!hidden) {
        cairo_move_to(cr, 2.5, 13.5);
        cairo_line_to(cr, 13.5, 2.5);
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, 16, 16);
    cairo_surface_destroy(surface);
    return pixbuf;
}

static void update_password_entry_icon(GtkEntry *entry) {
    gboolean visible = gtk_entry_get_visibility(entry);
    const char *icon_name = visible ? "view-conceal-symbolic" : "view-reveal-symbolic";
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    if (theme && gtk_icon_theme_has_icon(theme, icon_name)) {
        gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_SECONDARY, icon_name);
    } else {
        GdkPixbuf *pb = create_eye_fallback_pixbuf(!visible);
        gtk_entry_set_icon_from_pixbuf(entry, GTK_ENTRY_ICON_SECONDARY, pb);
        if (pb) g_object_unref(pb);
    }
}

static void on_password_icon_press(GtkEntry *entry, GtkEntryIconPosition icon_pos,
                                   GdkEvent *event, gpointer user_data) {
    (void)event;
    (void)user_data;
    if (icon_pos == GTK_ENTRY_ICON_SECONDARY) {
        gboolean visible = gtk_entry_get_visibility(entry);
        gtk_entry_set_visibility(entry, !visible);
        update_password_entry_icon(entry);
    }
}

GtkWidget* create_password_entry_box(GtkWidget **entry_ptr, const gchar *placeholder) {
    *entry_ptr = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(*entry_ptr), FALSE);
    if (placeholder) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(*entry_ptr), placeholder);
    }
    gtk_widget_set_hexpand(*entry_ptr, TRUE);

    update_password_entry_icon(GTK_ENTRY(*entry_ptr));
    gtk_entry_set_icon_activatable(GTK_ENTRY(*entry_ptr), GTK_ENTRY_ICON_SECONDARY, TRUE);
    gtk_entry_set_icon_tooltip_text(GTK_ENTRY(*entry_ptr), GTK_ENTRY_ICON_SECONDARY, "Show/Hide password");
    g_signal_connect(*entry_ptr, "icon-press", G_CALLBACK(on_password_icon_press), NULL);

    return *entry_ptr;
}

GtkWidget* create_vertical_password_input(const gchar *label_text, GtkWidget **entry_ptr, GtkWidget **label_ptr) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_bottom(vbox, 6);

    GtkWidget *label = gtk_label_new(label_text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    if (label_ptr) *label_ptr = label;

    GtkWidget *pass_entry = create_password_entry_box(entry_ptr, NULL);

    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), pass_entry, FALSE, FALSE, 0);
    return vbox;
}