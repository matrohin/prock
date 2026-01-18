#pragma once

struct State;
struct ViewState;
struct StateSnapshot;
struct FrameContext;

void views_update(ViewState &view_state, State &state);
void views_draw(FrameContext &ctx, ViewState &view_state, const State &state);
