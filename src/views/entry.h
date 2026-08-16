#pragma once

struct State;
struct ViewState;
struct StateSnapshot;
struct FrameContext;
struct UpdateSnapshot;

// A frozen view keeps showing the snapshot its rows were built from
bool entry_views_frozen(const ViewState &view_state);

void entry_views_update(FrameContext &ctx, ViewState &view_state, State &state);
void entry_views_on_demand_update(ViewState &view_state);
void entry_views_reset_history(ViewState &view_state);
void entry_views_draw(FrameContext &ctx, ViewState &view_state,
                      const State &state);
