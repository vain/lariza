#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>
#include <webkit/webkit.h>


static void adblock(WebKitWebView *, WebKitWebFrame *, WebKitWebResource *,
                    WebKitNetworkRequest *, WebKitNetworkResponse *, gpointer);
static void adblock_load(void);
static void client_destroy(GtkWidget *, gpointer);
static gboolean client_destroy_request(WebKitWebView *, gpointer);
static WebKitWebView *client_new(const gchar *);
static WebKitWebView *client_new_request(WebKitWebView *, WebKitWebFrame *,
                                         gpointer);
static void cooperation_setup(void);
static void changed_download_progress(GObject *, GParamSpec *, gpointer);
static void changed_load_progress(GObject *, GParamSpec *, gpointer);
static void changed_load_status(GObject *, GParamSpec *, gpointer);
static void changed_title(GObject *, GParamSpec *, gpointer);
static void changed_uri(GObject *, GParamSpec *, gpointer);
static gboolean download_handle(WebKitWebView *, WebKitDownload *, gpointer);
static gboolean download_reset_indicator(gpointer);
static gboolean download_request(WebKitWebView *, WebKitWebFrame *,
                                 WebKitNetworkRequest *, gchar *,
                                 WebKitWebPolicyDecision *, gpointer);
static void downloadmanager_cancel(GtkToolButton *, gpointer data);
static void downloadmanager_setup(void);
static gchar *ensure_uri_scheme(const gchar *);
static void grab_environment_configuration(void);
static void hover_web_view(WebKitWebView *, gchar *, gchar *, gpointer);
static gboolean key_downloadmanager(GtkWidget *, GdkEvent *, gpointer);
static gboolean key_location(GtkWidget *, GdkEvent *, gpointer);
static gboolean key_web_view(GtkWidget *, GdkEvent *, gpointer);
static void keywords_load(void);
static gboolean keywords_try_search(WebKitWebView *, const gchar *);
static gboolean remote_msg(GIOChannel *, GIOCondition, gpointer);
static void search(gpointer, gint);
static Window tabbed_launch(void);
static void usage(void);
static void history_load(void);
static gboolean matchCompletion(GtkEntryCompletion *, const gchar *,
                                GtkTreeIter *, gpointer);


struct Client
{
	GtkWidget *location;
	GtkWidget *progress;
	GtkWidget *scroll;
	GtkWidget *status;
	GtkWidget *top_box;
	GtkWidget *vbox;
	GtkWidget *web_view;
	GtkWidget *win;
	GtkEntryCompletion *completion;
};

struct DownloadManager
{
	GtkWidget *scroll;
	GtkWidget *toolbar;
	GtkWidget *win;
} dm;


static gchar *accepted_language = "en-US";
static GSList *adblock_patterns = NULL;
static gint clients = 0;
static gboolean cooperative_alone = TRUE;
static gboolean cooperative_instances = TRUE;
static int cooperative_pipe_fp = 0;
static gchar *download_dir = "/tmp";
static gint downloads_indicated = 0;
static Window embed = 0;
static gchar *fifo_suffix = "main";
static gdouble global_zoom = 1.0;
static gchar *home_uri = "about:blank";
static GHashTable *keywords = NULL;
static gboolean language_is_set = FALSE;
static gchar *search_text = NULL;
static gboolean show_all_requests = FALSE;
static gboolean tabbed_automagic = TRUE;
static GtkListStore *history;

gboolean
matchCompletion(GtkEntryCompletion *completion, const gchar *key, GtkTreeIter *iter, gpointer user_data)
{
	// get current item from the history
	const gchar *uri, *title;
	gtk_tree_model_get(GTK_TREE_MODEL(history), iter, 0, &uri, 1, &title, -1);
	
	gboolean matchesUri = FALSE, matchesTitle = FALSE;
	// if uri or title exist, compare them to the input
	// uri and title are casefolded because key is casefolded either
	if (uri != NULL)
		matchesUri = (g_strstr_len(g_utf8_casefold(uri, -1), -1, key) != NULL);
	if (title != NULL)
		matchesTitle = (g_strstr_len(g_utf8_casefold(title, -1), -1, key) != NULL);
	return (matchesUri || matchesTitle);
}

void
adblock(WebKitWebView *web_view, WebKitWebFrame *frame,
        WebKitWebResource *resource, WebKitNetworkRequest *request,
        WebKitNetworkResponse *response, gpointer data)
{
	GSList *it = adblock_patterns;
	const gchar *uri;

	uri = webkit_network_request_get_uri(request);
	if (show_all_requests)
		fprintf(stderr, "   -> %s\n", uri);

	while (it)
	{
		if (g_regex_match((GRegex *)(it->data), uri, 0, NULL))
		{
			webkit_network_request_set_uri(request, "about:blank");
			if (show_all_requests)
				fprintf(stderr, "            BLOCKED!\n");
			return;
		}
		it = g_slist_next(it);
	}
}

void
adblock_load(void)
{
	GRegex *re = NULL;
	GError *err = NULL;
	GIOChannel *channel = NULL;
	gchar *path = NULL, *buf = NULL;

	path = g_build_filename(g_get_user_config_dir(), __NAME__, "adblock.black",
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
				re = g_regex_new(buf,
				                 G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
				                 G_REGEX_MATCH_PARTIAL, &err);
				if (err != NULL)
				{
					fprintf(stderr, __NAME__": Could not compile regex: %s\n", buf);
					g_error_free(err);
					err = NULL;
				}
				else
					adblock_patterns = g_slist_append(adblock_patterns, re);
			}
			g_free(buf);
		}
		g_io_channel_shutdown(channel, FALSE, NULL);
	}
	g_free(path);
}

void
history_load(void)
{
	history = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
}

void
client_destroy(GtkWidget *obj, gpointer data)
{
	struct Client *c = (struct Client *)data;

	g_signal_handlers_disconnect_by_func(G_OBJECT(c->web_view),
	                                     changed_load_progress, c);

	free(c);
	clients--;

	if (clients == 0)
		gtk_main_quit();
}

gboolean
client_destroy_request(WebKitWebView *web_view, gpointer data)
{
	struct Client *c = (struct Client *)data;

	gtk_widget_destroy(c->win);

	return TRUE;
}

WebKitWebView *
client_new(const gchar *uri)
{
	struct Client *c;
	gchar *f;

	if (uri != NULL && cooperative_instances && !cooperative_alone)
	{
		write(cooperative_pipe_fp, uri, strlen(uri));
		write(cooperative_pipe_fp, "\n", 1);
		return NULL;
	}

	c = malloc(sizeof(struct Client));
	if (!c)
	{
		fprintf(stderr, __NAME__": fatal: malloc failed\n");
		exit(EXIT_FAILURE);
	}

	c->win = NULL;
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
	{
		c->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_wmclass(GTK_WINDOW(c->win), __NAME__, __NAME_CAPITALIZED__);
	}

	/* When using Gtk2, it only shows a white area when run in suckless'
	 * tabbed. It appears we need to set a default window size for this
	 * to work. This is not needed when using Gtk3. */
	gtk_window_set_default_size(GTK_WINDOW(c->win), 1024, 768);

	g_signal_connect(G_OBJECT(c->win), "destroy", G_CALLBACK(client_destroy), c);
	gtk_window_set_title(GTK_WINDOW(c->win), __NAME__);

	c->web_view = webkit_web_view_new();

	/* XXX I really do want to enable this option. However, I get
	 * reproducible crashes with it enabled. I've seen bug reports from
	 * 2010 about this... WebKit crashes in libpixman, so maybe it's not
	 * a WebKit issue.
	 * Yeah, well. I'll turn it off for now. */
	/*webkit_web_view_set_full_content_zoom(WEBKIT_WEB_VIEW(c->web_view), TRUE);*/

	webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(c->web_view), global_zoom);
	g_signal_connect(G_OBJECT(c->web_view), "notify::title",
	                 G_CALLBACK(changed_title), c);
	g_signal_connect(G_OBJECT(c->web_view), "notify::uri",
	                 G_CALLBACK(changed_uri), c);
	g_signal_connect(G_OBJECT(c->web_view), "notify::progress",
	                 G_CALLBACK(changed_load_progress), c);
	g_signal_connect(G_OBJECT(c->web_view), "notify::load-status",
	                 G_CALLBACK(changed_load_status), c);
	g_signal_connect(G_OBJECT(c->web_view), "create-web-view",
	                 G_CALLBACK(client_new_request), NULL);
	g_signal_connect(G_OBJECT(c->web_view), "close-web-view",
	                 G_CALLBACK(client_destroy_request), c);
	g_signal_connect(G_OBJECT(c->web_view),
	                 "mime-type-policy-decision-requested",
	                 G_CALLBACK(download_request), NULL);
	g_signal_connect(G_OBJECT(c->web_view), "download-requested",
	                 G_CALLBACK(download_handle), c);
	g_signal_connect(G_OBJECT(c->web_view), "key-press-event",
	                 G_CALLBACK(key_web_view), c);
	g_signal_connect(G_OBJECT(c->web_view), "button-press-event",
	                 G_CALLBACK(key_web_view), c);
	g_signal_connect(G_OBJECT(c->web_view), "scroll-event",
	                 G_CALLBACK(key_web_view), c);
	g_signal_connect(G_OBJECT(c->web_view), "hovering-over-link",
	                 G_CALLBACK(hover_web_view), c);
	g_signal_connect(G_OBJECT(c->web_view), "resource-request-starting",
	                 G_CALLBACK(adblock), NULL);

	if (!language_is_set)
	{
		g_object_set(webkit_get_default_session(), "accept-language",
		             accepted_language, NULL);
		language_is_set = TRUE;
	}

	c->scroll = gtk_scrolled_window_new(NULL, NULL);

	gtk_container_add(GTK_CONTAINER(c->scroll), c->web_view);

	c->location = gtk_entry_new();
	g_signal_connect(G_OBJECT(c->location), "key-press-event",
	                 G_CALLBACK(key_location), c);
	// create completion element and connect it to the entry widget and the history
	c->completion = gtk_entry_completion_new();
	gtk_entry_set_completion (GTK_ENTRY(c->location),
	                          GTK_ENTRY_COMPLETION(c->completion));
	gtk_entry_completion_set_model(GTK_ENTRY_COMPLETION(c->completion),
	                               GTK_TREE_MODEL(history));
	// set the column to search in, the match function and enable automatic completion insert
	gtk_entry_completion_set_text_column(GTK_ENTRY_COMPLETION(c->completion), 0);
	gtk_entry_completion_set_match_func(GTK_ENTRY_COMPLETION(c->completion),
	                                    matchCompletion, NULL, NULL);
	gtk_entry_completion_set_inline_selection(GTK_ENTRY_COMPLETION(c->completion), TRUE);

	c->progress = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(c->progress), 0);

	c->status = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(c->status), 0);
	gtk_widget_set_size_request(c->status, 20, -1);

	c->top_box = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(c->top_box), c->status, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(c->top_box), c->location, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(c->top_box), c->progress, FALSE, TRUE, 2);

	c->vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(c->vbox), c->top_box, FALSE, FALSE, 2);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->scroll);

	gtk_container_add(GTK_CONTAINER(c->win), c->vbox);

	gtk_widget_grab_focus(c->web_view);
	gtk_widget_show_all(c->win);

	if (uri != NULL)
	{
		f = ensure_uri_scheme(uri);
		if (show_all_requests)
			fprintf(stderr, "====> %s\n", uri);
		webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), f);
		g_free(f);
	}

	clients++;

	return WEBKIT_WEB_VIEW(c->web_view);
}

WebKitWebView *
client_new_request(WebKitWebView *web_view, WebKitWebFrame *frame, gpointer data)
{
	return client_new(NULL);
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
	GtkToolItem *tb = GTK_TOOL_ITEM(data);
	gdouble p;
	const gchar *uri;
	gchar *t, *filename, *base;

	p = webkit_download_get_progress(download) * 100;

	uri = webkit_download_get_destination_uri(download);
	filename = g_filename_from_uri(uri, NULL, NULL);
	if (filename == NULL)
	{
		/* This really should not happen because WebKit uses that URI to
		 * write to a file... */
		fprintf(stderr, __NAME__": Could not construct file name from URI!\n");
		t = g_strdup_printf("%s (%.0f%%)",
		                    webkit_download_get_suggested_filename(download), p);
	}
	else
	{
		base = g_path_get_basename(filename);
		t = g_strdup_printf("%s (%.0f%%)", base, p);
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

	p = webkit_web_view_get_progress(WEBKIT_WEB_VIEW(c->web_view));
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(c->progress), p);
}

void
changed_load_status(GObject *obj, GParamSpec *pspec, gpointer data)
{
	const gchar *uri, *title;
	struct Client *c = (struct Client *)data;
	WebKitLoadStatus status;
	GtkTreeIter iter;

	// get current load-status of web_view
	status = webkit_web_view_get_load_status(WEBKIT_WEB_VIEW(c->web_view));
	switch(status)
	{
		case WEBKIT_LOAD_COMMITTED:
			break;
		case WEBKIT_LOAD_FIRST_VISUALLY_NON_EMPTY_LAYOUT:
			break;
		case WEBKIT_LOAD_FINISHED:
			// add uri and title to history after the web_view finished loading
			uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));
			title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(c->web_view));
			gtk_list_store_append(history, &iter);
			gtk_list_store_set(history, &iter, 0, uri, 1, title, -1); 
			break;
		default:
			break;
	}
}

void
changed_title(GObject *obj, GParamSpec *pspec, gpointer data)
{
	const gchar *t;
	struct Client *c = (struct Client *)data;

	t = webkit_web_view_get_title(WEBKIT_WEB_VIEW(c->web_view));
	gtk_window_set_title(GTK_WINDOW(c->win), (t == NULL ? __NAME__ : t));
}

void
changed_uri(GObject *obj, GParamSpec *pspec, gpointer data)
{
	const gchar *t;
	struct Client *c = (struct Client *)data;

	t = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));
	gtk_entry_set_text(GTK_ENTRY(c->location), (t == NULL ? __NAME__ : t));
}

gboolean
download_handle(WebKitWebView *web_view, WebKitDownload *download, gpointer data)
{
	struct Client *c = (struct Client *)data;
	gchar *path, *path2 = NULL, *uri;
	GtkToolItem *tb;
	gboolean ret;
	int suffix = 1;

	path = g_build_filename(download_dir,
	                        webkit_download_get_suggested_filename(download),
	                        NULL);
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
		ret = FALSE;
	}
	else
	{
		uri = g_filename_to_uri(path2, NULL, NULL);
		webkit_download_set_destination_uri(download, uri);
		ret = TRUE;
		g_free(uri);

		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(c->status), 1);
		downloads_indicated++;
		g_timeout_add(500, download_reset_indicator, c);

		tb = gtk_tool_button_new_from_stock(GTK_STOCK_DELETE);
		gtk_tool_button_set_label(GTK_TOOL_BUTTON(tb),
		                          webkit_download_get_suggested_filename(download));
		gtk_toolbar_insert(GTK_TOOLBAR(dm.toolbar), tb, 0);
		gtk_widget_show_all(dm.toolbar);

		g_signal_connect(G_OBJECT(download), "notify::progress",
		                 G_CALLBACK(changed_download_progress), tb);

		g_object_ref(download);
		g_signal_connect(G_OBJECT(tb), "clicked",
		                 G_CALLBACK(downloadmanager_cancel), download);
	}

	g_free(path);
	g_free(path2);

	return ret;
}

gboolean
download_reset_indicator(gpointer data)
{
	struct Client *c = (struct Client *)data;

	downloads_indicated--;
	if (downloads_indicated == 0)
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(c->status), 0);

	return FALSE;
}

gboolean
download_request(WebKitWebView *web_view, WebKitWebFrame *frame,
                 WebKitNetworkRequest *request, gchar *mime_type,
                 WebKitWebPolicyDecision *policy_decision, gpointer data)
{
	if (!webkit_web_view_can_show_mime_type(web_view, mime_type))
	{
		webkit_web_policy_decision_download(policy_decision);
		return TRUE;
	}
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

void
downloadmanager_setup(void)
{
	dm.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_type_hint(GTK_WINDOW(dm.win), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_window_set_default_size(GTK_WINDOW(dm.win), 500, 250);
	gtk_window_set_title(GTK_WINDOW(dm.win), __NAME__" - Download Manager");
	g_signal_connect(G_OBJECT(dm.win), "delete-event",
	                 G_CALLBACK(gtk_widget_hide_on_delete), NULL);
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
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(dm.scroll),
	                                      dm.toolbar);

	gtk_container_add(GTK_CONTAINER(dm.win), dm.scroll);
}

gchar *
ensure_uri_scheme(const gchar *t)
{
	gchar *f;

	f = g_ascii_strdown(t, -1);
	if (!g_str_has_prefix(f, "http:") &&
	    !g_str_has_prefix(f, "https:") &&
	    !g_str_has_prefix(f, "file:") &&
	    !g_str_has_prefix(f, "about:"))
	{
		g_free(f);
		f = g_strdup_printf("http://%s", t);
		return f;
	}
	else
		return g_strdup(t);
}

void
grab_environment_configuration(void)
{
	const gchar *e;

	e = g_getenv(__NAME_UPPERCASE__"_ACCEPTED_LANGUAGE");
	if (e != NULL)
		accepted_language = g_strdup(e);

	e = g_getenv(__NAME_UPPERCASE__"_DOWNLOAD_DIR");
	if (e != NULL)
		download_dir = g_strdup(e);

	e = g_getenv(__NAME_UPPERCASE__"_FIFO_SUFFIX");
	if (e != NULL)
		fifo_suffix = g_strdup(e);

	e = g_getenv(__NAME_UPPERCASE__"_HOME_URI");
	if (e != NULL)
		home_uri = g_strdup(e);

	e = g_getenv(__NAME_UPPERCASE__"_ZOOM");
	if (e != NULL)
		global_zoom = atof(e);
}

void
hover_web_view(WebKitWebView *web_view, gchar *title, gchar *uri, gpointer data)
{
	struct Client *c = (struct Client *)data;

	if (!gtk_widget_is_focus(c->location))
	{
		if (uri == NULL)
			gtk_entry_set_text(GTK_ENTRY(c->location),
			                   webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view)));
		else
			gtk_entry_set_text(GTK_ENTRY(c->location), uri);
	}
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
					gtk_widget_hide(dm.win);
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

	if (event->type == GDK_KEY_PRESS)
	{
		if (((GdkEventKey *)event)->state & GDK_MOD1_MASK)
		{
			switch (((GdkEventKey *)event)->keyval)
			{
				case GDK_KEY_q:  /* close window (left hand) */
					gtk_widget_destroy(c->win);
					return TRUE;
				case GDK_KEY_d:  /* download manager (left hand) */
					gtk_widget_show_all(dm.win);
					return TRUE;
				case GDK_KEY_r:  /* reload (left hand) */
					webkit_web_view_reload_bypass_cache(WEBKIT_WEB_VIEW(
					                                    c->web_view));
					return TRUE;
				case GDK_KEY_k:  /* initiate search (BOTH hands) */
					gtk_entry_set_text(GTK_ENTRY(c->location), "/");
					gtk_editable_set_position(GTK_EDITABLE(c->location), -1);
					return TRUE;
			}
		}
		else
		{
			switch (((GdkEventKey *)event)->keyval)
			{
				case GDK_KEY_Return:
					gtk_widget_grab_focus(c->web_view);
					t = gtk_entry_get_text(GTK_ENTRY(c->location));
					if (t != NULL && t[0] == '/')
					{
						if (search_text != NULL)
							g_free(search_text);
						search_text = g_strdup(t + 1);  /* XXX whacky */
						search(c, 1);
					}
					else if (!keywords_try_search(WEBKIT_WEB_VIEW(c->web_view), t))
					{
						f = ensure_uri_scheme(t);
						if (show_all_requests)
							fprintf(stderr, "====> %s\n", f);
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
	}

	return FALSE;
}

gboolean
key_web_view(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	struct Client *c = (struct Client *)data;
	WebKitHitTestResultContext ht_context;
	WebKitHitTestResult *ht_result = NULL;
	gchar *ht_uri = NULL, *f;
	gfloat z;
	gboolean b;

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
					if (show_all_requests)
						fprintf(stderr, "====> %s\n", f);
					webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), f);
					g_free(f);
					return TRUE;
				case GDK_KEY_e:  /* new tab (left hand) */
					f = ensure_uri_scheme(home_uri);
					if (show_all_requests)
						fprintf(stderr, "====> %s\n", f);
					client_new(f);
					g_free(f);
					return TRUE;
				case GDK_KEY_r:  /* reload (left hand) */
					webkit_web_view_reload_bypass_cache(WEBKIT_WEB_VIEW(
					                                    c->web_view));
					return TRUE;
				case GDK_KEY_s:  /* toggle source view (left hand) */
					b = webkit_web_view_get_view_source_mode(WEBKIT_WEB_VIEW(
					                                         c->web_view));
					b = !b;
					webkit_web_view_set_view_source_mode(WEBKIT_WEB_VIEW(
					                                     c->web_view), b);
					webkit_web_view_reload(WEBKIT_WEB_VIEW(c->web_view));
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
					gtk_entry_set_text(GTK_ENTRY(c->location), "/");
					gtk_editable_set_position(GTK_EDITABLE(c->location), -1);
					return TRUE;
			}
		}
		else if (((GdkEventKey *)event)->keyval == GDK_KEY_Escape)
		{
			webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(c->web_view));
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(c->progress), 0);
		}
	}
	else if (event->type == GDK_BUTTON_PRESS)
	{
		switch (((GdkEventButton *)event)->button)
		{
			case 2:
				ht_result = webkit_web_view_get_hit_test_result(
				                                   WEBKIT_WEB_VIEW(c->web_view),
				                                       (GdkEventButton *)event);
				g_object_get(ht_result, "context", &ht_context, NULL);
				if (ht_context & WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK)
				{
					g_object_get(ht_result, "link-uri", &ht_uri, NULL);
					client_new(ht_uri);
					g_object_unref(ht_result);
					return TRUE;
				}
				g_object_unref(ht_result);
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
			switch (((GdkEventScroll *)event)->direction)
			{
				case GDK_SCROLL_UP:
					z = webkit_web_view_get_zoom_level(WEBKIT_WEB_VIEW(
					                                             c->web_view));
					z += 0.1;
					webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(c->web_view),
					                               z);
					return TRUE;
				case GDK_SCROLL_DOWN:
					z = webkit_web_view_get_zoom_level(WEBKIT_WEB_VIEW(
					                                             c->web_view));
					z -= 0.1;
					webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(c->web_view),
					                               z);
					return TRUE;
				default:
					break;
			}
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
					g_hash_table_insert(keywords, tokens[0], tokens[1]);
				else
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
	gchar *val = NULL, *uri = NULL;

	tokens = g_strsplit(t, " ", 2);
	if (tokens[0] != NULL && tokens[1] != NULL)
	{
		val = g_hash_table_lookup(keywords, tokens[0]);
		if (val != NULL)
		{
			uri = g_strdup_printf((gchar *)val, tokens[1]);
			if (show_all_requests)
				fprintf(stderr, "====> %s\n", uri);
			webkit_web_view_load_uri(web_view, uri);
			g_free(uri);
			ret = TRUE;
		}
	}
	g_strfreev(tokens);

	return ret;
}

gboolean
remote_msg(GIOChannel *channel, GIOCondition condition, gpointer data)
{
	gchar *uri = NULL;

	g_io_channel_read_line(channel, &uri, NULL, NULL, NULL);
	if (uri)
	{
		g_strstrip(uri);
		client_new(uri);
		g_free(uri);
	}
	return TRUE;
}

void
search(gpointer data, gint direction)
{
	struct Client *c = (struct Client *)data;

	if (search_text == NULL)
		return;

	webkit_web_view_search_text(WEBKIT_WEB_VIEW(c->web_view), search_text,
	                            FALSE, direction == 1, TRUE);
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
usage(void)
{
	fprintf(stderr, "Usage: "__NAME__" [OPTION]... [URI]...\n");
	exit(EXIT_FAILURE);
}


int
main(int argc, char **argv)
{
	int opt, i;

	gtk_init(&argc, &argv);

	grab_environment_configuration();

	while ((opt = getopt(argc, argv, "e:rCT")) != -1)
	{
		switch (opt)
		{
			case 'e':
				embed = atol(optarg);
				tabbed_automagic = FALSE;
				break;
			case 'r':
				show_all_requests = TRUE;
				break;
			case 'C':
				cooperative_instances = FALSE;
				break;
			case 'T':
				tabbed_automagic = FALSE;
				break;
			default:
				usage();
		}
	}

	adblock_load();
	keywords_load();
	history_load();
	cooperation_setup();
	downloadmanager_setup();

	if (tabbed_automagic && !(cooperative_instances && !cooperative_alone))
		embed = tabbed_launch();

	if (optind >= argc)
		client_new(home_uri);
	else
	{
		for (i = optind; i < argc; i++)
			client_new(argv[i]);
	}

	if (!cooperative_instances || cooperative_alone)
		gtk_main();
	exit(EXIT_SUCCESS);
}
