#include "state.h"


StateSnapshot state_snapshot_update(BumpArena &arena, const State &old_state, const UpdateSnapshot &snapshot) {
  const StateSnapshot &old = old_state.snapshot;
  Array<ProcessDerivedStat> derived_stats = Array<ProcessDerivedStat>::create(arena, snapshot.stats.size);

  const double ticks_passed = old_state.system.ticks_in_second *
    std::chrono::duration_cast<Seconds>(snapshot.at - old.at).count();

  size_t old_state_idx = 0;
  for (size_t i = 0; i < derived_stats.size; ++i) {
    ProcessDerivedStat &result = derived_stats.data[i];
    const ProcessStat &new_stat = snapshot.stats.data[i];

    while (old_state_idx < old.stats.size && new_stat.pid > old.stats.data[old_state_idx].pid) {
      ++old_state_idx;
    }

    if (old_state_idx < old.stats.size && new_stat.pid == old.stats.data[old_state_idx].pid) {
      const ProcessStat &old_stat = old.stats.data[old_state_idx];
      result.cpu_user_perc = (new_stat.utime - old_stat.utime) / ticks_passed * 100;
      result.cpu_kernel_perc = (new_stat.stime - old_stat.stime) / ticks_passed * 100;
      result.mem_resident_bytes = new_stat.statm_resident * old_state.system.mem_page_size;
    }
  }

  return StateSnapshot{snapshot.stats, derived_stats, snapshot.at};
}
