#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *progress;
    int status;
    guint pulse_timer;
} AppData;

static AppData *global_app = NULL;

static gboolean pulse_progress(gpointer data) {
    AppData *app = (AppData *)data;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(app->progress));
    return TRUE;
}

static void show_error_dialog(AppData *app) {
    GtkWidget *err = gtk_message_dialog_new(
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_OK,
        "No internet connection detected!"
    );
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(err),
        "Please connect to the internet and try again."
    );
    gtk_window_set_position(GTK_WINDOW(err), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_dialog_run(GTK_DIALOG(err));
    gtk_widget_destroy(err);
    gtk_widget_destroy(app->window);
}

static gboolean download_complete_idle(gpointer data) {
    AppData *app = (AppData *)data;
    if (app->pulse_timer > 0) {
        g_source_remove(app->pulse_timer);
        app->pulse_timer = 0;
    }
    if (app->status != 0) {
        fprintf(stderr, "[IDLE] showing error status=%d\n", app->status);
        show_error_dialog(app);
    } else {
        system("cd /tmp && tar -xf installer.tar && chmod +x neko_installer && (./neko_installer >/dev/null 2>&1 &)");
        gtk_widget_destroy(app->window);
    }
    return G_SOURCE_REMOVE;
}

static gpointer check_internet_thread(gpointer data) {
    AppData *app = (AppData *)data;
    global_app = app;

    int ret = system("wget -q --spider --timeout=5 --tries=1 https://github.com/javiercplus/Neko-Void/releases/download/Installer/neko_installer.tar 2>&1");
    int wstat = WEXITSTATUS(ret);
    fprintf(stderr, "[NET] wget ret=%d wstat=%d\n", ret, wstat);

    if (wstat != 0) {
        app->status = 1;
    } else {
        app->status = 0;
        ret = system("cd /tmp && wget -q --timeout=30 https://github.com/javiercplus/Neko-Void/releases/download/Installer/neko_installer.tar -O installer.tar 2>&1");
        if (WEXITSTATUS(ret) != 0) app->status = 1;
    }

    g_usleep(100000);
    g_main_context_invoke(NULL, download_complete_idle, app);

    return NULL;
}

static void activate(GtkApplication *app, gpointer data) {
    (void)data;

    global_app = g_new0(AppData, 1);
    global_app->status = -1;

    global_app->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(global_app->window), "Kasha Installer");
    gtk_window_set_default_size(GTK_WINDOW(global_app->window), 400, 150);
    gtk_window_set_position(GTK_WINDOW(global_app->window), GTK_WIN_POS_CENTER);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_top(box, 30);
    gtk_widget_set_margin_bottom(box, 30);
    gtk_widget_set_margin_start(box, 30);
    gtk_widget_set_margin_end(box, 30);
    gtk_box_set_homogeneous(GTK_BOX(box), TRUE);
    gtk_container_add(GTK_CONTAINER(global_app->window), box);

    GtkWidget *label = gtk_label_new("Loading...");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_size_new(16 * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(label), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

    global_app->progress = gtk_progress_bar_new();
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(global_app->progress));
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(global_app->progress), 0.1);
    gtk_widget_set_size_request(global_app->progress, 300, -1);
    gtk_box_pack_start(GTK_BOX(box), global_app->progress, TRUE, TRUE, 0);
    global_app->pulse_timer = g_timeout_add(100, pulse_progress, global_app);

    gtk_widget_show_all(global_app->window);

    g_thread_new("download", check_internet_thread, global_app);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.nekovoid.installer", 0);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
