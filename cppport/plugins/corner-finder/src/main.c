#include "gsender/plugin.h"
#include <string.h>
#include <stdio.h>

int32_t gsender_plugin_init(void) {
    gs_host_log(GS_LOG_INFO, "Corner Finder plugin initialized");
    return 0;
}

void gsender_plugin_shutdown(void) {
    gs_host_log(GS_LOG_INFO, "Corner Finder plugin shutdown");
}

int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len) {
    const char* reply = "{\"ok\":true,\"corners\":{\"bl\":[0,0],\"tl\":[0,100],\"tr\":[150,100],\"br\":[150,0]}}";
    int len = (int)strlen(reply);
    if (len < buf_len) {
        memcpy(response_buf, reply, len + 1);
        return len;
    }
    return -1;
}

void gsender_plugin_on_topic_event(const char* topic, const char* event_json) {
    (void)topic;
    (void)event_json;
}
