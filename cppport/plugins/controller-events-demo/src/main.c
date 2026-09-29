#include "gsender/plugin.h"
#include <string.h>
#include <stdio.h>

static int g_event_count = 0;

int32_t gsender_plugin_init(void) {
    gs_host_log(GS_LOG_INFO, "Controller Events Demo plugin initialized");
    return 0;
}

void gsender_plugin_shutdown(void) {
    gs_host_log(GS_LOG_INFO, "Controller Events Demo plugin shutdown");
}

int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len) {
    char reply[128];
    snprintf(reply, sizeof(reply), "{\"ok\":true,\"eventCount\":%d}", g_event_count);
    int len = (int)strlen(reply);
    if (len < buf_len) {
        memcpy(response_buf, reply, len + 1);
        return len;
    }
    return -1;
}

void gsender_plugin_on_topic_event(const char* topic, const char* event_json) {
    g_event_count++;
    if (strcmp(topic, "workspace") == 0) {
        gs_dro_coords_t coords;
        if (gs_host_get_wpos(&coords) == 0) {
            char log_msg[128];
            snprintf(log_msg, sizeof(log_msg), "Workspace WPOS: X=%.3f Y=%.3f Z=%.3f", coords.x, coords.y, coords.z);
            gs_host_log(GS_LOG_DEBUG, log_msg);
        }
    }
}
