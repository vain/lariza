#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>
#include <webkit2/webkit2.h>


static void client_destroy(GtkWidget *, gpointer);
static gboolean client_destroy_request(WebKitWebView *, gpointer);
static WebKitWebView *client_new(const gchar *, WebKitWebView *, gboolean);
static WebKitWebView *client_new_request(WebKitWebView *, WebKitNavigationAction *,
                                         gpointer);
static void cooperation_setup(void);
static void changed_download_progress(GObject *, GParamSpec *, gpointer);
static void changed_load_progress(GObject *, GParamSpec *, gpointer);
static void changed_title(GObject *, GParamSpec *, gpointer);
static void changed_uri(GObject *, GParamSpec *, gpointer);
static gboolean crashed_web_view(WebKitWebView *, gpointer);
static gboolean decide_policy(WebKitWebView *, WebKitPolicyDecision *,
                              WebKitPolicyDecisionType, gpointer);
static gboolean download_handle(WebKitDownload *, gchar *, gpointer);
static void download_handle_start(WebKitWebView *, WebKitDownload *, gpointer);
static void downloadmanager_cancel(GtkToolButton *, gpointer);
static gboolean downloadmanager_delete(GtkWidget *, gpointer);
static void downloadmanager_setup(void);
static gchar *ensure_uri_scheme(const gchar *);
static void external_handler_run(GSimpleAction *, GVariant *, gpointer);
static void grab_environment_configuration(void);
static void hover_web_view(WebKitWebView *, WebKitHitTestResult *, guint, gpointer);
static gboolean key_common(GtkWidget *, GdkEvent *, gpointer);
static gboolean key_downloadmanager(GtkWidget *, GdkEvent *, gpointer);
static gboolean key_location(GtkWidget *, GdkEvent *, gpointer);
static gboolean key_web_view(GtkWidget *, GdkEvent *, gpointer);
static void keywords_load(void);
static gboolean keywords_try_search(WebKitWebView *, const gchar *);
static gboolean menu_web_view(WebKitWebView *, WebKitContextMenu *, GdkEvent *,
                              WebKitHitTestResult *, gpointer);
static gboolean quit_if_nothing_active(void);
static gboolean remote_msg(GIOChannel *, GIOCondition, gpointer);
static void search(gpointer, gint);
static void show_web_view(WebKitWebView *, gpointer);
static Window tabbed_launch(void);
static void trust_user_certs(WebKitWebContext *);


struct Client
{
    gchar *external_handler_uri;
    gchar *hover_uri;
    GtkWidget *location;
    GtkWidget *vbox;
    GtkWidget *web_view;
    GtkWidget *win;
};

struct DownloadManager
{
    GtkWidget *scroll;
    GtkWidget *toolbar;
    GtkWidget *win;
} dm;


static const gchar *accepted_language[2] = { NULL, NULL };
static gint clients = 0, downloads = 0;
static gboolean cooperative_alone = TRUE;
static gboolean cooperative_instances = TRUE;
static int cooperative_pipe_fp = 0;
static gchar *download_dir = "/var/tmp";
static gboolean enable_webgl = FALSE;
static gboolean enable_console_to_stdout = FALSE;
static Window embed = 0;
static gchar *fifo_suffix = "main";
static gdouble global_zoom = 1.0;
static gchar *history_file = NULL;
static gchar *home_uri = "about:blank";
static gboolean initial_wc_setup_done = FALSE;
static GHashTable *keywords = NULL;
static gchar *search_text = NULL;
static gboolean tabbed_automagic = TRUE;
static gchar *user_agent = NULL;


void
client_destroy(GtkWidget *widget, gpointer data)
{
    struct Client *c = (struct Client *)data;

    g_signal_handlers_disconnect_by_func(G_OBJECT(c->web_view),
                                         changed_load_progress, c);

    free(c);
    clients--;

    quit_if_nothing_active();
}

gboolean
client_destroy_request(WebKitWebView *web_view, gpointer data)
{
    struct Client *c = (struct Client *)data;

    gtk_widget_destroy(c->win);

    return TRUE;
}

WebKitWebView *
client_new(const gchar *uri, WebKitWebView *related_wv, gboolean show)
{
    struct Client *c;
    WebKitWebContext *wc;
    gchar *f;

    if (uri != NULL && cooperative_instances && !cooperative_alone)
    {
        f = ensure_uri_scheme(uri);
        write(cooperative_pipe_fp, f, strlen(f));
        write(cooperative_pipe_fp, "\n", 1);
        g_free(f);
        return NULL;
    }

    c = calloc(1, sizeof(struct Client));
    if (!c)
    {
        fprintf(stderr, __NAME__": fatal: calloc failed\n");
        exit(EXIT_FAILURE);
    }

    if (embed != 0)
    {
        c->win = gtk_plug_new(embed);
        if (!gtk_plug_get_embedded(GTK_PLUG(c->win)))
        {
            fprintf(stderr, __NAME__": Can't plug-in to XID %ld.\n", embed);
            gtk_widget_destroy(c->win);
            c->win = NULL;
            embed = 0;
        }
    }

    if (c->win == NULL)
        c->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_default_size(GTK_WINDOW(c->win), 800, 600);

    g_signal_connect(G_OBJECT(c->win), "destroy", G_CALLBACK(client_destroy), c);
    gtk_window_set_title(GTK_WINDOW(c->win), __NAME__);

    if (related_wv == NULL)
        c->web_view = webkit_web_view_new();
    else
        c->web_view = webkit_web_view_new_with_related_view(related_wv);
    wc = webkit_web_view_get_context(WEBKIT_WEB_VIEW(c->web_view));

    webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(c->web_view), global_zoom);
    g_signal_connect(G_OBJECT(c->web_view), "notify::title",
                     G_CALLBACK(changed_title), c);
    g_signal_connect(G_OBJECT(c->web_view), "notify::uri",
                     G_CALLBACK(changed_uri), c);
    g_signal_connect(G_OBJECT(c->web_view), "notify::estimated-load-progress",
                     G_CALLBACK(changed_load_progress), c);
    g_signal_connect(G_OBJECT(c->web_view), "create",
                     G_CALLBACK(client_new_request), NULL);
    g_signal_connect(G_OBJECT(c->web_view), "context-menu",
                     G_CALLBACK(menu_web_view), c);
    g_signal_connect(G_OBJECT(c->web_view), "close",
                     G_CALLBACK(client_destroy_request), c);
    g_signal_connect(G_OBJECT(c->web_view), "decide-policy",
                     G_CALLBACK(decide_policy), NULL);
    g_signal_connect(G_OBJECT(c->web_view), "key-press-event",
                     G_CALLBACK(key_web_view), c);
    g_signal_connect(G_OBJECT(c->web_view), "button-press-event",
                     G_CALLBACK(key_web_view), c);
    g_signal_connect(G_OBJECT(c->web_view), "scroll-event",
                     G_CALLBACK(key_web_view), c);
    g_signal_connect(G_OBJECT(c->web_view), "mouse-target-changed",
                     G_CALLBACK(hover_web_view), c);
    g_signal_connect(G_OBJECT(c->web_view), "web-process-crashed",
                     G_CALLBACK(crashed_web_view), c);

    if (!initial_wc_setup_done)
    {
        if (accepted_language[0] != NULL)
            webkit_web_context_set_preferred_languages(wc, accepted_language);

        g_signal_connect(G_OBJECT(wc), "download-started",
                         G_CALLBACK(download_handle_start), NULL);

        trust_user_certs(wc);

        initial_wc_setup_done = TRUE;
    }

    if (user_agent != NULL)
        g_object_set(G_OBJECT(webkit_web_view_get_settings(WEBKIT_WEB_VIEW(c->web_view))),
                     "user-agent", user_agent, NULL);

    if (enable_console_to_stdout)
        webkit_settings_set_enable_write_console_messages_to_stdout(webkit_web_view_get_settings(WEBKIT_WEB_VIEW(c->web_view)), TRUE);

    if (enable_webgl)
        webkit_settings_set_enable_webgl(webkit_web_view_get_settings(WEBKIT_WEB_VIEW(c->web_view)), TRUE);

    c->location = gtk_entry_new();
    g_signal_connect(G_OBJECT(c->location), "key-press-event",
                     G_CALLBACK(key_location), c);

    c->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(c->vbox), c->location, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(c->vbox), c->web_view, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(c->win), c->vbox);

    if (show)
        show_web_view(NULL, c);
    else
        g_signal_connect(G_OBJECT(c->web_view), "ready-to-show",
                         G_CALLBACK(show_web_view), c);

    if (uri != NULL)
    {
        f = ensure_uri_scheme(uri);
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), f);
        g_free(f);
    }

    clients++;

    return WEBKIT_WEB_VIEW(c->web_view);
}

WebKitWebView *
client_new_request(WebKitWebView *web_view,
                   WebKitNavigationAction *navigation_action, gpointer data)
{
    return client_new(NULL, web_view, FALSE);
}

void
cooperation_setup(void)
{
    GIOChannel *towatch;
    gchar *fifofilename, *fifopath;

    fifofilename = g_strdup_printf("%s-%s", __NAME__".fifo", fifo_suffix);
    fifopath = g_build_filename(g_get_user_runtime_dir(), fifofilename, NULL);
    g_free(fifofilename);

    if (!g_file_test(fifopath, G_FILE_TEST_EXISTS))
        mkfifo(fifopath, 0600);

    cooperative_pipe_fp = open(fifopath, O_WRONLY | O_NONBLOCK);
    if (!cooperative_pipe_fp)
    {
        fprintf(stderr, __NAME__": Can't open FIFO at all.\n");
    }
    else
    {
        if (write(cooperative_pipe_fp, "", 0) == -1)
        {
            /* Could not do an empty write to the FIFO which means there's
             * no one listening. */
            close(cooperative_pipe_fp);
            towatch = g_io_channel_new_file(fifopath, "r+", NULL);
            g_io_add_watch(towatch, G_IO_IN, (GIOFunc)remote_msg, NULL);
        }
        else
            cooperative_alone = FALSE;
    }

    g_free(fifopath);
}

void
changed_download_progress(GObject *obj, GParamSpec *pspec, gpointer data)
{
    WebKitDownload *download = WEBKIT_DOWNLOAD(obj);
    WebKitURIResponse *resp;
    GtkToolItem *tb = GTK_TOOL_ITEM(data);
    gdouble p, size_mb;
    const gchar *uri;
    gchar *t, *filename, *base;

    p = webkit_download_get_estimated_progress(download);
    p = p > 1 ? 1 : p;
    p = p < 0 ? 0 : p;
    p *= 100;
    resp = webkit_download_get_response(download);
    size_mb = webkit_uri_response_get_content_length(resp) / 1e6;

    uri = webkit_download_get_destination(download);
    filename = g_filename_from_uri(uri, NULL, NULL);
    if (filename == NULL)
    {
        /* This really should not happen because WebKit uses that URI to
         * write to a file... */
        fprintf(stderr, __NAME__": Could not construct file name from URI!\n");
        t = g_strdup_printf("%s (%.0f%% of %.1f MB)",
                            webkit_uri_response_get_uri(resp), p, size_mb);
    }
    else
    {
        base = g_path_get_basename(filename);
        t = g_strdup_printf("%s (%.0f%% of %.1f MB)", base, p, size_mb);
        g_free(filename);
        g_free(base);
    }
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(tb), t);
    g_free(t);
}

void
changed_load_progress(GObject *obj, GParamSpec *pspec, gpointer data)
{
    struct Client *c = (struct Client *)data;
    gdouble p;

    p = webkit_web_view_get_estimated_load_progress(WEBKIT_WEB_VIEW(c->web_view));
    if (p == 1)
        p = 0;
    gtk_entry_set_progress_fraction(GTK_ENTRY(c->location), p);
}

void
changed_title(GObject *obj, GParamSpec *pspec, gpointer data)
{
    const gchar *t, *u;
    struct Client *c = (struct Client *)data;

    u = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));
    t = webkit_web_view_get_title(WEBKIT_WEB_VIEW(c->web_view));

    u = u == NULL ? __NAME__ : u;
    u = u[0] == 0 ? __NAME__ : u;

    t = t == NULL ? u : t;
    t = t[0] == 0 ? u : t;

    gtk_window_set_title(GTK_WINDOW(c->win), t);
}

void
changed_uri(GObject *obj, GParamSpec *pspec, gpointer data)
{
    const gchar *t;
    struct Client *c = (struct Client *)data;
    FILE *fp;

    t = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));

    /* When a web process crashes, we get a "notify::uri" signal, but we
     * can no longer read a meaningful URI. It's just an empty string
     * now. Not updating the location bar in this scenario is important,
     * because we would override the "WEB PROCESS CRASHED" message. */
    if (t != NULL && strlen(t) > 0)
    {
        gtk_entry_set_text(GTK_ENTRY(c->location), t);

        if (history_file != NULL)
        {
            fp = fopen(history_file, "a");
            if (fp != NULL)
            {
                fprintf(fp, "%s\n", t);
                fclose(fp);
            }
            else
                perror(__NAME__": Error opening history file");
        }
    }
}

gboolean
crashed_web_view(WebKitWebView *web_view, gpointer data)
{
    gchar *t;
    struct Client *c = (struct Client *)data;

    t = g_strdup_printf("WEB PROCESS CRASHED: %s",
                        webkit_web_view_get_uri(WEBKIT_WEB_VIEW(web_view)));
    gtk_entry_set_text(GTK_ENTRY(c->location), t);
    g_free(t);

    return TRUE;
}

gboolean
decide_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision,
              WebKitPolicyDecisionType type, gpointer data)
{
    WebKitResponsePolicyDecision *r;

    switch (type)
    {
        case WEBKIT_POLICY_DECISION_TYPE_RESPONSE:
            r = WEBKIT_RESPONSE_POLICY_DECISION(decision);
            if (!webkit_response_policy_decision_is_mime_type_supported(r))
                webkit_policy_decision_download(decision);
            else
                webkit_policy_decision_use(decision);
            break;
        default:
            /* Use whatever default there is. */
            return FALSE;
    }
    return TRUE;
}

void
download_handle_finished(WebKitDownload *download, gpointer data)
{
    downloads--;
}

void
download_handle_start(WebKitWebView *web_view, WebKitDownload *download,
                      gpointer data)
{
    g_signal_connect(G_OBJECT(download), "decide-destination",
                     G_CALLBACK(download_handle), data);
}

gboolean
download_handle(WebKitDownload *download, gchar *suggested_filename, gpointer data)
{
    gchar *sug_clean, *path, *path2 = NULL, *uri;
    GtkToolItem *tb;
    int suffix = 1;
    size_t i;

    sug_clean = g_strdup(suggested_filename);
    for (i = 0; i < strlen(sug_clean); i++)
        if (sug_clean[i] == G_DIR_SEPARATOR)
            sug_clean[i] = '_';

    path = g_build_filename(download_dir, sug_clean, NULL);
    path2 = g_strdup(path);
    while (g_file_test(path2, G_FILE_TEST_EXISTS) && suffix < 1000)
    {
        g_free(path2);

        path2 = g_strdup_printf("%s.%d", path, suffix);
        suffix++;
    }

    if (suffix == 1000)
    {
        fprintf(stderr, __NAME__": Suffix reached limit for download.\n");
        webkit_download_cancel(download);
    }
    else
    {
        uri = g_filename_to_uri(path2, NULL, NULL);
        webkit_download_set_destination(download, uri);
        g_free(uri);

        tb = gtk_tool_button_new(NULL, NULL);
        gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(tb), "gtk-delete");
        gtk_tool_button_set_label(GTK_TOOL_BUTTON(tb), sug_clean);
        gtk_toolbar_insert(GTK_TOOLBAR(dm.toolbar), tb, 0);
        gtk_widget_show_all(dm.win);

        g_signal_connect(G_OBJECT(download), "notify::estimated-progress",
                         G_CALLBACK(changed_download_progress), tb);

        downloads++;
        g_signal_connect(G_OBJECT(download), "finished",
                         G_CALLBACK(download_handle_finished), NULL);

        g_object_ref(download);
        g_signal_connect(G_OBJECT(tb), "clicked",
                         G_CALLBACK(downloadmanager_cancel), download);
    }

    g_free(sug_clean);
    g_free(path);
    g_free(path2);

    /* Propagate -- to whom it may concern. */
    return FALSE;
}

void
downloadmanager_cancel(GtkToolButton *tb, gpointer data)
{
    WebKitDownload *download = WEBKIT_DOWNLOAD(data);

    webkit_download_cancel(download);
    g_object_unref(download);

    gtk_widget_destroy(GTK_WIDGET(tb));
}

gboolean
downloadmanager_delete(GtkWidget *obj, gpointer data)
{
    if (!quit_if_nothing_active())
        gtk_widget_hide(dm.win);

    return TRUE;
}

void
downloadmanager_setup(void)
{
    dm.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_type_hint(GTK_WINDOW(dm.win), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_default_size(GTK_WINDOW(dm.win), 500, 250);
    gtk_window_set_title(GTK_WINDOW(dm.win), __NAME__" - Download Manager");
    g_signal_connect(G_OBJECT(dm.win), "delete-event",
                     G_CALLBACK(downloadmanager_delete), NULL);
    g_signal_connect(G_OBJECT(dm.win), "key-press-event",
                     G_CALLBACK(key_downloadmanager), NULL);

    dm.toolbar = gtk_toolbar_new();
    gtk_orientable_set_orientation(GTK_ORIENTABLE(dm.toolbar),
                                   GTK_ORIENTATION_VERTICAL);
    gtk_toolbar_set_style(GTK_TOOLBAR(dm.toolbar), GTK_TOOLBAR_BOTH_HORIZ);
    gtk_toolbar_set_show_arrow(GTK_TOOLBAR(dm.toolbar), FALSE);

    dm.scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(dm.scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(dm.scroll), dm.toolbar);

    gtk_container_add(GTK_CONTAINER(dm.win), dm.scroll);
}

gchar *
ensure_uri_scheme(const gchar *t)
{
    gchar *f, *fabs;

    f = g_ascii_strdown(t, -1);
    if (!g_str_has_prefix(f, "http:") &&
        !g_str_has_prefix(f, "https:") &&
        !g_str_has_prefix(f, "file:") &&
        !g_str_has_prefix(f, "about:"))
    {
        g_free(f);
        fabs = realpath(t, NULL);
        if (fabs != NULL)
        {
            f = g_strdup_printf("file://%s", fabs);
            free(fabs);
        }
        else
            f = g_strdup_printf("http://%s", t);
        return f;
    }
    else
        return g_strdup(t);
}

void
external_handler_run(GSimpleAction *simple, GVariant *param, gpointer data)
{
    struct Client *c = (struct Client *)data;
    gchar *argv[] = { "lariza-external-handler", "-u", NULL, NULL };
    GPid pid;
    GError *err = NULL;

    (void)simple;
    (void)param;

    argv[2] = c->external_handler_uri;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                       &pid, &err))
    {
        fprintf(stderr, __NAME__": Could not launch key handler: %s\n",
                err->message);
        g_error_free(err);
    }
    else
        g_spawn_close_pid(pid);
}

void
grab_environment_configuration(void)
{
    const gchar *e;

    e = g_getenv(__NAME_UPPERCASE__"_ACCEPTED_LANGUAGE");
    if (e != NULL)
        accepted_language[0] = g_strdup(e);

    e = g_getenv(__NAME_UPPERCASE__"_DOWNLOAD_DIR");
    if (e != NULL)
        download_dir = g_strdup(e);

    e = g_getenv(__NAME_UPPERCASE__"_ENABLE_CONSOLE_TO_STDOUT");
    if (e != NULL)
        enable_console_to_stdout = TRUE;

    e = g_getenv(__NAME_UPPERCASE__"_ENABLE_EXPERIMENTAL_WEBGL");
    if (e != NULL)
        enable_webgl = TRUE;

    e = g_getenv(__NAME_UPPERCASE__"_FIFO_SUFFIX");
    if (e != NULL)
        fifo_suffix = g_strdup(e);

    e = g_getenv(__NAME_UPPERCASE__"_HISTORY_FILE");
    if (e != NULL)
        history_file = g_strdup(e);

    e = g_getenv(__NAME_UPPERCASE__"_HOME_URI");
    if (e != NULL)
        home_uri = g_strdup(e);

    e = g_getenv(__NAME_UPPERCASE__"_USER_AGENT");
    if (e != NULL)
        user_agent = g_strdup(e);

    e = g_getenv(__NAME_UPPERCASE__"_ZOOM");
    if (e != NULL)
        global_zoom = atof(e);
}

void
hover_web_view(WebKitWebView *web_view, WebKitHitTestResult *ht, guint modifiers,
               gpointer data)
{
    struct Client *c = (struct Client *)data;

    if (!gtk_widget_is_focus(c->location))
    {
        if (webkit_hit_test_result_context_is_link(ht))
        {
            gtk_entry_set_text(GTK_ENTRY(c->location),
                               webkit_hit_test_result_get_link_uri(ht));

            if (c->hover_uri != NULL)
                g_free(c->hover_uri);
            c->hover_uri = g_strdup(webkit_hit_test_result_get_link_uri(ht));
        }
        else
        {
            gtk_entry_set_text(GTK_ENTRY(c->location),
                               webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view)));

            if (c->hover_uri != NULL)
                g_free(c->hover_uri);
            c->hover_uri = NULL;
        }
    }
}

gboolean
key_common(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    struct Client *c = (struct Client *)data;
    WebKitWebContext *wc = webkit_web_view_get_context(WEBKIT_WEB_VIEW(c->web_view));
    gchar *f;

    if (event->type == GDK_KEY_PRESS)
    {
        if (((GdkEventKey *)event)->state & GDK_MOD1_MASK)
        {
            switch (((GdkEventKey *)event)->keyval)
            {
                case GDK_KEY_q:  /* close window (left hand) */
                    gtk_widget_destroy(c->win);
                    return TRUE;
                case GDK_KEY_w:  /* home (left hand) */
                    f = ensure_uri_scheme(home_uri);
                    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), f);
                    g_free(f);
                    return TRUE;
                case GDK_KEY_e:  /* new tab (left hand) */
                    f = ensure_uri_scheme(home_uri);
                    client_new(f, NULL, TRUE);
                    g_free(f);
                    return TRUE;
                case GDK_KEY_r:  /* reload (left hand) */
                    webkit_web_view_reload_bypass_cache(WEBKIT_WEB_VIEW(
                                                        c->web_view));
                    return TRUE;
                case GDK_KEY_d:  /* download manager (left hand) */
                    gtk_widget_show_all(dm.win);
                    return TRUE;
                case GDK_KEY_2:  /* search forward (left hand) */
                case GDK_KEY_n:  /* search forward (maybe both hands) */
                    search(c, 1);
                    return TRUE;
                case GDK_KEY_3:  /* search backward (left hand) */
                    search(c, -1);
                    return TRUE;
                case GDK_KEY_l:  /* location (BOTH hands) */
                    gtk_widget_grab_focus(c->location);
                    return TRUE;
                case GDK_KEY_k:  /* initiate search (BOTH hands) */
                    gtk_widget_grab_focus(c->location);
                    gtk_entry_set_text(GTK_ENTRY(c->location), ":/");
                    gtk_editable_set_position(GTK_EDITABLE(c->location), -1);
                    return TRUE;
                case GDK_KEY_c:  /* reload trusted certs (left hand) */
                    trust_user_certs(wc);
                    return TRUE;
                case GDK_KEY_x:  /* launch external handler (left hand) */
                    if (c->external_handler_uri != NULL)
                        g_free(c->external_handler_uri);
                    c->external_handler_uri = g_strdup(
                        webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view)));
                    external_handler_run(NULL, NULL, c);
                    return TRUE;
            }
        }
        /* navigate backward (left hand) */
        else if (((GdkEventKey *)event)->keyval == GDK_KEY_F2)
        {
            webkit_web_view_go_back(WEBKIT_WEB_VIEW(c->web_view));
            return TRUE;
        }
        /* navigate forward (left hand) */
        else if (((GdkEventKey *)event)->keyval == GDK_KEY_F3)
        {
            webkit_web_view_go_forward(WEBKIT_WEB_VIEW(c->web_view));
            return TRUE;
        }
    }

    return FALSE;
}

gboolean
key_downloadmanager(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    if (event->type == GDK_KEY_PRESS)
    {
        if (((GdkEventKey *)event)->state & GDK_MOD1_MASK)
        {
            switch (((GdkEventKey *)event)->keyval)
            {
                case GDK_KEY_d:  /* close window (left hand) */
                case GDK_KEY_q:
                    downloadmanager_delete(dm.win, NULL);
                    return TRUE;
            }
        }
    }

    return FALSE;
}

gboolean
key_location(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    struct Client *c = (struct Client *)data;
    const gchar *t;
    gchar *f;

    if (key_common(widget, event, data))
        return TRUE;

    if (event->type == GDK_KEY_PRESS)
    {
        switch (((GdkEventKey *)event)->keyval)
        {
            case GDK_KEY_KP_Enter:
            case GDK_KEY_Return:
                gtk_widget_grab_focus(c->web_view);
                t = gtk_entry_get_text(GTK_ENTRY(c->location));
                if (t != NULL && t[0] == ':' && t[1] == '/')
                {
                    if (search_text != NULL)
                        g_free(search_text);
                    search_text = g_strdup(t + 2);
                    search(c, 0);
                }
                else if (!keywords_try_search(WEBKIT_WEB_VIEW(c->web_view), t))
                {
                    f = ensure_uri_scheme(t);
                    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), f);
                    g_free(f);
                }
                return TRUE;
            case GDK_KEY_Escape:
                t = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));
                gtk_entry_set_text(GTK_ENTRY(c->location),
                                   (t == NULL ? __NAME__ : t));
                return TRUE;
        }
    }

    return FALSE;
}

gboolean
key_web_view(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    struct Client *c = (struct Client *)data;
    gdouble dx, dy;
    gfloat z;

    if (key_common(widget, event, data))
        return TRUE;

    if (event->type == GDK_KEY_PRESS)
    {
        if (((GdkEventKey *)event)->keyval == GDK_KEY_Escape)
        {
            webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(c->web_view));
            gtk_entry_set_progress_fraction(GTK_ENTRY(c->location), 0);
        }
    }
    else if (event->type == GDK_BUTTON_PRESS)
    {
        switch (((GdkEventButton *)event)->button)
        {
            case 2:
                if (c->hover_uri != NULL)
                {
                    client_new(c->hover_uri, NULL, TRUE);
                    return TRUE;
                }
                break;
            case 8:
                webkit_web_view_go_back(WEBKIT_WEB_VIEW(c->web_view));
                return TRUE;
            case 9:
                webkit_web_view_go_forward(WEBKIT_WEB_VIEW(c->web_view));
                return TRUE;
        }
    }
    else if (event->type == GDK_SCROLL)
    {
        if (((GdkEventScroll *)event)->state & GDK_MOD1_MASK ||
            ((GdkEventScroll *)event)->state & GDK_CONTROL_MASK)
        {
            gdk_event_get_scroll_deltas(event, &dx, &dy);
            z = webkit_web_view_get_zoom_level(WEBKIT_WEB_VIEW(c->web_view));
            z += -dy * 0.1;
            z = dx != 0 ? global_zoom : z;
            webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(c->web_view), z);
            return TRUE;
        }
    }

    return FALSE;
}

void
keywords_load(void)
{
    GError *err = NULL;
    GIOChannel *channel = NULL;
    gchar *path = NULL, *buf = NULL;
    gchar **tokens = NULL;

    keywords = g_hash_table_new(g_str_hash, g_str_equal);

    path = g_build_filename(g_get_user_config_dir(), __NAME__, "keywordsearch",
                            NULL);
    channel = g_io_channel_new_file(path, "r", &err);
    if (channel != NULL)
    {
        while (g_io_channel_read_line(channel, &buf, NULL, NULL, NULL)
               == G_IO_STATUS_NORMAL)
        {
            g_strstrip(buf);
            if (buf[0] != '#')
            {
                tokens = g_strsplit(buf, " ", 2);
                if (tokens[0] != NULL && tokens[1] != NULL)
                    g_hash_table_insert(keywords, g_strdup(tokens[0]),
                                        g_strdup(tokens[1]));
                g_strfreev(tokens);
            }
            g_free(buf);
        }
        g_io_channel_shutdown(channel, FALSE, NULL);
    }
    g_free(path);
}

gboolean
keywords_try_search(WebKitWebView *web_view, const gchar *t)
{
    gboolean ret = FALSE;
    gchar **tokens = NULL;
    gchar *val = NULL, *escaped = NULL, *uri = NULL;

    tokens = g_strsplit(t, " ", 2);
    if (tokens[0] != NULL && tokens[1] != NULL)
    {
        val = g_hash_table_lookup(keywords, tokens[0]);
        if (val != NULL)
        {
            escaped = g_uri_escape_string(tokens[1], NULL, TRUE);
            uri = g_strdup_printf((gchar *)val, escaped);
            webkit_web_view_load_uri(web_view, uri);
            g_free(uri);
            g_free(escaped);
            ret = TRUE;
        }
    }
    g_strfreev(tokens);

    return ret;
}

gboolean
menu_web_view(WebKitWebView *web_view, WebKitContextMenu *menu, GdkEvent *ev,
              WebKitHitTestResult *ht, gpointer data)
{
    struct Client *c = (struct Client *)data;
    GSimpleAction *action = NULL;
    WebKitContextMenuItem *mi = NULL;
    const gchar *uri = NULL;

    (void)ev;

    if (webkit_hit_test_result_context_is_link(ht))
        uri = webkit_hit_test_result_get_link_uri(ht);
    else if (webkit_hit_test_result_context_is_image(ht))
        uri = webkit_hit_test_result_get_image_uri(ht);
    else if (webkit_hit_test_result_context_is_media(ht))
        uri = webkit_hit_test_result_get_media_uri(ht);

    if (uri != NULL)
    {
        webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());

        if (c->external_handler_uri != NULL)
            g_free(c->external_handler_uri);
        c->external_handler_uri = g_strdup(uri);
        action = g_simple_action_new("external_handler", NULL);
        g_signal_connect(G_OBJECT(action), "activate",
                         G_CALLBACK(external_handler_run), data);
        mi = webkit_context_menu_item_new_from_gaction(G_ACTION(action),
                                                       "Open with external handler",
                                                       NULL);
        webkit_context_menu_append(menu, mi);
        g_object_unref(action);
    }

    /* FALSE = Show the menu. (TRUE = Don't ever show it.) */
    return FALSE;
}

gboolean
quit_if_nothing_active(void)
{
    if (clients == 0)
    {
        if (downloads == 0)
        {
            gtk_main_quit();
            return TRUE;
        }
        else
            gtk_widget_show_all(dm.win);
    }

    return FALSE;
}

gboolean
remote_msg(GIOChannel *channel, GIOCondition condition, gpointer data)
{
    gchar *uri = NULL;

    g_io_channel_read_line(channel, &uri, NULL, NULL, NULL);
    if (uri)
    {
        g_strstrip(uri);
        client_new(uri, NULL, TRUE);
        g_free(uri);
    }
    return TRUE;
}

void
search(gpointer data, gint direction)
{
    struct Client *c = (struct Client *)data;
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(c->web_view);
    WebKitFindController *fc = webkit_web_view_get_find_controller(web_view);

    if (search_text == NULL)
        return;

    switch (direction)
    {
        case 0:
            webkit_find_controller_search(fc, search_text,
                                          WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE |
                                          WEBKIT_FIND_OPTIONS_WRAP_AROUND,
                                          G_MAXUINT);
            break;
        case 1:
            webkit_find_controller_search_next(fc);
            break;
        case -1:
            webkit_find_controller_search_previous(fc);
            break;
    }
}

void
show_web_view(WebKitWebView *web_view, gpointer data)
{
    struct Client *c = (struct Client *)data;

    (void)web_view;

    gtk_widget_grab_focus(c->web_view);
    gtk_widget_show_all(c->win);
}

Window
tabbed_launch(void)
{
    gint tabbed_stdout;
    GIOChannel *tabbed_stdout_channel;
    GError *err = NULL;
    gchar *output = NULL;
    char *argv[] = { "tabbed", "-c", "-d", "-p", "s1", "-n", __NAME__, NULL };
    Window plug_into;

    if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL,
                                  NULL, NULL, NULL, &tabbed_stdout, NULL,
                                  &err))
    {
        fprintf(stderr, __NAME__": Could not launch tabbed: %s\n", err->message);
        g_error_free(err);
        return 0;
    }

    tabbed_stdout_channel = g_io_channel_unix_new(tabbed_stdout);
    if (tabbed_stdout_channel == NULL)
    {
        fprintf(stderr, __NAME__": Could open tabbed's stdout\n");
        return 0;
    }
    g_io_channel_read_line(tabbed_stdout_channel, &output, NULL, NULL, NULL);
    g_io_channel_shutdown(tabbed_stdout_channel, FALSE, NULL);
    if (output == NULL)
    {
        fprintf(stderr, __NAME__": Could not read XID from tabbed\n");
        return 0;
    }
    g_strstrip(output);
    plug_into = strtol(output, NULL, 16);
    g_free(output);
    if (plug_into == 0)
        fprintf(stderr, __NAME__": The XID from tabbed is 0\n");
    return plug_into;
}

void
trust_user_certs(WebKitWebContext *wc)
{
    GTlsCertificate *cert;
    const gchar *basedir, *file, *absfile;
    GDir *dir;

    basedir = g_build_filename(g_get_user_config_dir(), __NAME__, "certs", NULL);
    dir = g_dir_open(basedir, 0, NULL);
    if (dir != NULL)
    {
        file = g_dir_read_name(dir);
        while (file != NULL)
        {
            absfile = g_build_filename(g_get_user_config_dir(), __NAME__, "certs",
                                       file, NULL);
            cert = g_tls_certificate_new_from_file(absfile, NULL);
            if (cert == NULL)
                fprintf(stderr, __NAME__": Could not load trusted cert '%s'\n", file);
            else
                webkit_web_context_allow_tls_certificate_for_host(wc, cert, file);
            file = g_dir_read_name(dir);
        }
        g_dir_close(dir);
    }
}


int
main(int argc, char **argv)
{
    gchar *c;
    int opt, i;

    gtk_init(&argc, &argv);
    webkit_web_context_set_process_model(webkit_web_context_get_default(),
        WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES);

    grab_environment_configuration();

    while ((opt = getopt(argc, argv, "e:CT")) != -1)
    {
        switch (opt)
        {
            case 'e':
                embed = atol(optarg);
                tabbed_automagic = FALSE;
                break;
            case 'C':
                cooperative_instances = FALSE;
                break;
            case 'T':
                tabbed_automagic = FALSE;
                break;
            default:
                fprintf(stderr, "Usage: "__NAME__" [OPTION]... [URI]...\n");
                exit(EXIT_FAILURE);
        }
    }

    keywords_load();
    if (cooperative_instances)
        cooperation_setup();
    downloadmanager_setup();

    if (tabbed_automagic && !(cooperative_instances && !cooperative_alone))
        embed = tabbed_launch();

    if (!cooperative_instances || cooperative_alone)
    {
        c = g_build_filename(g_get_user_config_dir(), __NAME__, "web_extensions",
                             NULL);
        webkit_web_context_set_web_extensions_directory(
            webkit_web_context_get_default(), c
        );
    }

    if (optind >= argc)
        client_new(home_uri, NULL, TRUE);
    else
    {
        for (i = optind; i < argc; i++)
            client_new(argv[i], NULL, TRUE);
    }

    if (!cooperative_instances || cooperative_alone)
        gtk_main();
    exit(EXIT_SUCCESS);
}
