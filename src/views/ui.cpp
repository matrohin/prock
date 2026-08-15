#include "ui.h"

#include "imgui_internal.h"

#include <algorithm>

static constexpr int MAX_LINES = 12;

static int wrapped_line_count(const String &value, const float wrap_width) {
  ImFont *font = ImGui::GetFont();
  const float font_size = ImGui::GetFontSize();
  const char *end = value.data + value.len;
  int lines = 0;
  for (const char *s = value.data; s < end && lines < MAX_LINES;) {
    ++lines;
    s = font->CalcWordWrapPosition(font_size, s, end, wrap_width);
    if (s < end && *s == '\n') ++s;
  }
  if (end > value.data && end[-1] == '\n') ++lines;
  return std::clamp(lines, 1, MAX_LINES);
}

static ImVec2 value_box_size(const String &value) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const float wrap_width =
      std::max(1.0f, ImGui::GetContentRegionAvail().x - style.FramePadding.x -
                         style.ScrollbarSize);
  const int lines = wrapped_line_count(value, wrap_width);
  return ImVec2(-FLT_MIN,
                lines * ImGui::GetFontSize() + style.FramePadding.y * 2.0f);
}

static UiTextSelection current_selection() {
  const ImGuiInputTextState *state =
      ImGui::GetInputTextState(ImGui::GetItemID());
  if (!ImGui::IsItemActive() || !state || !state->HasSelection()) {
    return UiTextSelection{0, 0};
  }
  const int start = state->GetSelectionStart();
  const int end = state->GetSelectionEnd();
  return UiTextSelection{static_cast<uint32_t>(std::min(start, end)),
                         static_cast<uint32_t>(std::max(start, end))};
}

void ui_hold_text_selection(const char *popup_id) {
  ImGuiContext &g = *ImGui::GetCurrentContext();
  if (g.ActiveId != ImGui::GetItemID()) return;
  const bool hold = ImGui::IsPopupOpen(popup_id);
  g.ActiveIdNoClearOnFocusLoss = hold;
  if (hold) g.ActiveIdAllowOverlap = true;
}

UiTextSelection ui_selectable_text(const char *id, const String &value) {
  ImGui::InputTextMultiline(
      id, const_cast<char *>(value.data), value.len + 1, value_box_size(value),
      ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_ReadOnly);
  return current_selection();
}
