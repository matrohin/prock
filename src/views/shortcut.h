#pragma once

#include "imgui.h"

#include <cstdint>

// Ctrl+C "copy selected row" gate for the viewer tables. Confirms the stored
// selection is still in range before the caller indexes data[selected_index]:
// a Refresh can replace the list with fewer rows and leave selected_index
// pointing past the end.
inline bool shortcut_copy_row(const int selected_index, const uint32_t size) {
  return selected_index >= 0 && selected_index < static_cast<int>(size) &&
         ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C);
}

// Alt+Enter keyboard counterpart of right-clicking the selected row. Call
// right after the row's spanning Selectable/TreeNode (it must be the last
// item): BeginPopupContextItem's popup ID is that item's ID, whether taken
// from the last item (NULL str_id) or hashed from the same label. Returns
// true when it opened the popup.
inline bool shortcut_row_context_menu(const bool row_is_selected) {
  if (row_is_selected &&
      (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter) ||
       ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadEnter))) {
    ImGui::OpenPopup(ImGui::GetItemID());
    return true;
  }
  return false;
}
