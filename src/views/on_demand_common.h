#pragma once

#include "base/string.h"

enum OnDemandViewerStatus {
  eOnDemandViewerStatus_Loading,
  eOnDemandViewerStatus_Ready,
  eOnDemandViewerStatus_Error,
};

String on_demand_viewer_title(BumpArena &frame_arena,
                              OnDemandViewerStatus status,
                              const char *viewer_name,
                              const char *results_label, uint32_t results_size,
                              const char *process_name, Pid pid);