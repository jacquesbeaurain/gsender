#include "gsender/plugin.h"
#include <string.h>
#include <stdio.h>

int32_t gsender_plugin_init(void) {
    gs_host_log(GS_LOG_INFO, "Example Hello plugin initialized");
    return 0;
}

void gsender_plugin_shutdown(void) {
    gs_host_log(GS_LOG_INFO, "Example Hello plugin shutdown");
}

int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len) {
    /* Handle simple RPC request */
    const char* reply = "{\"ok\":true,\"message\":\"Hello from Wasm plugin!\"}";
    int len = (int)strlen(reply);
    if (len < buf_len) {
        memcpy(response_buf, reply, len + 1);
        return len;
    }
    return -1;
}

void gsender_plugin_on_topic_event(const char* topic, const char* event_json) {
    /* Respond to topic stream */
    if (strcmp(topic, "workspace") == 0) {
        gs_dro_coords_t dro;
        if (gs_host_get_wpos(&dro) == 0) {
            /* Example: inspect coordinates */
        }
    }
}
