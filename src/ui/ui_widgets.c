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