// Keyboard and mouse input for the players' remotes, shared by the window layers.
#pragma once

// A key (a GLFW key code) going down or up. Repeats are the caller's to leave out.
void window_input_key(int key, int down);
// The mouse at (x, y) in a window of width x height; a button (0 left, 1 right, 2 middle).
void window_input_mouse_move(double x, double y, int width, int height);
void window_input_mouse_button(int button, int down);
