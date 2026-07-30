#pragma once

struct ViewState;

struct CommandState {
  bool show_palette = false;
  int selected = 0;
  char filter[128] = {};
};

void command_dispatch(ViewState &vs);
void command_palette_draw(ViewState &vs);
