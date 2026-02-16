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
  static const char* const yds_names[] = {"Activity", "Scheme", "Activity + Scheme"};
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
        color_scheme_ = (ColorScheme)cycle_enum((int)color_scheme_, 3);
        if (ha_color_scheme_) ha_color_scheme_->publish_state(cs_names[color_scheme_]);
        ESP_LOGD(TAG, "Color scheme: %d", (int)color_scheme_);
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
    } else if (screen_id == SCREEN_YEAR) {
      if (local == 0) {
        color_scheme_ = (ColorScheme)cycle_enum((int)color_scheme_, 3);
        if (ha_color_scheme_) ha_color_scheme_->publish_state(cs_names[color_scheme_]);
        ESP_LOGD(TAG, "Color scheme: %d", (int)color_scheme_);
      } else if (local == 1) {
        marker_style_ = (MarkerStyle)cycle_enum((int)marker_style_, 2);
        if (ha_marker_style_) ha_marker_style_->publish_state(ms_names[marker_style_]);
        ESP_LOGD(TAG, "Marker style: %d", (int)marker_style_);
      } else if (local == 2) {
        year_day_style_ = (YearDayStyle)cycle_enum((int)year_day_style_, 2);
        if (ha_year_day_style_) ha_year_day_style_->publish_state(yds_names[year_day_style_]);
        ESP_LOGD(TAG, "Year day style: %d", (int)year_day_style_);
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
    if (local == 0) return "Color";
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
  } else if (screen_id == SCREEN_YEAR) {
    if (local == 0) return "Color";
    if (local == 1) return "Mark";
    if (local == 2) return "Days";
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
      if (color_scheme_ == COLOR_SINGLE) return "Singl";
      if (color_scheme_ == COLOR_GRADIENT) return "Gradt";
      if (color_scheme_ == COLOR_TIME_SEGMENTS) return "TmSeg";
      if (color_scheme_ == COLOR_RAINBOW) return "Rainb";
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
  } else if (screen_id == SCREEN_YEAR) {
    if (local == 0) {
      // Color scheme
      if (color_scheme_ == COLOR_SINGLE) return "Singl";
      if (color_scheme_ == COLOR_GRADIENT) return "Gradt";
      if (color_scheme_ == COLOR_TIME_SEGMENTS) return "TmSeg";
      if (color_scheme_ == COLOR_RAINBOW) return "Rainb";
    } else if (local == 1) {
      // Marker style
      if (marker_style_ == MARKER_NONE) return "None";
      if (marker_style_ == MARKER_SINGLE_DOT) return "Dot";
      if (marker_style_ == MARKER_GRADIENT_PEAK) return "Peak";
    } else if (local == 2) {
      // Day style
      if (year_day_style_ == YEAR_DAY_ACTIVITY) return "Activ";
      if (year_day_style_ == YEAR_DAY_SCHEME) return "Schme";
      if (year_day_style_ == YEAR_DAY_ACTIVITY_SCHEME) return "Both";
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

void LifeMatrix::render(display::Display &it, ESPTime &time) {
  // OTA guard - show minimal UI during update
  if (ota_in_progress_) {
    int center_x = it.get_width() / 2;
    int center_y = it.get_height() / 2;
    it.print(center_x, center_y, font_small_, color_active_, display::TextAlign::CENTER, "OTA");
    return;
  }

  // Use fake time if override is active
  ESPTime &display_time = time_override_active_ ? fake_time_ : time;

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
      it.draw_pixel_at(6, 40, pattern_color);
      it.draw_pixel_at(7, 41, pattern_color);
      it.draw_pixel_at(5, 42, pattern_color);
      it.draw_pixel_at(6, 42, pattern_color);
      it.draw_pixel_at(7, 42, pattern_color);

      // Blinker (center)
      it.draw_pixel_at(15, 41, pattern_color);
      it.draw_pixel_at(16, 41, pattern_color);
      it.draw_pixel_at(17, 41, pattern_color);

      // Block (right side)
      it.draw_pixel_at(25, 41, pattern_color);
      it.draw_pixel_at(26, 41, pattern_color);
      it.draw_pixel_at(25, 42, pattern_color);
      it.draw_pixel_at(26, 42, pattern_color);

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

        it.draw_pixel_at(col, y_pos, cell_color);
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
        it.draw_pixel_at(col, y_pos, Color(255, 255, 255));
      } else if (dist2 >= inner_r2 && dist2 <= outer_r2) {
        // Rainbow ring — atan2f only for the few ring pixels
        int hue = (int)((atan2f(dy, dx) + 3.14159f) * 57.2958f);
        hue = (hue + hue_offset) % 360;
        it.draw_pixel_at(col, y_pos, hsv_to_rgb(hue, 1.0f, brightness));
      }
    }
  }
}

void LifeMatrix::render_month_view(display::Display &it, ESPTime &time, int viz_y, int viz_height) {
  int center_x = it.get_width() / 2;
  Viewport vp = calculate_viewport(it);

  // Display month name
  char month_str[4];
  time.strftime(month_str, sizeof(month_str), "%b");
  it.print(center_x, vp.text_y, font_small_, color_active_, display::TextAlign::CENTER, month_str);

  // Calculate days in month
  int month = time.month;
  int year = time.year;
  int days_in_month;
  if (month == 2) {
    days_in_month = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
  } else if (month == 4 || month == 6 || month == 9 || month == 11) {
    days_in_month = 30;
  } else {
    days_in_month = 31;
  }

  // Fill rows with gradient based on day progress
  int dom = time.day_of_month;
  int filled_rows = (dom * viz_height) / days_in_month;

  for (int row = 0; row < viz_height; row++) {
    int y_pos = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - row) : (viz_y + row);

    if (row < filled_rows) {
      float progress = (float)row / (float)viz_height;
      Color pixel_color = Color(
        (int)(255 * progress),
        (int)(200 * (1.0f - progress)),
        (int)(100 + 100 * progress)
      );

      for (int col = 1; col <= 30; col++) {
        it.draw_pixel_at(col, y_pos, pixel_color);
      }
    }
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

      it.draw_pixel_at(col, y_pos, pixel_color);
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
  if (color_scheme_ == COLOR_TIME_SEGMENTS) {
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
        it.draw_pixel_at(draw_col, y_pos, quarter_color);
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
          it.draw_pixel_at(current_col + 1, y_pos, quarter_color);
          spiral_pos++;
        }
        direction = (direction + 1) % 4;

        // Second side
        for (int i = 0; i < length && spiral_pos < seconds_in_quarter; i++) {
          if (direction == 2) current_col--;
          else if (direction == 0) current_col++;

          int abs_row = quarter_start_row + current_spiral_row;
          int y_pos = fill_direction_bottom_to_top_ ? (viz_y + viz_height - 1 - abs_row) : (viz_y + abs_row);
          it.draw_pixel_at(current_col + 1, y_pos, quarter_color);
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

      if (color_scheme_ == COLOR_GRADIENT) {
        float progress = (float)row / 120.0f;
        pixel_color = interpolate_gradient(progress, gradient_type_);
      } else if (color_scheme_ == COLOR_RAINBOW) {
        int hue = (row * 360) / 120;
        pixel_color = hsv_to_rgb(hue, 1.0f, 1.0f);
      }
      // else: Single Color - use color_active_ (already set)

      it.draw_pixel_at(x_pos, y_pos, pixel_color);
    }
  }
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
          it.draw_pixel_at(0, mark_y, marker_clr);
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
              it.draw_pixel_at(0, dot_y, faded);
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

    if (color_scheme_ == COLOR_SINGLE) {
      scheme_colors[m] = color_active_;
    } else if (color_scheme_ == COLOR_GRADIENT) {
      scheme_colors[m] = interpolate_gradient(progress, gradient_type_);
    } else if (color_scheme_ == COLOR_TIME_SEGMENTS) {
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
    if (year_day_style_ == YEAR_DAY_ACTIVITY) {
      // Fixed activity colors
      activity_colors[m][0] = Color(50, 50, 50);      // Sleep: dark gray
      activity_colors[m][1] = Color(255, 120, 0);     // Work: orange
      activity_colors[m][2] = Color(0, 200, 100);     // Life: green
    } else if (year_day_style_ == YEAR_DAY_ACTIVITY_SCHEME) {
      // Activity + Scheme: sleep=dark, work=scheme*0.5, life=scheme
      activity_colors[m][0] = Color(50, 50, 50);
      activity_colors[m][1] = Color(scheme_colors[m].r >> 1, scheme_colors[m].g >> 1, scheme_colors[m].b >> 1);
      activity_colors[m][2] = scheme_colors[m];
    } else {  // YEAR_DAY_SCHEME
      // All same color
      activity_colors[m][0] = scheme_colors[m];
      activity_colors[m][1] = scheme_colors[m];
      activity_colors[m][2] = scheme_colors[m];
    }
  }

  // Pre-compute event colors (for Pulse mode) - use complementary colors
  Color event_colors[12];
  for (int m = 0; m < 12; m++) {
    // Convert scheme color to HSV to get complementary hue
    Color scheme_color = scheme_colors[m];
    uint8_t max_c = std::max({scheme_color.r, scheme_color.g, scheme_color.b});
    uint8_t min_c = std::min({scheme_color.r, scheme_color.g, scheme_color.b});
    int hue = 0;

    if (max_c != min_c) {
      if (max_c == scheme_color.r) {
        hue = 60 * ((scheme_color.g - scheme_color.b) / (float)(max_c - min_c));
      } else if (max_c == scheme_color.g) {
        hue = 60 * (2 + (scheme_color.b - scheme_color.r) / (float)(max_c - min_c));
      } else {
        hue = 60 * (4 + (scheme_color.r - scheme_color.g) / (float)(max_c - min_c));
      }
      if (hue < 0) hue += 360;
    }

    // Complementary hue: opposite on color wheel (+180°)
    int complementary_hue = (hue + 180) % 360;
    event_colors[m] = hsv_to_rgb(complementary_hue, 1.0f, 1.0f);
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

      it.draw_pixel_at(0, screen_y, event_marker_color);
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

          it.draw_pixel_at(day, screen_y, pixel_color);
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

          it.draw_pixel_at(day, screen_y, pixel_color);
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

          it.draw_pixel_at(day, screen_y, pixel_color);
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

    it.draw_pixel_at(0, screen_y, breathing);
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

void LifeMatrix::set_color_scheme(const std::string &scheme) {
  if (scheme == "Single Color") {
    color_scheme_ = COLOR_SINGLE;
  } else if (scheme == "Gradient") {
    color_scheme_ = COLOR_GRADIENT;
  } else if (scheme == "Time Segments") {
    color_scheme_ = COLOR_TIME_SEGMENTS;
  } else if (scheme == "Rainbow") {
    color_scheme_ = COLOR_RAINBOW;
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

void LifeMatrix::set_year_day_style(const std::string &style) {
  if (style == "Activity") {
    year_day_style_ = YEAR_DAY_ACTIVITY;
  } else if (style == "Scheme Color") {
    year_day_style_ = YEAR_DAY_SCHEME;
  } else if (style == "Activity + Scheme") {
    year_day_style_ = YEAR_DAY_ACTIVITY_SCHEME;
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
    it.draw_pixel_at(0, mark_y, color);
    it.draw_pixel_at(width - 1, mark_y, color);
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
      it.draw_pixel_at(0, dot_y, faded_clr);
      it.draw_pixel_at(width - 1, dot_y, faded_clr);
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

  ESP_LOGD(TAG, "Parsed %d year events", (int)year_events_.size());
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
  ESP_LOGI(TAG, "Time override set to: %04d-%02d-%02d %02d:%02d:%02d (DoW=%d, DoY=%d)",
           year, month, day, hour, minute, second, fake_time_.day_of_week, fake_time_.day_of_year);
}

void LifeMatrix::clear_time_override() {
  if (time_override_active_) {
    time_override_active_ = false;
    ESP_LOGI(TAG, "Time override cleared - using real time");
  }
}

}  // namespace life_matrix
}  // namespace esphome
