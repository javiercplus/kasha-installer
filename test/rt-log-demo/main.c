/*
 * rt-log-demo
 * Demo aislada para validar la propuesta de mostrar en la interfaz (GTK)
 * la salida real de los comandos, en tiempo real.
 *
 * Mecánica:
 *   - fork() + pipe() + execl("/bin/sh","-c",cmd)  (equivalente a system())
 *   - stdout y stderr del hijo van a la pipe
 *   - el padre lee y va acumulando en un buffer protegido por mutex
 *   - un g_timeout de 100ms vuelca lo acumulado en el GtkTextView (hilo principal)
 *   - se respeta el código de salida (WIFEXITED/WIFSIGNALED)
 *
 * Build:
 *   gcc -Wall -Wextra -o rt_demo main.c $(pkg-config --cflags --libs gtk+-3.0)
 *
 * Run:
 *   ./rt_demo                 (modo interactivo, botón "Ejecutar")
 *   RT_DEMO_AUTORUN=1 ./rt_demo  (arranca solo y cierra al terminar)
 */

#include <gtk/gtk.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

static GtkWidget *console_text;
static GtkWidget *status_label;
static GtkWidget *btn_start;

static const char *ts(void) {
    static char buf[64];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(buf, sizeof(buf), "%ld.%03ld", (long)tv.tv_sec % 1000, (long)tv.tv_usec / 1000);
    return buf;
}

/* Buffer pendiente: solo lo tocan worker (append) y el timeout (swap). */
static GString *pending = NULL;
static GMutex  pending_mutex;

/* ------------------------------------------------------------------ */
/*  UI helpers                                                         */
/* ------------------------------------------------------------------ */

/* append_direct: SOLO desde el hilo principal (GTK). */
static void append_direct(const char *s) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console_text));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, s, -1);

    GtkTextMark *mark = gtk_text_buffer_get_insert(buf);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(console_text), mark);

    /* El terminal también ve todo (como hoy). */
    g_print("[%s] %s", ts(), s);
    fflush(stdout);
}

static gboolean idle_set_status(gpointer data) {
    char *s = data;
    gtk_label_set_text(GTK_LABEL(status_label), s);
    g_free(s);
    return G_SOURCE_REMOVE;
}

static void ui_status_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    g_idle_add(idle_set_status, s);
}

/* ------------------------------------------------------------------ */
/*  Acumulador + volcado periódico (coalescing)                        */
/* ------------------------------------------------------------------ */

static void pend_append(const char *s, gssize len) {
    g_mutex_lock(&pending_mutex);
    if (!pending) pending = g_string_new(NULL);
    g_string_append_len(pending, s, len);
    g_mutex_unlock(&pending_mutex);
}

static void pend_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    pend_append(s, -1);
    g_free(s);
}

/* Corre en el hilo principal cada 100ms. */
static gboolean flush_pending(gpointer data) {
    (void)data;
    char *out = NULL;

    g_mutex_lock(&pending_mutex);
    if (pending && pending->len > 0) {
        out = g_string_free(pending, FALSE);  /* transfiere el contenido */
        pending = NULL;
    }
    g_mutex_unlock(&pending_mutex);

    if (out) {
        append_direct(out);
        g_free(out);
    }
    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/*  run_capture: fork + pipe + exec, stream a la UI                    */
/* ------------------------------------------------------------------ */

static int run_capture(const char *cmd) {
    int fds[2];
    if (pipe(fds) != 0) { perror("pipe"); return -1; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }

    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    close(fds[1]);

    char buf[8192];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
        char *start = buf;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n' || buf[i] == '\r') {
                pend_append(start, &buf[i] - start);   /* sin el separador */
                pend_append("\n", 1);                   /* normalizamos a \n */
                start = &buf[i] + 1;
            }
        }
        if (start < &buf[n])
            pend_append(start, &buf[n] - start);        /* trozo parcial */
    }
    close(fds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

static void on_start_clicked(GtkWidget *w, gpointer data);
static gboolean start_wrapper(gpointer data);
static gboolean quit_timeout_cb(gpointer data);

/* ------------------------------------------------------------------ */
/*  Worker thread                                                      */
/* ------------------------------------------------------------------ */

static gpointer worker(gpointer data) {
    (void)data;

    const char *cmds[] = {
        /* 1. stream lento -> visible en tiempo real */
        "for i in 1 2 3 4 5 6 7 8 9 10; do echo \"stream line $i\"; sleep 0.4; done",

        /* 2. barra de progreso con \r -> se normaliza */
        "for i in $(seq 1 40); do printf 'progress %02d/40\\r' $i; sleep 0.1; done; echo ' progress done'",

        /* 3. error a stderr + exit code != 0 */
        "ls /tmp/nonexistent-xyz",

        /* 4. exit code 1 */
        "false",

        /* 5. redirect interno respetado: el propio comando silencia su stderr */
        "echo '== el siguiente error NO debe verse (2>/dev/null del propio cmd):'; "
        "ls /nope 2>/dev/null; echo '== fin (stderr silenciado por el comando)'",

        NULL
    };

    gboolean failed = FALSE;
    for (int i = 0; cmds[i]; i++) {
        /* Eco del comando y exit code pasan por el MISMO buffer que la
         * salida, así el orden es siempre correcto (aun con el flush de 100ms). */
        pend_printf("=== $ %s\n", cmds[i]);
        int rc = run_capture(cmds[i]);
        pend_printf("--- exit code: %d\n", rc);
        ui_status_printf("exit code: %d (%s)", rc, rc == 0 ? "OK" : "ERROR");
        if (rc != 0) failed = TRUE;
    }

    ui_status_printf("FIN — %s", failed ? "hubo algún exit != 0 (esperado en algunos tests)" : "todo OK");
    if (getenv("RT_DEMO_AUTORUN")) {
        /* cierra la app 2s después de terminar */
        g_timeout_add(2000, quit_timeout_cb, NULL);
    }
    return NULL;
}

static gboolean quit_timeout_cb(gpointer data) {
    (void)data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

static gboolean start_wrapper(gpointer data) {
    (void)data;
    on_start_clicked(NULL, NULL);
    return G_SOURCE_REMOVE;
}

static void on_start_clicked(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    gtk_widget_set_sensitive(btn_start, FALSE);
    g_thread_try_new("worker", worker, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "rt-log-demo — salida en tiempo real");
    gtk_window_set_default_size(GTK_WINDOW(win), 720, 420);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_container_add(GTK_CONTAINER(win), vbox);

    console_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(console_text), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(console_text), TRUE);
    gtk_widget_set_vexpand(console_text, TRUE);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), console_text);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    status_label = gtk_label_new("listo");
    gtk_box_pack_start(GTK_BOX(vbox), status_label, FALSE, FALSE, 0);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    btn_start = gtk_button_new_with_label("Ejecutar comandos");
    g_signal_connect(btn_start, "clicked", G_CALLBACK(on_start_clicked), NULL);
    GtkWidget *btn_quit = gtk_button_new_with_label("Salir");
    g_signal_connect(btn_quit, "clicked", G_CALLBACK(gtk_main_quit), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), btn_start, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_quit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    g_timeout_add(100, flush_pending, NULL);

    gtk_widget_show_all(win);

    if (getenv("RT_DEMO_AUTORUN")) {
        g_idle_add(start_wrapper, NULL);
    }

    gtk_main();
    return 0;
}
