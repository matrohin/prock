#pragma once

struct ViewState;

// Listed roughly by how often the action is reached from the palette; the
// list and dispatch both follow this order.
enum CommandId {
  eCommand_FocusProcessFilter,
  eCommand_ToggleAutoFollow,
  eCommand_ToggleAutoFitY,
  eCommand_ZoomIn,
  eCommand_ZoomOut,
  eCommand_TogglePerCoreCpu,
  eCommand_ToggleStacked,
  eCommand_ToggleMenuOnAlt,
  eCommand_OpenPreferences,
  eCommand_OpenPalette,
  eCommand_ShowLicenses,
  eCommand_ShowAbout,
  eCommand_ToggleRecording,
  eCommand_Count,
};

struct CommandState {
  bool show_palette = false;
  int selected = 0;
  char filter[128] = {};
};

void command_dispatch(ViewState &vs);
void command_palette_draw(ViewState &vs);
