/* Parser Demo: the host matches the parsers declared in the manifest against
 * the firmware's replies and sends each match on the "parser" topic; this
 * plugin counts them and keeps the last one for its UI. */

#include "gsender/plugin.h"
#include "gsender/runtime.h"

static int64_t g_matches = 0;
static char g_last_parser[64] = "";
static char g_last_line[256] = "";

int32_t gsender_plugin_init(void) {
    gs_host_log(GS_LOG_INFO, "Parser Demo plugin initialized");
    return 0;
}

void gsender_plugin_shutdown(void) {}

int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len) {
    (void)request_json;
    gs_str_t reply;
    gs_str_init(&reply);
    gs_str_append(&reply, "{\"ok\":true,\"matches\":");
    gs_str_append_int(&reply, g_matches);
    gs_str_append(&reply, ",\"lastParser\":");
    gs_str_append_json_string(&reply, g_last_parser);
    gs_str_append(&reply, ",\"lastLine\":");
    gs_str_append_json_string(&reply, g_last_line);
    gs_str_append(&reply, "}");
    const int32_t n = gs_respond(response_buf, buf_len, reply.data);
    gs_str_free(&reply);
    return n;
}

void gsender_plugin_on_topic_event(const char* topic, const char* event_json) {
    if (strcmp(topic, "parser") != 0) return;
    ++g_matches;
    gs_json_string(event_json, "parserId", g_last_parser, sizeof g_last_parser);
    gs_json_string(event_json, "line", g_last_line, sizeof g_last_line);
}
