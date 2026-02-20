#include "life_matrix.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstdlib>
#include <ctime>

namespace esphome {
namespace life_matrix {

static const char *const TAG = "life_matrix";

void LifeMatrix::setup() {
  ESP_LOGD(TAG, "Setting up Life Matrix component");
  // Seed random number generator for Game of Life
  std::srand(std::time(nullptr));

  // Initialize Game of Life with default pattern
  initialize_game_of_life(game_config_.complex_patterns ? PATTERN_MIXED : PATTERN_RANDOM);

  // Enable demo mode on startup
  game_demo_mode_ = true;
  game_demo_start_time_ = millis();

  // Lifespan: extract birthdays into lifespan_year_events_ and precompute active phases
  apply_lifespan_year_events();
  precompute_lifespan_phases();

  // Set initial status LED state
  update_status_led();
}

void LifeMatrix::loop() {
  // Skip all processing during OTA to speed up updates
  if (ota_in_progress_) {
    return;
  }

  bool gol_visible = (get_current_screen_id() == SCREEN_GAME_OF_LIFE);

  // Handle GoL screen visibility changes (pause/resume timer)
  if (gol_visible != gol_was_visible_) {
    if (!gol_visible && gol_was_visible_) {
      // Switching away from GoL - pause the stability timer
      if (game_is_stable_ && game_stable_since_ > 0) {
        game_stable_paused_elapsed_ = millis() - game_stable_since_;
        ESP_LOGD(TAG, "Pausing GoL timer at %lu ms", game_stable_paused_elapsed_);
      }
    } else if (gol_visible && !gol_was_visible_) {
      // Switching back to GoL - resume the stability timer
      if (game_is_stable_ && game_stable_paused_elapsed_ > 0) {
        game_stable_since_ = millis() - game_stable_paused_elapsed_;
        ESP_LOGD(TAG, "Resuming GoL timer from %lu ms", game_stable_paused_elapsed_);
      }
    }
    gol_was_visible_ = gol_visible;
  }

  // Update Game of Life only when visible and not in reset/demo state
  if (game_initialized_ && gol_visible && !game_reset_animation_ && !game_demo_mode_) {
    update_game_of_life();

    // Check for long-standing stability and reset if needed
    if (game_is_stable_ && game_config_.auto_reset_on_stable) {
      unsigned long elapsed = millis() - game_stable_since_;
      if (elapsed >= (unsigned long)game_config_.stability_timeout_ms) {
        ESP_LOGD(TAG, "Auto-resetting Game of Life after %lu ms of stability", elapsed);
        reset_game_of_life();
      }
    }
  }

  // Update screen cycling
  update_screen_cycle();

  // Check for UI timeouts
  check_ui_timeout();
}

// ============================================================================
// GAME OF LIFE IMPLEMENTATION
// ============================================================================

uint8_t LifeMatrix::get_cell(int x, int y) {
  if (x < 0 || x >= grid_width_ || y < 0 || y >= grid_height_) {
    return 0;
  }
  return game_grid_[y * grid_width_ + x];
}

void LifeMatrix::set_cell(int x, int y, uint8_t value) {
  if (x < 0 || x >= grid_width_ || y < 0 || y >= grid_height_) {
    return;
  }
  game_grid_[y * grid_width_ + x] = value;
}

int LifeMatrix::count_neighbors(int x, int y) {
  int count = 0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) continue;  // Skip center cell
      int nx = x + dx;
      int ny = y + dy;
      // Wrap around edges (toroidal topology)
      if (nx < 0) nx += grid_width_;
      if (nx >= grid_width_) nx -= grid_width_;
      if (ny < 0) ny += grid_height_;
      if (ny >= grid_height_) ny -= grid_height_;
      if (get_cell(nx, ny) > 0) count++;
    }
  }
  return count;
}

void LifeMatrix::set_grid_dimensions(int width, int height) {
  grid_width_ = width;
  grid_height_ = height;
  ESP_LOGD(TAG, "Grid dimensions set to %dx%d", width, height);
}

void LifeMatrix::place_pattern(int x, int y, PatternType pattern) {
  switch (pattern) {
    case PATTERN_R_PENTOMINO:
      // R-pentomino: famous methuselah pattern
      //  XX
      // XX
      //  X
      set_cell(x + 1, y, 1);
      set_cell(x + 2, y, 1);
      set_cell(x, y + 1, 1);
      set_cell(x + 1, y + 1, 1);
      set_cell(x + 1, y + 2, 1);
      break;

    case PATTERN_ACORN:
      // Acorn: another methuselah (takes 5206 generations to stabilize)
      //  X
      //    X
      // XX  XXX
      set_cell(x + 1, y, 1);
      set_cell(x + 3, y + 1, 1);
      set_cell(x, y + 2, 1);
      set_cell(x + 1, y + 2, 1);
      set_cell(x + 4, y + 2, 1);
      set_cell(x + 5, y + 2, 1);
      set_cell(x + 6, y + 2, 1);
      break;

    case PATTERN_GLIDER:
      // Glider: moves diagonally
      //  X
      //   X
      // XXX
      set_cell(x + 1, y, 1);
      set_cell(x + 2, y + 1, 1);
      set_cell(x, y + 2, 1);
      set_cell(x + 1, y + 2, 1);
      set_cell(x + 2, y + 2, 1);
      break;

    case PATTERN_DIEHARD:
      // Diehard: vanishes after 130 generations
      //       X
      // XX
      //  X   XXX
      set_cell(x + 6, y, 1);
      set_cell(x, y + 1, 1);
      set_cell(x + 1, y + 1, 1);
      set_cell(x + 1, y + 2, 1);
      set_cell(x + 5, y + 2, 1);
      set_cell(x + 6, y + 2, 1);
      set_cell(x + 7, y + 2, 1);
      break;

    default:
      break;
  }
}

void LifeMatrix::randomize_cells(int density_percent) {
  for (int y = 0; y < grid_height_; y++) {
    for (int x = 0; x < grid_width_; x++) {
      if (std::rand() % 100 < density_percent) {
        set_cell(x, y, 1);
      }
    }
  }
}

void LifeMatrix::initialize_game_of_life(PatternType pattern) {
  ESP_LOGD(TAG, "Initializing Game of Life grid with pattern type %d", pattern);

  // Clear grid
  game_grid_.fill(0);

  if (pattern == PATTERN_MIXED && game_config_.complex_patterns) {
    // Place interesting methuselahs
    place_pattern(5, 15, PATTERN_R_PENTOMINO);
    place_pattern(10, 50, PATTERN_ACORN);
    place_pattern(15, 85, PATTERN_DIEHARD);

    // Place some gliders
    place_pattern(3, 10, PATTERN_GLIDER);
    place_pattern(20, 30, PATTERN_GLIDER);
    place_pattern(8, 100, PATTERN_GLIDER);

    // Add 10% random noise
    randomize_cells(10);
  } else if (pattern == PATTERN_RANDOM) {
    // Random initialization with ~30% density
    randomize_cells(30);
  } else {
    // Place a single pattern
    place_pattern(grid_width_ / 2 - 3, grid_height_ / 2 - 3, pattern);
  }

  game_initialized_ = true;
  game_generation_ = 0;
  game_last_update_ = millis();
  game_start_time_ = millis();
  population_history_.fill(0);
  history_idx_ = 0;
  history_filled_ = false;
  game_is_stable_ = false;
  game_stable_since_ = 0;
  game_stable_paused_elapsed_ = 0;
}

void LifeMatrix::update_game_of_life() {
  unsigned long now = millis();

  // Use configurable update interval
  unsigned long update_interval = game_config_.update_interval_ms;

  if (now - game_last_update_ < update_interval) {
    return;
  }

  game_last_update_ = now;

  // Use back buffer for next generation
  game_grid_back_.fill(0);

  game_births_ = 0;
  game_deaths_ = 0;
  int max_age = 0;
  int population = 0;

  const int w = grid_width_;
  const int h = grid_height_;
  const auto &src = game_grid_;

  // Apply Conway's rules with inlined neighbor counting
  // Precomputes row/col offsets to avoid per-cell function calls and bounds checks
  for (int y = 0; y < h; y++) {
    // Yield every 30 rows to let WiFi stack process events
    if (y > 0 && (y % 30) == 0) delay(0);

    int row_above = ((y == 0) ? h - 1 : y - 1) * w;
    int row_cur   = y * w;
    int row_below = ((y == h - 1) ? 0 : y + 1) * w;

    for (int x = 0; x < w; x++) {
      int xl = (x == 0) ? w - 1 : x - 1;
      int xr = (x == w - 1) ? 0 : x + 1;

      // Direct array access — no function calls or redundant bounds checks
      int neighbors =
        (src[row_above + xl] > 0) + (src[row_above + x] > 0) + (src[row_above + xr] > 0) +
        (src[row_cur   + xl] > 0) +                             (src[row_cur   + xr] > 0) +
        (src[row_below + xl] > 0) + (src[row_below + x] > 0) + (src[row_below + xr] > 0);

      uint8_t current_age = src[row_cur + x];
      bool alive = current_age > 0;
      bool next_alive;

      if (alive) {
        next_alive = (neighbors == 2 || neighbors == 3);
        if (!next_alive) game_deaths_++;
      } else {
        next_alive = (neighbors == 3);
        if (next_alive) game_births_++;
      }

      uint8_t next_age = 0;
      if (next_alive) {
        next_age = (current_age == 0) ? 1 : (uint8_t)std::min(255, (int)current_age + 1);
        if (next_age > max_age) max_age = next_age;
        population++;
      }

      game_grid_back_[row_cur + x] = next_age;
    }
  }

  // Swap buffers
  std::swap(game_grid_, game_grid_back_);
  game_generation_++;
  game_last_max_age_ = max_age;
  population_history_[history_idx_] = population;
  history_idx_ = (history_idx_ + 1) % 30;
  if (!history_filled_ && history_idx_ == 0) {
    history_filled_ = true;
  }

  // Check for stability, extinction, or low population
  if (population == 0) {
    // Extinct - reset immediately
    ESP_LOGD(TAG, "Game of Life extinct at generation %d - resetting", game_generation_);
    reset_game_of_life();
  } else if (population < 58 && game_config_.auto_reset_on_stable) {
    // Low population - likely boring, mark as stable
    if (!game_is_stable_) {
      game_is_stable_ = true;
      game_stable_since_ = now;
      ESP_LOGD(TAG, "Game of Life low population (%d) at generation %d", population, game_generation_);

      // Export statistics to Home Assistant
      if (gol_final_generation_sensor_) gol_final_generation_sensor_->publish_state(game_generation_);
      if (gol_final_population_sensor_) gol_final_population_sensor_->publish_state(population);
    }
  } else if (is_stable()) {
    // Pattern is repeating, mark as stable
    if (!game_is_stable_) {
      game_is_stable_ = true;
      game_stable_since_ = now;
      ESP_LOGD(TAG, "Game of Life became stable at generation %d", game_generation_);

      // Export statistics to Home Assistant
      if (gol_final_generation_sensor_) gol_final_generation_sensor_->publish_state(game_generation_);
      if (gol_final_population_sensor_) gol_final_population_sensor_->publish_state(population);
    }
  } else {
    // Still changing, reset stability flag
    game_is_stable_ = false;
  }

  // Note: Timeout check is now in loop() so it works even if screen switches away
}

void LifeMatrix::reset_game_of_life() {
  // Trigger big bang animation
  game_reset_animation_ = true;
  game_reset_animation_start_ = millis();

  // Enable demo mode on reset (will show after animation)
  game_demo_mode_ = true;
  game_demo_start_time_ = millis();

  // Clear stability state to prevent loop() from re-triggering reset every frame
  game_is_stable_ = false;
  game_stable_since_ = 0;
  game_stable_paused_elapsed_ = 0;

  // Don't initialize world yet - wait until demo ends
}

void LifeMatrix::set_demo_mode(bool enabled) {
  game_demo_mode_ = enabled;
  if (enabled) {
    game_demo_start_time_ = millis();
  }
}

bool LifeMatrix::is_stable() {
  if (!history_filled_) return false;

  // Check if population has been constant for last 30 updates
  int first_pop = population_history_[0];
  for (int i = 1; i < 30; i++) {
    if (population_history_[i] != first_pop) {
      return false;
    }
  }
  return true;
}

int LifeMatrix::get_population() {
  int count = 0;
  for (int i = 0; i < GRID_SIZE; i++) {
    if (game_grid_[i] > 0) count++;
  }
  return count;
}

// ============================================================================
// UI STATE MANAGEMENT
// ============================================================================

void LifeMatrix::set_ui_mode(UIMode mode) {
  if (ui_mode_ != mode) {
    ESP_LOGD(TAG, "UI mode changed: %d -> %d", ui_mode_, mode);
    ui_mode_ = mode;
    handle_input();  // Reset timeout timer
    update_status_led();  // Update LED color

    // Reset screen cycle timer when entering/exiting settings or manual browse
    // This prevents unexpected screen changes when returning to auto-cycle
    if (mode == AUTO_CYCLE || mode == SETTINGS) {
      last_switch_time_ = millis();
    }
  }
}

void LifeMatrix::check_ui_timeout() {
  unsigned long now = millis();
  unsigned long elapsed = now - ui_last_input_ms_;

  if (ui_mode_ == MANUAL_BROWSE && elapsed >= 10000) {
    // Return to auto cycle after 10s of inactivity
    set_ui_mode(AUTO_CYCLE);
  } else if (ui_mode_ == SETTINGS && elapsed >= 10000) {
    // Exit settings to manual browse after 10s
    set_ui_mode(MANUAL_BROWSE);
    settings_cursor_ = 0;
    ui_last_input_ms_ = now;
  }
}

void LifeMatrix::handle_input() {
  ui_last_input_ms_ = millis();
}

void LifeMatrix::toggle_pause() {
  ui_paused_ = !ui_paused_;
  ESP_LOGD(TAG, "UI pause toggled: %s", ui_paused_ ? "paused" : "playing");
  update_status_led();  // Update LED for pause state
}

void LifeMatrix::update_status_led() {
  if (!status_led_) return;

  auto call = status_led_->make_call();

  if (ui_paused_) {
    // Paused: Red with pulse effect
    call.set_state(true);
    call.set_rgb(1.0f, 0.0f, 0.0f);
    call.set_effect("Slow Pulse");
  } else {
    // Not paused: color based on mode, no effect
    call.set_effect("none");
    call.set_state(true);

    if (ui_mode_ == AUTO_CYCLE) {
      // Auto cycle: Dim blue
      call.set_rgb(0.0f, 0.0f, 0.3f);
    } else if (ui_mode_ == MANUAL_BROWSE) {
      // Manual browse: Dim green
      call.set_rgb(0.0f, 0.3f, 0.0f);
    } else {
      // Settings: Dim yellow
      call.set_rgb(0.3f, 0.3f, 0.0f);
    }
  }

  call.perform();
}

void LifeMatrix::next_settings_cursor() {
  handle_input();  // Reset timeout

  // Get current screen to determine max cursor position
  int screen_id = get_current_screen_id();
  int max_cursor = 2;  // Global settings (brightness, cycle time, text position)

  // Per-screen settings
  if (screen_id == SCREEN_HOUR) {
    max_cursor = 7;  // 3 global + 5 hour settings
  } else if (screen_id == SCREEN_DAY) {
    max_cursor = 5;  // 3 global + 3 day settings
  } else if (screen_id == SCREEN_GAME_OF_LIFE) {
    max_cursor = 4;  // 3 global + 2 GoL settings
  } else if (screen_id == SCREEN_MONTH) {
    max_cursor = 6;  // 3 global + 4 month settings (style, fill dir, day fill, marker color)
  } else if (screen_id == SCREEN_YEAR) {
    max_cursor = 6;  // 3 global + 4 year settings
  }

  settings_cursor_ = (settings_cursor_ + 1) % (max_cursor + 1);
  settings_flash_ms_ = millis();
  ESP_LOGD(TAG, "Settings cursor: %d", settings_cursor_);
}

void LifeMatrix::prev_settings_cursor() {
  handle_input();  // Reset timeout

  // Get current screen to determine max cursor position
  int screen_id = get_current_screen_id();
  int max_cursor = 2;  // Global settings

  // Per-screen settings
  if (screen_id == SCREEN_HOUR) {
    max_cursor = 7;
  } else if (screen_id == SCREEN_DAY) {
    max_cursor = 5;
  } else if (screen_id == SCREEN_GAME_OF_LIFE) {
    max_cursor = 4;
  } else if (screen_id == SCREEN_MONTH) {
    max_cursor = 6;  // 3 global + 4 month settings (style, fill dir, day fill, marker color)
  } else if (screen_id == SCREEN_YEAR) {
    max_cursor = 6;
  }

  settings_cursor_ = (settings_cursor_ - 1 + max_cursor + 1) % (max_cursor + 1);
  settings_flash_ms_ = millis();
  ESP_LOGD(TAG, "Settings cursor: %d", settings_cursor_);
}

void LifeMatrix::adjust_setting(int direction) {
  handle_input();  // Reset timeout

  // Helper: cycle through enum values
  auto cycle_enum = [direction](int current, int max_value) {
    int new_val = current + direction;
    if (new_val < 0) new_val = max_value;
    if (new_val > max_value) new_val = 0;
    return new_val;
  };

  // Helper: adjust number with wrapping
  auto adjust_number = [direction](int current, int min_val, int max_val, bool wrap) {
    int new_val = current + direction;
    if (wrap) {
      if (new_val > max_val) new_val = min_val;
      if (new_val < min_val) new_val = max_val;
    } else {
      if (new_val > max_val) new_val = max_val;
      if (new_val < min_val) new_val = min_val;
    }
    return new_val;
  };

  // Enum-to-HA-option-string lookup tables
  static const char* const cs_names[] = {"Single Color", "Gradient", "Time Segments", "Rainbow"};
  static const char* const gt_names[] = {"Red-Blue", "Green-Yellow", "Cyan-Magenta", "Purple-Orange", "Blue-Yellow"};
  static const char* const ms_names[] = {"None", "Single Dot", "Gradient Peak"};
  static const char* const mc_names[] = {"Blue", "White", "Yellow", "Red", "Green", "Cyan", "Magenta"};
  static const char* const yds_names[] = {"Fixed", "Flat", "Shaded"};
  static const char* const speed_names[] = {"Fast (50ms)", "Normal (200ms)", "Slow (1000ms)"};

  // Not in settings mode: adjust screen cycle time
  if (ui_mode_ != SETTINGS) {
    screen_cycle_time_ = (float)adjust_number((int)screen_cycle_time_, 1, 10, false);
    if (ha_cycle_time_) ha_cycle_time_->publish_state(screen_cycle_time_);
    ESP_LOGD(TAG, "Adjusted cycle time: %.0fs", screen_cycle_time_);
    settings_flash_ms_ = millis();
    return;
  }

  // Get current screen
  int screen_id = get_current_screen_id();
  int cursor = settings_cursor_;

  // Global settings (cursor 0-2)
  if (cursor == 0) {
    // Brightness - not stored in component, handled by YAML
    ESP_LOGD(TAG, "Brightness adjustment (external control)");
  } else if (cursor == 1) {
    // Cycle time
    screen_cycle_time_ = (float)adjust_number((int)screen_cycle_time_, 1, 10, false);
    if (ha_cycle_time_) ha_cycle_time_->publish_state(screen_cycle_time_);
    ESP_LOGD(TAG, "Cycle time: %.0fs", screen_cycle_time_);
  } else if (cursor == 2) {
    // Text area position
    if (text_area_position_ == "Top") text_area_position_ = "Bottom";
    else if (text_area_position_ == "Bottom") text_area_position_ = "None";
    else text_area_position_ = "Top";
    if (ha_text_area_position_) ha_text_area_position_->publish_state(text_area_position_);
    ESP_LOGD(TAG, "Text position: %s", text_area_position_.c_str());
  } else {
    // Per-screen settings (cursor 3+)
    int local = cursor - 3;

    if (screen_id == SCREEN_HOUR) {
      if (local == 0) {
        style_ = (DisplayStyle)cycle_enum((int)style_, 3);
        if (ha_style_) ha_style_->publish_state(cs_names[style_]);
        ESP_LOGD(TAG, "Style: %d", (int)style_);
      } else if (local == 1) {
        gradient_type_ = (GradientType)cycle_enum((int)gradient_type_, 4);
        if (ha_gradient_type_) ha_gradient_type_->publish_state(gt_names[gradient_type_]);
        ESP_LOGD(TAG, "Gradient type: %d", (int)gradient_type_);
      } else if (local == 2) {
        fill_direction_bottom_to_top_ = !fill_direction_bottom_to_top_;
        if (ha_fill_direction_) ha_fill_direction_->publish_state(fill_direction_bottom_to_top_ ? "Bottom to Top" : "Top to Bottom");
        ESP_LOGD(TAG, "Fill direction: %s", fill_direction_bottom_to_top_ ? "Bottom to Top" : "Top to Bottom");
      } else if (local == 3) {
        marker_style_ = (MarkerStyle)cycle_enum((int)marker_style_, 2);
        if (ha_marker_style_) ha_marker_style_->publish_state(ms_names[marker_style_]);
        ESP_LOGD(TAG, "Marker style: %d", (int)marker_style_);
      } else if (local == 4) {
        marker_color_ = (MarkerColor)cycle_enum((int)marker_color_, 6);
        if (ha_marker_color_) ha_marker_color_->publish_state(mc_names[marker_color_]);
        ESP_LOGD(TAG, "Marker color: %d", (int)marker_color_);
      }
    } else if (screen_id == SCREEN_DAY) {
      if (local == 0) {
        time_segments_.bed_time_hour = adjust_number(time_segments_.bed_time_hour, 0, 23, true);
        if (ha_bed_time_hour_) ha_bed_time_hour_->publish_state(time_segments_.bed_time_hour);
        ESP_LOGD(TAG, "Bed time: %d:00", time_segments_.bed_time_hour);
      } else if (local == 1) {
        time_segments_.work_start_hour = adjust_number(time_segments_.work_start_hour, 0, 23, true);
        if (ha_work_start_hour_) ha_work_start_hour_->publish_state(time_segments_.work_start_hour);
        ESP_LOGD(TAG, "Work start: %d:00", time_segments_.work_start_hour);
      } else if (local == 2) {
        time_segments_.work_end_hour = adjust_number(time_segments_.work_end_hour, 0, 23, true);
        if (ha_work_end_hour_) ha_work_end_hour_->publish_state(time_segments_.work_end_hour);
        ESP_LOGD(TAG, "Work end: %d:00", time_segments_.work_end_hour);
      }
    } else if (screen_id == SCREEN_GAME_OF_LIFE) {
      if (local == 0) {
        // Speed: cycle through 50, 200, 1000 ms
        int speeds[] = {50, 200, 1000};
        int current_idx = 1;  // Default 200ms
        for (int i = 0; i < 3; i++) {
          if (game_config_.update_interval_ms == speeds[i]) {
            current_idx = i;
            break;
          }
        }
        current_idx = (current_idx + direction + 3) % 3;
        game_config_.update_interval_ms = speeds[current_idx];
        if (ha_conway_speed_) ha_conway_speed_->publish_state(speed_names[current_idx]);
        ESP_LOGD(TAG, "GoL speed: %dms", game_config_.update_interval_ms);
      } else if (local == 1) {
        game_config_.complex_patterns = !game_config_.complex_patterns;
        if (ha_complex_patterns_) ha_complex_patterns_->publish_state(game_config_.complex_patterns);
        ESP_LOGD(TAG, "Complex patterns: %s", game_config_.complex_patterns ? "on" : "off");
      }
    } else if (screen_id == SCREEN_MONTH) {
      if (local == 0) {
        style_ = (DisplayStyle)cycle_enum((int)style_, 3);
        if (ha_style_) ha_style_->publish_state(cs_names[style_]);
        ESP_LOGD(TAG, "Style: %d", (int)style_);
      } else if (local == 1) {
        fill_direction_bottom_to_top_ = !fill_direction_bottom_to_top_;
        if (ha_fill_direction_) ha_fill_direction_->publish_state(fill_direction_bottom_to_top_ ? "Bottom to Top" : "Top to Bottom");
        ESP_LOGD(TAG, "Fill direction: %s", fill_direction_bottom_to_top_ ? "Bottom to Top" : "Top to Bottom");
      } else if (local == 2) {
        day_fill_style_ = (DayFillStyle)cycle_enum((int)day_fill_style_, 2);
        if (ha_day_fill_) ha_day_fill_->publish_state(yds_names[day_fill_style_]);
        ESP_LOGD(TAG, "Day fill: %d", (int)day_fill_style_);
      } else if (local == 3) {
        marker_color_ = (MarkerColor)cycle_enum((int)marker_color_, 6);
        if (ha_marker_color_) ha_marker_color_->publish_state(mc_names[marker_color_]);
        ESP_LOGD(TAG, "Marker color: %d", (int)marker_color_);
      }
    } else if (screen_id == SCREEN_YEAR) {
      if (local == 0) {
        style_ = (DisplayStyle)cycle_enum((int)style_, 3);
        if (ha_style_) ha_style_->publish_state(cs_names[style_]);
        ESP_LOGD(TAG, "Style: %d", (int)style_);
      } else if (local == 1) {
        marker_style_ = (MarkerStyle)cycle_enum((int)marker_style_, 2);
        if (ha_marker_style_) ha_marker_style_->publish_state(ms_names[marker_style_]);
        ESP_LOGD(TAG, "Marker style: %d", (int)marker_style_);
      } else if (local == 2) {
        day_fill_style_ = (DayFillStyle)cycle_enum((int)day_fill_style_, 2);
        if (ha_day_fill_) ha_day_fill_->publish_state(yds_names[day_fill_style_]);
        ESP_LOGD(TAG, "Day fill: %d", (int)day_fill_style_);
      } else if (local == 3) {
        if (year_event_style_ == YEAR_EVENT_NONE) {
          year_event_style_ = YEAR_EVENT_PULSE;
        } else if (year_event_style_ == YEAR_EVENT_PULSE) {
          year_event_style_ = YEAR_EVENT_MARKERS;
        } else {
          year_event_style_ = YEAR_EVENT_NONE;
        }
        const char* yes_name = (year_event_style_ == YEAR_EVENT_NONE) ? "None" :
                               (year_event_style_ == YEAR_EVENT_PULSE) ? "Pulse" : "Markers";
        if (ha_year_event_style_) ha_year_event_style_->publish_state(yes_name);
        ESP_LOGD(TAG, "Year event style: %d", (int)year_event_style_);
      }
    }
  }

  // Flash animation timing
  settings_flash_ms_ = millis();
}

std::string LifeMatrix::get_current_setting_name() {
  int cursor = settings_cursor_;
  int screen_id = get_current_screen_id();

  // Global settings
  if (cursor == 0) return "Brite";
  if (cursor == 1) return "Cycle";
  if (cursor == 2) return "Text";

  // Per-screen settings
  int local = cursor - 3;
  if (screen_id == SCREEN_HOUR) {
    if (local == 0) return "Style";
    if (local == 1) return "Grad";
    if (local == 2) return "Fill";
    if (local == 3) return "Mark";
    if (local == 4) return "MkClr";
  } else if (screen_id == SCREEN_DAY) {
    if (local == 0) return "Sleep";
    if (local == 1) return "WkBeg";
    if (local == 2) return "WkEnd";
  } else if (screen_id == SCREEN_GAME_OF_LIFE) {
    if (local == 0) return "Speed";
    if (local == 1) return "Cmplx";
  } else if (screen_id == SCREEN_MONTH) {
    if (local == 0) return "Style";
    if (local == 1) return "Fill";
    if (local == 2) return "DFill";
    if (local == 3) return "MkClr";
  } else if (screen_id == SCREEN_YEAR) {
    if (local == 0) return "Style";
    if (local == 1) return "Mark";
    if (local == 2) return "DFill";
    if (local == 3) return "Event";
  }
  return "?";
}

std::string LifeMatrix::get_current_setting_value() {
  int cursor = settings_cursor_;
  int screen_id = get_current_screen_id();
  char buf[32];

  // Global settings (these need to be read from external state in YAML)
  if (cursor == 0) return "**";  // Brightness - read from HA entity
  if (cursor == 1) return "**";  // Cycle time - read from HA entity
  if (cursor == 2) {
    // Text area position
    if (text_area_position_ == "Top") return "Top";
    if (text_area_position_ == "Bottom") return "Bottom";
    return "None";
  }

  // Per-screen settings
  int local = cursor - 3;
  if (screen_id == SCREEN_HOUR) {
    if (local == 0) {
      // Color scheme
      if (style_ == STYLE_SINGLE) return "Singl";
      if (style_ == STYLE_GRADIENT) return "Gradt";
      if (style_ == STYLE_TIME_SEGMENTS) return "TmSeg";
      if (style_ == STYLE_RAINBOW) return "Rainb";
    } else if (local == 1) {
      // Gradient type
      if (gradient_type_ == GRADIENT_RED_BLUE) return "RedBl";
      if (gradient_type_ == GRADIENT_GREEN_YELLOW) return "GrnYl";
      if (gradient_type_ == GRADIENT_CYAN_MAGENTA) return "CynMg";
      if (gradient_type_ == GRADIENT_PURPLE_ORANGE) return "PurOr";
      if (gradient_type_ == GRADIENT_BLUE_YELLOW) return "BluYl";
    } else if (local == 2) {
      // Fill direction
      return fill_direction_bottom_to_top_ ? "BotT" : "TopB";
    } else if (local == 3) {
      // Marker style
      if (marker_style_ == MARKER_NONE) return "None";
      if (marker_style_ == MARKER_SINGLE_DOT) return "Dot";
      if (marker_style_ == MARKER_GRADIENT_PEAK) return "Peak";
    } else if (local == 4) {
      // Marker color
      if (marker_color_ == MARKER_BLUE) return "Blue";
      if (marker_color_ == MARKER_WHITE) return "White";
      if (marker_color_ == MARKER_YELLOW) return "Yellw";
      if (marker_color_ == MARKER_RED) return "Red";
      if (marker_color_ == MARKER_GREEN) return "Green";
      if (marker_color_ == MARKER_CYAN) return "Cyan";
      if (marker_color_ == MARKER_MAGENTA) return "Magnt";
    }
  } else if (screen_id == SCREEN_DAY) {
    if (local == 0) {
      snprintf(buf, sizeof(buf), "%dh", time_segments_.bed_time_hour);
      return buf;
    } else if (local == 1) {
      snprintf(buf, sizeof(buf), "%dh", time_segments_.work_start_hour);
      return buf;
    } else if (local == 2) {
      snprintf(buf, sizeof(buf), "%dh", time_segments_.work_end_hour);
      return buf;
    }
  } else if (screen_id == SCREEN_GAME_OF_LIFE) {
    if (local == 0) {
      // Speed
      int ms = game_config_.update_interval_ms;
      if (ms <= 50) return "Fast";
      if (ms <= 200) return "Norml";
      return "Slow";
    } else if (local == 1) {
      return game_config_.complex_patterns ? "ON" : "OFF";
    }
  } else if (screen_id == SCREEN_MONTH) {
    if (local == 0) {
      if (style_ == STYLE_SINGLE) return "Singl";
      if (style_ == STYLE_GRADIENT) return "Gradt";
      if (style_ == STYLE_TIME_SEGMENTS) return "TmSeg";
      if (style_ == STYLE_RAINBOW) return "Rainb";
    } else if (local == 1) {
      return fill_direction_bottom_to_top_ ? "BotT" : "TopB";
    } else if (local == 2) {
      // Day fill
      if (day_fill_style_ == DAY_FILL_ACTIVITY) return "Fixed";
      if (day_fill_style_ == DAY_FILL_SCHEME) return "Flat";
      if (day_fill_style_ == DAY_FILL_MIXED) return "Shade";
    } else if (local == 3) {
      if (marker_color_ == MARKER_BLUE) return "Blue";
      if (marker_color_ == MARKER_WHITE) return "White";
      if (marker_color_ == MARKER_YELLOW) return "Yellw";
      if (marker_color_ == MARKER_RED) return "Red";
      if (marker_color_ == MARKER_GREEN) return "Green";
      if (marker_color_ == MARKER_CYAN) return "Cyan";
      if (marker_color_ == MARKER_MAGENTA) return "Magnt";
    }
  } else if (screen_id == SCREEN_YEAR) {
    if (local == 0) {
      // Color scheme
      if (style_ == STYLE_SINGLE) return "Singl";
      if (style_ == STYLE_GRADIENT) return "Gradt";
      if (style_ == STYLE_TIME_SEGMENTS) return "TmSeg";
      if (style_ == STYLE_RAINBOW) return "Rainb";
    } else if (local == 1) {
      // Marker style
      if (marker_style_ == MARKER_NONE) return "None";
      if (marker_style_ == MARKER_SINGLE_DOT) return "Dot";
      if (marker_style_ == MARKER_GRADIENT_PEAK) return "Peak";
    } else if (local == 2) {
      // Day fill
      if (day_fill_style_ == DAY_FILL_ACTIVITY) return "Fixed";
      if (day_fill_style_ == DAY_FILL_SCHEME) return "Flat";
      if (day_fill_style_ == DAY_FILL_MIXED) return "Shade";
    } else if (local == 3) {
      // Event style
      if (year_event_style_ == YEAR_EVENT_NONE) return "None";
      if (year_event_style_ == YEAR_EVENT_PULSE) return "Pulse";
      if (year_event_style_ == YEAR_EVENT_MARKERS) return "Marks";
    }
  }
  return "?";
}

// ============================================================================
// SCREEN MANAGEMENT
// ============================================================================

void LifeMatrix::register_screen(int screen_id, bool enabled) {
  // Check if screen already registered
  bool found = false;
  for (auto &screen : screens_) {
    if (screen.id == screen_id) {
      screen.enabled = enabled;
      found = true;
      ESP_LOGD(TAG, "Updated screen %d (%s): %s", screen_id, screen.name.c_str(), enabled ? "enabled" : "disabled");
      break;
    }
  }

  // Add new screen if not found
  if (!found) {
    ScreenConfig config;
    config.id = screen_id;
    config.enabled = enabled;

    // Set screen names
    switch (screen_id) {
      case SCREEN_YEAR: config.name = "Year"; break;
      case SCREEN_MONTH: config.name = "Month"; break;
      case SCREEN_DAY: config.name = "Day"; break;
      case SCREEN_HOUR: config.name = "Hour"; break;
      case SCREEN_HABITS: config.name = "Habits"; break;
      case SCREEN_GAME_OF_LIFE: config.name = "Conway"; break;
      case SCREEN_LIFESPAN:     config.name = "Life"; break;
      default: config.name = "Unknown"; break;
    }

    screens_.push_back(config);
    ESP_LOGD(TAG, "Registered new screen %d (%s): %s", screen_id, config.name.c_str(), enabled ? "enabled" : "disabled");
  }

  // Always rebuild enabled screens list to ensure consistency
  enabled_screen_ids_.clear();
  for (const auto &screen : screens_) {
    if (screen.enabled) {
      enabled_screen_ids_.push_back(screen.id);
    }
  }

  // If current screen was disabled, switch to another enabled screen
  if (!enabled && get_current_screen_id() == screen_id && !enabled_screen_ids_.empty()) {
    current_screen_idx_ = 0;
    ESP_LOGD(TAG, "Current screen was disabled, switching to screen ID %d", get_current_screen_id());
  }

  ESP_LOGD(TAG, "Total enabled screens: %d", (int)enabled_screen_ids_.size());
}

void LifeMatrix::update_screen_cycle() {
  // Only auto-advance in AUTO_CYCLE mode when not paused
  if (ui_mode_ != AUTO_CYCLE || ui_paused_ || enabled_screen_ids_.empty()) {
    return;
  }

  // Pause cycling when Game of Life is showing demo mode or reset animation
  if (get_current_screen_id() == SCREEN_GAME_OF_LIFE) {
    if (game_demo_mode_ || game_reset_animation_) {
      return;  // Don't auto-advance while showing instructions or animation
    }
  }

  unsigned long current_time_ms = millis();
  unsigned long cycle_interval_ms = (unsigned long)(screen_cycle_time_ * 1000.0f);

  if ((current_time_ms - last_switch_time_) >= cycle_interval_ms) {
    current_screen_idx_ = (current_screen_idx_ + 1) % enabled_screen_ids_.size();
    last_switch_time_ = current_time_ms;
    ESP_LOGD(TAG, "Auto-cycled to screen index %d (ID %d)", current_screen_idx_, get_current_screen_id());
  }
}

void LifeMatrix::next_screen() {
  if (enabled_screen_ids_.empty()) {
    return;
  }

  handle_input();  // Reset timeout
  set_ui_mode(MANUAL_BROWSE);

  current_screen_idx_ = (current_screen_idx_ + 1) % enabled_screen_ids_.size();
  last_switch_time_ = millis();

  ESP_LOGD(TAG, "Next screen: index %d (ID %d)", current_screen_idx_, get_current_screen_id());
}

void LifeMatrix::prev_screen() {
  if (enabled_screen_ids_.empty()) {
    return;
  }

  handle_input();  // Reset timeout
  set_ui_mode(MANUAL_BROWSE);

  current_screen_idx_--;
  if (current_screen_idx_ < 0) {
    current_screen_idx_ = enabled_screen_ids_.size() - 1;
  }
  last_switch_time_ = millis();

  ESP_LOGD(TAG, "Prev screen: index %d (ID %d)", current_screen_idx_, get_current_screen_id());
}

void LifeMatrix::set_current_screen(int screen_idx) {
  if (enabled_screen_ids_.empty() || screen_idx < 0) {
    return;
  }

  current_screen_idx_ = screen_idx % enabled_screen_ids_.size();
  last_switch_time_ = millis();
}

int LifeMatrix::get_current_screen_id() {
  if (enabled_screen_ids_.empty() || current_screen_idx_ < 0 || current_screen_idx_ >= (int)enabled_screen_ids_.size()) {
    return -1;
  }
  return enabled_screen_ids_[current_screen_idx_];
}

// ============================================================================
// RENDERING
// ============================================================================

Viewport LifeMatrix::calculate_viewport(display::Display &it) {
  Viewport vp;
  int width = it.get_width();
  int height = it.get_height();
  int text_area_height = 8;

  if (text_area_position_ == "Top") {
    vp.text_y = 2;
    vp.viz_y = text_area_height;
    vp.viz_height = height - text_area_height;
  } else if (text_area_position_ == "Bottom") {
    vp.text_y = height - 5;
    vp.viz_y = 0;
    vp.viz_height = height - text_area_height;
  } else {
    // None - use full screen
    vp.text_y = height / 2;
    vp.viz_y = 0;
    vp.viz_height = height;
  }

  return vp;
}

Color LifeMatrix::hsv_to_rgb(int hue, float saturation, float value) {
  // Convert HSV to RGB (hue in degrees 0-360)
  hue = hue % 360;

  if (hue < 60) {
    return Color((uint8_t)(255 * value), (uint8_t)(hue * 255 / 60 * value), 0);
  } else if (hue < 120) {
    return Color((uint8_t)((255 - (hue - 60) * 255 / 60) * value), (uint8_t)(255 * value), 0);
  } else if (hue < 180) {
    return Color(0, (uint8_t)(255 * value), (uint8_t)((hue - 120) * 255 / 60 * value));
  } else if (hue < 240) {
    return Color(0, (uint8_t)((255 - (hue - 180) * 255 / 60) * value), (uint8_t)(255 * value));
  } else if (hue < 300) {
    return Color((uint8_t)((hue - 240) * 255 / 60 * value), 0, (uint8_t)(255 * value));
  } else {
    return Color((uint8_t)(255 * value), 0, (uint8_t)((255 - (hue - 300) * 255 / 60) * value));
  }
}

// draw_pixel: routes all main-display pixels through the active per-frame color transform.
// CTM_HUE_SHIFT uses a precomputed circulant matrix (set in render() pre-render block):
//   a = cosθ + (1-cosθ)/3   b = (1-cosθ)/3 + sinθ/√3   c = (1-cosθ)/3 - sinθ/√3
//   r' = a·r + c·g + b·b    g' = b·r + a·g + c·b    b' = c·r + b·g + a·b
// Celebration overlay functions call it.draw_pixel_at directly to bypass transforms.
void LifeMatrix::draw_pixel(display::Display &it, int x, int y, Color c) {
  if (ctm_ == CTM_HUE_SHIFT && (c.r | c.g | c.b)) {
    int r = (int)(c.r * hue_mat_a_ + c.g * hue_mat_c_ + c.b * hue_mat_b_);
    int g = (int)(c.r * hue_mat_b_ + c.g * hue_mat_a_ + c.b * hue_mat_c_);
    int b = (int)(c.r * hue_mat_c_ + c.g * hue_mat_b_ + c.b * hue_mat_a_);
    c = Color((uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r),
              (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g),
              (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b));
  }
  it.draw_pixel_at(x, y, c);
}

void LifeMatrix::render(display::Display &it, ESPTime &time) {
  // OTA guard - show minimal UI during update
  if (ota_in_progress_) {
    int center_x = it.get_width() / 2;
    int center_y = it.get_height() / 2;
    it.print(center_x, center_y, font_small_, color_active_, display::TextAlign::CENTER, "OTA");
    return;
  }

  // Build display time — fake time ticks forward from the moment it was set
  ESPTime display_time_val;
  if (time_override_active_) {
    display_time_val = fake_time_;
    uint32_t elapsed_s = (uint32_t)((millis() - time_override_start_ms_) / 1000u);
    int s = display_time_val.second + (int)elapsed_s;
    int m = display_time_val.minute + s / 60;
    display_time_val.second = s % 60;
    int h = display_time_val.hour + m / 60;
    display_time_val.minute = m % 60;
    int day_carry = h / 24;
    display_time_val.hour = h % 24;
    if (day_carry > 0) {
      display_time_val.day_of_month += day_carry;
      display_time_val.day_of_year  += day_carry;
    }
  } else {
    display_time_val = time;
  }
  ESPTime &display_time = display_time_val;

  // Check for hourly celebration trigger (on all screens)
  check_celebration(display_time);

  // Calculate viewport
  Viewport vp = calculate_viewport(it);

  // Get current screen to render
  int screen_id = get_current_screen_id();
  if (screen_id < 0) {
    // No screens enabled
    int center_x = it.get_width() / 2;
    int center_y = it.get_height() / 2;
    it.print(center_x, center_y - 5, font_small_, color_active_, display::TextAlign::CENTER, "No");
    it.print(center_x, center_y + 5, font_small_, color_active_, display::TextAlign::CENTER, "Views");
    return;
  }

  // Pre-render: configure color transform for CELEB_HUE_CYCLE.
  // Precompute the circulant hue-rotation matrix once per frame (2 trig calls total).
  ctm_ = CTM_NONE;
  hue_mat_a_ = 1.f; hue_mat_b_ = 0.f; hue_mat_c_ = 0.f;  // identity
  if (celebration_active_ && celeb_seq_idx_ < celeb_seq_len_ &&
      celeb_sequence_[celeb_seq_idx_] == CELEB_HUE_CYCLE) {
    uint32_t pre_elapsed = (uint32_t)(millis() - celebration_start_);
    uint32_t dur = get_celeb_duration(CELEB_HUE_CYCLE);
    float theta = (float)(pre_elapsed % dur) * (6.28318f / (float)dur);  // 0 → 2π over duration
    float cos_h = cosf(theta), sin_h = sinf(theta);
    float k = (1.f - cos_h) / 3.f;
    float s = sin_h * 0.57735f;  // sinθ / √3
    hue_mat_a_ = cos_h + k;
    hue_mat_b_ = k + s;
    hue_mat_c_ = k - s;
    ctm_ = CTM_HUE_SHIFT;
  }

  // Render the appropriate screen
  switch (screen_id) {
    case SCREEN_YEAR:
      render_year_view(it, display_time, vp.viz_y, vp.viz_height);
      break;
    case SCREEN_MONTH:
      render_month_view(it, display_time, vp.viz_y, vp.viz_height);
      break;
    case SCREEN_DAY:
      render_day_view(it, display_time, vp.viz_y, vp.viz_height);
      break;
    case SCREEN_HOUR:
      render_hour_view(it, display_time, vp.viz_y, vp.viz_height);
      break;
    case SCREEN_GAME_OF_LIFE:
      render_game_of_life(it, vp.viz_y, vp.viz_height);
      break;
    case SCREEN_LIFESPAN:
      render_lifespan_view(it, display_time, vp.viz_y, vp.viz_height);
      break;
    case SCREEN_HABITS:
      // Placeholder for habits screen
      {
        int center_x = it.get_width() / 2;
        int center_y = vp.viz_y + vp.viz_height / 2;
        it.print(center_x, vp.text_y, font_small_, color_active_, display::TextAlign::CENTER, "Habit");
        it.print(center_x, center_y - 5, font_small_, color_highlight_, display::TextAlign::CENTER, "Soon");
      }
      break;
  }

  // Celebration sequence: advance phases, draw overlays for styles that need them
  if (celebration_active_) {
    bool is_time_screen = (screen_id == SCREEN_HOUR || screen_id == SCREEN_DAY ||
                           screen_id == SCREEN_MONTH || screen_id == SCREEN_YEAR);
    uint32_t elapsed = (uint32_t)(millis() - celebration_start_);
    CelebrationStyle cur_style = celeb_sequence_[celeb_seq_idx_];
    uint32_t cur_dur = get_celeb_duration(cur_style);

    if (elapsed >= cur_dur) {
      celeb_seq_idx_++;
      if (celeb_seq_idx_ >= celeb_seq_len_) {
        celebration_active_ = false;
      } else {
        celebration_start_ = millis();
      }
    } else if (is_time_screen && cur_style != CELEB_HUE_CYCLE) {
      // HUE_CYCLE works entirely via the pre-render matrix transform — no overlay needed
      render_celebration_overlay(it, elapsed);
    }
  }

  // Render UI overlays on top
  render_ui_overlays(it);
}

void LifeMatrix::render_game_of_life(display::Display &it, int viz_y, int viz_height) {
  int center_x = it.get_width() / 2;
  int width = it.get_width();

  // Check if big bang animation is active (first 1 second after reset)
  if (game_reset_animation_) {
    unsigned long elapsed = millis() - game_reset_animation_start_;
    if (elapsed >= 1000) {
      game_reset_animation_ = false;  // End animation after 1 second
    } else {
      render_big_bang_animation(it, viz_y, viz_height);
      return;
    }
  }

  // Check if still in demo mode (first 5 seconds)
  if (game_demo_mode_) {
    if (millis() - game_demo_start_time_ >= 5000) {
      game_demo_mode_ = false;  // Exit demo mode after 5 seconds
      last_switch_time_ = millis();  // Reset cycle timer so screen stays for full duration

      // NOW initialize the world (choose pattern based on complex_patterns setting)
      PatternType pattern = game_config_.complex_patterns ? PATTERN_MIXED : PATTERN_RANDOM;
      initialize_game_of_life(pattern);
    } else {
      // Display rules - title with each word on one line (tighter spacing)
      it.print(center_x, 4, font_small_, color_highlight_, display::TextAlign::TOP_CENTER, "Game");
      it.print(center_x, 12, font_small_, color_highlight_, display::TextAlign::TOP_CENTER, "of");
      it.print(center_x, 20, font_small_, color_highlight_, display::TextAlign::TOP_CENTER, "Life");

      // Draw common patterns between title and rules
      Color pattern_color = Color(80, 80, 120);

      // Glider (left side)
      draw_pixel(it, 6, 40, pattern_color);
      draw_pixel(it, 7, 41, pattern_color);
      draw_pixel(it, 5, 42, pattern_color);
      draw_pixel(it, 6, 42, pattern_color);
      draw_pixel(it, 7, 42, pattern_color);

      // Blinker (center)
      draw_pixel(it, 15, 41, pattern_color);
      draw_pixel(it, 16, 41, pattern_color);
      draw_pixel(it, 17, 41, pattern_color);

      // Block (right side)
      draw_pixel(it, 25, 41, pattern_color);
      draw_pixel(it, 26, 41, pattern_color);
      draw_pixel(it, 25, 42, pattern_color);
      draw_pixel(it, 26, 42, pattern_color);

      it.print(center_x, 50, font_small_, color_active_, display::TextAlign::TOP_CENTER, "Rules:");
      it.print(center_x, 62, font_small_, Color(0, 255, 150), display::TextAlign::TOP_CENTER, "2-3 OK");
      it.print(center_x, 72, font_small_, Color(0, 150, 255), display::TextAlign::TOP_CENTER, "3 Born");
      it.print(center_x, 82, font_small_, Color(255, 50, 0), display::TextAlign::TOP_CENTER, "* Die");

      return;  // Don't update game during demo
    }
  }

  // Display generation statistics in text area
  Viewport vp = calculate_viewport(it);
  unsigned long current_millis = millis();

  // Show countdown if stable with breathing animation
  if (game_is_stable_) {
    unsigned long elapsed_ms = current_millis - game_stable_since_;
    int seconds_remaining = (game_config_.stability_timeout_ms / 1000) - (elapsed_ms / 1000);

    // Breathing animation: ±50% of base brightness (sine wave 0.5 to 1.5)
    float breath = 1.0f + 0.5f * sinf((float)(current_millis % 2000) / 318.3f);
    int base_brightness = 150;  // Base red brightness
    Color countdown_color = Color(
      (uint8_t)(base_brightness * breath),
      0,
      0
    );

    it.printf(center_x, vp.text_y, font_small_, countdown_color, display::TextAlign::CENTER,
              "-%ds", seconds_remaining);
  } else {
    // Alternate between generation and births/deaths
    bool show_generation = ((current_millis / 5000) % 2) == 0;
    if (show_generation) {
      const char* gen_label = (game_generation_ >= 100) ? "G" : "Gen";
      it.printf(2, vp.text_y, font_small_, color_active_, display::TextAlign::CENTER_LEFT,
                "%s %d", gen_label, game_generation_);
    } else {
      it.printf(1, vp.text_y, font_small_, Color(0, 150, 255), display::TextAlign::CENTER_LEFT,
                "%d", game_births_);
      it.printf(width, vp.text_y, font_small_, Color(255, 50, 0), display::TextAlign::CENTER_RIGHT,
                "%d", game_deaths_);
    }
  }

  // Draw the grid with age-based coloring (direct array access)
  int max_row = std::min(viz_height, grid_height_);
  int hue_divisor = grid_width_ + grid_height_;

  for (int row = 0; row < max_row; row++) {
    // Yield every 30 rows to let WiFi stack process
    if (row > 0 && (row % 30) == 0) delay(0);

    int y_pos = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - row) : (viz_y + row);
    int row_offset = row * grid_width_;

    for (int col = 0; col < grid_width_; col++) {
      uint8_t age = game_grid_[row_offset + col];

      if (age > 0) {
        Color cell_color;

        if (age < 5) {
          cell_color = Color(0, 255, 255);
        } else if (age < 15) {
          cell_color = Color(0, 255, 0);
        } else if (age < 30) {
          cell_color = Color(255, 255, 0);
        } else {
          int hue = ((col + row) * 360 / hue_divisor) % 360;
          cell_color = hsv_to_rgb(hue, 1.0f, 1.0f);
        }

        draw_pixel(it, col, y_pos, cell_color);
      }
    }
  }
}

void LifeMatrix::render_big_bang_animation(display::Display &it, int viz_y, int viz_height) {
  int center_x = it.get_width() / 2;
  int half_h = viz_height / 2;

  // Animation progress (0.0 to 1.0 over 1 second)
  unsigned long elapsed = millis() - game_reset_animation_start_;
  float progress = std::min(1.0f, (float)elapsed / 1000.0f);

  // Expanding radius
  float radius = progress * 80.0f;
  float ring_thickness = std::max(1.0f, 8.0f * (1.0f - progress));
  float brightness = 1.0f - (progress * 0.7f);

  // Precompute squared thresholds (avoids sqrtf entirely)
  float inner_r = std::max(0.0f, radius - ring_thickness);
  float inner_r2 = inner_r * inner_r;
  float outer_r2 = radius * radius;
  float cc_r2 = (2.0f + progress * 2.0f);
  cc_r2 *= cc_r2;  // square it

  int hue_offset = (int)(elapsed / 3);

  // Single merged loop for ring + center circle
  for (int row = 0; row < viz_height && row < grid_height_; row++) {
    if (row > 0 && (row % 30) == 0) delay(0);
    int y_pos = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - row) : (viz_y + row);
    float dy = (float)(row - half_h);
    float dy2 = dy * dy;

    for (int col = 0; col < grid_width_; col++) {
      float dx = (float)(col - center_x);
      float dist2 = dx * dx + dy2;

      if (dist2 <= cc_r2) {
        // White center circle (drawn on top of ring)
        draw_pixel(it, col, y_pos, Color(255, 255, 255));
      } else if (dist2 >= inner_r2 && dist2 <= outer_r2) {
        // Rainbow ring — atan2f only for the few ring pixels
        int hue = (int)((atan2f(dy, dx) + 3.14159f) * 57.2958f);
        hue = (hue + hue_offset) % 360;
        draw_pixel(it, col, y_pos, hsv_to_rgb(hue, 1.0f, brightness));
      }
    }
  }
}

void LifeMatrix::render_month_view(display::Display &it, ESPTime &time, int viz_y, int viz_height) {
  Viewport vp = calculate_viewport(it);
  int center_x = it.get_width() / 2;

  // Text area: month name only (no year)
  char month_str[4];
  time.strftime(month_str, sizeof(month_str), "%b");
  it.print(center_x, vp.text_y, font_small_, color_active_, display::TextAlign::CENTER, month_str);

  // Days in this month
  uint8_t month_days[12];
  get_days_in_month(time.year, month_days);
  int days_in_month = month_days[time.month - 1];

  // Build per-day event lookup for the current month
  bool event_day[32] = {};  // index = day-1
  for (const auto &evt : year_events_) {
    if (evt.month == time.month && evt.day >= 1 && evt.day <= 31)
      event_day[evt.day - 1] = true;
  }

  // Grid: 4 columns × 8 rows = 32 slots, days flow left→right, top→bottom
  const int COLS = 4;
  const int ROWS = 8;
  const int cell_w = 8;   // 32px / 4 cols
  int cell_h = viz_height / ROWS;
  if (cell_h < 2) cell_h = 2;

  // Marker color for today border blending
  Color today_clr = get_marker_color_value(marker_color_);

  // Breathing rainbow animation
  float breath_factor = 0.5f + 0.5f * sinf((float)(millis() % 3000u) / 477.0f);
  int hue_offset = (int)((millis() / 27u) % 360u);

  float prog_scale = (days_in_month > 1) ? 1.0f / (float)(days_in_month - 1) : 0.0f;

  // European week offset: day-of-week of day 1, 0=Mon … 6=Sun
  // dow formula: 1=Sun, 2=Mon, …, 7=Sat
  int dow_day1 = ((time.day_of_week - 1 + (1 - (int)time.day_of_month)) % 7 + 7) % 7 + 1;
  int eu_offset_day1 = (dow_day1 == 1) ? 6 : (dow_day1 - 2);  // 0=Mon, …, 6=Sun

  // Track today's cell position for the moment pixel (drawn after the loop)
  int today_cy = viz_y;
  int today_cx = 0;

  for (int day = 1; day <= days_in_month; day++) {
    int slot = day - 1;
    int col = slot % COLS;
    int row_idx = slot / COLS;
    if (row_idx >= ROWS) break;

    int cx = col * cell_w;
    // Fill direction controls row order: bottom-to-top puts day 1 at the bottom row
    int cy = fill_direction_bottom_to_top_
               ? viz_y + (ROWS - 1 - row_idx) * cell_h
               : viz_y + row_idx * cell_h;

    // Day-of-week for this day (to detect weekends)
    int delta = day - time.day_of_month;
    int dow = ((time.day_of_week - 1 + delta) % 7 + 7) % 7 + 1;  // 1=Sun, 7=Sat
    bool is_weekend = (dow == 1 || dow == 7);

    bool is_today  = (day == time.day_of_month);
    bool is_future = (day > time.day_of_month);
    bool is_event  = event_day[day - 1];

    if (is_today) {
      today_cy = cy;
      today_cx = cx;
    }

    // Elapsed pixels within cell_h (uses full 24h span, same as get_activity_type)
    int elapsed_px;
    int today_x_frac = 0;  // x pixels already elapsed within the current-time row
    if (is_future) {
      elapsed_px = 0;
    } else if (is_today) {
      float tf = (time.hour * 60.0f + time.minute) / (24.0f * 60.0f);
      elapsed_px = (int)(tf * cell_h);
      today_x_frac = (int)(tf * cell_w);
      if (today_x_frac >= cell_w) today_x_frac = cell_w;
    } else {
      elapsed_px = cell_h;
    }

    // Per-day accent color driven by style setting
    // Week palette for Time Segments: 5 colors mapping weeks→seasons within the month
    static const Color week_colors[5] = {
      Color(60,  180,  80),   // week 1: spring green
      Color(220, 180,   0),   // week 2: summer gold
      Color(220, 100,  20),   // week 3: autumn orange
      Color(180,  40,  40),   // week 4: late autumn red
      Color( 60,  80, 180),   // week 5: winter blue
    };
    int week_idx = (day - 1 + eu_offset_day1) / 7;  // 0-4, European Mon–Sun weeks
    if (week_idx > 4) week_idx = 4;

    float prog = (float)(day - 1) * prog_scale;
    Color accent;
    if (style_ == STYLE_TIME_SEGMENTS) {
      accent = week_colors[week_idx];
    } else if (style_ == STYLE_GRADIENT) {
      accent = interpolate_gradient(prog, gradient_type_);
    } else if (style_ == STYLE_RAINBOW) {
      accent = hsv_to_rgb((int)(prog * 360.0f), 1.0f, 1.0f);
    } else {
      accent = color_active_;
    }

    // Complementary color for event borders
    Color event_clr = get_complementary_color(accent);
    float pulse_sin = 0.5f + 0.5f * sinf((float)(millis() % 2513u) / 400.0f);
    int pulse_bright = 77 + (int)(pulse_sin * 178.0f);  // 77-255 range

    // Draw bar pixels — p=0 is start-of-day, p=cell_h-1 is end-of-day
    for (int p = 0; p < cell_h; p++) {
      // Activity type uses bed_time_hour, work_start_hour, work_end_hour (same as year view)
      uint8_t activity = get_activity_type(p, cell_h, is_weekend);

      Color c;
      if (day_fill_style_ == DAY_FILL_ACTIVITY) {
        // Fixed activity palette
        if (activity == 0)      c = Color(50, 50, 50);   // sleep: dark gray
        else if (activity == 1) c = Color(255, 120, 0);  // work: orange
        else                    c = Color(0, 200, 100);   // life: green
      } else if (day_fill_style_ == DAY_FILL_SCHEME) {
        c = accent;  // all pixels use full accent color
      } else {  // DAY_FILL_MIXED
        if (activity == 0)      c = Color(accent.r >> 2, accent.g >> 2, accent.b >> 2);  // sleep: dim
        else if (activity == 1) c = Color(accent.r >> 1, accent.g >> 1, accent.b >> 1);  // work: half
        else                    c = accent;                                                 // life: full
      }

      Color c_full = c;  // full-brightness color before any dimming
      if (p >= elapsed_px) {
        if (is_future) {
          // Future days: completely dark
          c = Color(0, 0, 0);
        } else {
          // Today's remaining time: 1/4 brightness of activity color
          c = Color(c.r >> 2, c.g >> 2, c.b >> 2);
        }
      }

      int y_pos = fill_direction_bottom_to_top_ ? (cy + cell_h - 1 - p) : (cy + p);
      bool past_or_today = !is_future;

      for (int bx = 0; bx < cell_w; bx++) {
        Color draw_c = c;
        // Current-time row in today's cell: left portion (already elapsed) at full brightness
        if (is_today && p == elapsed_px && bx < today_x_frac) {
          draw_c = c_full;
        }
        bool on_border = (y_pos == cy || y_pos == cy + cell_h - 1 || bx == 0 || bx == cell_w - 1);

        if (on_border) {
          if (marker_style_ != MARKER_NONE && is_today && is_event) {
            // Three-way blend: cell color + today marker + event complementary
            draw_c = Color(
              (uint8_t)((c.r + today_clr.r + event_clr.r) / 3),
              (uint8_t)((c.g + today_clr.g + event_clr.g) / 3),
              (uint8_t)((c.b + today_clr.b + event_clr.b) / 3)
            );
          } else if (marker_style_ != MARKER_NONE && is_today) {
            draw_c = Color(
              (uint8_t)((c.r + today_clr.r) >> 1),
              (uint8_t)((c.g + today_clr.g) >> 1),
              (uint8_t)((c.b + today_clr.b) >> 1)
            );
          } else if (is_event && year_event_style_ != YEAR_EVENT_NONE) {
            Color border_clr;
            if (year_event_style_ == YEAR_EVENT_MARKERS) {
              if (past_or_today) {
                border_clr = Color(
                  (uint8_t)((c.r + event_clr.r) >> 1),
                  (uint8_t)((c.g + event_clr.g) >> 1),
                  (uint8_t)((c.b + event_clr.b) >> 1)
                );
              } else {
                Color dim_evt = Color(event_clr.r >> 3, event_clr.g >> 3, event_clr.b >> 3);
                border_clr = Color(
                  (uint8_t)((c.r + dim_evt.r) >> 1),
                  (uint8_t)((c.g + dim_evt.g) >> 1),
                  (uint8_t)((c.b + dim_evt.b) >> 1)
                );
              }
            } else {  // YEAR_EVENT_PULSE
              if (past_or_today) {
                Color pulsed = Color(
                  (uint8_t)((event_clr.r * pulse_bright) >> 8),
                  (uint8_t)((event_clr.g * pulse_bright) >> 8),
                  (uint8_t)((event_clr.b * pulse_bright) >> 8)
                );
                border_clr = Color(
                  (uint8_t)((c.r + pulsed.r) >> 1),
                  (uint8_t)((c.g + pulsed.g) >> 1),
                  (uint8_t)((c.b + pulsed.b) >> 1)
                );
              } else {
                Color dim_evt = Color(event_clr.r >> 2, event_clr.g >> 2, event_clr.b >> 2);
                border_clr = Color(
                  (uint8_t)((c.r + dim_evt.r) >> 1),
                  (uint8_t)((c.g + dim_evt.g) >> 1),
                  (uint8_t)((c.b + dim_evt.b) >> 1)
                );
              }
            }
            draw_c = border_clr;
          }
        }
        draw_pixel(it, cx + bx, y_pos, draw_c);
      }
    }
  }

  // Current moment: breathing rainbow pixel at the fill edge inside today's cell
  // x sweeps left→right across the cell width; y tracks the fill boundary
  {
    float time_frac = (time.hour * 60.0f + time.minute) / (24.0f * 60.0f);

    // x: maps time of day across today's cell width
    int pixel_x = today_cx + (int)(time_frac * cell_w);
    if (pixel_x >= today_cx + cell_w) pixel_x = today_cx + cell_w - 1;

    // y: at the leading edge of the filled region within today's cell
    int elapsed_px = (int)(time_frac * cell_h);
    if (elapsed_px >= cell_h) elapsed_px = cell_h - 1;
    int pixel_y = fill_direction_bottom_to_top_
                    ? (today_cy + cell_h - 1 - elapsed_px)
                    : (today_cy + elapsed_px);

    int hue = (hue_offset + (time.month - 1) * 30) % 360;
    Color rainbow = hsv_to_rgb(hue, 1.0f, 1.0f);
    Color breathing = Color(
      (uint8_t)(rainbow.r * breath_factor),
      (uint8_t)(rainbow.g * breath_factor),
      (uint8_t)(rainbow.b * breath_factor)
    );
    draw_pixel(it, pixel_x, pixel_y, breathing);
  }
}

void LifeMatrix::render_day_view(display::Display &it, ESPTime &time, int viz_y, int viz_height) {
  int center_x = it.get_width() / 2;
  Viewport vp = calculate_viewport(it);

  // Display day name
  char day_str[8];
  time.strftime(day_str, sizeof(day_str), "%a %d");
  it.print(center_x, vp.text_y, font_small_, color_active_, display::TextAlign::CENTER, day_str);

  // Calculate time segments
  int bed_hour = time_segments_.bed_time_hour;
  int wake_hour = (bed_hour + 8) % 24;
  int work_start = time_segments_.work_start_hour;
  int work_end = time_segments_.work_end_hour;

  // No work on weekends
  bool is_weekend = (time.day_of_week == 1 || time.day_of_week == 7);

  // Current position in day
  int current_minutes = time.hour * 60 + time.minute;

  // Determine segment type per row: 0=sleep, 1=work, 2=life
  uint8_t row_type[120];
  for (int row = 0; row < 120; row++) {
    int hour = (int)((row * 24.0f) / 120.0f);

    bool sleep = false;
    if (bed_hour < wake_hour) {
      sleep = (hour >= bed_hour && hour < wake_hour);
    } else {
      sleep = (hour >= bed_hour || hour < wake_hour);
    }

    bool work = false;
    if (!is_weekend && !sleep) {
      if (work_start < work_end) {
        work = (hour >= work_start && hour < work_end);
      } else {
        work = (hour >= work_start || hour < work_end);
      }
    }

    row_type[row] = sleep ? 0 : (work ? 1 : 2);
  }

  // Calculate rainbow progress for life segments
  float life_progress[120] = {};
  int seg_start = -1;
  for (int row = 0; row <= 120; row++) {
    bool is_life = (row < 120 && row_type[row] == 2);
    if (is_life && seg_start < 0) {
      seg_start = row;
    } else if (!is_life && seg_start >= 0) {
      int seg_size = row - seg_start;
      for (int i = 0; i < seg_size; i++) {
        life_progress[seg_start + i] = (float)i / (float)seg_size;
      }
      seg_start = -1;
    }
  }

  // Draw 24-hour day
  for (int row = 0; row < viz_height && row < 120; row++) {
    int y_pos = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - row) : (viz_y + row);

    float hour_float = (row * 24.0f) / 120.0f;
    int row_minutes = (int)(hour_float * 60);

    // Only draw past time
    if (row_minutes > current_minutes) continue;

    // Draw pixels for this row
    for (int col = 1; col <= 30; col++) {
      Color pixel_color;

      if (row_type[row] == 0) {
        // Sleep - dark gray
        pixel_color = Color(50, 50, 50);
      } else if (row_type[row] == 1) {
        // Work - orange
        pixel_color = Color(255, 120, 0);
      } else {
        // Life - full rainbow per segment
        int hue = (int)(life_progress[row] * 360.0f);
        pixel_color = hsv_to_rgb(hue, 1.0f, 1.0f);
      }

      draw_pixel(it, col, y_pos, pixel_color);
    }
  }
}

void LifeMatrix::render_hour_view(display::Display &it, ESPTime &time, int viz_y, int viz_height) {
  int center_x = it.get_width() / 2;
  int width = it.get_width();
  Viewport vp = calculate_viewport(it);

  // Display current time in text area
  char time_str[6];
  time.strftime(time_str, sizeof(time_str), "%H:%M");
  it.print(center_x, vp.text_y, font_small_, color_active_, display::TextAlign::CENTER, time_str);

  // Get current time components
  int minute = time.minute;
  int second = time.second;

  // Calculate position in hour (120 rows = 60 min × 2 rows/min)
  int current_row = minute * 2 + (second >= 30 ? 1 : 0);  // Which row we're filling
  int pixels_in_row = second % 30;  // How many pixels filled in current row (0-29)

  // Handle Time Segments separately (spiral filling, not line-by-line)
  if (style_ == STYLE_TIME_SEGMENTS) {
    // Draw all 4 quarters as spirals
    for (int q = 0; q < 4; q++) {
      int quarter_start_row = q * 30;

      // Determine if this quarter should be filled
      int current_quarter = minute / 15;
      bool is_past_quarter = (q < current_quarter);
      bool is_current_quarter = (q == current_quarter);

      if (!is_past_quarter && !is_current_quarter) continue;  // Skip future quarters

      int seconds_in_quarter = is_past_quarter ? 900 : ((minute % 15) * 60 + second);

      // Choose color for this quarter
      Color quarter_color;
      if (q == 0) quarter_color = Color(0, 100, 255);      // Blue
      else if (q == 1) quarter_color = Color(0, 255, 100); // Green
      else if (q == 2) quarter_color = Color(255, 200, 0); // Yellow-Orange
      else quarter_color = Color(255, 0, 100);             // Red-Magenta

      // Draw spiral for this quarter
      int spiral_pos = 0;
      int length = 30;

      // 1. Draw top edge (row 0, cols 0-29)
      int draw_row = quarter_start_row;
      for (int draw_col = 1; draw_col <= 30 && spiral_pos < seconds_in_quarter; draw_col++) {
        int y_pos = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - draw_row) : (viz_y + draw_row);
        draw_pixel(it, draw_col, y_pos, quarter_color);
        spiral_pos++;
      }

      // 2. Spiral inward
      int current_spiral_row = 0;
      int current_col = 29;
      int direction = 1;  // 0=right, 1=down, 2=left, 3=up

      while (length > 0 && spiral_pos < seconds_in_quarter) {
        length--;

        // First side
        for (int i = 0; i < length && spiral_pos < seconds_in_quarter; i++) {
          if (direction == 1) current_spiral_row++;
          else if (direction == 3) current_spiral_row--;

          int abs_row = quarter_start_row + current_spiral_row;
          int y_pos = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - abs_row) : (viz_y + abs_row);
          draw_pixel(it, current_col + 1, y_pos, quarter_color);
          spiral_pos++;
        }
        direction = (direction + 1) % 4;

        // Second side
        for (int i = 0; i < length && spiral_pos < seconds_in_quarter; i++) {
          if (direction == 2) current_col--;
          else if (direction == 0) current_col++;

          int abs_row = quarter_start_row + current_spiral_row;
          int y_pos = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - abs_row) : (viz_y + abs_row);
          draw_pixel(it, current_col + 1, y_pos, quarter_color);
          spiral_pos++;
        }
        direction = (direction + 1) % 4;
      }
    }
    return;  // Done with Time Segments
  }

  // Normal drawing for other schemes (not Time Segments)

  // Draw minute markers (every 10 minutes = every 20 rows, skip 0)
  if (marker_style_ != MARKER_NONE) {
    Color marker_clr = get_marker_color_value(marker_color_);

    for (int mark_min = 10; mark_min <= 50; mark_min += 10) {
      int mark_row = mark_min * 2;
      if (mark_row < viz_height) {
        int mark_y = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - mark_row) : (viz_y + mark_row);
        draw_marker(it, mark_y, width, marker_style_, marker_clr);
      }
    }
  }

  // Draw time visualization (use columns 1-30, leave 0 and 31 for markers)
  for (int row = 0; row < viz_height && row < 120; row++) {
    int y_pos = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - row) : (viz_y + row);
    bool is_current_row = (row == current_row);
    bool is_past_row = (row < current_row);

    // Determine how many pixels to draw in this row
    int pixels_to_draw = 0;
    if (is_past_row) {
      pixels_to_draw = 30;  // Fully filled
    } else if (is_current_row) {
      pixels_to_draw = pixels_in_row;  // Partially filled
    }

    // Draw pixels in this row
    for (int col = 0; col < pixels_to_draw; col++) {
      int x_pos = 1 + col;  // Start at column 1 (skip marker column)

      // Determine color based on scheme
      Color pixel_color = color_active_;

      if (style_ == STYLE_GRADIENT) {
        float progress = (float)row / 120.0f;
        pixel_color = interpolate_gradient(progress, gradient_type_);
      } else if (style_ == STYLE_RAINBOW) {
        int hue = (row * 360) / 120;
        pixel_color = hsv_to_rgb(hue, 1.0f, 1.0f);
      }
      // else: Single Color - use color_active_ (already set)

      draw_pixel(it, x_pos, y_pos, pixel_color);
    }
  }
}

Color LifeMatrix::get_complementary_color(Color c) {
  uint8_t max_c = std::max({c.r, c.g, c.b});
  uint8_t min_c = std::min({c.r, c.g, c.b});
  int hue = 0;
  if (max_c != min_c) {
    if (max_c == c.r) {
      hue = (int)(60.0f * ((c.g - c.b) / (float)(max_c - min_c)));
    } else if (max_c == c.g) {
      hue = (int)(60.0f * (2.0f + (c.b - c.r) / (float)(max_c - min_c)));
    } else {
      hue = (int)(60.0f * (4.0f + (c.r - c.g) / (float)(max_c - min_c)));
    }
    if (hue < 0) hue += 360;
  }
  return hsv_to_rgb((hue + 180) % 360, 1.0f, 1.0f);
}

void LifeMatrix::render_year_view(display::Display &it, ESPTime &time, int viz_y, int viz_height) {
  int center_x = it.get_width() / 2;
  int width = it.get_width();
  Viewport vp = calculate_viewport(it);

  // Display year
  char year_str[5];
  time.strftime(year_str, sizeof(year_str), "%Y");
  it.print(center_x, vp.text_y, font_small_, color_active_, display::TextAlign::CENTER, year_str);

  // Current date/time
  int cur_year = time.year;
  int cur_month = time.month;
  int cur_day = time.day_of_month;
  int cur_hour = time.hour;
  int cur_minute = time.minute;

  // Calculate days in each month
  uint8_t days_in_month[12];
  get_days_in_month(cur_year, days_in_month);

  // Calculate month height in pixels
  int month_h = viz_height / 12;
  if (month_h < 1) month_h = 1;

  // Create event lookup bitmap for O(1) access
  bool event_map[13][32] = {};  // [month 1-12][day 1-31]
  for (const auto &evt : year_events_) {
    if (evt.month >= 1 && evt.month <= 12 && evt.day >= 1 && evt.day <= 31) {
      event_map[evt.month][evt.day] = true;
    }
  }

  // Get marker color
  Color marker_clr = get_marker_color_value(marker_color_);

  // === Column 0: Month boundary markers ===
  if (marker_style_ != MARKER_NONE) {
    for (int month_boundary = 1; month_boundary <= 11; month_boundary++) {
      int mark_row = month_boundary * month_h;
      if (mark_row < viz_height) {
        int mark_y = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - mark_row) : (viz_y + mark_row);

        if (marker_style_ == MARKER_SINGLE_DOT) {
          draw_pixel(it, 0, mark_y, marker_clr);
        } else if (marker_style_ == MARKER_GRADIENT_PEAK) {
          // Gradient peak: 5 pixels with intensity gradient
          for (int gi = 0; gi < 5; gi++) {
            int dot_y = mark_y + (gi - 2);
            if (dot_y >= viz_y && dot_y < viz_y + viz_height) {
              float intensity = (gi == 2) ? 1.0f : ((gi == 1 || gi == 3) ? 0.5f : 0.25f);
              Color faded = Color(
                (uint8_t)(marker_clr.r * intensity),
                (uint8_t)(marker_clr.g * intensity),
                (uint8_t)(marker_clr.b * intensity)
              );
              draw_pixel(it, 0, dot_y, faded);
            }
          }
        }
      }
    }
  }

  // Pre-compute scheme colors for all 12 months
  Color scheme_colors[12];
  for (int m = 0; m < 12; m++) {
    float progress = (float)m / 11.0f;

    if (style_ == STYLE_SINGLE) {
      scheme_colors[m] = color_active_;
    } else if (style_ == STYLE_GRADIENT) {
      scheme_colors[m] = interpolate_gradient(progress, gradient_type_);
    } else if (style_ == STYLE_TIME_SEGMENTS) {
      // Season palette for Time Segments
      static const uint8_t season_r[] = {100,140,60,120,180,255,255,255,180,150,100,60};
      static const uint8_t season_g[] = {160,190,180,220,240,200,120,160,100,60,60,100};
      static const uint8_t season_b[] = {255,240,60,80,60,0,0,30,40,30,40,200};
      scheme_colors[m] = Color(season_r[m], season_g[m], season_b[m]);
    } else {  // RAINBOW
      int hue = (m * 360) / 12;
      scheme_colors[m] = hsv_to_rgb(hue, 1.0f, 1.0f);
    }
  }

  // Pre-compute activity colors for each month and activity type
  Color activity_colors[12][3];  // [month][type: 0=sleep, 1=work, 2=life]
  for (int m = 0; m < 12; m++) {
    if (day_fill_style_ == DAY_FILL_ACTIVITY) {
      // Fixed activity colors
      activity_colors[m][0] = Color(50, 50, 50);      // Sleep: dark gray
      activity_colors[m][1] = Color(255, 120, 0);     // Work: orange
      activity_colors[m][2] = Color(0, 200, 100);     // Life: green
    } else if (day_fill_style_ == DAY_FILL_MIXED) {
      // Activity + Scheme: sleep=dark, work=scheme*0.5, life=scheme
      activity_colors[m][0] = Color(50, 50, 50);
      activity_colors[m][1] = Color(scheme_colors[m].r >> 1, scheme_colors[m].g >> 1, scheme_colors[m].b >> 1);
      activity_colors[m][2] = scheme_colors[m];
    } else {  // DAY_FILL_SCHEME
      // All same color
      activity_colors[m][0] = scheme_colors[m];
      activity_colors[m][1] = scheme_colors[m];
      activity_colors[m][2] = scheme_colors[m];
    }
  }

  // Pre-compute event colors (for Pulse mode) - use complementary colors
  Color event_colors[12];
  for (int m = 0; m < 12; m++) {
    event_colors[m] = get_complementary_color(scheme_colors[m]);
  }

  // Pulse animation (breathing effect, 0.3-1.0 range)
  float pulse_sin = 0.5f + 0.5f * sinf((float)(millis() % 2513u) / 400.0f);
  int pulse_brightness = 77 + (int)(pulse_sin * 178.0f);  // 77-255 range

  // === Column 0: Event markers (Markers mode only) ===
  if (year_event_style_ == YEAR_EVENT_MARKERS) {
    for (const auto &evt : year_events_) {
      int month_idx = evt.month - 1;  // 0-based
      int day = evt.day;

      if (month_idx < 0 || month_idx >= 12) continue;

      int pixel_y = ((day - 1) * month_h) / days_in_month[month_idx];
      int logical_row = month_idx * month_h + pixel_y;
      int screen_y = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - logical_row) : (viz_y + logical_row);

      // Past/today events: full color, future: dimmed
      bool past_or_today = (evt.month < cur_month) || (evt.month == cur_month && evt.day <= cur_day);
      Color event_marker_color = event_colors[month_idx];
      if (!past_or_today) {
        event_marker_color = Color(event_marker_color.r >> 3, event_marker_color.g >> 3, event_marker_color.b >> 3);
      }

      draw_pixel(it, 0, screen_y, event_marker_color);
    }
  }

  // === Main rendering loop: Draw all days in all months ===
  for (int month_idx = 0; month_idx < 12; month_idx++) {
    int month_num = month_idx + 1;  // 1-12
    int month_base_row = month_idx * month_h;

    for (int day = 1; day <= days_in_month[month_idx]; day++) {
      bool is_past = (month_num < cur_month) || (month_num == cur_month && day < cur_day);
      bool is_today = (month_num == cur_month && day == cur_day);
      bool has_event = event_map[month_num][day];

      // Determine if weekend
      int day_of_week = day_of_week_sakamoto(cur_year, month_num, day);
      bool is_weekend = (day_of_week == 0 || day_of_week == 6);  // Sunday=0, Saturday=6

      // Render this day's column
      if (is_past) {
        // Past day: draw fully
        bool pulse_event = has_event && year_event_style_ == YEAR_EVENT_PULSE;

        for (int py = 0; py < month_h; py++) {
          int logical_row = month_base_row + py;
          int screen_y = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - logical_row) : (viz_y + logical_row);

          uint8_t activity_type = get_activity_type(py, month_h, is_weekend);

          Color pixel_color;
          if (pulse_event && activity_type != 0) {
            // Pulsing event on non-sleep pixels
            pixel_color = Color(
              (event_colors[month_idx].r * pulse_brightness) >> 8,
              (event_colors[month_idx].g * pulse_brightness) >> 8,
              (event_colors[month_idx].b * pulse_brightness) >> 8
            );
          } else {
            // Normal activity color
            pixel_color = activity_colors[month_idx][activity_type];
          }

          draw_pixel(it, day, screen_y, pixel_color);
        }
      } else if (is_today) {
        // Today: fill up to current time
        int filled_pixels = ((cur_hour * 60 + cur_minute) * month_h) / 1440;
        if (filled_pixels > month_h) filled_pixels = month_h;

        bool pulse_event = has_event && year_event_style_ == YEAR_EVENT_PULSE;

        for (int py = 0; py < filled_pixels; py++) {
          int logical_row = month_base_row + py;
          int screen_y = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - logical_row) : (viz_y + logical_row);

          uint8_t activity_type = get_activity_type(py, month_h, is_weekend);

          Color pixel_color;
          if (pulse_event && activity_type != 0) {
            // Pulsing event on non-sleep pixels
            pixel_color = Color(
              (event_colors[month_idx].r * pulse_brightness) >> 8,
              (event_colors[month_idx].g * pulse_brightness) >> 8,
              (event_colors[month_idx].b * pulse_brightness) >> 8
            );
          } else {
            // Normal activity color
            pixel_color = activity_colors[month_idx][activity_type];
          }

          draw_pixel(it, day, screen_y, pixel_color);
        }
      } else if (has_event && year_event_style_ == YEAR_EVENT_PULSE) {
        // Future event with pulse: dim static preview
        for (int py = 0; py < month_h; py++) {
          uint8_t activity_type = get_activity_type(py, month_h, is_weekend);
          if (activity_type == 0) continue;  // Skip sleep pixels

          int logical_row = month_base_row + py;
          int screen_y = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - logical_row) : (viz_y + logical_row);

          // Dim static preview (~25%)
          Color pixel_color = Color(
            event_colors[month_idx].r >> 2,
            event_colors[month_idx].g >> 2,
            event_colors[month_idx].b >> 2
          );

          draw_pixel(it, day, screen_y, pixel_color);
        }
      }
      // Future non-event days: leave black (skip)
    }
  }

  // === Column 0: Breathing rainbow for today (Markers mode only) ===
  if (year_event_style_ == YEAR_EVENT_MARKERS) {
    // Breathing: slow sine wave 0.5-1.0
    float breath_factor = 0.5f + 0.5f * sinf((float)(millis() % 3000u) / 477.0f);

    // Rotating hue: completes full rainbow every ~10 seconds
    int hue_offset = ((millis() / 27) % 360);

    // Find today's position
    int today_month_idx = cur_month - 1;
    int pixel_y = ((cur_day - 1) * month_h) / days_in_month[today_month_idx];
    int logical_row = today_month_idx * month_h + pixel_y;
    int screen_y = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - logical_row) : (viz_y + logical_row);

    // Rainbow color with breathing
    int hue = (hue_offset + today_month_idx * 30) % 360;
    Color rainbow = hsv_to_rgb(hue, 1.0f, 1.0f);

    Color breathing = Color(
      (uint8_t)(rainbow.r * breath_factor),
      (uint8_t)(rainbow.g * breath_factor),
      (uint8_t)(rainbow.b * breath_factor)
    );

    draw_pixel(it, 0, screen_y, breathing);
  }
}

void LifeMatrix::check_celebration(ESPTime &time) {
  // Re-trigger once per minute on event days
  if (time.hour   == last_celebration_hour_   &&
      time.minute == last_celebration_minute_ &&
      time.day_of_month == last_celebration_day_   &&
      time.month  == last_celebration_month_)
    return;
  last_celebration_hour_   = time.hour;
  last_celebration_minute_ = time.minute;
  last_celebration_day_    = time.day_of_month;
  last_celebration_month_  = time.month;
  for (const auto &evt : year_events_) {
    if (evt.month == time.month && evt.day == time.day_of_month) {
      celebration_active_ = true;
      celebration_start_  = millis();
      celeb_seq_idx_ = 0;
      ctm_           = CTM_NONE;
      ESP_LOGD(TAG, "Celebration triggered for %d-%02d %02d:%02d", time.month, time.day_of_month, time.hour, time.minute);
      break;
    }
  }
}

void LifeMatrix::render_sparkle_celebration(display::Display &it, uint32_t elapsed_ms) {
  float progress = (float)elapsed_ms / 3000.0f;
  float density  = (1.0f - progress) * (1.0f - progress);  // quadratic falloff
  int   count    = (int)(60.0f * density);

  uint32_t seed = elapsed_ms / 40u;  // Changes every 40ms for flicker
  int w = it.get_width(), h = it.get_height();
  for (int i = 0; i < count; i++) {
    seed = seed * 1664525u + 1013904223u;
    int x = (int)((seed >> 16) % (uint32_t)w);
    seed = seed * 1664525u + 1013904223u;
    int y = (int)((seed >> 16) % (uint32_t)h);
    seed = seed * 1664525u + 1013904223u;
    int hue = (int)((seed >> 16) % 360u);
    it.draw_pixel_at(x, y, hsv_to_rgb(hue, 1.0f, 1.0f));
  }
}

void LifeMatrix::render_plasma_celebration(display::Display &it, uint32_t elapsed_ms) {
  float t = (float)elapsed_ms * 0.001f;  // seconds
  int w = it.get_width();
  int h = it.get_height();

  // Brightness envelope: fade in over 0.4s, hold, fade out over final 0.6s
  float progress = (float)elapsed_ms / 3000.0f;
  float brightness;
  if (progress < 0.13f) {
    brightness = progress / 0.13f;
  } else if (progress > 0.8f) {
    brightness = 1.0f - (progress - 0.8f) / 0.2f;
  } else {
    brightness = 1.0f;
  }

  for (int y = 0; y < h; y++) {
    float fy = (float)y;
    for (int x = 0; x < w; x++) {
      float fx = (float)x;
      // Four overlapping sine waves: horizontal, vertical, diagonal, radial
      float v = sinf(fx * 0.30f + t * 2.1f)
              + sinf(fy * 0.13f + t * 1.7f)
              + sinf((fx + fy) * 0.18f + t * 1.4f)
              + sinf(sqrtf(fx * fx + fy * fy) * 0.22f - t * 1.1f);
      // v in [-4, 4] → hue 0–360
      int hue = (int)((v + 4.0f) * 45.0f) % 360;
      if (hue < 0) hue += 360;
      it.draw_pixel_at(x, y, hsv_to_rgb(hue, 1.0f, brightness));
    }
  }
}

void LifeMatrix::render_celebration_overlay(display::Display &it, uint32_t elapsed_ms) {
  CelebrationStyle cur_style =
      (celeb_seq_idx_ < celeb_seq_len_) ? celeb_sequence_[celeb_seq_idx_] : CELEB_SPARKLE;
  switch (cur_style) {
    case CELEB_PLASMA:    render_plasma_celebration(it, elapsed_ms);    break;
    case CELEB_FIREWORKS: render_fireworks_celebration(it, elapsed_ms); break;
    case CELEB_SPARKLE:
    default:              render_sparkle_celebration(it, elapsed_ms);   break;
  }
}

uint32_t LifeMatrix::get_celeb_duration(CelebrationStyle style) {
  switch (style) {
    case CELEB_FIREWORKS: return 5000;
    case CELEB_HUE_CYCLE: return 5000;  // one full 360° cycle (2π radians)
    case CELEB_PLASMA:    return 3000;
    case CELEB_SPARKLE:
    default:              return 3000;
  }
}

// ============================================================================
// FIREWORKS — 7 staggered rockets, 20 sparks each, trailing streaks,
//             burst core flash, and a secondary mini-burst per firework.
// ============================================================================
void LifeMatrix::render_fireworks_celebration(display::Display &it, uint32_t elapsed_ms) {
  struct FireworkDef {
    uint32_t start_ms;
    int8_t   launch_x, burst_x, burst_y;
    int16_t  base_hue;
    uint8_t  num_sparks;
  };
  static const FireworkDef FWS[] = {
    {100,   8, 10, 24,   0, 20},  // red
    {700,  24, 22, 14, 120, 20},  // green
    {1300, 14, 16, 32,  55, 22},  // yellow
    {1900,  5,  7, 18, 200, 20},  // cyan  \_ near-simultaneous double burst
    {2000, 27, 25, 24, 280, 20},  // purple/
    {2700, 12, 14, 14, 330, 20},  // pink
    {3300, 20, 22, 28,  30, 20},  // orange
  };
  static constexpr float    PI_F      = 3.14159265f;
  static constexpr uint32_t ROCKET_MS = 550;   // ascent duration
  static constexpr uint32_t SPARK_MS  = 1600;  // primary spark lifetime
  static constexpr uint32_t FLASH_MS  = 120;   // burst-core flash duration
  static constexpr uint32_t SEC_START = 500;   // secondary burst delay after ROCKET_MS
  static constexpr uint32_t SEC_MS    = 900;   // secondary spark lifetime
  static constexpr float GRAVITY      = 22.f;
  static constexpr float SPEED_BASE   = 14.f;

  int w = it.get_width(), h = it.get_height();

  for (const auto &fw : FWS) {
    if (elapsed_ms < fw.start_ms) continue;
    uint32_t fw_t = elapsed_ms - fw.start_ms;

    // ---- Rocket ascent (4-pixel trail) ----
    if (fw_t < ROCKET_MS) {
      float prog = (float)fw_t / (float)ROCKET_MS;
      for (int seg = 0; seg < 4; seg++) {
        float p = prog - (float)seg * 0.06f;
        if (p < 0.f) break;
        int ix = (int)roundf(fw.launch_x + (fw.burst_x - fw.launch_x) * p);
        int iy = (int)roundf((h - 1) + (fw.burst_y - (h - 1)) * p);
        if (ix >= 0 && ix < w && iy >= 0 && iy < h) {
          float br = (seg == 0) ? 1.f : (seg == 1 ? 0.6f : seg == 2 ? 0.28f : 0.10f);
          it.draw_pixel_at(ix, iy, Color((uint8_t)(255*br), (uint8_t)(215*br), 0));
        }
      }
      continue;
    }

    // ---- Burst-core flash (white 3×3 glow for FLASH_MS) ----
    uint32_t post = fw_t - ROCKET_MS;
    if (post < FLASH_MS) {
      float flash = 1.f - (float)post / (float)FLASH_MS;
      for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int ix = fw.burst_x + dx, iy = fw.burst_y + dy;
        if (ix >= 0 && ix < w && iy >= 0 && iy < h)
          it.draw_pixel_at(ix, iy, Color((uint8_t)(255*flash), (uint8_t)(255*flash), (uint8_t)(255*flash)));
      }
    }

    // ---- Primary sparks with 3-step trailing streaks ----
    if (post < SPARK_MS) {
      float t = (float)post * 0.001f;
      float life_frac = (float)post / (float)SPARK_MS;
      float brightness = 1.f - life_frac;
      for (int i = 0; i < fw.num_sparks; i++) {
        float angle = ((float)i * (360.f / fw.num_sparks) + (float)(i % 5) * 8.f) * PI_F / 180.f;
        float speed = SPEED_BASE + (float)(i % 5) * 2.f;  // 14–22 px/s
        for (int tr = 0; tr < 3; tr++) {
          float tt = t - (float)tr * 0.07f;
          if (tt <= 0.f) break;
          float dx = cosf(angle) * speed * tt;
          float dy = -sinf(angle) * speed * tt + 0.5f * GRAVITY * tt * tt;
          int ix = (int)roundf(fw.burst_x + dx);
          int iy = (int)roundf(fw.burst_y + dy);
          if (ix >= 0 && ix < w && iy >= 0 && iy < h) {
            float br = brightness * (tr == 0 ? 1.f : tr == 1 ? 0.38f : 0.13f);
            int hue = ((int)fw.base_hue + i * 14) % 360;
            it.draw_pixel_at(ix, iy, hsv_to_rgb(hue, 1.f, br));
          }
        }
      }
    }

    // ---- Secondary star-burst (8 fast diagonal sparks, delayed SEC_START ms) ----
    if (post >= SEC_START && post < SEC_START + SEC_MS) {
      float t2 = (float)(post - SEC_START) * 0.001f;
      float b2 = 1.f - (float)(post - SEC_START) / (float)SEC_MS;
      for (int i = 0; i < 8; i++) {
        float a = (float)i * 45.f * PI_F / 180.f;
        float spd = 22.f;
        int ix = (int)roundf(fw.burst_x + cosf(a) * spd * t2);
        int iy = (int)roundf(fw.burst_y - sinf(a) * spd * t2 + 0.5f * GRAVITY * t2 * t2);
        if (ix >= 0 && ix < w && iy >= 0 && iy < h)
          it.draw_pixel_at(ix, iy, hsv_to_rgb(((int)fw.base_hue + 60) % 360, 0.8f, b2));
      }
    }
  }
}

void LifeMatrix::render_ui_overlays(display::Display &it) {
  int width = it.get_width();

  // Pause indicator - two vertical bars top-right
  if (ui_paused_) {
    Color pause_color = Color(180, 180, 180);  // Brighter gray/white
    // Left bar (2 pixels wide, 5 pixels tall)
    for (int y = 1; y <= 5; y++) {
      it.draw_pixel_at(width - 7, y, pause_color);
      it.draw_pixel_at(width - 6, y, pause_color);
    }
    // Right bar (2 pixels wide, 5 pixels tall)
    for (int y = 1; y <= 5; y++) {
      it.draw_pixel_at(width - 4, y, pause_color);
      it.draw_pixel_at(width - 3, y, pause_color);
    }
  }

  // Mode indicator is now shown on the neopixel LED
  // No longer using bottom-right pixel
}

// ============================================================================
// CONFIGURATION SETTERS (String-based for YAML template entities)
// ============================================================================

void LifeMatrix::set_style(const std::string &style) {
  if (style == "Single Color") {
    style_ = STYLE_SINGLE;
  } else if (style == "Gradient") {
    style_ = STYLE_GRADIENT;
  } else if (style == "Time Segments") {
    style_ = STYLE_TIME_SEGMENTS;
  } else if (style == "Rainbow") {
    style_ = STYLE_RAINBOW;
  }
}

void LifeMatrix::set_gradient_type(const std::string &type) {
  if (type == "Red-Blue") {
    gradient_type_ = GRADIENT_RED_BLUE;
  } else if (type == "Green-Yellow") {
    gradient_type_ = GRADIENT_GREEN_YELLOW;
  } else if (type == "Cyan-Magenta") {
    gradient_type_ = GRADIENT_CYAN_MAGENTA;
  } else if (type == "Purple-Orange") {
    gradient_type_ = GRADIENT_PURPLE_ORANGE;
  } else if (type == "Blue-Yellow") {
    gradient_type_ = GRADIENT_BLUE_YELLOW;
  }
}

void LifeMatrix::set_marker_style(const std::string &style) {
  if (style == "None") {
    marker_style_ = MARKER_NONE;
  } else if (style == "Single Dot") {
    marker_style_ = MARKER_SINGLE_DOT;
  } else if (style == "Gradient Peak") {
    marker_style_ = MARKER_GRADIENT_PEAK;
  }
}

void LifeMatrix::set_marker_color(const std::string &color) {
  if (color == "Blue") {
    marker_color_ = MARKER_BLUE;
  } else if (color == "White") {
    marker_color_ = MARKER_WHITE;
  } else if (color == "Yellow") {
    marker_color_ = MARKER_YELLOW;
  } else if (color == "Red") {
    marker_color_ = MARKER_RED;
  } else if (color == "Green") {
    marker_color_ = MARKER_GREEN;
  } else if (color == "Cyan") {
    marker_color_ = MARKER_CYAN;
  } else if (color == "Magenta") {
    marker_color_ = MARKER_MAGENTA;
  }
}

void LifeMatrix::set_year_events(const std::string &events) {
  parse_year_events(events);
}

void LifeMatrix::set_day_fill(const std::string &style) {
  if (style == "Fixed" || style == "Activity") {  // "Activity" kept for backward compat
    day_fill_style_ = DAY_FILL_ACTIVITY;
  } else if (style == "Flat" || style == "Scheme") {  // "Scheme" kept for backward compat
    day_fill_style_ = DAY_FILL_SCHEME;
  } else if (style == "Shaded" || style == "Activity + Scheme") {  // old name kept
    day_fill_style_ = DAY_FILL_MIXED;
  }
}

void LifeMatrix::set_year_event_style(const std::string &style) {
  if (style == "None") {
    year_event_style_ = YEAR_EVENT_NONE;
  } else if (style == "Markers") {
    year_event_style_ = YEAR_EVENT_MARKERS;
  } else if (style == "Pulse") {
    year_event_style_ = YEAR_EVENT_PULSE;
  }
}

// ============================================================================
// HOUR VIEW HELPER METHODS
// ============================================================================

Color LifeMatrix::interpolate_gradient(float progress, GradientType type) {
  uint8_t start_r, start_g, start_b, end_r, end_g, end_b;

  switch (type) {
    case GRADIENT_RED_BLUE:
      start_r = 255; start_g = 0; start_b = 0;      // Red
      end_r = 0; end_g = 0; end_b = 255;            // Blue
      break;
    case GRADIENT_GREEN_YELLOW:
      start_r = 0; start_g = 255; start_b = 0;      // Green
      end_r = 255; end_g = 255; end_b = 0;          // Yellow
      break;
    case GRADIENT_CYAN_MAGENTA:
      start_r = 0; start_g = 255; start_b = 255;    // Cyan
      end_r = 255; end_g = 0; end_b = 255;          // Magenta
      break;
    case GRADIENT_PURPLE_ORANGE:
      start_r = 128; start_g = 0; start_b = 255;    // Purple
      end_r = 255; end_g = 128; end_b = 0;          // Orange
      break;
    case GRADIENT_BLUE_YELLOW:
    default:
      start_r = 0; start_g = 0; start_b = 255;      // Blue
      end_r = 255; end_g = 255; end_b = 0;          // Yellow
      break;
  }

  uint8_t r = start_r + (end_r - start_r) * progress;
  uint8_t g = start_g + (end_g - start_g) * progress;
  uint8_t b = start_b + (end_b - start_b) * progress;

  return Color(r, g, b);
}

Color LifeMatrix::get_marker_color_value(MarkerColor color) {
  switch (color) {
    case MARKER_BLUE:
      return color_marker_;  // Default blue
    case MARKER_WHITE:
      return color_active_;  // White
    case MARKER_YELLOW:
      return color_highlight_;  // Yellow
    case MARKER_RED:
      return color_weekend_;  // Red
    case MARKER_GREEN:
      return Color(0, 255, 0);
    case MARKER_CYAN:
      return color_gradient_start_;  // Cyan
    case MARKER_MAGENTA:
      return color_gradient_end_;  // Magenta
    default:
      return color_marker_;
  }
}

void LifeMatrix::draw_marker(display::Display &it, int mark_y, int width, MarkerStyle style, Color color) {
  if (style == MARKER_SINGLE_DOT) {
    // Single dot on left and right edges
    draw_pixel(it, 0, mark_y, color);
    draw_pixel(it, width - 1, mark_y, color);
  } else if (style == MARKER_GRADIENT_PEAK) {
    // 5 dots with gradient: 25%, 50%, 100%, 50%, 25%
    float intensities[] = {0.25f, 0.5f, 1.0f, 0.5f, 0.25f};
    for (int i = 0; i < 5; i++) {
      int offset = i - 2;  // -2, -1, 0, 1, 2 (center at 0)
      int dot_y = mark_y + offset;
      Color faded_clr = Color(
        (uint8_t)(color.r * intensities[i]),
        (uint8_t)(color.g * intensities[i]),
        (uint8_t)(color.b * intensities[i])
      );
      draw_pixel(it, 0, dot_y, faded_clr);
      draw_pixel(it, width - 1, dot_y, faded_clr);
    }
  }
}

// ============================================================================
// YEAR VIEW HELPER METHODS
// ============================================================================

void LifeMatrix::parse_year_events(const std::string &events_str) {
  year_events_.clear();

  int i = 0;
  while (i < (int)events_str.length()) {
    // Skip commas and spaces
    while (i < (int)events_str.length() && (events_str[i] == ',' || events_str[i] == ' ')) {
      i++;
    }

    // Parse month
    int month = 0;
    while (i < (int)events_str.length() && events_str[i] >= '0' && events_str[i] <= '9') {
      month = month * 10 + (events_str[i] - '0');
      i++;
    }

    // Skip separator (/ or -)
    if (i < (int)events_str.length() && (events_str[i] == '/' || events_str[i] == '-')) {
      i++;
    }

    // Parse day
    int day = 0;
    while (i < (int)events_str.length() && events_str[i] >= '0' && events_str[i] <= '9') {
      day = day * 10 + (events_str[i] - '0');
      i++;
    }

    // Validate and add
    if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
      YearEvent evt;
      evt.month = month;
      evt.day = day;
      year_events_.push_back(evt);

      if (year_events_.size() >= 32) break;  // Limit to 32 events
    }
  }

  // Merge lifespan birthdays (kids, parents, siblings) so they appear in year/month views
  // and trigger celebrations — re-added here so they survive user updates to year_events text
  for (const auto &le : lifespan_year_events_) {
    bool dup = false;
    for (const auto &existing : year_events_) {
      if (existing.month == le.month && existing.day == le.day) { dup = true; break; }
    }
    if (!dup && year_events_.size() < 48) year_events_.push_back(le);
  }

  ESP_LOGD(TAG, "Parsed %d year events (%d lifespan)", (int)year_events_.size(), (int)lifespan_year_events_.size());
}

int LifeMatrix::day_of_week_sakamoto(int y, int m, int d) {
  // Sakamoto's algorithm for day of week (0=Sun, 6=Sat)
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) {
    y--;
  }
  return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

void LifeMatrix::get_days_in_month(int year, uint8_t days_out[12]) {
  static const uint8_t days_normal[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool is_leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));

  for (int i = 0; i < 12; i++) {
    days_out[i] = days_normal[i];
  }

  if (is_leap) {
    days_out[1] = 29;  // February in leap year
  }
}

uint8_t LifeMatrix::get_activity_type(int pixel_y, int month_h, bool is_weekend) {
  // Calculate hour from pixel position within month
  int hour = (pixel_y * 24) / month_h;

  // Determine activity type based on time segments
  int bed_hour = time_segments_.bed_time_hour;
  int wake_hour = (bed_hour + 8) % 24;
  int work_start = time_segments_.work_start_hour;
  int work_end = time_segments_.work_end_hour;

  // Check if sleeping
  bool is_sleep = false;
  if (bed_hour < wake_hour) {
    is_sleep = (hour >= bed_hour && hour < wake_hour);
  } else {
    is_sleep = (hour >= bed_hour || hour < wake_hour);
  }

  if (is_sleep) {
    return 0;  // Sleep
  }

  // Check if working (not on weekends)
  if (!is_weekend) {
    bool is_work = false;
    if (work_start < work_end) {
      is_work = (hour >= work_start && hour < work_end);
    } else {
      is_work = (hour >= work_start || hour < work_end);
    }

    if (is_work) {
      return 1;  // Work
    }
  }

  return 2;  // Life
}

// ============================================================================
// TIME OVERRIDE FOR TESTING
// ============================================================================

void LifeMatrix::set_time_override(const std::string &time_str) {
  // Clear override if empty string
  if (time_str.empty() || time_str == "clear" || time_str == "off") {
    clear_time_override();
    return;
  }

  // Parse format: "YYYY-MM-DD HH:MM:SS" or "YYYY-MM-DD HH:MM"
  // Example: "2025-06-15 14:30:00"

  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  int parsed = sscanf(time_str.c_str(), "%d-%d-%d %d:%d:%d",
                      &year, &month, &day, &hour, &minute, &second);

  if (parsed < 5) {
    ESP_LOGW(TAG, "Invalid time override format: '%s' (use YYYY-MM-DD HH:MM:SS)", time_str.c_str());
    return;
  }

  // Validate ranges
  if (year < 2000 || year > 2100 || month < 1 || month > 12 ||
      day < 1 || day > 31 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 59) {
    ESP_LOGW(TAG, "Time override values out of range: %d-%d-%d %d:%d:%d",
             year, month, day, hour, minute, second);
    return;
  }

  // Set fake time
  fake_time_.year = year;
  fake_time_.month = month;
  fake_time_.day_of_month = day;
  fake_time_.hour = hour;
  fake_time_.minute = minute;
  fake_time_.second = second;

  // Calculate day of week (0=Sunday, 6=Saturday)
  fake_time_.day_of_week = day_of_week_sakamoto(year, month, day) + 1;  // +1 because ESPTime uses 1=Sunday
  if (fake_time_.day_of_week == 7) fake_time_.day_of_week = 0;  // Wrap Saturday

  // Calculate day of year
  int day_of_year = day;
  static const int days_before_month[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  if (month > 1) {
    day_of_year += days_before_month[month - 1];
    // Add leap day if after February in a leap year
    if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
      day_of_year++;
    }
  }
  fake_time_.day_of_year = day_of_year;

  time_override_active_ = true;
  time_override_start_ms_ = millis();
  // Reset celebration tracking so new time is evaluated immediately
  last_celebration_hour_   = 255;
  last_celebration_minute_ = 255;
  ESP_LOGI(TAG, "Time override set to: %04d-%02d-%02d %02d:%02d:%02d (DoW=%d, DoY=%d)",
           year, month, day, hour, minute, second, fake_time_.day_of_week, fake_time_.day_of_year);
}

void LifeMatrix::clear_time_override() {
  if (time_override_active_) {
    time_override_active_ = false;
    ESP_LOGI(TAG, "Time override cleared - using real time");
  }
}

// ============================================================================
// LIFESPAN VIEW — PARSING HELPERS
// ============================================================================

LifeDate LifeMatrix::parse_life_date(const std::string &s) const {
  LifeDate d;
  // Accept "YYYY-MM-DD" or "YYYY/MM/DD"
  if (s.size() >= 8) {
    d.year  = (int16_t)std::atoi(s.substr(0, 4).c_str());
    d.month = (uint8_t)std::atoi(s.substr(5, 2).c_str());
    d.day   = (uint8_t)std::atoi(s.substr(8, 2).c_str());
    if (d.month < 1 || d.month > 12) d.year = 0;  // invalid
  }
  return d;
}

LifeRange LifeMatrix::parse_life_range(const std::string &s) const {
  LifeRange r;
  // Trim whitespace
  size_t first = s.find_first_not_of(" \t");
  if (first == std::string::npos) return r;
  std::string t = s.substr(first, s.find_last_not_of(" \t") - first + 1);
  // Split on '/' that appears after the 4th char (skip date separators)
  size_t slash = std::string::npos;
  for (size_t i = 4; i < t.size(); i++) {
    if (t[i] == '/') { slash = i; break; }
  }
  if (slash != std::string::npos) {
    r.start = parse_life_date(t.substr(0, slash));
    r.end   = parse_life_date(t.substr(slash + 1));
  } else {
    r.start = parse_life_date(t);
  }
  return r;
}

void LifeMatrix::parse_comma_dates(const std::string &s, std::vector<LifeDate> &out) const {
  out.clear();
  size_t pos = 0;
  while (pos < s.size()) {
    size_t comma = s.find(',', pos);
    if (comma == std::string::npos) comma = s.size();
    std::string tok = s.substr(pos, comma - pos);
    LifeDate d = parse_life_date(tok);
    if (d.is_set()) out.push_back(d);
    pos = comma + 1;
  }
}

void LifeMatrix::parse_comma_ranges(const std::string &s, std::vector<LifeRange> &out) const {
  out.clear();
  // Ranges are separated by comma; each range may itself contain a '/' (handled by parse_life_range)
  // We split on commas, but must not split inside a range's slash.
  // Since dates are "YYYY-MM-DD", the slash in a range appears as the 11th char of "YYYY-MM-DD/YYYY-MM-DD".
  // Strategy: split on comma, then try parse_life_range on each token.
  size_t pos = 0;
  while (pos < s.size()) {
    size_t comma = s.find(',', pos);
    if (comma == std::string::npos) comma = s.size();
    std::string tok = s.substr(pos, comma - pos);
    LifeRange r = parse_life_range(tok);
    if (r.is_set()) out.push_back(r);
    pos = comma + 1;
  }
}

int LifeMatrix::compute_doy(int year, int month, int day) const {
  static const int days_before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int doy = days_before[month > 12 ? 0 : month - 1] + day - 1;
  if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) doy++;
  return doy;
}

// ============================================================================
// LIFESPAN VIEW — SETTERS
// ============================================================================

void LifeMatrix::set_lifespan_birthday(const std::string &date) {
  lifespan_config_.birthday = parse_life_date(date);
  ESP_LOGD(TAG, "Lifespan birthday: %d-%02d-%02d", lifespan_config_.birthday.year,
           lifespan_config_.birthday.month, lifespan_config_.birthday.day);
}

void LifeMatrix::set_lifespan_kids(const std::string &dates) {
  parse_comma_dates(dates, lifespan_config_.kids);
  // Sort kids by birth year
  std::sort(lifespan_config_.kids.begin(), lifespan_config_.kids.end(),
            [](const LifeDate &a, const LifeDate &b) { return a.year < b.year; });
}

void LifeMatrix::set_lifespan_parents(const std::string &ranges) {
  lifespan_config_.parent_count = 0;
  std::vector<LifeRange> tmp;
  parse_comma_ranges(ranges, tmp);
  for (size_t i = 0; i < tmp.size() && i < 2; i++) {
    lifespan_config_.parents[i] = tmp[i];
    lifespan_config_.parent_count++;
  }
}

void LifeMatrix::set_lifespan_siblings(const std::string &dates) {
  parse_comma_dates(dates, lifespan_config_.siblings);
}

void LifeMatrix::set_lifespan_partner_ranges(const std::string &ranges) {
  parse_comma_ranges(ranges, lifespan_config_.partner_ranges);
}

void LifeMatrix::set_lifespan_marriage_ranges(const std::string &ranges) {
  parse_comma_ranges(ranges, lifespan_config_.marriage_ranges);
}

void LifeMatrix::set_lifespan_milestones(const std::string &milestones_str) {
  lifespan_config_.milestones.clear();
  size_t pos = 0;
  while (pos < milestones_str.size()) {
    size_t comma = milestones_str.find(',', pos);
    if (comma == std::string::npos) comma = milestones_str.size();
    std::string tok = milestones_str.substr(pos, comma - pos);
    // Trim
    while (!tok.empty() && tok.front() == ' ') tok.erase(0, 1);
    while (!tok.empty() && tok.back()  == ' ') tok.pop_back();
    LifeMilestone m;
    // Format: "YYYY-MM-DD:label" or just "YYYY-MM-DD"
    // Find first ':' after position 9 (to skip date separators)
    size_t colon = tok.find(':', 9);
    if (colon != std::string::npos) {
      m.date  = parse_life_date(tok.substr(0, colon));
      m.label = tok.substr(colon + 1);
    } else {
      m.date = parse_life_date(tok);
    }
    if (m.date.is_set()) lifespan_config_.milestones.push_back(m);
    pos = comma + 1;
  }
}

// ============================================================================
// LIFESPAN VIEW — BIRTHDAY EXTRACTION & PHASE PRECOMPUTATION
// ============================================================================

void LifeMatrix::apply_lifespan_year_events() {
  lifespan_year_events_.clear();
  if (!lifespan_config_.birthday.is_set()) return;

  auto add_event = [&](uint8_t month, uint8_t day) {
    if (month < 1 || month > 12 || day < 1 || day > 31) return;
    // Deduplicate
    for (const auto &e : lifespan_year_events_) {
      if (e.month == month && e.day == day) return;
    }
    YearEvent evt;
    evt.month = month;
    evt.day   = day;
    lifespan_year_events_.push_back(evt);
  };

  // Own birthday
  add_event(lifespan_config_.birthday.month, lifespan_config_.birthday.day);

  // Kids' birthdays
  for (const auto &k : lifespan_config_.kids)
    add_event(k.month, k.day);

  // Parents' birthdays
  for (int i = 0; i < lifespan_config_.parent_count; i++)
    if (lifespan_config_.parents[i].start.is_set())
      add_event(lifespan_config_.parents[i].start.month, lifespan_config_.parents[i].start.day);

  // Siblings' birthdays
  for (const auto &s : lifespan_config_.siblings)
    add_event(s.month, s.day);

  ESP_LOGD(TAG, "Lifespan year events: %d birthdays", (int)lifespan_year_events_.size());
}

// ============================================================================
// LIFESPAN VIEW — PHASE LOGIC
// ============================================================================

uint16_t LifeMatrix::get_active_phases(int age, int row_year) const {
  if (!lifespan_config_.birthday.is_set()) return 0;
  uint16_t mask = 0;
  int birth_year = lifespan_config_.birthday.year;

  // PARENTS: birth → moved_out_age (inclusive)
  if (lifespan_config_.moved_out_age > 0) {
    if (age <= lifespan_config_.moved_out_age) mask |= (1 << PHASE_PARENTS);
  }

  // SCHOOL phases: always starts at age 6 (European: 8 primary, 4 highschool, rest university)
  if (lifespan_config_.school_years_count > 0) {
    const int ss    = 6;
    const int se    = ss + lifespan_config_.school_years_count;
    const int total = lifespan_config_.school_years_count;
    const int primary_end    = ss + 8;
    const int highschool_end = ss + 12;

    if (age >= ss && age < primary_end)
      mask |= (1 << PHASE_PRIMARY);
    if (total > 8 && age >= primary_end && age < highschool_end)
      mask |= (1 << PHASE_HIGHSCHOOL);
    if (total > 12 && age >= highschool_end && age < se)
      mask |= (1 << PHASE_UNIVERSITY);
  }

  // CAREER: max(school_end, moved_out_age) → retirement_age (or life expectancy)
  {
    int career_start = -1;
    if (lifespan_config_.school_years_count > 0)
      career_start = 6 + lifespan_config_.school_years_count;
    if (lifespan_config_.moved_out_age > 0)
      career_start = (career_start < 0) ? lifespan_config_.moved_out_age
                                        : std::max(career_start, lifespan_config_.moved_out_age);
    int career_end = (lifespan_config_.retirement_age > 0) ? lifespan_config_.retirement_age
                                                           : lifespan_config_.life_expectancy_age;
    if (career_start >= 0 && age >= career_start && age < career_end)
      mask |= (1 << PHASE_CAREER);
  }

  // CHILDREN: first kid birth → last kid + 18
  if (!lifespan_config_.kids.empty()) {
    int first_age = lifespan_config_.kids.front().year - birth_year;
    int last_age  = lifespan_config_.kids.back().year  - birth_year;
    if (age >= first_age && age <= last_age + 18)
      mask |= (1 << PHASE_CHILDREN);
  }

  // PARTNER ranges
  for (const auto &r : lifespan_config_.partner_ranges) {
    if (!r.is_set()) continue;
    if (row_year >= r.start.year && (r.end.year == 0 || row_year <= r.end.year))
      mask |= (1 << PHASE_PARTNER);
  }

  // MARRIAGE ranges
  for (const auto &r : lifespan_config_.marriage_ranges) {
    if (!r.is_set()) continue;
    if (row_year >= r.start.year && (r.end.year == 0 || row_year <= r.end.year))
      mask |= (1 << PHASE_MARRIED);
  }

  // RETIREMENT
  if (lifespan_config_.retirement_age > 0 && age >= lifespan_config_.retirement_age)
    mask |= (1 << PHASE_RETIREMENT);

  return mask;
}

Color LifeMatrix::get_phase_color(int phase) const {
  switch (phase) {
    case PHASE_PARENTS:    return Color(255, 136,   0);  // amber
    case PHASE_PRIMARY:    return Color(  0, 200, 200);  // cyan
    case PHASE_HIGHSCHOOL: return Color(  0, 160, 100);  // teal
    case PHASE_UNIVERSITY: return Color(  0,  80, 255);  // blue
    case PHASE_CAREER:     return Color(  0, 200,  60);  // green
    case PHASE_CHILDREN:   return Color(255, 200,   0);  // golden yellow
    case PHASE_PARTNER:    return Color(255,  80, 160);  // rose
    case PHASE_MARRIED:    return Color(180,   0, 100);  // deep magenta
    case PHASE_RETIREMENT: return Color(140,  80, 255);  // lavender
    default:               return Color( 80,  80,  80);  // grey
  }
}

const char *LifeMatrix::get_phase_short_name(int phase) const {
  switch (phase) {
    case PHASE_PARENTS:    return "Home";
    case PHASE_PRIMARY:    return "Prim";
    case PHASE_HIGHSCHOOL: return "High";
    case PHASE_UNIVERSITY: return "Uni";
    case PHASE_CAREER:     return "Work";
    case PHASE_CHILDREN:   return "Kids";
    case PHASE_PARTNER:    return "Love";
    case PHASE_MARRIED:    return "Wed";
    case PHASE_RETIREMENT: return "Retir";
    default:               return "";
  }
}

Color LifeMatrix::blend_phase_colors(uint16_t phase_mask) const {
  if (phase_mask == 0) return Color(50, 50, 50);  // no phase: dim neutral
  int r = 0, g = 0, b = 0, count = 0;
  for (int i = 0; i < PHASE_COUNT; i++) {
    if (phase_mask & (1 << i)) {
      Color c = get_phase_color(i);
      r += c.r; g += c.g; b += c.b; count++;
    }
  }
  return Color((uint8_t)(r / count), (uint8_t)(g / count), (uint8_t)(b / count));
}

void LifeMatrix::precompute_lifespan_phases() {
  lifespan_active_phases_.clear();
  if (!lifespan_config_.birthday.is_set()) return;
  int birth_year = lifespan_config_.birthday.year;
  int le_age = lifespan_config_.life_expectancy_age;
  for (int phase = 0; phase < PHASE_COUNT; phase++) {
    for (int age = 0; age <= le_age; age++) {
      if (get_active_phases(age, birth_year + age) & (1 << phase)) {
        lifespan_active_phases_.push_back(phase);
        break;
      }
    }
  }
  ESP_LOGD(TAG, "Lifespan active phases: %d", (int)lifespan_active_phases_.size());
}

void LifeMatrix::update_lifespan_phase_cycle() {
  if (lifespan_config_.phase_cycle_s < 0.1f || lifespan_active_phases_.empty()) {
    lifespan_highlighted_phase_ = -1;
    return;
  }
  uint32_t cycle_ms = (uint32_t)(lifespan_config_.phase_cycle_s * 1000.f);
  uint32_t now = millis();

  // Initialize on first call
  if (lifespan_highlighted_phase_ == -1) {
    lifespan_phase_idx_ = 0;
    lifespan_highlighted_phase_ = lifespan_active_phases_[0];
    lifespan_phase_changed_ms_ = now;
    return;
  }
  if (now - lifespan_phase_changed_ms_ < cycle_ms) return;

  lifespan_phase_idx_ = (lifespan_phase_idx_ + 1) % (uint8_t)lifespan_active_phases_.size();
  lifespan_highlighted_phase_ = lifespan_active_phases_[lifespan_phase_idx_];
  lifespan_phase_changed_ms_ = now;
}

// ============================================================================
// LIFESPAN VIEW — RENDERING
// ============================================================================

void LifeMatrix::render_lifespan_view(display::Display &it, ESPTime &time, int viz_y, int viz_height) {
  int width = it.get_width();  // 32

  if (!lifespan_config_.birthday.is_set()) {
    int cx = width / 2, cy = viz_y + viz_height / 2;
    it.print(cx, cy - 9, font_small_, Color(80, 80, 80), display::TextAlign::CENTER, "Set");
    it.print(cx, cy + 9, font_small_, Color(80, 80, 80), display::TextAlign::CENTER, "bday");
    return;
  }

  int birth_year   = lifespan_config_.birthday.year;
  int current_year = time.year;
  int le_age       = lifespan_config_.life_expectancy_age;

  // Current day-of-year (0-based) and days in current year
  int doy = time.day_of_year - 1;
  if (doy < 0) doy = 0;
  bool is_leap = (current_year % 4 == 0 && (current_year % 100 != 0 || current_year % 400 == 0));
  int days_in_year = is_leap ? 366 : 365;

  // Update phase cycling
  update_lifespan_phase_cycle();
  int highlighted_phase = lifespan_highlighted_phase_;

  int max_rows = std::min(viz_height, 120);

  for (int age = 0; age < max_rows; age++) {
    int row_year = birth_year + age;
    int row_y    = viz_y + age;

    bool is_past    = (row_year < current_year);
    bool is_current = (row_year == current_year);
    bool is_grave = (age >= le_age);

    // ── MARKER COLUMN (x=0): decade ticks, life events ───────────────────────
    if (!is_grave) {
      // Determine highest-priority marker for this year
      bool has_milestone = false;
      for (const auto &m : lifespan_config_.milestones)
        if (m.date.year == row_year) { has_milestone = true; break; }

      bool has_event = false;
      for (const auto &k : lifespan_config_.kids)
        if (k.year == row_year) { has_event = true; break; }
      if (!has_event)
        for (int i = 0; i < lifespan_config_.parent_count; i++)
          if (lifespan_config_.parents[i].end.year == row_year) { has_event = true; break; }
      if (!has_event && lifespan_config_.moved_out_age > 0
          && birth_year + lifespan_config_.moved_out_age == row_year)   has_event = true;
      if (!has_event && lifespan_config_.retirement_age > 0
          && birth_year + lifespan_config_.retirement_age == row_year)  has_event = true;
      if (!has_event) {
        for (const auto &r : lifespan_config_.marriage_ranges)
          if (r.start.year == row_year) { has_event = true; break; }
      }

      bool is_decade = (age > 0 && age % 10 == 0);

      if (is_decade && !has_milestone && !has_event) {
        // Decade ticks: user marker color, kept very dim as structural orientation
        Color dcl = get_marker_color_value(marker_color_);
        uint8_t div = is_past ? 8 : (!is_current ? 16 : 4);
        draw_pixel(it, 0, row_y, Color(dcl.r / div, dcl.g / div, dcl.b / div));
      } else if (marker_style_ != MARKER_NONE && (has_milestone || has_event)) {
        // Events and milestones: complementary of this row's phase color (like year view)
        uint16_t phase_mask = get_active_phases(age, row_year);
        Color comp = get_complementary_color(blend_phase_colors(phase_mask));
        // Milestones at full brightness, life events at 60%
        float scale = has_milestone ? 1.0f : 0.6f;
        Color mc = Color((uint8_t)(comp.r * scale),
                         (uint8_t)(comp.g * scale),
                         (uint8_t)(comp.b * scale));
        // Temporal dimming
        if (is_past)          mc = Color(mc.r / 2, mc.g / 2, mc.b / 2);
        else if (!is_current) mc = Color(mc.r / 4, mc.g / 4, mc.b / 4);

        if (marker_style_ == MARKER_GRADIENT_PEAK) {
          // Vertical gradient spread: full at marker row, 50% at ±1, 25% at ±2
          float intensities[] = {0.25f, 0.5f, 1.0f, 0.5f, 0.25f};
          for (int i = 0; i < 5; i++) {
            int dot_y = row_y + (i - 2);
            if (dot_y < viz_y || dot_y >= viz_y + viz_height) continue;
            draw_pixel(it, 0, dot_y,
              Color((uint8_t)(mc.r * intensities[i]),
                    (uint8_t)(mc.g * intensities[i]),
                    (uint8_t)(mc.b * intensities[i])));
          }
        } else {
          draw_pixel(it, 0, row_y, mc);
        }
      } else {
        draw_pixel(it, 0, row_y, Color(0, 0, 0));
      }
    }

    // ── GRAVE: COSMOS / STARDUST ─────────────────────────────────────────────
    if (is_grave) {
      int grave_offset = age - le_age - 1;
      float t = (float)millis() * 0.001f;
      float depth = (float)grave_offset * 0.025f;  // 0.0 → ~1.0 over 40 rows
      for (int x = 0; x < width; x++) {
        // Good-distribution hash from (age, x) pair
        uint32_t h = (uint32_t)age * 2654435761u ^ (uint32_t)x * 2246822519u;
        h ^= h >> 15; h *= 0x45d9f3b7u; h ^= h >> 15;

        uint8_t star_roll  = h & 0xFF;
        uint8_t color_type = (h >> 8) & 0xFF;
        float   phase      = (float)((h >> 16) & 0xFF) * (6.283f / 255.0f);
        float   freq       = 0.4f + (float)((h >> 24) & 0x3F) * (1.2f / 63.0f);

        if (star_roll > 210) {
          // Star (~18% of pixels) — twinkles independently
          float tw = 0.35f + 0.65f * (0.5f + 0.5f * sinf(t * freq * 6.283f + phase));
          uint8_t br;
          Color sc;
          if (star_roll > 248) {
            // Bright star (~3%): near-white, strong twinkle
            br = (uint8_t)(tw * 255);
            sc = Color(br, br, br);
          } else if (color_type < 130) {
            // White star (~51% of stars)
            br = (uint8_t)(tw * 160);
            sc = Color(br, br, br);
          } else if (color_type < 205) {
            // Blue-white star (~29% of stars)
            br = (uint8_t)(tw * 140);
            sc = Color((uint8_t)(br * 0.7f), (uint8_t)(br * 0.85f), br);
          } else {
            // Warm/amber star (~20% of stars)
            br = (uint8_t)(tw * 140);
            sc = Color(br, (uint8_t)(br * 0.85f), (uint8_t)(br * 0.5f));
          }
          draw_pixel(it, x, row_y, sc);
        } else {
          // Deep space background: near-black with faint nebula gradient
          // Shifts from deep blue at LE line toward indigo deeper in
          uint8_t nv = (h >> 12) & 0x07;             // 0-7 patch variation
          uint8_t nb = (uint8_t)(2 + depth * 5 + nv * 0.4f);  // 2-10
          uint8_t nr = (uint8_t)(depth * 2);          // 0-2 red tint at depth
          draw_pixel(it, x, row_y, Color(nr, 0, nb));
        }
      }
      continue;
    }

    // ── NORMAL LIFE ROW (x=1..31) ────────────────────────────────────────────
    uint16_t phase_mask = get_active_phases(age, row_year);

    Color base_color;
    if (highlighted_phase >= 0) {
      if (phase_mask & (1 << highlighted_phase))
        base_color = get_phase_color(highlighted_phase);
      else
        base_color = Color(8, 8, 8);  // very dim when not in highlighted phase
    } else if (style_ == STYLE_TIME_SEGMENTS) {
      base_color = blend_phase_colors(phase_mask);
    } else if (style_ == STYLE_GRADIENT) {
      base_color = interpolate_gradient((float)age / (float)le_age, gradient_type_);
    } else if (style_ == STYLE_RAINBOW) {
      base_color = hsv_to_rgb((age * 360) / le_age, 1.0f, 1.0f);
    } else {
      base_color = color_active_;  // STYLE_SINGLE
    }

    // Present pixel x within current year (1–31)
    int present_x = -1;
    if (is_current) {
      present_x = 1 + (int)((float)doy / (float)days_in_year * 30.0f + 0.5f);
      if (present_x > 31) present_x = 31;
    }

    for (int x = 1; x < width; x++) {
      Color c;
      if (is_current && x == present_x) {
        c = Color(255, 255, 255);  // present pixel: bright white
      } else {
        float brightness;
        if (is_past) {
          brightness = 0.50f;
        } else if (!is_current) {
          brightness = 0.25f;  // future year
        } else {
          brightness = (x < present_x) ? 0.50f : 0.25f;  // elapsed vs remaining
        }
        c = Color((uint8_t)(base_color.r * brightness),
                  (uint8_t)(base_color.g * brightness),
                  (uint8_t)(base_color.b * brightness));
      }
      draw_pixel(it, x, row_y, c);
    }
  }

  // ── MILESTONE PIXELS overlaid at their exact day position ────────────────
  for (const auto &m : lifespan_config_.milestones) {
    if (!m.date.is_set()) continue;
    int age = m.date.year - birth_year;
    if (age < 0 || age >= max_rows || age > le_age) continue;
    int doy_m = compute_doy(m.date.year, m.date.month, m.date.day);
    bool leap_m = (m.date.year % 4 == 0 && (m.date.year % 100 != 0 || m.date.year % 400 == 0));
    int x = 1 + (int)((float)doy_m / (float)(leap_m ? 366 : 365) * 30.0f + 0.5f);
    if (x > 31) x = 31;
    bool past = (m.date.year < current_year);
    draw_pixel(it, x, viz_y + age, past ? Color(110, 110, 0) : Color(220, 220, 0));
  }

  // Kid birth markers (golden dot at birth day)
  for (const auto &k : lifespan_config_.kids) {
    if (!k.is_set()) continue;
    int age = k.year - birth_year;
    if (age < 0 || age >= max_rows || age > le_age) continue;
    int doy_k = compute_doy(k.year, k.month, k.day);
    bool leap_k = (k.year % 4 == 0 && (k.year % 100 != 0 || k.year % 400 == 0));
    int x = 1 + (int)((float)doy_k / (float)(leap_k ? 366 : 365) * 30.0f + 0.5f);
    if (x > 31) x = 31;
    draw_pixel(it, x, viz_y + age,
               (k.year < current_year) ? Color(128, 100, 0) : Color(255, 210, 0));
  }

  // ── TEXT AREA: time, phase name, or active milestone label ────────────────
  if (text_area_position_ != "None" && font_small_) {
    Viewport vp = calculate_viewport(it);
    if (highlighted_phase >= 0 && lifespan_config_.phase_cycle_s > 0.1f) {
      it.print(width / 2, vp.text_y, font_small_,
               get_phase_color(highlighted_phase),
               display::TextAlign::CENTER,
               get_phase_short_name(highlighted_phase));
    } else {
      // Check for active milestone label in the current year
      const char *label = nullptr;
      for (const auto &m : lifespan_config_.milestones) {
        if (m.date.year == current_year && !m.label.empty()) {
          label = m.label.c_str(); break;
        }
      }
      if (label) {
        it.print(width / 2, vp.text_y, font_small_,
                 Color(200, 200, 0), display::TextAlign::CENTER, label);
      } else {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", time.hour, time.minute);
        it.print(width / 2, vp.text_y, font_small_,
                 color_active_, display::TextAlign::CENTER, buf);
      }
    }
  }
}

}  // namespace life_matrix
}  // namespace esphome
