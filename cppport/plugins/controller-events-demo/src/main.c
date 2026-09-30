/* Controller Events Demo: counts the controller and workspace events it is
 * sent and remembers the last machine state, for its UI to show. */

#include "gsender/plugin.h"
#include "gsender/runtime.h"

static int64_t g_controller_events = 0;
static int64_t g_workspace_events = 0;
static char g_last_state[32] = "";

int32_t gsender_plugin_init(void) {
    gs_host_log(GS_LOG_INFO, "Controller Events Demo plugin initialized");
    return 0;
}

void gsender_plugin_shutdown(void) {}

int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len) {
    (void)request_json;
    gs_str_t reply;
    gs_str_init(&reply);
    gs_str_append(&reply, "{\"ok\":true,\"eventCount\":");
    gs_str_append_int(&reply, g_controller_events + g_workspace_events);
    gs_str_append(&reply, ",\"controllerEvents\":");
    gs_str_append_int(&reply, g_controller_events);
    gs_str_append(&reply, ",\"workspaceEvents\":");
    gs_str_append_int(&reply, g_workspace_events);
    gs_str_append(&reply, ",\"lastState\":");
    gs_str_append_json_string(&reply, g_last_state);
    gs_str_append(&reply, "}");
    const int32_t n = gs_respond(response_buf, buf_len, reply.data);
    gs_str_free(&reply);
    return n;
}

void gsender_plugin_on_topic_event(const char* topic, const char* event_json) {
    if (strcmp(topic, "controller") == 0) {
        ++g_controller_events;
        gs_json_string(event_json, "activeState", g_last_state, sizeof g_last_state);
    } else if (strcmp(topic, "workspace") == 0) {
        ++g_workspace_events;
    }
}
