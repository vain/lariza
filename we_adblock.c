#include <stdio.h>

#include <glib.h>
#include <webkit2/webkit-web-extension.h>


static GSList *adblock_patterns = NULL;


static void
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

static gboolean
web_page_send_request(WebKitWebPage *web_page, WebKitURIRequest *request,
                      WebKitURIResponse *redirected_response, gpointer user_data)
{
    GSList *it = adblock_patterns;
    const gchar *uri;

    uri = webkit_uri_request_get_uri(request);

    while (it)
    {
        if (g_regex_match((GRegex *)(it->data), uri, 0, NULL))
            return TRUE;
        it = g_slist_next(it);
    }

    return FALSE;
}

static void
web_page_created_callback(WebKitWebExtension *extension, WebKitWebPage *web_page,
                          gpointer user_data)
{
    g_signal_connect_object(web_page, "send-request",
                            G_CALLBACK(web_page_send_request), NULL, 0);
}

G_MODULE_EXPORT void
webkit_web_extension_initialize(WebKitWebExtension *extension)
{
    adblock_load();
    g_signal_connect(extension, "page-created",
                     G_CALLBACK(web_page_created_callback), NULL);
}
