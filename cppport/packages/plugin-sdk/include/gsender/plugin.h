#pragma once

#include "types.h"
#include "capabilities.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Host Imports (Implemented by gSender host, callable by Wasm plugin)      */
/* ========================================================================= */

/** Emit a G-code line or real-time command (Requires: "machine:command"). */
extern int32_t gs_host_emit_gcode(const char* gcode);

/** Retrieve current work position coordinates (Requires: "workspace:get:state"). */
extern int32_t gs_host_get_wpos(gs_dro_coords_t* out_wpos);

/** Read a key from the plugin's isolated persistent storage (Requires: "storage:get"). */
extern int32_t gs_host_storage_get(const char* key, char* out_buf, int32_t buf_len);

/** Write a key to the plugin's isolated persistent storage (Requires: "storage:set"). */
extern int32_t gs_host_storage_set(const char* key, const char* value);

/** Delete a key from the plugin's isolated persistent storage (Requires: "storage:delete"). */
extern int32_t gs_host_storage_delete(const char* key);

/** Feed G-code directly into the visualizer / loaded program (Requires: "gcode:load:to:visualizer"). */
extern int32_t gs_host_load_gcode(const char* gcode_text, const char* name);

/** Log a message to gSender's console / diagnostics window. */
extern void gs_host_log(int32_t level, const char* message);

/* ========================================================================= */
/* Plugin Exports (Implemented by Wasm plugin, callable by gSender host)     */
/* ========================================================================= */

/** Called when the plugin module is loaded and initialized by gSender.
 *  Return 0 on success, non-zero on initialization error. */
int32_t gsender_plugin_init(void);

/** Called before the plugin is unloaded. Clean up any allocated resources. */
void gsender_plugin_shutdown(void);

/** Generic RPC handler for incoming requests from QML or the bridge.
 *  @param request_json  Null-terminated JSON request payload
 *  @param response_buf  Buffer provided by host for null-terminated JSON response
 *  @param buf_len       Size of response_buf in bytes
 *  @return Bytes written into response_buf, or negative on error */
int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len);

/** Called when a subscribed topic receives a new event snapshot.
 *  @param topic       Topic name (e.g. "workspace", "controller", "viewer")
 *  @param event_json  Null-terminated JSON event payload */
void gsender_plugin_on_topic_event(const char* topic, const char* event_json);

#ifdef __cplusplus
}
#endif
