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
static void client_destroy(GtkWidget *obj, gpointer data);
static gboolean client_destroy_request(WebKitWebView *, gpointer);
static WebKitWebView *client_new(const gchar *uri);
static WebKitWebView *client_new_request(WebKitWebView *, WebKitWebFrame *,
                                         gpointer);
static void cooperation_setup(void);
static void changed_load_status(GObject *obj, GParamSpec *pspec,
                                gpointer data);
static void changed_title(GObject *, GParamSpec *, gpointer);
static void changed_uri(GObject *, GParamSpec *, gpointer);
static gboolean download_handle(WebKitWebView *, WebKitDownload *, gpointer);
static gboolean download_request(WebKitWebView *, WebKitWebFrame *,
                                 WebKitNetworkRequest *, gchar *,
                                 WebKitWebPolicyDecision *, gpointer);
static gchar *ensure_url_scheme(const gchar *);
static void grab_environment_configuration(void);
static void hover_web_view(WebKitWebView *, gchar *, gchar *, gpointer);
static gboolean key_location(GtkWidget *, GdkEvent *, gpointer);
static gboolean key_web_view(GtkWidget *, GdkEvent *, gpointer);
static gboolean remote_msg(GIOChannel *, GIOCondition, gpointer);
static void search(gpointer, gint);
static void scroll(GtkAdjustment *, gint, gdouble);
static Window tabbed_launch(void);
static void usage(void);


struct Client
{
	GtkWidget *win;
	GtkWidget *vbox;
	GtkWidget *location;
	GtkWidget *status;
	GtkWidget *scroll;
	GtkWidget *web_view;
};


static gchar *accepted_language = "en-US";
static GSList *adblock_patterns = NULL;
static gint clients = 0;
static gboolean cooperative_alone = TRUE;
static gboolean cooperative_instances = TRUE;
static int cooperative_pipe_fp = 0;
static gchar *download_dir = "/tmp";
static Window embed = 0;
static gchar *first_uri = NULL;
static gdouble global_zoom = 1.0;
static gboolean language_is_set = FALSE;
static gchar *search_text = NULL;
static gboolean show_all_requests = FALSE;
static gboolean tabbed_automagic = TRUE;


void
adblock(WebKitWebView *web_view, WebKitWebFrame *frame,
        WebKitWebResource *resource, WebKitNetworkRequest *request,
        WebKitNetworkResponse *response, gpointer data)
{
	GSList *it = adblock_patterns;
	const gchar *uri;

	(void)web_view;
	(void)frame;
	(void)resource;
	(void)response;
	(void)data;

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
	gchar *path = NULL;
	gchar *buf = NULL;

	path = g_build_filename(g_get_user_config_dir(), __NAME__, "adblock.black",
	                        NULL);
	channel = g_io_channel_new_file(path, "r", &err);
	if (channel != NULL)
	{
		while (g_io_channel_read_line(channel, &buf, NULL, NULL, NULL)
		       == G_IO_STATUS_NORMAL)
		{
			g_strstrip(buf);
			re = g_regex_new(buf,
			                 G_REGEX_CASELESS | G_REGEX_OPTIMIZE,
			                 G_REGEX_MATCH_PARTIAL, &err);
			if (err != NULL)
			{
				fprintf(stderr, __NAME__": Could not compile regex: %s\n", buf);
				g_error_free(err);
				err = NULL;
			}
			adblock_patterns = g_slist_append(adblock_patterns, re);

			g_free(buf);
		}
	}
	g_free(path);
}

void
client_destroy(GtkWidget *obj, gpointer data)
{
	struct Client *c = (struct Client *)data;

	(void)obj;
	(void)data;

	free(c);
	clients--;

	if (clients == 0)
		gtk_main_quit();
}

gboolean
client_destroy_request(WebKitWebView *web_view, gpointer data)
{
	struct Client *c = (struct Client *)data;

	(void)web_view;

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
	 * reproducable crashes with it enabled. I've seen bug reports from
	 * 2010 about this... WebKit crashes in libpixman, so maybe it's not
	 * a WebKit issue.
	 * Yeah, well. I'll turn it off for now. */
	/*webkit_web_view_set_full_content_zoom(WEBKIT_WEB_VIEW(c->web_view), TRUE);*/

	webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(c->web_view), global_zoom);
	g_signal_connect(G_OBJECT(c->web_view), "notify::title",
	                 G_CALLBACK(changed_title), c);
	g_signal_connect(G_OBJECT(c->web_view), "notify::uri",
	                 G_CALLBACK(changed_uri), c);
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
	                 G_CALLBACK(download_handle), NULL);
	g_signal_connect(G_OBJECT(c->web_view), "key-press-event",
	                 G_CALLBACK(key_web_view), c);
	g_signal_connect(G_OBJECT(c->web_view), "button-press-event",
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

	c->status = gtk_statusbar_new();
	gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(c->status), FALSE);

	c->vbox = gtk_vbox_new(FALSE, 2);
	gtk_box_pack_start(GTK_BOX(c->vbox), c->location, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->scroll);
	gtk_box_pack_end(GTK_BOX(c->vbox), c->status, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(c->win), c->vbox);

	gtk_widget_grab_focus(c->web_view);
	gtk_widget_show_all(c->win);

	if (uri != NULL)
	{
		f = ensure_url_scheme(uri);
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
	(void)web_view;
	(void)frame;
	(void)data;

	return client_new(NULL);
}

void
cooperation_setup(void)
{
	GIOChannel *towatch;
	gchar *fifopath;

	fifopath = g_build_filename(g_get_user_runtime_dir(), __NAME__".fifo", NULL);

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
changed_load_status(GObject *obj, GParamSpec *pspec, gpointer data)
{
	struct Client *c = (struct Client *)data;

	(void)obj;
	(void)pspec;

	if (webkit_web_view_get_load_status(WEBKIT_WEB_VIEW(c->web_view))
	    == WEBKIT_LOAD_FINISHED)
	{
		gtk_statusbar_pop(GTK_STATUSBAR(c->status), 1);
		gtk_statusbar_push(GTK_STATUSBAR(c->status), 1, "Finished.");
	}
	else
	{
		gtk_statusbar_pop(GTK_STATUSBAR(c->status), 1);
		gtk_statusbar_push(GTK_STATUSBAR(c->status), 1, "Loading...");
	}
}

void
changed_title(GObject *obj, GParamSpec *pspec, gpointer data)
{
	const gchar *t;
	struct Client *c = (struct Client *)data;

	(void)obj;
	(void)pspec;

	t = webkit_web_view_get_title(WEBKIT_WEB_VIEW(c->web_view));
	gtk_window_set_title(GTK_WINDOW(c->win), (t == NULL ? __NAME__ : t));
}

void
changed_uri(GObject *obj, GParamSpec *pspec, gpointer data)
{
	const gchar *t;
	struct Client *c = (struct Client *)data;

	(void)obj;
	(void)pspec;

	t = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));
	gtk_entry_set_text(GTK_ENTRY(c->location), (t == NULL ? __NAME__ : t));
}

gboolean
download_handle(WebKitWebView *web_view, WebKitDownload *download, gpointer data)
{
	gchar *path, *path2 = NULL, *uri;
	gboolean ret;
	int suffix = 1;

	(void)web_view;
	(void)data;

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
	}

	g_free(path);
	g_free(path2);

	return ret;
}

gboolean
download_request(WebKitWebView *web_view, WebKitWebFrame *frame,
                 WebKitNetworkRequest *request, gchar *mime_type,
                 WebKitWebPolicyDecision *policy_decision, gpointer data)
{
	(void)frame;
	(void)request;
	(void)data;

	if (!webkit_web_view_can_show_mime_type(web_view, mime_type))
	{
		webkit_web_policy_decision_download(policy_decision);
		return TRUE;
	}
	return FALSE;
}

gchar *
ensure_url_scheme(const gchar *t)
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

	e = g_getenv(__NAME_UPPERCASE__"_ZOOM");
	if (e != NULL)
		global_zoom = atof(e);
}

void
hover_web_view(WebKitWebView *web_view, gchar *title, gchar *uri, gpointer data)
{
	struct Client *c = (struct Client *)data;

	(void)web_view;
	(void)title;

	gtk_statusbar_pop(GTK_STATUSBAR(c->status), 0);
	if (uri != NULL)
		gtk_statusbar_push(GTK_STATUSBAR(c->status), 0, uri);
}

gboolean
key_location(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	struct Client *c = (struct Client *)data;
	const gchar *t;
	gchar *f;

	(void)widget;

	if (event->type == GDK_KEY_PRESS)
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
				else
				{
					f = ensure_url_scheme(t);
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

	return FALSE;
}

gboolean
key_web_view(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	struct Client *c = (struct Client *)data;
	WebKitHitTestResultContext ht_context;
	WebKitHitTestResult *ht_result = NULL;
	gchar *ht_uri = NULL, *f;

	(void)widget;

	if (event->type == GDK_KEY_PRESS)
	{
		if (((GdkEventKey *)event)->state & GDK_CONTROL_MASK)
		{
			switch (((GdkEventKey *)event)->keyval)
			{
				case GDK_KEY_o:
					gtk_widget_grab_focus(c->location);
					return TRUE;
				case GDK_KEY_h:
					scroll(gtk_scrolled_window_get_hadjustment(
					       GTK_SCROLLED_WINDOW(c->scroll)), 0, -1);
					return TRUE;
				case GDK_KEY_j:
					scroll(gtk_scrolled_window_get_vadjustment(
					       GTK_SCROLLED_WINDOW(c->scroll)), 0, 1);
					return TRUE;
				case GDK_KEY_k:
					scroll(gtk_scrolled_window_get_vadjustment(
					       GTK_SCROLLED_WINDOW(c->scroll)), 0, -1);
					return TRUE;
				case GDK_KEY_l:
					scroll(gtk_scrolled_window_get_hadjustment(
					       GTK_SCROLLED_WINDOW(c->scroll)), 0, 1);
					return TRUE;
				case GDK_KEY_f:
					scroll(gtk_scrolled_window_get_vadjustment(
					       GTK_SCROLLED_WINDOW(c->scroll)), 1, 0.5);
					return TRUE;
				case GDK_KEY_b:
					scroll(gtk_scrolled_window_get_vadjustment(
					       GTK_SCROLLED_WINDOW(c->scroll)), 1, -0.5);
					return TRUE;
				case GDK_KEY_n:
					search(c, 1);
					return TRUE;
				case GDK_KEY_p:
					search(c, -1);
					return TRUE;
				case GDK_KEY_g:
					f = ensure_url_scheme(first_uri);
					if (show_all_requests)
						fprintf(stderr, "====> %s\n", f);
					webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), f);
					g_free(f);
					return TRUE;
			}
		}
		else if (((GdkEventKey *)event)->keyval == GDK_KEY_Escape)
		{
			webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(c->web_view));
			gtk_statusbar_pop(GTK_STATUSBAR(c->status), 1);
			gtk_statusbar_push(GTK_STATUSBAR(c->status), 1, "Aborted.");
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
				return FALSE;
			case 8:
				webkit_web_view_go_back(WEBKIT_WEB_VIEW(c->web_view));
				return TRUE;
			case 9:
				webkit_web_view_go_forward(WEBKIT_WEB_VIEW(c->web_view));
				return TRUE;
		}
	}

	return FALSE;
}

gboolean
remote_msg(GIOChannel *channel, GIOCondition condition, gpointer data)
{
	gchar *uri = NULL;

	(void)condition;
	(void)data;

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

void
scroll(GtkAdjustment *a, gint step_type, gdouble factor)
{
	gdouble new, lower, upper, step;
	lower = gtk_adjustment_get_lower(a);
	upper = gtk_adjustment_get_upper(a) - gtk_adjustment_get_page_size(a) + lower;
	if (step_type == 0)
		step = gtk_adjustment_get_step_increment(a);
	else
		step = gtk_adjustment_get_page_increment(a);
	new = gtk_adjustment_get_value(a) + factor * step;
	new = new < lower ? lower : new;
	new = new > upper ? upper : new;
	gtk_adjustment_set_value(a, new);
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
	g_io_channel_read_line(tabbed_stdout_channel, &output, NULL, NULL, NULL);
	if (output == NULL)
	{
		fprintf(stderr, __NAME__": Could not read XID from tabbed\n");
		return 0;
	}

	g_io_channel_shutdown(tabbed_stdout_channel, FALSE, NULL);

	g_strstrip(output);
	plug_into = strtol(output, NULL, 16);
	g_free(output);
	return plug_into;
}

void
usage(void)
{
	fprintf(stderr, "Usage: "__NAME__" [OPTION]... <URI>...\n");
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

	if (optind >= argc)
		usage();

	adblock_load();
	cooperation_setup();

	if (tabbed_automagic && !(cooperative_instances && !cooperative_alone))
		embed = tabbed_launch();

	first_uri = g_strdup(argv[optind]);
	for (i = optind; i < argc; i++)
		client_new(argv[i]);
	if (!cooperative_instances || cooperative_alone)
		gtk_main();
	exit(EXIT_SUCCESS);
}
