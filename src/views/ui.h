#pragma once

#include "base/string.h"
#include "constants.h"
#include "icons.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "shortcut.h"
#include "style_control.h"

// Data tables render in the monospaced font: push right after a successful
// BeginTable, pop right before the matching EndTable. Sized off FontSizeBase
// (zoom composes on top) with the optical correction factor applied.
inline void ui_push_mono_font() {
  ImGui::PushFont(style_control_mono_font(),
                  ImGui::GetStyle().FontSizeBase * MONO_FONT_SIZE_FACTOR);
}
inline void ui_pop_mono_font() { ImGui::PopFont(); }

// Fixed pixel sizes are authored at the base font size; scale them by the
// live zoom/DPI so they track the text instead of staying frozen when the UI
// is zoomed or on a HiDPI monitor. Call with the default UI font active.
inline float ui_scale() { return ImGui::GetFontSize() / BASE_FONT_SIZE; }

inline bool ui_context_menu(const bool row_is_selected,
                            const char *str_id = nullptr) {
  shortcut_row_context_menu(row_is_selected);
  return ImGui::BeginPopupContextItem(str_id);
}

inline bool ui_context_menu(const bool row_is_selected, int &menu_column,
                            const int column_count,
                            const char *str_id = nullptr) {
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
    const int hovered = ImGui::TableGetHoveredColumn();
    menu_column = hovered >= 0 && hovered < column_count ? hovered : 0;
  }
  if (shortcut_row_context_menu(row_is_selected)) {
    menu_column = 0;
  }
  return ImGui::BeginPopupContextItem(str_id);
}

// True when the last item was right-clicked
inline bool ui_item_right_clicked() {
  return ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
         ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                              ImGuiHoveredFlags_AllowWhenBlockedByPopup);
}

// Ctrl+C (or Ctrl+Insert) over a focused ui_selectable_text() box
inline bool ui_copy_shortcut_pressed() {
  return ImGui::IsKeyDown(ImGuiMod_Ctrl) &&
         (ImGui::IsKeyPressed(ImGuiKey_C, false) ||
          ImGui::IsKeyPressed(ImGuiKey_Insert, false));
}

// Draw a filter input with Ctrl+F keyboard shortcut
// Refresh toolbar button (icon + label). When `pending`, shows a disabled
// "Refreshing..." state and returns false (smaps' in-flight reload state).
inline bool ui_refresh_button(const bool pending = false) {
  if (pending) {
    ImGui::BeginDisabled();
    ImGui::Button(ICON_MD_REFRESH " Refreshing...");
    ImGui::EndDisabled();
    return false;
  }
  return ImGui::Button(ICON_MD_REFRESH " Refresh");
}

// Draw a muted "Updated Xs ago" on the current toolbar line. `last_updated` is
// an ImGui::GetTime() timestamp captured when data last arrived; 0 means never.
inline void ui_last_updated(const double last_updated) {
  if (last_updated <= 0.0) return;
  const double secs = ImGui::GetTime() - last_updated;
  if (secs < 1.0) {
    ImGui::TextDisabled("Updated just now");
  } else if (secs < 60.0) {
    ImGui::TextDisabled("Updated %.0fs ago", secs);
  } else if (secs < 3600.0) {
    ImGui::TextDisabled("Updated %.0fm ago", secs / 60.0);
  } else {
    ImGui::TextDisabled("Updated %.0fh ago", secs / 3600.0);
  }
}

inline void ui_filter_input(ImGuiTextFilter &filter, const char *id,
                            char *filter_text, const size_t filter_text_size,
                            const char *hint = "Filter") {
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F)) {
    ImGui::SetKeyboardFocusHere();
  }

  // Get widget ID to check if it's active and to reload its buffer after
  // modification
  const ImGuiID input_id = ImGui::GetID(id);
  const bool is_active = ImGui::GetActiveID() == input_id;
  bool buffer_modified = false;

  // Handle shortcuts when filter input is active (before drawing)
  if (is_active) {
    // Ctrl+W: delete last filter entry (word)
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W,
                        ImGuiInputFlags_RouteAlways)) {
      uint32_t len = static_cast<uint32_t>(strlen(filter_text));
      if (len > 0) {
        while (len > 0 && filter_text[len - 1] == ',') {
          len--;
        }
        while (len > 0 && filter_text[len - 1] != ',') {
          len--;
        }
        filter_text[len] = '\0';
        buffer_modified = true;
      }
    }

    // Tell ImGui to reload from user buffer if we modified it
    if (buffer_modified) {
      if (ImGuiInputTextState *state = ImGui::GetInputTextState(input_id)) {
        state->ReloadUserBufAndMoveToEnd();
      }
    }
  }

  ImGui::InputTextWithHint(id, hint, filter_text, filter_text_size);

  if (filter_text[0] != '\0') {
    strncpy(filter.InputBuf, filter_text, sizeof(filter.InputBuf));
    filter.InputBuf[sizeof(filter.InputBuf) - 1] = '\0';
    filter.Build();
  }
}

// Fits `path` into `avail_width` pixels by middle-eliding it
String ui_path_fit(BumpArena &arena, const String &path, float avail_width);

// Draws a path in the current table cell, middle-elided when it does not fit
void ui_path_text(BumpArena &arena, const String &path);

inline void ui_path_text(BumpArena &arena, const char *path) {
  ui_path_text(arena, String::static_string(path));
}

struct UiTextSelection {
  uint32_t begin;
  uint32_t end;
};

UiTextSelection ui_selectable_text(const char *id, const String &value);

// Keeps the box active while `popup_id`'s popup - opened over it
void ui_hold_text_selection(const char *popup_id);
