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
#include <algorithm>
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
  SCREEN_LIFESPAN = 5,
  SCREEN_GAME_OF_LIFE = 6
};

// Life phases for the lifespan view
enum LifePhase {
  PHASE_PARENTS    = 0,
  PHASE_PRIMARY    = 1,
  PHASE_HIGHSCHOOL = 2,
  PHASE_UNIVERSITY = 3,
  PHASE_CAREER     = 4,
  PHASE_CHILDREN   = 5,
  PHASE_PARTNER    = 6,
  PHASE_MARRIED    = 7,
  PHASE_RETIREMENT = 8,
  PHASE_COUNT      = 9
};

// A single point in time (year=0 means "not set")
struct LifeDate {
  int16_t year{0};
  uint8_t month{1};
  uint8_t day{1};
  bool is_set() const { return year != 0; }
};

// A date range (end.year==0 means open/ongoing)
struct LifeRange {
  LifeDate start;
  LifeDate end;
  bool is_set() const { return start.is_set(); }
};

// A named life milestone
struct LifeMilestone {
  LifeDate date;
  std::string label;
};

// Full biographical config for the lifespan view
struct LifespanConfig {
  LifeDate birthday;           // required anchor
  int moved_out_age{0};        // age when left parents' home (0 = not set)
  int school_years_count{0};   // total years in school, starts at age 6 (0 = not set)
  int retirement_age{0};       // age at retirement (0 = not set)
  int life_expectancy_age{90};
  float phase_cycle_s{3.0f};   // 0 = disabled
  LifeRange parents[2];
  int parent_count{0};
  std::vector<LifeDate> kids;
  std::vector<LifeDate> siblings;
  std::vector<LifeRange> partner_ranges;
  std::vector<LifeRange> marriage_ranges;
  std::vector<LifeMilestone> milestones;
};

// Display styles (how fill-bar views are colored)
enum DisplayStyle {
  STYLE_SINGLE = 0,
  STYLE_GRADIENT = 1,
  STYLE_TIME_SEGMENTS = 2,
  STYLE_RAINBOW = 3
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

// Day fill styles (used by year and month views)
enum DayFillStyle {
  DAY_FILL_ACTIVITY = 0,
  DAY_FILL_SCHEME   = 1,
  DAY_FILL_MIXED    = 2
};

// Year view event styles
enum YearEventStyle {
  YEAR_EVENT_NONE = 0,
  YEAR_EVENT_PULSE = 1,
  YEAR_EVENT_MARKERS = 4
};

// Celebration animation styles
enum CelebrationStyle {
  CELEB_SPARKLE   = 0,
  CELEB_PLASMA    = 1,
  CELEB_FIREWORKS = 2,  // staggered particle rockets + explosions (off by default)
  CELEB_HUE_CYCLE = 3   // full 360° hue rotation of the current display
};

// Per-frame color transform applied in draw_pixel()
enum ColorTransformMode {
  CTM_NONE      = 0,
  CTM_HUE_SHIFT = 1  // rotate all pixel hues using precomputed circulant matrix
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
  void set_style(int s) { style_ = (DisplayStyle)s; }
  void set_style(const std::string &style);
  void set_gradient_type(GradientType type) { gradient_type_ = type; }
  void set_gradient_type(const std::string &type);
  void set_marker_style(MarkerStyle style) { marker_style_ = style; }
  void set_marker_style(const std::string &style);
  void set_marker_color(MarkerColor color) { marker_color_ = color; }
  void set_marker_color(const std::string &color);

  // Year view configuration
  void set_year_events(const std::string &events);
  void set_day_fill(DayFillStyle style) { day_fill_style_ = style; }
  void set_day_fill(const std::string &style);
  void set_year_event_style(YearEventStyle style) { year_event_style_ = style; }
  void set_year_event_style(const std::string &style);

  // Lifespan view configuration (all YAML-only, biographical data)
  void set_lifespan_birthday(const std::string &date);
  void set_lifespan_moved_out_age(int age) { lifespan_config_.moved_out_age = age; }
  void set_lifespan_school_years(int years) { lifespan_config_.school_years_count = years; }
  void set_lifespan_kids(const std::string &dates);
  void set_lifespan_parents(const std::string &ranges);
  void set_lifespan_siblings(const std::string &dates);
  void set_lifespan_partner_ranges(const std::string &ranges);
  void set_lifespan_marriage_ranges(const std::string &ranges);
  void set_lifespan_milestones(const std::string &milestones_str);
  void set_lifespan_retirement_age(int age) { lifespan_config_.retirement_age = age; }
  void set_lifespan_life_expectancy(int age) { lifespan_config_.life_expectancy_age = age; }
  void set_lifespan_phase_cycle(float seconds) { lifespan_config_.phase_cycle_s = seconds; }
  void refresh_lifespan() { apply_lifespan_year_events(); precompute_lifespan_phases(); }

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
  void set_ha_style(select::Select *s) { ha_style_ = s; }
  void set_ha_gradient_type(select::Select *s) { ha_gradient_type_ = s; }
  void set_ha_fill_direction(select::Select *s) { ha_fill_direction_ = s; }
  void set_ha_marker_style(select::Select *s) { ha_marker_style_ = s; }
  void set_ha_marker_color(select::Select *s) { ha_marker_color_ = s; }
  void set_ha_text_area_position(select::Select *s) { ha_text_area_position_ = s; }
  void set_ha_day_fill(select::Select *s) { ha_day_fill_ = s; }
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
  DisplayStyle style_{STYLE_TIME_SEGMENTS};
  GradientType gradient_type_{GRADIENT_RED_BLUE};
  MarkerStyle marker_style_{MARKER_SINGLE_DOT};
  MarkerColor marker_color_{MARKER_BLUE};

  // Year view configuration
  std::vector<YearEvent> year_events_;
  DayFillStyle day_fill_style_{DAY_FILL_MIXED};
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
  void render_lifespan_view(display::Display &it, ESPTime &time, int viz_y, int viz_height);

  // Lifespan helpers
  void apply_lifespan_year_events();
  void precompute_lifespan_phases();
  void update_lifespan_phase_cycle();
  uint16_t get_active_phases(int age, int row_year) const;
  Color blend_phase_colors(uint16_t phase_mask) const;
  Color get_phase_color(int phase) const;
  const char *get_phase_short_name(int phase) const;
  LifeDate parse_life_date(const std::string &s) const;
  LifeRange parse_life_range(const std::string &s) const;
  void parse_comma_dates(const std::string &s, std::vector<LifeDate> &out) const;
  void parse_comma_ranges(const std::string &s, std::vector<LifeRange> &out) const;
  int compute_doy(int year, int month, int day) const;
  void render_big_bang_animation(display::Display &it, int viz_y, int viz_height);
  void render_ui_overlays(display::Display &it);
  void check_celebration(ESPTime &time);
  void render_celebration_overlay(display::Display &it, uint32_t elapsed_ms);
  void render_sparkle_celebration(display::Display &it, uint32_t elapsed_ms);
  void render_plasma_celebration(display::Display &it, uint32_t elapsed_ms);
  void render_fireworks_celebration(display::Display &it, uint32_t elapsed_ms);
  uint32_t get_celeb_duration(CelebrationStyle style);
  // draw_pixel: routes main-display pixels through the active per-frame color transform (CTM_HUE_SHIFT)
  void draw_pixel(display::Display &it, int x, int y, Color c);
  Color get_complementary_color(Color c);
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

  // Celebration animation state
  bool celebration_active_{false};
  uint32_t celebration_start_{0};
  uint8_t last_celebration_hour_{255};   // 255 = never fired
  uint8_t last_celebration_minute_{255};
  uint8_t last_celebration_day_{0};
  uint8_t last_celebration_month_{0};
  // Sequence: up to 4 styles played one after another
  CelebrationStyle celeb_sequence_[4]{CELEB_HUE_CYCLE, CELEB_SPARKLE, CELEB_SPARKLE, CELEB_SPARKLE};
  uint8_t celeb_seq_len_{1};   // number of active entries in celeb_sequence_
  uint8_t celeb_seq_idx_{0};   // current phase index
  // Per-frame color transform (CTM_HUE_SHIFT): circulant matrix precomputed once per frame in render()
  ColorTransformMode ctm_{CTM_NONE};
  float hue_mat_a_{1.f}, hue_mat_b_{0.f}, hue_mat_c_{0.f};  // identity by default

  // Lifespan view state
  LifespanConfig lifespan_config_{};
  std::vector<YearEvent> lifespan_year_events_;   // birthdays extracted from lifespan config
  std::vector<int> lifespan_active_phases_;        // phases that have active years (for cycling)
  int  lifespan_highlighted_phase_{-1};            // -1 = no highlight
  uint8_t lifespan_phase_idx_{0};                  // index into lifespan_active_phases_
  uint32_t lifespan_phase_changed_ms_{0};

  // OTA state
  bool ota_in_progress_{false};
  float ota_progress_{0.0f};

  // Time override for testing
  bool time_override_active_{false};
  ESPTime fake_time_{};
  uint32_t time_override_start_ms_{0};

  // HA entity pointers for bidirectional sync
  switch_::Switch *ha_complex_patterns_{nullptr};
  select::Select *ha_conway_speed_{nullptr};
  select::Select *ha_style_{nullptr};
  select::Select *ha_gradient_type_{nullptr};
  select::Select *ha_fill_direction_{nullptr};
  select::Select *ha_marker_style_{nullptr};
  select::Select *ha_marker_color_{nullptr};
  select::Select *ha_text_area_position_{nullptr};
  select::Select *ha_day_fill_{nullptr};
  select::Select *ha_year_event_style_{nullptr};
  number::Number *ha_bed_time_hour_{nullptr};
  number::Number *ha_work_start_hour_{nullptr};
  number::Number *ha_work_end_hour_{nullptr};
  number::Number *ha_cycle_time_{nullptr};
};

}  // namespace life_matrix
}  // namespace esphome
