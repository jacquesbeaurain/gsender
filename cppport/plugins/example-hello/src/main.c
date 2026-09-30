/* Example Hello: the smallest useful plugin. Counts its launches in its own
 * storage and answers "hello" requests with the current work position. */

#include "gsender/plugin.h"
#include "gsender/runtime.h"

static int64_t g_launches = 0;
static int64_t g_events = 0;

int32_t gsender_plugin_init(void) {
    char buf[32];
    double stored = 0;
    if (gs_host_storage_get("launches", buf, sizeof buf) >= 0) {
        gs_str_t json;
        gs_str_init(&json);
        gs_str_append(&json, "{\"n\":");
        gs_str_append(&json, buf);
        gs_str_append(&json, "}");
        gs_json_number(json.data, "n", &stored);
        gs_str_free(&json);
    }
    g_launches = (int64_t)stored + 1;

    gs_str_t value;
    gs_str_init(&value);
    gs_str_append_int(&value, g_launches);
    gs_host_storage_set("launches", value.data);
    gs_str_free(&value);

    gs_host_log(GS_LOG_INFO, "Example Hello plugin initialized");
    return 0;
}

void gsender_plugin_shutdown(void) {
    gs_host_log(GS_LOG_INFO, "Example Hello plugin shutdown");
}

int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len) {
    (void)request_json;
    gs_str_t reply;
    gs_str_init(&reply);
    gs_str_append(&reply, "{\"ok\":true,\"message\":\"Hello from Wasm plugin!\",\"launches\":");
    gs_str_append_int(&reply, g_launches);
    gs_str_append(&reply, ",\"events\":");
    gs_str_append_int(&reply, g_events);
    gs_dro_coords_t wpos;
    if (gs_host_get_wpos(&wpos) == 0) {
        gs_str_append(&reply, ",\"wpos\":{\"x\":");
        gs_str_append_fixed(&reply, wpos.x, 3);
        gs_str_append(&reply, ",\"y\":");
        gs_str_append_fixed(&reply, wpos.y, 3);
        gs_str_append(&reply, ",\"z\":");
        gs_str_append_fixed(&reply, wpos.z, 3);
        gs_str_append(&reply, "}");
    }
    gs_str_append(&reply, "}");
    const int32_t n = gs_respond(response_buf, buf_len, reply.data);
    gs_str_free(&reply);
    return n;
}

void gsender_plugin_on_topic_event(const char* topic, const char* event_json) {
    (void)topic;
    (void)event_json;
    ++g_events;
}
