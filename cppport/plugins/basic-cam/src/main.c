#include "gsender/plugin.h"
#include <string.h>
#include <stdio.h>

int32_t gsender_plugin_init(void) {
    gs_host_log(GS_LOG_INFO, "Basic CAM plugin initialized");
    return 0;
}

void gsender_plugin_shutdown(void) {
    gs_host_log(GS_LOG_INFO, "Basic CAM plugin shutdown");
}

int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len) {
    const char* gcode = "G21 G90\nG0 Z5.000\nG0 X0 Y0\nG1 Z-1.000 F300\nG1 X100.000 F1000\nG1 Y50.000\nG1 X0\nG1 Y0\nG0 Z5.000\n";
    int32_t res = gs_host_load_gcode(gcode, "basic_cam_surfacing.gcode");
    const char* reply = (res == 0) ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Failed to load\"}";
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
