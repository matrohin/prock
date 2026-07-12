#include "views/vim_nav.h"

#include "imgui_internal.h"

struct VimNavKey {
  ImGuiKey from;
  ImGuiKey to;
  bool translated;
};

static VimNavKey g_vim_nav_keys[] = {
    {ImGuiKey_J, ImGuiKey_DownArrow, false},
    {ImGuiKey_K, ImGuiKey_UpArrow, false},
    {ImGuiKey_H, ImGuiKey_LeftArrow, false},
    {ImGuiKey_L, ImGuiKey_RightArrow, false},
};

void vim_nav_translate_events() {
  ImGuiContext &g = *ImGui::GetCurrentContext();
  bool ctrl = g.IO.KeyCtrl;
  for (int i = 0; i < g.InputEventsQueue.Size; ++i) {
    const ImGuiInputEvent e = g.InputEventsQueue[i];
    if (e.Type != ImGuiInputEventType_Key) {
      continue;
    }
    if (e.Key.Key == ImGuiMod_Ctrl) {
      ctrl = e.Key.Down;
      continue;
    }
    for (VimNavKey &key : g_vim_nav_keys) {
      if (e.Key.Key != key.from) {
        continue;
      }
      if (e.Key.Down ? ctrl : key.translated) {
        // Mirror the event onto the arrow key instead of rewriting it: the
        // source key's ImGui state must keep tracking the physical state, or
        // io.AddKeyEvent() would filter the following release as a no-op
        // transition and the arrow would stick down.
        ImGuiInputEvent mirrored = e;
        mirrored.EventId = g.InputEventsNextEventId++;
        mirrored.Key.Key = key.to;
        ++i;
        g.InputEventsQueue.insert(g.InputEventsQueue.Data + i, mirrored);
        key.translated = e.Key.Down;
      }
    }
  }
}
