#pragma once

// Mirrors pending Ctrl+J/K/H/L key events onto arrow-key events, so vim-style
// navigation drives ImGui's keyboard nav everywhere.
void vim_nav_translate_events();
