#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Standard capability request types supported by the gSender plugin bridge. */
#define GS_CAP_MACHINE_GET_CONTEXT      "machine:get:context"
#define GS_CAP_MACHINE_COMMAND          "machine:command"
#define GS_CAP_MACHINE_QUERY            "machine:query"
#define GS_CAP_MACHINE_BUSY_SET         "machine:busy:set"
#define GS_CAP_MACHINE_PARSER_REGISTER  "machine:parser:register"
#define GS_CAP_MACHINE_PARSER_UNREGISTER "machine:parser:unregister"

#define GS_CAP_GCODE_LOAD_TO_VISUALIZER "gcode:load:to:visualizer"
#define GS_CAP_WORKSPACE_GET_STATE      "workspace:get:state"

#define GS_CAP_STORAGE_GET              "storage:get"
#define GS_CAP_STORAGE_SET              "storage:set"
#define GS_CAP_STORAGE_DELETE           "storage:delete"
#define GS_CAP_STORAGE_GET_ALL          "storage:get:all"
#define GS_CAP_STORAGE_SET_ALL          "storage:set:all"
#define GS_CAP_STORAGE_CLEAR            "storage:clear"

#define GS_CAP_VIEWER_SCREEN_TO_WORLD   "viewer:screen-to-world"
#define GS_CAP_VIEWER_WORLD_TO_SCREEN   "viewer:world-to-screen"
#define GS_CAP_VIEWER_CAMERA_SET        "viewer:camera:set"
#define GS_CAP_VIEWER_OVERLAY_SET       "viewer:overlay:set"

/** Standard reactive topic streams. */
#define GS_TOPIC_WORKSPACE              "workspace"
#define GS_TOPIC_CONTROLLER             "controller"
#define GS_TOPIC_VIEWER                 "viewer"
#define GS_TOPIC_PARSER                 "parser"

#ifdef __cplusplus
}
#endif
