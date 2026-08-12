/*
 * ui_desktop.c
 * Desktop selection page construction
 */
#include "neko_installer.h"
#include "ui_internal.h"

#ifdef HAS_DESKTOP_TAB
/* Build one selectable desktop row: a check button carrying a name + a
 * description label. The returned widget is the row container; the actual
 * GtkCheckButton is stored in *chk_out and the description label in *desc_out. */
static GtkWidget* build_desktop_row(const char *name, const char *desc,
                                    GtkWidget **chk_out, GtkWidget **desc_out) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_bottom(row, 6);

    GtkWidget *check = gtk_check_button_new_with_label(name);
    GtkStyleContext *ctx = gtk_widget_get_style_context(check);
    gtk_style_context_add_class(ctx, "desktop-row");
    gtk_widget_set_valign(check, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(row), check, FALSE, FALSE, 0);

    GtkWidget *dsc = gtk_label_new(desc);
    gtk_label_set_xalign(GTK_LABEL(dsc), 1.0);
    gtk_label_set_line_wrap(GTK_LABEL(dsc), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(dsc), 55);
    GtkStyleContext *dctx = gtk_widget_get_style_context(dsc);
    gtk_style_context_add_class(dctx, "desktop-desc");
    gtk_widget_set_hexpand(dsc, TRUE);
    gtk_widget_set_valign(dsc, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_end(dsc, 4);
    gtk_box_pack_start(GTK_BOX(row), dsc, TRUE, TRUE, 0);

    *chk_out = check;
    *desc_out = dsc;
    return row;
}

static void connect_desktop_toggle(GtkWidget *check, AppData *app) {
    g_signal_connect(check, "toggled", G_CALLBACK(on_desktop_selected), app);
}

GtkWidget* create_desktop_page(AppData *app) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(page), 20);

    app->lbl_desktop_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(app->lbl_desktop_title),
        "<b><span size='large'>Desktop Environment</span></b>");
    gtk_widget_set_halign(app->lbl_desktop_title, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(app->lbl_desktop_title, 2);
    gtk_box_pack_start(GTK_BOX(page), app->lbl_desktop_title, FALSE, FALSE, 0);

    app->lbl_desktop_desc = gtk_label_new("Select a desktop environment to install (optional):");
    gtk_widget_set_halign(app->lbl_desktop_desc, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(app->lbl_desktop_desc, 10);
    gtk_box_pack_start(GTK_BOX(page), app->lbl_desktop_desc, FALSE, FALSE, 0);

    /* Scrollable list of desktop options */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(list_box, 6);
    gtk_widget_set_margin_end(list_box, 6);
    gtk_container_add(GTK_CONTAINER(scrolled), list_box);

    GtkWidget *row;

    /* "local (default)": copy the live image as-is. When checked it locks
     * the desktop selection (other desktops are only reachable with the
     * rootfs-based install, i.e. with this box unchecked). */
    row = build_desktop_row(get_loc("d_local_label", app->current_lang),
                            get_loc("d_default_desc", app->current_lang),
                            &app->chk_desktop_local, &app->lbl_d_default_desc);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->chk_desktop_local), TRUE);
    gtk_box_pack_start(GTK_BOX(list_box), row, FALSE, FALSE, 0);
    g_signal_connect(app->chk_desktop_local, "toggled", G_CALLBACK(on_local_default_toggled), app);

    /* Separator: desktops installable over the rootfs */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 6);
    gtk_widget_set_margin_bottom(sep, 8);
    gtk_box_pack_start(GTK_BOX(list_box), sep, FALSE, FALSE, 0);

    app->desktop_sel_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    row = build_desktop_row("Xfce", get_loc("d_xfce_desc", app->current_lang),
                            &app->chk_desktop_xfce, &app->lbl_d_xfce_desc);
    gtk_box_pack_start(GTK_BOX(app->desktop_sel_box), row, FALSE, FALSE, 0);
    connect_desktop_toggle(app->chk_desktop_xfce, app);
    row = build_desktop_row("Niri", get_loc("d_niri_desc", app->current_lang),
                            &app->chk_desktop_niri, &app->lbl_d_niri_desc);
    gtk_box_pack_start(GTK_BOX(app->desktop_sel_box), row, FALSE, FALSE, 0);
    connect_desktop_toggle(app->chk_desktop_niri, app);
    row = build_desktop_row("KDE Plasma", get_loc("d_kde_desc", app->current_lang),
                            &app->chk_desktop_kde, &app->lbl_d_kde_desc);
    gtk_box_pack_start(GTK_BOX(app->desktop_sel_box), row, FALSE, FALSE, 0);
    connect_desktop_toggle(app->chk_desktop_kde, app);
    row = build_desktop_row("IceJWM", get_loc("d_icejwm_desc", app->current_lang),
                            &app->chk_desktop_icejwm, &app->lbl_d_icejwm_desc);
    gtk_box_pack_start(GTK_BOX(app->desktop_sel_box), row, FALSE, FALSE, 0);
    connect_desktop_toggle(app->chk_desktop_icejwm, app);
    row = build_desktop_row("MATE", get_loc("d_mate_desc", app->current_lang),
                            &app->chk_desktop_mate, &app->lbl_d_mate_desc);
    gtk_box_pack_start(GTK_BOX(app->desktop_sel_box), row, FALSE, FALSE, 0);
    connect_desktop_toggle(app->chk_desktop_mate, app);
    row = build_desktop_row("Labwc", get_loc("d_labwc_desc", app->current_lang),
                            &app->chk_desktop_labwc, &app->lbl_d_labwc_desc);
    gtk_box_pack_start(GTK_BOX(app->desktop_sel_box), row, FALSE, FALSE, 0);
    connect_desktop_toggle(app->chk_desktop_labwc, app);
    row = build_desktop_row("LXQt", get_loc("d_lxqt_desc", app->current_lang),
                            &app->chk_desktop_lxqt, &app->lbl_d_lxqt_desc);
    gtk_box_pack_start(GTK_BOX(app->desktop_sel_box), row, FALSE, FALSE, 0);
    connect_desktop_toggle(app->chk_desktop_lxqt, app);

    gtk_box_pack_start(GTK_BOX(list_box), app->desktop_sel_box, FALSE, FALSE, 0);

    /* "local (default)" is the default: desktop rows stay disabled. */
    gtk_widget_set_sensitive(app->desktop_sel_box, FALSE);

    /* Center the list within the available width */
    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *spacer_l = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer_l, TRUE);
    GtkWidget *spacer_r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer_r, TRUE);
    gtk_box_pack_start(GTK_BOX(center), spacer_l, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(center), scrolled, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(center), spacer_r, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(page), center, TRUE, TRUE, 0);

    app->selected_desktop = NULL;

    return page;
}
#endif /* HAS_DESKTOP_TAB */