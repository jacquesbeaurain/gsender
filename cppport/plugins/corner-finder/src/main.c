/* Corner Finder: marks the four corners of a stock rectangle on the
 * visualizer and can snap the camera to the top view.
 *
 * Requests: {"action":"mark","x":0,"y":0,"width":150,"height":100}
 *           {"action":"clear"}   {"action":"top"} */

#include "gsender/plugin.h"
#include "gsender/runtime.h"

static char g_result[256];

static void marker(gs_str_t* s, const char* id, const char* label, double x, double y, int first) {
    if (!first) gs_str_append(s, ",");
    gs_str_append(s, "{\"id\":\"");
    gs_str_append(s, id);
    gs_str_append(s, "\",\"x\":");
    gs_str_append_fixed(s, x, 3);
    gs_str_append(s, ",\"y\":");
    gs_str_append_fixed(s, y, 3);
    gs_str_append(s, ",\"shape\":\"cross\",\"color\":\"#22c55e\",\"label\":\"");
    gs_str_append(s, label);
    gs_str_append(s, "\"}");
}

int32_t gsender_plugin_init(void) {
    gs_host_log(GS_LOG_INFO, "Corner Finder plugin initialized");
    return 0;
}

void gsender_plugin_shutdown(void) {}

int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len) {
    char action[16] = "mark";
    gs_json_string(request_json, "action", action, sizeof action);

    if (strcmp(action, "top") == 0) {
        const int32_t n = gs_host_request("viewer:camera:set", "{\"view\":\"top\"}", g_result, sizeof g_result);
        return gs_respond(response_buf, buf_len, n > 0 ? g_result : "{\"ok\":false,\"error\":\"No reply\"}");
    }

    double x = 0, y = 0, w = 150, h = 100;
    gs_json_number(request_json, "x", &x);
    gs_json_number(request_json, "y", &y);
    gs_json_number(request_json, "width", &w);
    gs_json_number(request_json, "height", &h);

    gs_str_t payload;
    gs_str_init(&payload);
    gs_str_append(&payload, "{\"markers\":[");
    if (strcmp(action, "clear") != 0) {
        marker(&payload, "bl", "BL", x, y, 1);
        marker(&payload, "tl", "TL", x, y + h, 0);
        marker(&payload, "tr", "TR", x + w, y + h, 0);
        marker(&payload, "br", "BR", x + w, y, 0);
    }
    gs_str_append(&payload, "]}");
    const int32_t n = gs_host_request("viewer:overlay:set", payload.data, g_result, sizeof g_result);
    gs_str_free(&payload);
    return gs_respond(response_buf, buf_len, n > 0 ? g_result : "{\"ok\":false,\"error\":\"No reply\"}");
}

void gsender_plugin_on_topic_event(const char* topic, const char* event_json) {
    (void)topic;
    (void)event_json;
}
