#pragma once

#include "esphome/core/component.h"
#include "esphome/components/display/display.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/font/font.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include <array>
#include <vector>
#include <string>

namespace esphome {
namespace life_matrix {

// Grid dimensions (after 90° rotation: 32w × 120h)
static const int GRID_WIDTH = 32;
static const int GRID_HEIGHT = 120;
static const int GRID_SIZE = GRID_WIDTH * GRID_HEIGHT;  // 3840

// UI modes
enum UIMode {
  AUTO_CYCLE = 0,
  MANUAL_BROWSE = 1,
  SETTINGS = 2
};

// Screen IDs
enum ScreenID {
  SCREEN_YEAR = 0,
  SCREEN_MONTH = 1,
  SCREEN_DAY = 2,
  SCREEN_HOUR = 3,
  SCREEN_HABITS = 4,
  SCREEN_GAME_OF_LIFE = 5
};

// Color schemes
enum ColorScheme {
  COLOR_SINGLE = 0,
  COLOR_GRADIENT = 1,
  COLOR_TIME_SEGMENTS = 2,
  COLOR_RAINBOW = 3
};

// Gradient types
enum GradientType {
  GRADIENT_RED_BLUE = 0,
  GRADIENT_GREEN_YELLOW = 1,
  GRADIENT_CYAN_MAGENTA = 2,
  GRADIENT_PURPLE_ORANGE = 3,
  GRADIENT_BLUE_YELLOW = 4
};

// Marker styles
enum MarkerStyle {
  MARKER_NONE = 0,
  MARKER_SINGLE_DOT = 1,
  MARKER_GRADIENT_PEAK = 2
};

// Marker colors
enum MarkerColor {
  MARKER_BLUE = 0,
  MARKER_WHITE = 1,
  MARKER_YELLOW = 2,
  MARKER_RED = 3,
  MARKER_GREEN = 4,
  MARKER_CYAN = 5,
  MARKER_MAGENTA = 6
};

// Year view day styles
enum YearDayStyle {
  YEAR_DAY_ACTIVITY = 0,
  YEAR_DAY_SCHEME = 1,
  YEAR_DAY_ACTIVITY_SCHEME = 2
};

// Year view event styles
enum YearEventStyle {
  YEAR_EVENT_NONE = 0,
  YEAR_EVENT_PULSE = 1,
  YEAR_EVENT_MARKERS = 4
};

// Event storage structure
struct YearEvent {
  uint8_t month;  // 1-12
  uint8_t day;    // 1-31
};

// Pattern types for Game of Life
enum PatternType {
  PATTERN_RANDOM,
  PATTERN_R_PENTOMINO,
  PATTERN_ACORN,
  PATTERN_GLIDER,
  PATTERN_DIEHARD,
  PATTERN_MIXED
};

// Configuration structures
struct ScreenConfig {
  int id;
  bool enabled;
  std::string name;
};

struct TimeSegmentsConfig {
  int bed_time_hour;
  int wake_time_hour;
  int work_start_hour;
  int work_end_hour;
};

struct GameOfLifeConfig {
  int update_interval_ms;
  bool complex_patterns;
  bool auto_reset_on_stable;
  int stability_timeout_ms;
  bool demo_mode_enabled;
};

struct Viewport {
  int viz_y;
  int viz_height;
  int text_y;
};

class LifeMatrix : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  // Component configuration
  void set_display(display::Display *display) { display_ = display; }
  void set_time(time::RealTimeClock *time) { time_ = time; }
  void set_font_small(font::Font *font) { font_small_ = font; }
  void set_font_medium(font::Font *font) { font_medium_ = font; }
  void set_status_led(light::LightState *led) { status_led_ = led; }
  void set_gol_final_generation_sensor(sensor::Sensor *sensor) { gol_final_generation_sensor_ = sensor; }
  void set_gol_final_population_sensor(sensor::Sensor *sensor) { gol_final_population_sensor_ = sensor; }
  void set_grid_dimensions(int width, int height);
  void set_screen_cycle_time(float seconds) { screen_cycle_time_ = seconds; }
  void set_text_area_position(const std::string &position) { text_area_position_ = position; }
  void set_fill_direction(const std::string &direction) { fill_direction_bottom_to_top_ = (direction == "Bottom to Top"); }

  // Color configuration
  void set_color_active(Color c) { color_active_ = c; }
  void set_color_weekend(Color c) { color_weekend_ = c; }
  void set_color_marker(Color c) { color_marker_ = c; }
  void set_color_highlight(Color c) { color_highlight_ = c; }
  void set_color_scheme(ColorScheme scheme) { color_scheme_ = scheme; }
  void set_color_scheme(const std::string &scheme);
  void set_gradient_type(GradientType type) { gradient_type_ = type; }
  void set_gradient_type(const std::string &type);
  void set_marker_style(MarkerStyle style) { marker_style_ = style; }
  void set_marker_style(const std::string &style);
  void set_marker_color(MarkerColor color) { marker_color_ = color; }
  void set_marker_color(const std::string &color);

  // Year view configuration
  void set_year_events(const std::string &events);
  void set_year_day_style(YearDayStyle style) { year_day_style_ = style; }
  void set_year_day_style(const std::string &style);
  void set_year_event_style(YearEventStyle style) { year_event_style_ = style; }
  void set_year_event_style(const std::string &style);

  // Screen management
  void register_screen(int screen_id, bool enabled);
  void next_screen();
  void prev_screen();
  void set_current_screen(int screen_idx);
  int get_current_screen_id();
  void update_screen_cycle();

  // Main rendering
  void render(display::Display &it, ESPTime &time);

  // Game of Life
  void initialize_game_of_life(PatternType pattern = PATTERN_MIXED);
  void update_game_of_life();
  void reset_game_of_life();
  void set_game_update_interval(int ms) { game_config_.update_interval_ms = ms; }
  void set_demo_mode(bool enabled);
  uint8_t get_cell(int x, int y);
  void set_cell(int x, int y, uint8_t value);
  int count_neighbors(int x, int y);
  bool is_stable();
  int get_population();
  int get_generation() { return game_generation_; }
  void place_pattern(int x, int y, PatternType pattern);
  void randomize_cells(int density_percent);

  // UI state management
  void set_ui_mode(UIMode mode);
  UIMode get_ui_mode() { return ui_mode_; }
  void handle_input();
  void check_ui_timeout();
  void toggle_pause();
  void set_paused(bool paused) { ui_paused_ = paused; }
  bool is_paused() { return ui_paused_; }
  void update_status_led();

  // Settings system
  void adjust_setting(int direction);
  void next_settings_cursor();
  void prev_settings_cursor();
  void set_settings_cursor(int pos) { settings_cursor_ = pos; }
  int get_settings_cursor() { return settings_cursor_; }
  std::string get_current_setting_name();
  std::string get_current_setting_value();

  // Time segments configuration
  void set_time_segments(const TimeSegmentsConfig &config) { time_segments_ = config; }
  TimeSegmentsConfig get_time_segments() { return time_segments_; }

  // Game of Life configuration
  void set_game_config(const GameOfLifeConfig &config) { game_config_ = config; }
  GameOfLifeConfig get_game_config() { return game_config_; }

  // OTA handling
  void set_ota_in_progress(bool in_progress) { ota_in_progress_ = in_progress; if (!in_progress) ota_progress_ = 0.0f; }
  bool is_ota_in_progress() { return ota_in_progress_; }
  void set_ota_progress(float progress) { ota_progress_ = progress; }
  float get_ota_progress() { return ota_progress_; }

  // Time override for testing
  void set_time_override(const std::string &time_str);
  void clear_time_override();
  bool has_time_override() { return time_override_active_; }
  ESPTime get_time_override() { return fake_time_; }

  // HA entity sync — register entities for bidirectional sync (called from YAML on_boot)
  void set_ha_complex_patterns(switch_::Switch *sw) { ha_complex_patterns_ = sw; }
  void set_ha_conway_speed(select::Select *s) { ha_conway_speed_ = s; }
  void set_ha_color_scheme(select::Select *s) { ha_color_scheme_ = s; }
  void set_ha_gradient_type(select::Select *s) { ha_gradient_type_ = s; }
  void set_ha_fill_direction(select::Select *s) { ha_fill_direction_ = s; }
  void set_ha_marker_style(select::Select *s) { ha_marker_style_ = s; }
  void set_ha_marker_color(select::Select *s) { ha_marker_color_ = s; }
  void set_ha_text_area_position(select::Select *s) { ha_text_area_position_ = s; }
  void set_ha_year_day_style(select::Select *s) { ha_year_day_style_ = s; }
  void set_ha_year_event_style(select::Select *s) { ha_year_event_style_ = s; }
  void set_ha_bed_time_hour(number::Number *n) { ha_bed_time_hour_ = n; }
  void set_ha_work_start_hour(number::Number *n) { ha_work_start_hour_ = n; }
  void set_ha_work_end_hour(number::Number *n) { ha_work_end_hour_ = n; }
  void set_ha_cycle_time(number::Number *n) { ha_cycle_time_ = n; }

 protected:
  // Component references
  display::Display *display_{nullptr};
  time::RealTimeClock *time_{nullptr};
  font::Font *font_small_{nullptr};
  font::Font *font_medium_{nullptr};
  light::LightState *status_led_{nullptr};
  sensor::Sensor *gol_final_generation_sensor_{nullptr};
  sensor::Sensor *gol_final_population_sensor_{nullptr};

  // Colors
  Color color_active_{255, 255, 255};
  Color color_weekend_{255, 0, 0};
  Color color_marker_{0, 102, 255};  // Blue instead of white
  Color color_highlight_{255, 255, 0};
  Color color_gradient_start_{0, 255, 255};  // Cyan
  Color color_gradient_end_{255, 0, 255};    // Magenta

  // Display configuration
  int grid_width_{GRID_WIDTH};
  int grid_height_{GRID_HEIGHT};
  float screen_cycle_time_{3.0f};
  std::string text_area_position_{"Top"};
  bool fill_direction_bottom_to_top_{true};
  ColorScheme color_scheme_{COLOR_TIME_SEGMENTS};
  GradientType gradient_type_{GRADIENT_RED_BLUE};
  MarkerStyle marker_style_{MARKER_SINGLE_DOT};
  MarkerColor marker_color_{MARKER_BLUE};

  // Year view configuration
  std::vector<YearEvent> year_events_;
  YearDayStyle year_day_style_{YEAR_DAY_ACTIVITY_SCHEME};
  YearEventStyle year_event_style_{YEAR_EVENT_MARKERS};

  // Screen management
  std::vector<ScreenConfig> screens_;
  std::vector<int> enabled_screen_ids_;
  int current_screen_idx_{0};
  unsigned long last_switch_time_{0};

  // Rendering helpers
  Viewport calculate_viewport(display::Display &it);
  void render_year_view(display::Display &it, ESPTime &time, int viz_y, int viz_height);
  void render_month_view(display::Display &it, ESPTime &time, int viz_y, int viz_height);
  void render_day_view(display::Display &it, ESPTime &time, int viz_y, int viz_height);
  void render_hour_view(display::Display &it, ESPTime &time, int viz_y, int viz_height);
  void render_game_of_life(display::Display &it, int viz_y, int viz_height);
  void render_big_bang_animation(display::Display &it, int viz_y, int viz_height);
  void render_ui_overlays(display::Display &it);
  Color hsv_to_rgb(int hue, float saturation, float value);
  Color get_gradient_color(float progress);
  Color get_time_segment_color(int hour);
  Color interpolate_gradient(float progress, GradientType type);
  Color get_marker_color_value(MarkerColor color);
  void draw_marker(display::Display &it, int mark_y, int width, MarkerStyle style, Color color);

  // Year view helpers
  void parse_year_events(const std::string &events_str);
  int day_of_week_sakamoto(int y, int m, int d);
  void get_days_in_month(int year, uint8_t days_out[12]);
  Color get_activity_color(int month_idx, int activity_type, bool use_scheme_color);
  Color get_event_color(int month_idx, const Color &scheme_color);
  uint8_t get_activity_type(int pixel_y, int month_h, bool is_weekend);

 protected:
  // Game of Life state
  std::array<uint8_t, GRID_SIZE> game_grid_{};       // Age of each cell (0 = dead)
  std::array<uint8_t, GRID_SIZE> game_grid_back_{};  // Back buffer for updates
  bool game_initialized_{false};
  unsigned long game_last_update_{0};
  unsigned long game_start_time_{0};
  int game_generation_{0};
  int game_last_max_age_{0};
  std::array<int, 30> population_history_{};  // Last 30 population counts
  int history_idx_{0};
  bool history_filled_{false};
  int game_births_{0};
  int game_deaths_{0};
  unsigned long game_stable_since_{0};
  unsigned long game_stable_paused_elapsed_{0};
  bool game_is_stable_{false};
  bool gol_was_visible_{false};
  bool game_demo_mode_{false};
  unsigned long game_demo_start_time_{0};
  bool game_reset_animation_{false};
  unsigned long game_reset_animation_start_{0};
  GameOfLifeConfig game_config_{200, true, true, 60000, false};

  // UI state
  UIMode ui_mode_{AUTO_CYCLE};
  unsigned long ui_last_input_ms_{0};
  bool ui_paused_{false};
  int settings_cursor_{0};
  unsigned long settings_flash_ms_{0};

  // Time segments configuration
  TimeSegmentsConfig time_segments_{22, 6, 9, 17};

  // OTA state
  bool ota_in_progress_{false};
  float ota_progress_{0.0f};

  // Time override for testing
  bool time_override_active_{false};
  ESPTime fake_time_{};

  // HA entity pointers for bidirectional sync
  switch_::Switch *ha_complex_patterns_{nullptr};
  select::Select *ha_conway_speed_{nullptr};
  select::Select *ha_color_scheme_{nullptr};
  select::Select *ha_gradient_type_{nullptr};
  select::Select *ha_fill_direction_{nullptr};
  select::Select *ha_marker_style_{nullptr};
  select::Select *ha_marker_color_{nullptr};
  select::Select *ha_text_area_position_{nullptr};
  select::Select *ha_year_day_style_{nullptr};
  select::Select *ha_year_event_style_{nullptr};
  number::Number *ha_bed_time_hour_{nullptr};
  number::Number *ha_work_start_hour_{nullptr};
  number::Number *ha_work_end_hour_{nullptr};
  number::Number *ha_cycle_time_{nullptr};
};

}  // namespace life_matrix
}  // namespace esphome
