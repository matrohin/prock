#include "brief_table.h"

#include "views/common.h"
#include "views/cpu_chart.h"
#include "views/mem_chart.h"

#include "imgui_internal.h"

#include <chrono>

namespace {

void sort_as_requested(BriefTableState &my_state, const StateSnapshot &state) {
  const auto sort_ascending = [&state, sorted_by = my_state.sorted_by](const BriefTableLine &left, const BriefTableLine &right) {
    switch (sorted_by) {
      case eBriefTableColumnId_Pid: return left.pid < right.pid;
      case eBriefTableColumnId_Name: return strcmp(state.stats.data[left.state_index].comm, state.stats.data[right.state_index].comm) < 0;
      case eBriefTableColumnId_CpuTotalPerc: {
        const double left_val = state.derived_stats.data[left.state_index].cpu_user_perc + state.derived_stats.data[left.state_index].cpu_kernel_perc;
        const double right_val = state.derived_stats.data[right.state_index].cpu_user_perc + state.derived_stats.data[right.state_index].cpu_kernel_perc;
        return left_val < right_val;
      }
      case eBriefTableColumnId_CpuUserPerc: return state.derived_stats.data[left.state_index].cpu_user_perc < state.derived_stats.data[right.state_index].cpu_user_perc;
      case eBriefTableColumnId_CpuKernelPerc: return state.derived_stats.data[left.state_index].cpu_kernel_perc < state.derived_stats.data[right.state_index].cpu_kernel_perc;
      case eBriefTableColumnId_MemRssBytes: return state.derived_stats.data[left.state_index].mem_resident_bytes < state.derived_stats.data[right.state_index].mem_resident_bytes;
    }
    return false;
  };

  if (my_state.sorted_order != ImGuiSortDirection_Descending) {
    std::stable_sort(my_state.lines.data, my_state.lines.data + my_state.lines.size, sort_ascending);
  } else {
    std::stable_sort(my_state.lines.data, my_state.lines.data + my_state.lines.size, [&](const auto& left, const auto& right) { return sort_ascending(right, left); });
  }
}

size_t binary_search_pid(const Array<ProcessStat> &stats, int pid) {
  size_t left = 0;
  size_t right = stats.size;
  while (left < right) {
    size_t mid = (left + right) / 2;
    if (stats.data[mid].pid < pid) {
      left = mid + 1;
    } else if (stats.data[mid].pid > pid) {
      right = mid;
    } else {
      return mid;
    }
  }
  return SIZE_MAX;
}

} // unnamed namespace


// Rebuilds lines in previous display order (with new processes appended) for stable sorting.
void brief_table_update(
  BriefTableState &my_state, State &state, const StateSnapshot &old) {

  const StateSnapshot &new_snapshot = state.snapshot;
  const Array<BriefTableLine> &old_lines = my_state.lines;

  Array<bool> added = Array<bool>::create(state.snapshot_arena, new_snapshot.stats.size);
  for (size_t i = 0; i < added.size; ++i) {
    added.data[i] = false;
  }

  Array<BriefTableLine> new_lines = Array<BriefTableLine>::create(state.snapshot_arena, new_snapshot.stats.size);
  size_t new_lines_count = 0;

  for (size_t i = 0; i < old_lines.size; ++i) {
    const BriefTableLine &old_line = old_lines.data[i];
    size_t state_index = binary_search_pid(new_snapshot.stats, old_line.pid);

    if (state_index != SIZE_MAX) {
      BriefTableLine &new_line = new_lines.data[new_lines_count++];
      new_line.pid = old_line.pid;
      new_line.state_index = state_index;
      new_line.selected = old_line.selected;
      added.data[state_index] = true;
    }
  }

  for (size_t i = 0; i < new_snapshot.stats.size; ++i) {
    if (!added.data[i]) {
      BriefTableLine &new_line = new_lines.data[new_lines_count++];
      new_line.pid = new_snapshot.stats.data[i].pid;
      new_line.state_index = i;
      new_line.selected = false;
    }
  }

  new_lines.size = new_lines_count;
  my_state.lines = new_lines;
  sort_as_requested(my_state, new_snapshot);
}

void brief_table_draw(ViewState &view_state, const State &state) {
  BriefTableState &my_state = view_state.brief_table_state;

  ImGui::Begin("Process Table", nullptr, COMMON_VIEW_FLAGS);
  if (ImGui::BeginTable("Processes", eBriefTableColumnId_Count,
                        ImGuiTableFlags_Resizable |
                        ImGuiTableFlags_Reorderable |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_Hideable |
                        ImGuiTableFlags_Sortable |
                        ImGuiTableFlags_NoSavedSettings |
                        ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("Process ID", ImGuiTableColumnFlags_NoHide, 0.0f, eBriefTableColumnId_Pid);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 0.0f, eBriefTableColumnId_Name);
    ImGui::TableSetupColumn("CPU Total (%)", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, eBriefTableColumnId_CpuTotalPerc);
    ImGui::TableSetupColumn("CPU User (%)", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, 0.0f, eBriefTableColumnId_CpuUserPerc);
    ImGui::TableSetupColumn("CPU Kernel (%)", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, eBriefTableColumnId_CpuKernelPerc);
    ImGui::TableSetupColumn("RSS (Bytes)", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, eBriefTableColumnId_MemRssBytes);
    ImGui::TableHeadersRow();

    const float cpu_total_width = ImGui::GetColumnWidth(eBriefTableColumnId_CpuTotalPerc);
    const float cpu_user_width = ImGui::GetColumnWidth(eBriefTableColumnId_CpuUserPerc);
    const float cpu_kernel_width = ImGui::GetColumnWidth(eBriefTableColumnId_CpuKernelPerc);
    const float mem_res_width = ImGui::GetColumnWidth(eBriefTableColumnId_MemRssBytes);

    if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
      if (sort_specs->SpecsDirty) {
        my_state.sorted_by = static_cast<BriefTableColumnId>(sort_specs->Specs->ColumnUserID);
        my_state.sorted_order = sort_specs->Specs->SortDirection;
        sort_as_requested(my_state, state.snapshot);
        sort_specs->SpecsDirty = false;
      }
    }

    for (size_t i = 0; i < my_state.lines.size; ++i) {
      ImGui::TableNextRow();

      BriefTableLine &line = my_state.lines.data[i];
      const ProcessStat &stat = state.snapshot.stats.data[line.state_index];
      const ProcessDerivedStat &derived_stat = state.snapshot.derived_stats.data[line.state_index];
      ImGui::TableSetColumnIndex(eBriefTableColumnId_Pid);
      {
        char label[32];
        sprintf(label, "%d", line.pid);
        ImGui::Selectable(label, &line.selected, ImGuiSelectableFlags_SpanAllColumns);
        if (ImGui::BeginPopupContextItem(label)) {
          line.selected = true;
          if (ImGui::Selectable("CPU Chart")) {
            cpu_chart_add(view_state.cpu_chart_state, line.pid, stat.comm);
          }
          if (ImGui::Selectable("Memory Chart")) {
            mem_chart_add(view_state.mem_chart_state, line.pid, stat.comm);
          }
          ImGui::EndPopup();
        }
      }
      if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Name)) ImGui::Text("%s", stat.comm);
      if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuTotalPerc)) ImGui::TextAligned(1.0f, cpu_total_width, "%.1f", derived_stat.cpu_user_perc + derived_stat.cpu_kernel_perc);
      if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuUserPerc)) ImGui::TextAligned(1.0f, cpu_user_width, "%.1f", derived_stat.cpu_user_perc);
      if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuKernelPerc)) ImGui::TextAligned(1.0f, cpu_kernel_width, "%.1f", derived_stat.cpu_kernel_perc);
      if (ImGui::TableSetColumnIndex(eBriefTableColumnId_MemRssBytes)) {
        ImGui::TextAligned(1.0f, mem_res_width, "%.0f K", derived_stat.mem_resident_bytes / 1024);
      }
    }

    ImGui::EndTable();
  }

  ImGui::End();
}
