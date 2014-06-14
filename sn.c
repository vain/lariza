#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <webkit/webkit.h>


struct sn_gui
{
	GtkWidget *window;
	GtkWidget *web_view;
	GtkWidget *scroll;
};

struct sn_app
{
	struct sn_gui gui;
	double global_zoom;
};

static void sn_create_gui(struct sn_app *app);
static void sn_init_defaults(struct sn_app *app);
static void sn_title_changed(GObject *obj, GParamSpec *pspec, gpointer app);


void
sn_create_gui(struct sn_app *app)
{
	app->gui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(G_OBJECT(app->gui.window), "delete_event",
	                 G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(G_OBJECT(app->gui.window), "destroy",
	                 G_CALLBACK(gtk_main_quit), NULL);
	gtk_window_set_has_resize_grip(GTK_WINDOW(app->gui.window), FALSE);
	gtk_window_set_title(GTK_WINDOW(app->gui.window), "sn");

	app->gui.web_view = webkit_web_view_new();
	webkit_web_view_set_full_content_zoom(WEBKIT_WEB_VIEW(app->gui.web_view),
	                                      TRUE);
	webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(app->gui.web_view),
	                               app->global_zoom);
	g_signal_connect(G_OBJECT(app->gui.web_view), "notify::title",
	                 G_CALLBACK(sn_title_changed), app);

	app->gui.scroll = gtk_scrolled_window_new(NULL, NULL);

	gtk_container_add(GTK_CONTAINER(app->gui.scroll), app->gui.web_view);
	gtk_container_add(GTK_CONTAINER(app->gui.window), app->gui.scroll);

	gtk_widget_show_all(app->gui.window);
}

void
sn_init_defaults(struct sn_app *app)
{
	app->global_zoom = 1.0;
}

void
sn_title_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
	const gchar *t;
	WebKitWebView *view = WEBKIT_WEB_VIEW(obj);
	struct sn_app *app = data;

	(void)pspec;

	t = webkit_web_view_get_title(view);
	gtk_window_set_title(GTK_WINDOW(app->gui.window), (t == NULL ? "sn" : t));
}

int
main(int argc, char **argv)
{
	int opt;
	struct sn_app app;

	gtk_init(&argc, &argv);
	sn_init_defaults(&app);

	while ((opt = getopt(argc, argv, "z:")) != -1)
	{
		switch (opt)
		{
			case 'z':
				app.global_zoom = atof(optarg);
				break;
		}
	}

	if (optind >= argc)
	{
		fprintf(stderr, "Usage: sn [OPTIONS] <URI>\n");
		exit(EXIT_FAILURE);
	}

	sn_create_gui(&app);
	webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app.gui.web_view), argv[optind]);
	gtk_main();
	exit(EXIT_SUCCESS);
}
