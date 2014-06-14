#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <webkit/webkit.h>

static void sn_new_window(char *uri);
static void sn_title_changed(GObject *, GParamSpec *, gpointer);


double global_zoom = 1.0;


void
sn_new_window(char *uri)
{
	GtkWidget *win;
	GtkWidget *web_view;
	GtkWidget *scroll;

	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(G_OBJECT(win), "delete_event", G_CALLBACK(gtk_main_quit),
	                 NULL);
	g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(gtk_main_quit),
	                 NULL);
	gtk_window_set_has_resize_grip(GTK_WINDOW(win), FALSE);
	gtk_window_set_title(GTK_WINDOW(win), "sn");

	web_view = webkit_web_view_new();
	webkit_web_view_set_full_content_zoom(WEBKIT_WEB_VIEW(web_view), TRUE);
	webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(web_view), global_zoom);
	g_signal_connect(G_OBJECT(web_view), "notify::title",
	                 G_CALLBACK(sn_title_changed), win);

	scroll = gtk_scrolled_window_new(NULL, NULL);

	gtk_container_add(GTK_CONTAINER(scroll), web_view);
	gtk_container_add(GTK_CONTAINER(win), scroll);

	gtk_widget_show_all(win);

	webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), uri);
}

void
sn_title_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
	const gchar *t;
	WebKitWebView *view = WEBKIT_WEB_VIEW(obj);
	GtkWindow *win = GTK_WINDOW(data);

	(void)pspec;

	t = webkit_web_view_get_title(view);
	gtk_window_set_title(win, (t == NULL ? "sn" : t));
}

int
main(int argc, char **argv)
{
	int opt;

	gtk_init(&argc, &argv);

	while ((opt = getopt(argc, argv, "z:")) != -1)
	{
		switch (opt)
		{
			case 'z':
				global_zoom = atof(optarg);
				break;
		}
	}

	if (optind >= argc)
	{
		fprintf(stderr, "Usage: sn [OPTIONS] <URI>\n");
		exit(EXIT_FAILURE);
	}

	sn_new_window(argv[optind]);
	gtk_main();
	exit(EXIT_SUCCESS);
}
