#pragma once

struct State;
struct ViewState;
struct StateSnapshot;

void views_update(State &state, const StateSnapshot &old_snapshot);
void views_draw(const State &state);
