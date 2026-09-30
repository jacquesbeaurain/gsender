#pragma once

#include "types.h"
#include "capabilities.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__wasm__)
#define GS_IMPORT(name) __attribute__((import_module("env"), import_name(#name)))
#define GS_EXPORT(name) __attribute__((export_name(#name)))
#else
#define GS_IMPORT(name)
#define GS_EXPORT(name)
#endif

/* ========================================================================= */
/* Host Imports (Implemented by gSender host, callable by Wasm plugin)      */
/* ========================================================================= */

/** Emit a G-code line or real-time command (Requires: "machine:command"). */
GS_IMPORT(gs_host_emit_gcode) int32_t gs_host_emit_gcode(const char* gcode);

/** Retrieve current work position coordinates (Requires: "workspace:get:state"). */
GS_IMPORT(gs_host_get_wpos) int32_t gs_host_get_wpos(gs_dro_coords_t* out_wpos);

/** Read a key from the plugin's isolated persistent storage (Requires: "storage:get").
 *  @return the value's length, or -1 when the key is missing, denied or does not fit. */
GS_IMPORT(gs_host_storage_get) int32_t gs_host_storage_get(const char* key, char* out_buf, int32_t buf_len);

/** Write a key to the plugin's isolated persistent storage (Requires: "storage:set"). */
GS_IMPORT(gs_host_storage_set) int32_t gs_host_storage_set(const char* key, const char* value);

/** Delete a key from the plugin's isolated persistent storage (Requires: "storage:delete"). */
GS_IMPORT(gs_host_storage_delete) int32_t gs_host_storage_delete(const char* key);

/** Feed G-code directly into the visualizer / loaded program (Requires: "gcode:load:to:visualizer"). */
GS_IMPORT(gs_host_load_gcode) int32_t gs_host_load_gcode(const char* gcode_text, const char* name);

/** Log a message to gSender's console / diagnostics window. */
GS_IMPORT(gs_host_log) void gs_host_log(int32_t level, const char* message);

/** Any bridge request, with the same capability check as the typed calls above.
 *  @param type          Request type, e.g. "viewer:overlay:set" (see capabilities.h)
 *  @param payload_json  JSON object with the request's payload ("{}" for none)
 *  @param out_buf       Receives {"ok":bool,"result":{...},"error":"..."}
 *  @return Bytes written (without the terminator), or minus the size needed when out_buf is too small */
GS_IMPORT(gs_host_request) int32_t gs_host_request(const char* type, const char* payload_json, char* out_buf, int32_t buf_len);

/* ========================================================================= */
/* Plugin Exports (Implemented by Wasm plugin, callable by gSender host)     */
/* ========================================================================= */

/** Called when the plugin module is loaded and initialized by gSender.
 *  Return 0 on success, non-zero on initialization error. */
GS_EXPORT(gsender_plugin_init) int32_t gsender_plugin_init(void);

/** Called before the plugin is unloaded. Clean up any allocated resources. */
GS_EXPORT(gsender_plugin_shutdown) void gsender_plugin_shutdown(void);

/** Generic RPC handler for incoming requests from the plugin's UI or the bridge.
 *  @param request_json  Null-terminated JSON request payload
 *  @param response_buf  Buffer provided by host for null-terminated JSON response
 *  @param buf_len       Size of response_buf in bytes
 *  @return Bytes written into response_buf, or negative on error */
GS_EXPORT(gsender_plugin_handle_request) int32_t gsender_plugin_handle_request(const char* request_json, char* response_buf, int32_t buf_len);

/** Called when a subscribed topic receives a new event snapshot.
 *  @param topic       Topic name (e.g. "workspace", "controller", "viewer", "parser")
 *  @param event_json  Null-terminated JSON event payload */
GS_EXPORT(gsender_plugin_on_topic_event) void gsender_plugin_on_topic_event(const char* topic, const char* event_json);

/** Allocation in the plugin's own memory, which the host uses to pass strings in.
 *  The SDK runtime (src/runtime.c) provides both; a plugin in another language exports its own. */
GS_EXPORT(gsender_plugin_alloc) void* gsender_plugin_alloc(int32_t size);
GS_EXPORT(gsender_plugin_free) void gsender_plugin_free(void* ptr);

#ifdef __cplusplus
}
#endif
