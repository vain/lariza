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


#define DOWNLOAD_DIR "/tmp/tmp"
#define LANGUAGE "en-US"


static void adblock(WebKitWebView *, WebKitWebFrame *, WebKitWebResource *,
                    WebKitNetworkRequest *, WebKitNetworkResponse *, gpointer);
static void destroy_client(GtkWidget *, gpointer);
static gboolean do_download(WebKitWebView *, WebKitDownload *, gpointer);
static gboolean download_request(WebKitWebView *, WebKitWebFrame *,
                                 WebKitNetworkRequest *, gchar *,
                                 WebKitWebPolicyDecision *, gpointer);
static Window launch_tabbed(void);
static void load_adblock(void);
static void load_status_changed(GObject *obj, GParamSpec *pspec,
                                gpointer data);
static gboolean location_key(GtkWidget *, GdkEvent *, gpointer);
static void new_client(const gchar *uri);
static gboolean new_client_request(WebKitWebView *, WebKitWebFrame *,
                                   WebKitNetworkRequest *,
                                   WebKitWebNavigationAction *,
                                   WebKitWebPolicyDecision *, gpointer);
static gboolean remote_msg(GIOChannel *, GIOCondition, gpointer);
static void search(gpointer, gint);
static void setup_cooperation(void);
static void scroll(GtkAdjustment *, gint, gdouble);
static void title_changed(GObject *, GParamSpec *, gpointer);
static void uri_changed(GObject *, GParamSpec *, gpointer);
static void usage(void);
static void web_view_hover(WebKitWebView *, gchar *, gchar *, gpointer);
static gboolean web_view_key(GtkWidget *, GdkEvent *, gpointer);


static Window embed = 0;
static gint clients = 0;
static gdouble global_zoom = 1.0;
static gchar *search_text = NULL;
static gchar *first_uri = NULL;
static gboolean show_all_requests = FALSE;
static GSList *adblock_patterns = NULL;
static gboolean cooperative_instances = TRUE;
static int cooperative_pipe_fp = 0;
static gboolean alone = TRUE;
static gboolean automagic_tabbed = TRUE;
static gboolean language_set = FALSE;


struct Client
{
	GtkWidget *win;
	GtkWidget *vbox;
	GtkWidget *location;
	GtkWidget *status;
	GtkWidget *scroll;
	GtkWidget *web_view;
};


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
		fprintf(stderr, "-> %s\n", uri);

	while (it)
	{
		if (g_regex_match((GRegex *)(it->data), uri, 0, NULL))
		{
			webkit_network_request_set_uri(request, "about:blank");
			if (show_all_requests)
				fprintf(stderr, "\tBLOCKED!\n");
			return;
		}
		it = g_slist_next(it);
	}
}

void
destroy_client(GtkWidget *obj, gpointer data)
{
	struct Client *c = (struct Client *)data;

	(void)obj;

	webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(c->web_view));
	gtk_widget_destroy(c->web_view);
	gtk_widget_destroy(c->scroll);
	gtk_widget_destroy(c->status);
	gtk_widget_destroy(c->location);
	gtk_widget_destroy(c->vbox);
	gtk_widget_destroy(c->win);
	free(c);

	clients--;
	if (clients == 0)
		gtk_main_quit();
}

gboolean
do_download(WebKitWebView *web_view, WebKitDownload *download, gpointer data)
{
	const gchar *uri;
	char id[16] = "";
	gint ret;

	(void)web_view;
	(void)data;

	uri = webkit_download_get_uri(download);
	if (fork() == 0)
	{
		chdir(DOWNLOAD_DIR);
		if (embed == 0)
			ret = execlp("xterm", "xterm", "-hold", "-e", "wget", uri, NULL);
		else
		{
			if (snprintf(id, 16, "%ld", embed) >= 16)
			{
				fprintf(stderr, __NAME__": id for xterm embed truncated!\n");
				exit(EXIT_FAILURE);
			}
			ret = execlp("xterm", "xterm", "-hold", "-into", id, "-e", "wget",
			             uri, NULL);
		}

		if (ret == -1)
		{
			fprintf(stderr, __NAME__": exec'ing xterm for download");
			perror(" failed");
			exit(EXIT_FAILURE);
		}
	}

	return FALSE;
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

gboolean
location_key(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	struct Client *c = (struct Client *)data;
	const gchar *t;

	(void)widget;

	if (event->type == GDK_KEY_PRESS)
	{
		if (((GdkEventKey *)event)->keyval == GDK_KEY_Return)
		{
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
				webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), t);
			return TRUE;
		}
	}

	return FALSE;
}

void
new_client(const gchar *uri)
{
	if (cooperative_instances && !alone)
	{
		write(cooperative_pipe_fp, uri, strlen(uri));
		write(cooperative_pipe_fp, "\n", 1);
		return;
	}

	struct Client *c = malloc(sizeof(struct Client));
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
		c->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	/* When using Gtk2, it only shows a white area when run in suckless'
	 * tabbed. It appears we need to set a default window size for this
	 * to work. This is not needed when using Gtk3. */
	gtk_window_set_default_size(GTK_WINDOW(c->win), 1024, 768);

	g_signal_connect(G_OBJECT(c->win), "destroy",
	                 G_CALLBACK(destroy_client), c);
	gtk_window_set_title(GTK_WINDOW(c->win), __NAME__);

	c->web_view = webkit_web_view_new();
	webkit_web_view_set_full_content_zoom(WEBKIT_WEB_VIEW(c->web_view), TRUE);
	webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(c->web_view), global_zoom);
	g_signal_connect(G_OBJECT(c->web_view), "notify::title",
	                 G_CALLBACK(title_changed), c);
	g_signal_connect(G_OBJECT(c->web_view), "notify::uri",
	                 G_CALLBACK(uri_changed), c);
	g_signal_connect(G_OBJECT(c->web_view), "notify::load-status",
	                 G_CALLBACK(load_status_changed), c);
	g_signal_connect(G_OBJECT(c->web_view),
	                 "new-window-policy-decision-requested",
	                 G_CALLBACK(new_client_request), NULL);
	g_signal_connect(G_OBJECT(c->web_view),
	                 "mime-type-policy-decision-requested",
	                 G_CALLBACK(download_request), NULL);
	g_signal_connect(G_OBJECT(c->web_view), "download-requested",
	                 G_CALLBACK(do_download), NULL);
	g_signal_connect(G_OBJECT(c->web_view), "key-press-event",
	                 G_CALLBACK(web_view_key), c);
	g_signal_connect(G_OBJECT(c->web_view), "hovering-over-link",
	                 G_CALLBACK(web_view_hover), c);
	g_signal_connect(G_OBJECT(c->web_view), "resource-request-starting",
	                 G_CALLBACK(adblock), NULL);

	if (!language_set)
	{
		g_object_set(webkit_get_default_session(), "accept-language", LANGUAGE,
		             NULL);
		language_set = TRUE;
	}

	c->scroll = gtk_scrolled_window_new(NULL, NULL);

	gtk_container_add(GTK_CONTAINER(c->scroll), c->web_view);

	c->location = gtk_entry_new();
	g_signal_connect(G_OBJECT(c->location), "key-press-event",
	                 G_CALLBACK(location_key), c);

	c->status = gtk_statusbar_new();
	gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(c->status), FALSE);

	c->vbox = gtk_vbox_new(FALSE, 2);
	gtk_box_pack_start(GTK_BOX(c->vbox), c->location, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->scroll);
	gtk_box_pack_end(GTK_BOX(c->vbox), c->status, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(c->win), c->vbox);

	gtk_widget_grab_focus(c->web_view);
	gtk_widget_show_all(c->win);

	webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view), uri);

	clients++;
}

gboolean
new_client_request(WebKitWebView *web_view, WebKitWebFrame *frame,
                   WebKitNetworkRequest *request,
                   WebKitWebNavigationAction *navigation_action,
                   WebKitWebPolicyDecision *policy_decision, gpointer user_data)
{
	(void)web_view;
	(void)frame;
	(void)navigation_action;
	(void)user_data;

	webkit_web_policy_decision_ignore(policy_decision);
	new_client(webkit_network_request_get_uri(request));

	return TRUE;
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
		new_client(uri);
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
setup_cooperation(void)
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
			alone = FALSE;
	}

	g_free(fifopath);
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

void
title_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
	const gchar *t;
	struct Client *c = (struct Client *)data;

	(void)obj;
	(void)pspec;

	t = webkit_web_view_get_title(WEBKIT_WEB_VIEW(c->web_view));
	gtk_window_set_title(GTK_WINDOW(c->win), (t == NULL ? __NAME__ : t));
}

void
uri_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
	const gchar *t;
	struct Client *c = (struct Client *)data;

	(void)obj;
	(void)pspec;

	t = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(c->web_view));
	gtk_entry_set_text(GTK_ENTRY(c->location), (t == NULL ? __NAME__ : t));
}

void
usage(void)
{
	fprintf(stderr, "Usage: "__NAME__" [OPTION]... <URI>...\n");
	exit(EXIT_FAILURE);
}

Window
launch_tabbed(void)
{
	gint tabbed_stdout;
	GIOChannel *tabbed_stdout_channel;
	GError *err = NULL;
	gchar *output = NULL;
	char *argv[] = { "tabbed", "-c", "-d", NULL };
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
load_adblock(void)
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
load_status_changed(GObject *obj, GParamSpec *pspec, gpointer data)
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
web_view_hover(WebKitWebView *web_view, gchar *title, gchar *uri,
                   gpointer data)
{
	struct Client *c = (struct Client *)data;

	(void)web_view;
	(void)title;

	gtk_statusbar_pop(GTK_STATUSBAR(c->status), 0);
	if (uri != NULL)
		gtk_statusbar_push(GTK_STATUSBAR(c->status), 0, uri);
}

gboolean
web_view_key(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	struct Client *c = (struct Client *)data;

	(void)widget;

	if (event->type == GDK_KEY_PRESS)
	{
		if (((GdkEventKey *)event)->state & GDK_CONTROL_MASK)
		{
			if (((GdkEventKey *)event)->keyval == GDK_KEY_o)
			{
				gtk_widget_grab_focus(c->location);
				return TRUE;
			}
			else if (((GdkEventKey *)event)->keyval == GDK_KEY_h)
			{
				scroll(gtk_scrolled_window_get_hadjustment(
				       GTK_SCROLLED_WINDOW(c->scroll)), 0, -1);
				return TRUE;
			}
			else if (((GdkEventKey *)event)->keyval == GDK_KEY_j)
			{
				scroll(gtk_scrolled_window_get_vadjustment(
				       GTK_SCROLLED_WINDOW(c->scroll)), 0, 1);
				return TRUE;
			}
			else if (((GdkEventKey *)event)->keyval == GDK_KEY_k)
			{
				scroll(gtk_scrolled_window_get_vadjustment(
				       GTK_SCROLLED_WINDOW(c->scroll)), 0, -1);
				return TRUE;
			}
			else if (((GdkEventKey *)event)->keyval == GDK_KEY_l)
			{
				scroll(gtk_scrolled_window_get_hadjustment(
				       GTK_SCROLLED_WINDOW(c->scroll)), 0, 1);
				return TRUE;
			}
			else if (((GdkEventKey *)event)->keyval == GDK_KEY_f)
			{
				scroll(gtk_scrolled_window_get_vadjustment(
				       GTK_SCROLLED_WINDOW(c->scroll)), 1, 0.5);
				return TRUE;
			}
			else if (((GdkEventKey *)event)->keyval == GDK_KEY_b)
			{
				scroll(gtk_scrolled_window_get_vadjustment(
				       GTK_SCROLLED_WINDOW(c->scroll)), 1, -0.5);
				return TRUE;
			}
			else if (((GdkEventKey *)event)->keyval == GDK_KEY_n)
			{
				search(c, 1);
				return TRUE;
			}
			else if (((GdkEventKey *)event)->keyval == GDK_KEY_p)
			{
				search(c, -1);
				return TRUE;
			}
			else if (((GdkEventKey *)event)->keyval == GDK_KEY_g)
			{
				webkit_web_view_load_uri(WEBKIT_WEB_VIEW(c->web_view),
				                         first_uri);
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

	return FALSE;
}

int
main(int argc, char **argv)
{
	int opt, i;

	gtk_init(&argc, &argv);

	while ((opt = getopt(argc, argv, "z:e:rCT")) != -1)
	{
		switch (opt)
		{
			case 'z':
				global_zoom = atof(optarg);
				break;
			case 'e':
				embed = atol(optarg);
				automagic_tabbed = FALSE;
				break;
			case 'r':
				show_all_requests = TRUE;
				break;
			case 'C':
				cooperative_instances = FALSE;
				break;
			case 'T':
				automagic_tabbed = FALSE;
				break;
			default:
				usage();
		}
	}

	if (optind >= argc)
		usage();

	load_adblock();
	setup_cooperation();

	if (automagic_tabbed && !(cooperative_instances && !alone))
		embed = launch_tabbed();

	first_uri = g_strdup(argv[optind]);
	for (i = optind; i < argc; i++)
		new_client(argv[i]);
	if (!cooperative_instances || alone)
		gtk_main();
	exit(EXIT_SUCCESS);
}
