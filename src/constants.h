#pragma once

inline constexpr int ZOOM_MIN_PCT = 50;
inline constexpr int ZOOM_MAX_PCT = 500;
inline constexpr int ZOOM_STEP_PCT = 10;

inline constexpr int TARGET_FPS_MAX = 144;
inline constexpr int TARGET_FPS_MIN = 15;

// Longest selectable gathering interval, in seconds (0 = paused).
inline constexpr float UPDATE_PERIOD_MAX_SEC = 5.0f;

// Unscaled UI font size; the live font is this times monitor scale * zoom.
inline constexpr float BASE_FONT_SIZE = 15.0f;

// The FreeType loader sizes fonts by their line box (FT_SIZE_REQUEST_TYPE_
// REAL_DIM), and JetBrains Mono's box is taller than Inter's (hhea 1.32 em vs
// 1.21 em), so at equal size its caps and digits render ~8% shorter. Scale the
// mono font up to optically match (cap-height ratio 1.087, x-height 1.083).
inline constexpr float MONO_FONT_SIZE_FACTOR = 1.085f;

// Stretch weight for the trailing spacer column in viewer header toolbars.
// Small relative to the filter column (default weight 1.0) so the spacer is a
// thin gap at the right edge that keeps the controls left-aligned.
constexpr float HEADER_SPACER_WEIGHT = 0.25f;
