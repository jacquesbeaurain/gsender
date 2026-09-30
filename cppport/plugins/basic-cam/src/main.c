/* Basic CAM: generates a rectangular facing (surfacing) toolpath, a zigzag
 * over the stock in passes down to the requested depth, and loads it into
 * gSender's visualizer.
 *
 * Request: {"action":"generate","width":100,"height":60,"depth":1,
 *           "stepDown":0.5,"stepover":5,"feed":1200,"plunge":300,"safeZ":5} */

#include "gsender/plugin.h"
#include "gsender/runtime.h"

typedef struct {
    double width, height, depth, step_down, stepover, feed, plunge, safe_z;
} cam_params_t;

static double param(const char* json, const char* key, double fallback, double min, double max) {
    double v = fallback;
    gs_json_number(json, key, &v);
    if (!(v >= min)) v = min;
    if (v > max) v = max;
    return v;
}

static void word(gs_str_t* g, const char* letter, double value, int decimals) {
    gs_str_append(g, letter);
    gs_str_append_fixed(g, value, decimals);
}

/* Returns the number of G-code lines generated. */
static int32_t generate(const cam_params_t* p, gs_str_t* g) {
    int32_t lines = 0;
    gs_str_append(g, "(Basic CAM facing ");
    gs_str_append_fixed(g, p->width, 1);
    gs_str_append(g, " x ");
    gs_str_append_fixed(g, p->height, 1);
    gs_str_append(g, " mm)\nG21 G90 G17\n");
    word(g, "G0 Z", p->safe_z, 3);
    gs_str_append(g, "\nG0 X0.000 Y0.000");
    lines += 4;

    double z = 0;
    while (z > -p->depth + 1e-9) {
        z -= p->step_down;
        if (z < -p->depth) z = -p->depth;
        word(g, "\nG1 Z", z, 3);
        word(g, " F", p->plunge, 0);
        ++lines;

        /* Zigzag: cut along X, step over along Y at the end of each row. */
        double y = 0;
        int forward = 1;
        word(g, "\nG1 X", p->width, 3);
        word(g, " F", p->feed, 0);
        ++lines;
        while (y < p->height) {
            y += p->stepover;
            if (y > p->height) y = p->height;
            word(g, "\nG1 Y", y, 3);
            forward = !forward;
            word(g, "\nG1 X", forward ? p->width : 0, 3);
            lines += 2;
        }
        word(g, "\nG0 Z", p->safe_z, 3);
        gs_str_append(g, "\nG0 X0.000 Y0.000");
        lines += 2;
    }
    gs_str_append(g, "\nM5\n");
    return lines + 1;
}

int32_t gsender_plugin_init(void) {
    gs_host_log(GS_LOG_INFO, "Basic CAM plugin initialized");
    return 0;
}

void gsender_plugin_shutdown(void) {}

int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len) {
    cam_params_t p;
    p.width = param(request_json, "width", 100, 1, 5000);
    p.height = param(request_json, "height", 60, 1, 5000);
    p.depth = param(request_json, "depth", 1, 0.01, 100);
    p.step_down = param(request_json, "stepDown", p.depth, 0.01, 100);
    p.stepover = param(request_json, "stepover", 5, 0.1, 1000);
    p.feed = param(request_json, "feed", 1200, 1, 50000);
    p.plunge = param(request_json, "plunge", 300, 1, 50000);
    p.safe_z = param(request_json, "safeZ", 5, 0.5, 200);

    gs_str_t gcode;
    gs_str_init(&gcode);
    const int32_t lines = generate(&p, &gcode);
    const int32_t loaded = gcode.data ? gs_host_load_gcode(gcode.data, "basic-cam-facing.gcode") : -1;
    gs_str_free(&gcode);

    gs_str_t reply;
    gs_str_init(&reply);
    gs_str_append(&reply, loaded == 0 ? "{\"ok\":true,\"lines\":" : "{\"ok\":false,\"error\":\"Could not load the toolpath\",\"lines\":");
    gs_str_append_int(&reply, lines);
    gs_str_append(&reply, "}");
    const int32_t n = gs_respond(response_buf, buf_len, reply.data);
    gs_str_free(&reply);
    return n;
}

void gsender_plugin_on_topic_event(const char* topic, const char* event_json) {
    (void)topic;
    (void)event_json;
}
