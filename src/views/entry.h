#pragma once

struct State;
struct ViewState;
struct StateSnapshot;
struct FrameContext;
struct UpdateSnapshot;

void views_update(ViewState &view_state, State &state);
void views_draw(FrameContext &ctx, ViewState &view_state, const State &state);
void views_process_thread_snapshots(ViewState &view_state, const State &state,
                                    const UpdateSnapshot &snapshot);
