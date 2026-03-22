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
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/text/text.h"
#include "esphome/components/button/button.h"
#include "nvs.h"
#include <algorithm>
#include <array>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <cmath>

namespace esphome {
namespace life_matrix {

// ---------------------------------------------------------------------------
// Minimal entity implementations for auto-generated HA entities
// (avoids dependency on esphome/components/template/* sub-components)
// ---------------------------------------------------------------------------

// Helper: open our dedicated NVS namespace for life_matrix settings.
// Using a fixed namespace avoids collisions with ESPHome's own preferences
// and makes keys stable regardless of component registration order.
static inline esp_err_t lm_nvs_open(nvs_handle_t &handle, nvs_open_mode_t mode) {
  return nvs_open("lm_cfg", mode, &handle);
}
// Build a stable 8-char NVS key from an entity's object_id hash + type salt.
static inline void lm_nvs_key(char *buf, uint32_t hash) {
  snprintf(buf, 12, "%08" PRIX32, hash);
}

class LMSwitch : public switch_::Switch {
 public:
  // ESPHome 2026+ removed runtime set_name/set_icon/set_entity_category from EntityBase.
  // These shims store values and apply them via configure_entity_() in finalize_entity_config().
  void set_name(const std::string &name) { lm_name_ = name; }
  void set_icon(const std::string & /*icon*/) {}  // icon string pool not accessible here
  void set_entity_category(uint8_t cat) { lm_entity_cat_ = cat; }
  void finalize_entity_config(const std::string &object_id = "", bool internal = false) {
    const std::string &src = object_id.empty() ? lm_name_ : object_id;
    std::string oid;
    for (char c : src) {
      if (isalnum((unsigned char)c)) oid += tolower((unsigned char)c);
      else if (!oid.empty() && oid.back() != '_') oid += '_';
    }
    while (!oid.empty() && oid.back() == '_') oid.pop_back();
    uint32_t hash = 2166136261UL;
    for (char c : oid) hash = (hash * 16777619UL) ^ (uint8_t)c;
    uint32_t fields = ((uint32_t)lm_entity_cat_ << 26) | (internal ? (1u << 24) : 0u);
    this->configure_entity_(lm_name_.c_str(), hash, fields);
  }

  void publish_initial_state() {
    bool initial = (this->restore_mode == switch_::SwitchRestoreMode::SWITCH_RESTORE_DEFAULT_ON);
    if (this->restore_mode == switch_::SwitchRestoreMode::SWITCH_ALWAYS_ON) initial = true;
    if (initial) this->turn_on(); else this->turn_off();
  }
  void write_state(bool state) override {
    this->publish_state(state);
    if (this->restore_mode == switch_::SwitchRestoreMode::SWITCH_RESTORE_DEFAULT_ON ||
        this->restore_mode == switch_::SwitchRestoreMode::SWITCH_RESTORE_DEFAULT_OFF) {
      nvs_handle_t h;
      if (lm_nvs_open(h, NVS_READWRITE) == ESP_OK) {
        char key[12];
        lm_nvs_key(key, this->get_object_id_hash() ^ 0x5753U);
        nvs_set_u8(h, key, (uint8_t)state);
        nvs_commit(h);
        nvs_close(h);
      }
    }
  }
  void set_optimistic(bool) {}
 private:
  std::string lm_name_;
  uint8_t lm_entity_cat_{0};
};

class LMSelect : public select::Select {
 public:
  void set_name(const std::string &name) { lm_name_ = name; }
  void set_icon(const std::string & /*icon*/) {}
  void set_entity_category(uint8_t cat) { lm_entity_cat_ = cat; }
  void finalize_entity_config(const std::string &object_id = "", bool internal = false) {
    const std::string &src = object_id.empty() ? lm_name_ : object_id;
    std::string oid;
    for (char c : src) {
      if (isalnum((unsigned char)c)) oid += tolower((unsigned char)c);
      else if (!oid.empty() && oid.back() != '_') oid += '_';
    }
    while (!oid.empty() && oid.back() == '_') oid.pop_back();
    uint32_t hash = 2166136261UL;
    for (char c : oid) hash = (hash * 16777619UL) ^ (uint8_t)c;
    uint32_t fields = ((uint32_t)lm_entity_cat_ << 26) | (internal ? (1u << 24) : 0u);
    this->configure_entity_(lm_name_.c_str(), hash, fields);
  }

  void control(const std::string &value) override {
    this->publish_state(value);
    if (!this->restore_value_) return;
    nvs_handle_t h;
    if (lm_nvs_open(h, NVS_READWRITE) == ESP_OK) {
      char key[12];
      lm_nvs_key(key, this->get_object_id_hash() ^ 0x4C53U);
      const auto &opts = this->traits.get_options();
      for (size_t i = 0; i < opts.size(); i++) {
        if (std::string(opts[i]) == value) { nvs_set_u8(h, key, (uint8_t)i); break; }
      }
      nvs_commit(h);
      nvs_close(h);
    }
  }
  void set_restore_value(bool v) { restore_value_ = v; }
  void set_options(std::initializer_list<const char*> opts) { this->traits.set_options(opts); }
  void set_initial_option(const std::string &s) { initial_option_ = s; }
  const std::string &get_initial_option() const { return initial_option_; }
  void set_optimistic(bool) {}
 protected:
  bool restore_value_{false};
  std::string initial_option_;
  std::string lm_name_;
  uint8_t lm_entity_cat_{0};
};

class LMNumber : public number::Number {
 public:
  void set_name(const std::string &name) { lm_name_ = name; }
  void set_icon(const std::string & /*icon*/) {}
  void set_entity_category(uint8_t cat) { lm_entity_cat_ = cat; }
  void set_unit_of_measurement(const std::string & /*unit*/) {}  // UOM pool not accessible here
  void finalize_entity_config(const std::string &object_id = "", bool internal = false) {
    const std::string &src = object_id.empty() ? lm_name_ : object_id;
    std::string oid;
    for (char c : src) {
      if (isalnum((unsigned char)c)) oid += tolower((unsigned char)c);
      else if (!oid.empty() && oid.back() != '_') oid += '_';
    }
    while (!oid.empty() && oid.back() == '_') oid.pop_back();
    uint32_t hash = 2166136261UL;
    for (char c : oid) hash = (hash * 16777619UL) ^ (uint8_t)c;
    uint32_t fields = ((uint32_t)lm_entity_cat_ << 26) | (internal ? (1u << 24) : 0u);
    this->configure_entity_(lm_name_.c_str(), hash, fields);
  }

  void control(float value) override {
    this->publish_state(value);
    if (!this->restore_value_) return;
    nvs_handle_t h;
    if (lm_nvs_open(h, NVS_READWRITE) == ESP_OK) {
      char key[12];
      lm_nvs_key(key, this->get_object_id_hash() ^ 0x4E4DU);
      nvs_set_blob(h, key, &value, sizeof(float));
      nvs_commit(h);
      nvs_close(h);
    }
  }
  void set_restore_value(bool v) { restore_value_ = v; }
  void set_initial_value(float v) { initial_value_ = v; }
  float get_initial_value() const { return initial_value_; }
  void set_mode(number::NumberMode m) { this->traits.set_mode(m); }
  void set_optimistic(bool) {}
 protected:
  bool restore_value_{false};
  float initial_value_{NAN};
  std::string lm_name_;
  uint8_t lm_entity_cat_{0};
};

class LMText : public text::Text {
 public:
  void set_name(const std::string &name) { lm_name_ = name; }
  void set_icon(const std::string & /*icon*/) {}
  void set_entity_category(uint8_t cat) { lm_entity_cat_ = cat; }
  void finalize_entity_config(const std::string &object_id = "", bool internal = false) {
    const std::string &src = object_id.empty() ? lm_name_ : object_id;
    std::string oid;
    for (char c : src) {
      if (isalnum((unsigned char)c)) oid += tolower((unsigned char)c);
      else if (!oid.empty() && oid.back() != '_') oid += '_';
    }
    while (!oid.empty() && oid.back() == '_') oid.pop_back();
    uint32_t hash = 2166136261UL;
    for (char c : oid) hash = (hash * 16777619UL) ^ (uint8_t)c;
    uint32_t fields = ((uint32_t)lm_entity_cat_ << 26) | (internal ? (1u << 24) : 0u);
    this->configure_entity_(lm_name_.c_str(), hash, fields);
  }

  void control(const std::string &value) override {
    this->publish_state(value);
    if (!this->restore_value_) return;
    nvs_handle_t h;
    if (lm_nvs_open(h, NVS_READWRITE) == ESP_OK) {
      char key[12];
      lm_nvs_key(key, this->get_object_id_hash() ^ 0x5458U);
      nvs_set_blob(h, key, value.c_str(), value.size());
      nvs_commit(h);
      nvs_close(h);
    }
  }
  void set_restore_value(bool v) { restore_value_ = v; }
  void set_initial_value(const std::string &s) { initial_value_ = s; }
  const std::string &get_initial_value() const { return initial_value_; }
  void set_mode(text::TextMode m) { this->traits.set_mode(m); }
  void set_optimistic(bool) {}
 protected:
  bool restore_value_{false};
  std::string initial_value_;
  std::string lm_name_;
  uint8_t lm_entity_cat_{0};
};

class LMButton : public button::Button {
 public:
  void set_name(const std::string &name) { lm_name_ = name; }
  void set_icon(const std::string & /*icon*/) {}
  void set_entity_category(uint8_t cat) { lm_entity_cat_ = cat; }
  void finalize_entity_config(const std::string &object_id = "", bool internal = false) {
    const std::string &src = object_id.empty() ? lm_name_ : object_id;
    std::string oid;
    for (char c : src) {
      if (isalnum((unsigned char)c)) oid += tolower((unsigned char)c);
      else if (!oid.empty() && oid.back() != '_') oid += '_';
    }
    while (!oid.empty() && oid.back() == '_') oid.pop_back();
    uint32_t hash = 2166136261UL;
    for (char c : oid) hash = (hash * 16777619UL) ^ (uint8_t)c;
    uint32_t fields = ((uint32_t)lm_entity_cat_ << 26) | (internal ? (1u << 24) : 0u);
    this->configure_entity_(lm_name_.c_str(), hash, fields);
  }
  void press_action() override {}
 private:
  std::string lm_name_;
  uint8_t lm_entity_cat_{0};
};

// Grid dimensions (after 90° rotation: 32w × 120h)
static const int GRID_WIDTH = 32;
static const int GRID_HEIGHT = 120;
static const int GRID_SIZE = GRID_WIDTH * GRID_HEIGHT;  // 3840

// Icon dimensions
static const int ICON_SIZE = 8;
static const int ICON_PIXELS = ICON_SIZE * ICON_SIZE;  // 64

// Transparent pixel marker in RGB565 (magenta)
static const uint16_t ICON_TRANSPARENT = 0xF81F;

// Maximum frames per animated icon
static const int MAX_ICON_FRAMES = 64;

// Icon data stored as contiguous array of frames
struct IconData {
  const uint16_t *data{nullptr};       // Pointer to all frame data (contiguous)
  uint16_t frame_durations[MAX_ICON_FRAMES]; // Duration of each frame in ms
  uint8_t frame_count{1};
  uint8_t width{ICON_SIZE};
  uint8_t height{ICON_SIZE};
};

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
  SCREEN_GAME_OF_LIFE = 6,
  SCREEN_POMODORO     = 7
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

// Pomodoro timer presets
enum PomodoroPreset {
  POMO_PRESET_CLASSIC   = 0,  // 25/5, long break 15
  POMO_PRESET_DEEP_WORK = 1,  // 50/10, long break 20
  POMO_PRESET_ULTRADIAN = 2,  // 90/20, long break 30
};

// Pomodoro timer phases
enum PomodoroPhase {
  POMO_IDLE       = 0,
  POMO_WORK       = 1,
  POMO_BREAK      = 2,
  POMO_LONG_BREAK = 3,
  POMO_COMPLETE   = 4,  // all rounds + long break done
};

struct PomodoroPresetConfig {
  int work_min;
  int break_min;
  int long_break_min;
};

struct ExerciseSnackState {
  int exercise_idx{0};
  int rep_count{10};
  bool ui_visible{false};
  unsigned long ui_start_ms{0};
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
  void set_show_future(bool v) { show_future_ = v; }
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

  // NVS save helpers for lifespan entities
  void save_to_nvs(const char *key, const std::string &value) {
    nvs_handle_t h;
    if (lm_nvs_open(h, NVS_READWRITE) == ESP_OK) {
      nvs_set_blob(h, key, value.c_str(), value.size());
      nvs_commit(h);
      nvs_close(h);
      ESP_LOGD("lm_nvs", "Saved %s='%s'", key, value.c_str());
    }
  }
  void save_to_nvs(const char *key, float value) {
    nvs_handle_t h;
    if (lm_nvs_open(h, NVS_READWRITE) == ESP_OK) {
      nvs_set_blob(h, key, &value, sizeof(float));
      nvs_commit(h);
      nvs_close(h);
      ESP_LOGD("lm_nvs", "Saved %s=%.2f", key, value);
    }
  }

  // Lifespan setters that also refresh (for on_value callbacks)
  void update_lifespan_birthday(const std::string &d)      { set_lifespan_birthday(d);       save_to_nvs("ls_birthday", d);    refresh_lifespan(); }
  void update_lifespan_kids(const std::string &d)          { set_lifespan_kids(d);           save_to_nvs("ls_kids", d);        refresh_lifespan(); }
  void update_lifespan_parents(const std::string &d)       { set_lifespan_parents(d);        save_to_nvs("ls_parents", d);     refresh_lifespan(); }
  void update_lifespan_siblings(const std::string &d)      { set_lifespan_siblings(d);       save_to_nvs("ls_siblings", d);    refresh_lifespan(); }
  void update_lifespan_milestones(const std::string &d)    { set_lifespan_milestones(d);     save_to_nvs("ls_milestones", d);  refresh_lifespan(); }
  void update_lifespan_partner_ranges(const std::string &d){ set_lifespan_partner_ranges(d); save_to_nvs("ls_partner", d);     refresh_lifespan(); }
  void update_lifespan_marriage_ranges(const std::string &d){ set_lifespan_marriage_ranges(d); save_to_nvs("ls_marriage", d);    refresh_lifespan(); }
  void update_lifespan_moved_out_age(int v)    { set_lifespan_moved_out_age(v);    save_to_nvs("ls_moved_out", (float)v);    refresh_lifespan(); }
  void update_lifespan_school_years(int v)     { set_lifespan_school_years(v);     save_to_nvs("ls_school_yr", (float)v);     refresh_lifespan(); }
  void update_lifespan_retirement_age(int v)   { set_lifespan_retirement_age(v);   save_to_nvs("ls_retirement", (float)v);    refresh_lifespan(); }
  void update_lifespan_life_expectancy(int v)  { set_lifespan_life_expectancy(v);  save_to_nvs("ls_life_exp", (float)v);      refresh_lifespan(); }
  void update_lifespan_phase_cycle(float v)    { set_lifespan_phase_cycle(v);      save_to_nvs("ls_phase_cyc", v);            refresh_lifespan(); }

  // Year events / exercise list updaters (NVS-backed, for on_value callbacks)
  void update_year_events(const std::string &v)       { set_year_events(v);        save_to_nvs("year_events", v);   apply_lifespan_year_events(); }
  void update_exercise_list_csv(const std::string &v) { set_exercise_list_csv(v);  save_to_nvs("exercise_list", v); }

  // Input handlers — called directly from YAML encoder/button on_press
  void enc1_clockwise();
  void enc1_anticlockwise();
  void enc2_clockwise();
  void enc2_anticlockwise();
  void toggle_settings_mode();
  void enc2_press();
  void button_down_press();
  void adjust_display_brightness(int delta);

  // Screen management
  void register_screen(int screen_id, bool enabled);
  void add_screen_switch(int screen_id, switch_::Switch *sw);
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
  void set_bed_time_hour(int h) { time_segments_.bed_time_hour = h; apply_brightness(); }
  void set_work_start_hour(int h) { time_segments_.work_start_hour = h; }
  void set_work_end_hour(int h) { time_segments_.work_end_hour = h; }

  // Game of Life configuration
  void set_game_config(const GameOfLifeConfig &config) { game_config_ = config; }
  GameOfLifeConfig get_game_config() { return game_config_; }
  void set_complex_patterns(bool enable) { game_config_.complex_patterns = enable; }
  void set_game_update_interval(const std::string &speed_str);

  // Time override for testing
  void set_time_override(const std::string &time_str);
  void set_time_override_from_str(const std::string &s);  // handles empty/"clear"/"off"
  void clear_time_override();
  bool has_time_override() { return time_override_active_; }
  ESPTime get_time_override() { return fake_time_; }
  ESPTime get_display_time() const;  // current time respecting any active override

  // Pomodoro control
  void start_pomodoro();
  void pause_pomodoro();
  void resume_pomodoro();
  void reset_pomodoro();
  void skip_pomodoro_phase();
  void log_exercise_snack();
  void exercise_adjust_reps(int delta);
  void exercise_next();
  void exercise_prev();
  void set_exercise_list(std::vector<std::string> list) { exercise_list_ = list; }
  void set_exercise_list_csv(const std::string &csv);
  void set_exercise_snacks_enabled(bool en) { exercise_snacks_enabled_ = en; }
  void set_pomo_preset(const std::string &name);
  void set_pomo_rounds(int rounds);
  void set_pomo_phase_override(const std::string &phase);

  // Pomodoro state accessors (for YAML lambdas)
  int get_pomo_phase() { return (int)pomo_phase_; }
  int get_pomo_completed_rounds() { return pomo_completed_rounds_; }
  bool is_pomo_paused() { return pomo_paused_; }
  bool is_exercise_ui_visible() { return exercise_snack_.ui_visible; }
  int get_pomo_elapsed_sec() const;
  int get_pomo_total_sec() const;
  int get_session_elapsed_sec() const;
  PomodoroPresetConfig get_preset_config() const;

  // Pomodoro entity registration
  void set_ha_pomo_preset(select::Select *s);
  void set_ha_pomo_rounds(number::Number *n);
  void set_ha_exercise_snacks(switch_::Switch *s);
  void set_pomo_event_sensor(text_sensor::TextSensor *ts) { pomo_event_sensor_ = ts; }
  void set_pomo_exercise_sensor(text_sensor::TextSensor *ts) { pomo_exercise_sensor_ = ts; }
  void set_ha_pomo_start_button(button::Button *b);

  // Night mode brightness control
  void set_base_brightness_pct(float pct);
  void set_night_mode_level(int level);
  void set_brightness_fn(std::function<void(uint8_t)> fn) { brightness_fn_ = std::move(fn); }
  void apply_brightness();
  float get_base_brightness_pct() const { return base_brightness_pct_; }
  float get_screen_cycle_time()   const { return screen_cycle_time_; }

  // HA entity sync — register entities, wire callbacks (called from __init__.py to_code)
  void set_ha_complex_patterns(switch_::Switch *sw);
  void set_ha_conway_speed(select::Select *s);
  void set_ha_style(select::Select *s);
  void set_ha_gradient_type(select::Select *s);
  void set_ha_fill_direction(select::Select *s);
  void set_ha_marker_style(select::Select *s);
  void set_ha_marker_color(select::Select *s);
  void set_ha_text_area_position(select::Select *s);
  void set_ha_day_fill(select::Select *s);
  void set_ha_year_event_style(select::Select *s);
  void set_ha_bed_time_hour(number::Number *n);
  void set_ha_work_start_hour(number::Number *n);
  void set_ha_work_end_hour(number::Number *n);
  void set_ha_cycle_time(number::Number *n);
  void set_ha_display_brightness(number::Number *n);
  void set_ha_night_mode_level(number::Number *n);
  void set_ha_show_future(switch_::Switch *s);

  // Text / number / button entity wiring (auto-generated entities)
  void set_year_events_entity(text::Text *t);
  void set_exercise_list_entity(text::Text *t);
  void set_time_override_entity(text::Text *t);
  void set_pomo_test_phase_entity(text::Text *t);
  void set_ls_birthday_entity(text::Text *t);
  void set_ls_kids_entity(text::Text *t);
  void set_ls_parents_entity(text::Text *t);
  void set_ls_siblings_entity(text::Text *t);
  void set_ls_milestones_entity(text::Text *t);
  void set_ls_partner_ranges_entity(text::Text *t);
  void set_ls_marriage_ranges_entity(text::Text *t);
  void set_ls_moved_out_entity(number::Number *n);
  void set_ls_school_years_entity(number::Number *n);
  void set_ls_retirement_entity(number::Number *n);
  void set_ls_life_expectancy_entity(number::Number *n);
  void set_ls_phase_cycle_entity(number::Number *n);

  // Icon management (called from Python __init__.py)
  void register_icon_frames(const std::string &icon_id, const uint16_t *data, uint8_t frame_count, std::vector<uint16_t> durations);
  void draw_icon(display::Display &it, const std::string &icon_id, int x, int y);
  void draw_icon(display::Display &it, const std::string &icon_id, int x, int y, Color tint_color);
  bool has_icon(const std::string &icon_id) const;
  void update_icon_animations();

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
  bool show_future_{true};
  DisplayStyle style_{STYLE_TIME_SEGMENTS};
  GradientType gradient_type_{GRADIENT_RED_BLUE};
  MarkerStyle marker_style_{MARKER_SINGLE_DOT};
  MarkerColor marker_color_{MARKER_BLUE};

  // Year view configuration
  std::vector<YearEvent> year_events_;
  DayFillStyle day_fill_style_{DAY_FILL_MIXED};
  YearEventStyle year_event_style_{YEAR_EVENT_MARKERS};

  // Screen management
  switch_::Switch *screen_switches_[8]{nullptr};
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

  // Pomodoro rendering
  void render_pomodoro_view(display::Display &it, ESPTime &time, Viewport vp);
  void render_exercise_snack_overlay(display::Display &it);
  void render_pomo_idle_logo(display::Display &it, Viewport vp);
  void render_spiral_timer(display::Display &it, int elapsed_sec, int total_sec, Viewport vp, Color colors[4]);
  void render_pomodoro_blocks(display::Display &it, Viewport vp);
  void advance_pomodoro_phase();
  void update_pomodoro();

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
  Color dim_future(Color c) const;

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

  // Pomodoro state
  PomodoroPhase pomo_phase_{POMO_IDLE};
  PomodoroPreset pomo_preset_{POMO_PRESET_CLASSIC};
  unsigned long pomo_phase_start_ms_{0};
  bool pomo_paused_{false};
  unsigned long pomo_pause_start_ms_{0};
  unsigned long pomo_paused_total_ms_{0};
  int pomo_completed_rounds_{0};
  int pomo_rounds_before_long_break_{4};
  int pomo_session_elapsed_at_phase_start_sec_{0};
  unsigned long pomo_work_done_anim_end_ms_{0};
  ExerciseSnackState exercise_snack_;
  bool exercise_snacks_enabled_{true};
  std::vector<std::string> exercise_list_;
  select::Select *ha_pomo_preset_{nullptr};
  number::Number *ha_pomo_rounds_{nullptr};
  switch_::Switch *ha_exercise_snacks_{nullptr};
  text_sensor::TextSensor *pomo_event_sensor_{nullptr};
  text_sensor::TextSensor *pomo_exercise_sensor_{nullptr};
  // Time override for testing
  bool time_override_active_{false};
  ESPTime fake_time_{};
  uint32_t time_override_start_ms_{0};

  // Night mode brightness state
  float base_brightness_pct_{20.0f};
  int night_mode_level_{0};
  uint8_t last_brightness_hour_{255};
  std::function<void(uint8_t)> brightness_fn_;

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
  number::Number *ha_display_brightness_{nullptr};
  number::Number *ha_night_mode_level_{nullptr};
  switch_::Switch *ha_show_future_{nullptr};

  // Non-lifespan entities needing initial-value publish
  text::Text *year_events_entity_{nullptr};
  text::Text *exercise_list_entity_{nullptr};

  // Lifespan entities (stored for NVS restoration)
  text::Text *ls_birthday_entity_{nullptr};
  text::Text *ls_kids_entity_{nullptr};
  text::Text *ls_parents_entity_{nullptr};
  text::Text *ls_siblings_entity_{nullptr};
  text::Text *ls_milestones_entity_{nullptr};
  text::Text *ls_partner_ranges_entity_{nullptr};
  text::Text *ls_marriage_ranges_entity_{nullptr};
  number::Number *ls_moved_out_entity_{nullptr};
  number::Number *ls_school_years_entity_{nullptr};
  number::Number *ls_retirement_entity_{nullptr};
  number::Number *ls_life_expectancy_entity_{nullptr};
  number::Number *ls_phase_cycle_entity_{nullptr};

  // Helper to restore lifespan entities from NVS
  void restore_lifespan_entities_from_nvs_();

  // Icon registry: maps icon_id string to animated icon data
  std::map<std::string, IconData> icon_registry_;

  // Icon animation state: tracks current frame for each animated icon
  std::map<std::string, uint8_t> icon_current_frame_;
  std::map<std::string, uint32_t> icon_last_frame_time_;
};

}  // namespace life_matrix
}  // namespace esphome
