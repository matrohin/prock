#pragma once

#include "base/const_string.h"

#include "views/brief_table_logic.h"

void brief_table_update(FrameContext &ctx, BriefTableState &my_state,
                        InternTable &string_interner, State &state);

void brief_table_draw(FrameContext &ctx, ViewState &view_state,
                      const State &state);

void brief_table_dump_update(Notifications &notifications, Sync &sync);
