#pragma once

struct State;
struct ViewState;
struct StateSnapshot;
struct FrameContext;
struct UpdateSnapshot;

void entry_views_update(ViewState &view_state, State &state);
void entry_views_on_demand_update(ViewState &view_state);
void entry_views_draw(FrameContext &ctx, ViewState &view_state,
                      const State &state);
