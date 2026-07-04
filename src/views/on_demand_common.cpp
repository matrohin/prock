#include "on_demand_common.h"

#include "labels.h"

String on_demand_viewer_title(BumpArena &frame_arena,
                              const OnDemandViewerStatus status,
                              const char *viewer_name,
                              const uint32_t results_size,
                              const char *process_name, const Pid pid) {
  switch (status) {
  case eOnDemandViewerStatus_Error:
    return String::sprintf(frame_arena, "%s (Error) - %s (%d)###%s%d",
                           viewer_name, process_name, pid, viewer_name, pid);
  case eOnDemandViewerStatus_Loading:
    return String::sprintf(frame_arena, "%s (Loading...) - %s (%d)###%s%d",
                           viewer_name, process_name, pid, viewer_name, pid);
  case eOnDemandViewerStatus_Ready:
    return String::sprintf(frame_arena, "%s (%u) - %s (%d)###%s%d", viewer_name,
                           results_size, process_name, pid, viewer_name, pid);
  }
  return INTERNAL_ERROR;
}
