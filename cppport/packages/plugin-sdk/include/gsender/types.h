#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Digital Read Out (DRO) coordinate snapshot in workspace units. */
typedef struct {
    double x;
    double y;
    double z;
    double a;
} gs_dro_coords_t;

/** Controller state snapshot. */
typedef struct {
    char state[32];          /**< "Idle", "Run", "Hold", "Alarm", "Door", etc. */
    gs_dro_coords_t wpos;    /**< Work position */
    gs_dro_coords_t mpos;    /**< Machine position */
    double feedrate;         /**< Current feed rate */
    double spindle_speed;    /**< Current spindle RPM */
    bool metric;             /**< True if workspace is mm, false if inches */
    bool connected;          /**< True if hardware controller is connected */
} gs_machine_status_t;

/** Log level for host reporting. */
typedef enum {
    GS_LOG_DEBUG = 0,
    GS_LOG_INFO  = 1,
    GS_LOG_WARN  = 2,
    GS_LOG_ERROR = 3
} gs_log_level_t;

/** Visualizer 3D overlay marker shape. */
typedef enum {
    GS_SHAPE_CIRCLE = 0,
    GS_SHAPE_CROSS  = 1,
    GS_SHAPE_RING   = 2
} gs_overlay_shape_t;

/** Visualizer 3D overlay marker. */
typedef struct {
    const char* id;
    double x;
    double y;
    double z;
    gs_overlay_shape_t shape;
    const char* color_hex;  /**< e.g. "#22c55e" */
    double size_px;
    const char* label;
} gs_overlay_marker_t;

#ifdef __cplusplus
}
#endif
