#pragma once
// Configuration_COREONEL.h reads the direction inversion from the config store. fake_world.cpp
// returns the defaults (DEFAULT_INVERT_*_DIR).
bool has_inverted_x();
bool has_inverted_y();
bool has_inverted_z();
bool has_inverted_e();
float get_z_max_pos_mm();
