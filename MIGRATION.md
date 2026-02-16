# Migration Guide: YAML to Component

This guide helps you migrate your existing `life-matrix.yaml` configuration to use the new Life Matrix component.

## Overview

The Life Matrix component extracts ~2100 lines of inline YAML logic into a reusable C++ component, leaving you with a clean ~250-line configuration file.

**Benefits:**
- 🎯 **Cleaner**: No more massive display lambda
- 🔧 **Maintainable**: Logic in C++ instead of inline scripts
- 🔄 **Reusable**: Share component across devices
- ⚙️ **Configurable**: Type-safe YAML configuration
- 🚀 **Publishable**: Ready for ESPHome community

## Migration Steps

### Step 1: Backup Current Configuration

```bash
cp /config/esphome/life-matrix.yaml /config/esphome/life-matrix-backup.yaml
```

### Step 2: Keep Unchanged Sections

These sections remain the same and should be kept:

```yaml
substitutions:  # ✅ Keep as-is
  devicename: life-matrix

packages:       # ✅ Keep as-is
  wifi: !include common/wifi.yaml
  device_base: !include common/device_base.yaml

esphome:       # ✅ Keep, but update on_boot if needed
  on_boot:
    priority: -100
    then:
      - lambda: id(life_matrix_component)->initialize_game_of_life();

ota:           # ✅ Keep, update on_begin
  - platform: esphome
    on_begin:
      - lambda: id(life_matrix_component)->set_ota_in_progress(true);

esp32:         # ✅ Keep as-is
time:          # ✅ Keep as-is
font:          # ✅ Keep as-is
color:         # ✅ Keep as-is
```

### Step 3: Add Life Matrix Component Configuration

**REPLACE** the massive `display.lambda` section with:

```yaml
life_matrix:
  id: life_matrix_component
  display: matrix_display
  time_id: sntp_time
  font_small: font_sm

  # Grid dimensions (from your display after rotation)
  grid_width: 32
  grid_height: 120

  # Screen cycle time
  screen_cycle_time: 3s  # was id(cycle_time_s).state

  # Enable/disable screens (from your switch entities)
  screens:
    year:
      enabled: true      # was id(show_year).state
    month:
      enabled: true      # was id(show_month).state
    day:
      enabled: true      # was id(show_day).state
    hour:
      enabled: true      # was id(show_hour).state
    habits:
      enabled: false     # was id(show_habits).state
    game_of_life:
      enabled: true      # was id(show_conway).state

  # Game of Life settings (from select entities)
  game_of_life:
    update_interval: 200ms        # was id(conway_speed).current_option()
    complex_patterns: true        # was id(gol_complex_patterns).state
    auto_reset_on_stable: true    # was hardcoded in lambda
    stability_timeout: 60s        # was hardcoded 60000ms
    demo_mode: false              # was id(game_demo_mode)

  # Time segments (from number entities)
  time_segments:
    bed_time_hour: 22             # was id(bed_time_hour).state
    work_start_hour: 9            # was id(work_start_hour).state
    work_end_hour: 17             # was id(work_end_hour).state

  # Visual styling (from select entities)
  color_scheme: "Time Segments"   # was id(color_scheme).current_option()
  gradient_type: "Red-Blue"       # was id(gradient_type).current_option()
  text_area_position: "Top"       # was id(text_area_position).current_option()
  fill_direction: "Bottom to Top" # was id(fill_direction).current_option()
```

### Step 4: Simplify Display Lambda

**REPLACE** lines 995-2375 (the entire display lambda) with:

```yaml
display:
  - platform: hub75
    id: matrix_display
    board: adafruit-matrix-portal-s3
    width: 64
    height: 64
    chain_length: 2
    layout_cols: 2
    layout: HORIZONTAL
    double_buffer: true
    update_interval: 50ms
    rotation: 90
    lambda: |-
      // Get time
      auto time = id(sntp_time).now();

      // Show WiFi/sync status if time not valid
      if (!time.is_valid()) {
        int center_x = it.get_width() / 2;
        int center_y = it.get_height() / 2;
        int dots = (millis() / 500) % 4;
        char dot_str[4] = {};
        for (int i = 0; i < dots; i++) dot_str[i] = '.';

        if (!wifi::global_wifi_component->is_connected()) {
          it.print(center_x, center_y - 14, id(font_sm), Color(80, 80, 80), TextAlign::CENTER, "No");
          it.print(center_x, center_y - 4, id(font_sm), id(c_active), TextAlign::CENTER, "WiFi");
          it.printf(center_x, center_y + 8, id(font_sm), Color(80, 80, 80), TextAlign::CENTER, "%s", dot_str);
        } else {
          it.print(center_x, center_y - 14, id(font_sm), Color(80, 80, 80), TextAlign::CENTER, "Sync");
          it.print(center_x, center_y - 4, id(font_sm), id(c_active), TextAlign::CENTER, "Clock");
          it.printf(center_x, center_y + 8, id(font_sm), Color(80, 80, 80), TextAlign::CENTER, "%s", dot_str);
        }
        return;
      }

      // Delegate all rendering to the component
      id(life_matrix_component)->render(it, time);
```

**Reduction:** ~1380 lines → ~30 lines (97.8% reduction!)

### Step 5: Update Button/Encoder Handlers

**BEFORE:**
```yaml
on_clockwise:
  - lambda: |-
      id(ui_last_input_ms) = millis();
      id(ui_mode) = 1;
      std::vector<int> enabled;
      if (id(show_year).state) enabled.push_back(0);
      // ... 20 more lines
      id(current_screen_idx) = (id(current_screen_idx) + 1) % enabled.size();
```

**AFTER:**
```yaml
on_clockwise:
  - lambda: id(life_matrix_component)->next_screen();
```

**Full Button/Encoder Migrations:**

```yaml
sensor:
  - platform: rotary_encoder
    id: enc1  # Navigation encoder
    pin_a: GPIO10
    pin_b: GPIO11
    on_clockwise:
      - lambda: id(life_matrix_component)->next_screen();
    on_anticlockwise:
      - lambda: id(life_matrix_component)->prev_screen();

  - platform: rotary_encoder
    id: enc2  # Value encoder
    pin_a: GPIO3
    pin_b: GPIO18
    on_clockwise:
      - lambda: id(life_matrix_component)->adjust_setting(+1);
    on_anticlockwise:
      - lambda: id(life_matrix_component)->adjust_setting(-1);

binary_sensor:
  - platform: gpio
    id: enc1_sw  # Mode button
    pin: GPIO9
    on_press:
      - lambda: |-
          int mode = (int)id(life_matrix_component)->get_ui_mode();
          mode = (mode + 1) % 3;  // Cycle: 0->1->2->0
          id(life_matrix_component)->set_ui_mode((life_matrix::UIMode)mode);

  - platform: gpio
    id: button_up  # Pause/resume
    pin: GPIO6
    on_press:
      - lambda: id(life_matrix_component)->toggle_pause();

  - platform: gpio
    id: button_down  # Reset Game of Life
    pin: GPIO7
    on_press:
      - lambda: id(life_matrix_component)->reset_game_of_life();
```

### Step 6: Remove/Consolidate Configuration Entities

#### Delete Globals (Lines 729-799)

**DELETE** these globals (now internal to component):
- `ota_in_progress`
- `current_screen_idx`
- `last_switch_time`
- `game_of_life_grid`
- `game_of_life_initialized`
- `game_last_update`
- `game_generation`
- `game_last_max_age`
- `game_population_history`
- `game_history_idx`
- `game_history_filled`
- `game_births`, `game_deaths`
- `game_stable_since`, `game_is_stable`
- `game_demo_mode`, `game_demo_start_time`
- `ui_mode`, `ui_last_input_ms`, `ui_paused`
- `settings_cursor`, `settings_flash_ms`
- `enc2_direction`

#### Delete Scripts (Lines 827-951)

**DELETE** these scripts (now component methods):
- `blink_red` (keep if used elsewhere)
- `update_status_led` (keep if used elsewhere)
- `check_ui_timeout` (now automatic in component loop)
- `adjust_setting_value` (now component method)

#### Simplify Number Entities (Lines 230-350)

**Keep for hardware control:**
```yaml
number:
  - platform: template
    id: display_brightness
    name: "Display Brightness"
    # ... keep as-is for brightness control
```

**Optional: Remove if using component config instead:**
- `cycle_time_s` (use `screen_cycle_time` in component)
- `bed_time_hour` (use `time_segments.bed_time_hour`)
- `work_start_hour` (use `time_segments.work_start_hour`)
- `work_end_hour` (use `time_segments.work_end_hour`)

**Or keep and sync to component:**
```yaml
number:
  - platform: template
    id: cycle_time_s
    on_value:
      - lambda: id(life_matrix_component)->set_screen_cycle_time(x);
```

#### Simplify Select Entities (Lines 351-455)

**Optional: Remove if using component config:**
- `color_scheme` (use `color_scheme` in component)
- `gradient_type` (use `gradient_type` in component)
- `marker_style` (future: component config)
- `marker_color` (future: component config)
- `fill_direction` (use `fill_direction` in component)
- `text_area_position` (use `text_area_position` in component)
- `conway_speed` (use `game_of_life.update_interval`)
- `year_day_style` (future: component config)
- `year_event_style` (future: component config)

**Or keep for HA integration:**
```yaml
select:
  - platform: template
    id: color_scheme
    options:
      - "Single Color"
      - "Gradient"
      - "Time Segments"
      - "Rainbow"
    on_value:
      - lambda: |-
          // Map string to enum and update component
          // (requires additional component API)
```

#### Keep Switch Entities for Screen Enable/Disable

**Option 1: Remove (use component config)**
```yaml
# Delete all show_* switches, configure in component
```

**Option 2: Keep for dynamic control via HA**
```yaml
switch:
  - platform: template
    id: show_year
    on_turn_on:
      - lambda: id(life_matrix_component)->register_screen(0, true);
    on_turn_off:
      - lambda: id(life_matrix_component)->register_screen(0, false);
  # ... repeat for other screens
```

### Step 7: Delete Interval Timer

**DELETE** (now automatic in component):
```yaml
interval:
  - interval: 1s
    then:
      - script.execute: check_ui_timeout
```

### Step 8: Flash and Test

```bash
esphome run life-matrix.yaml
```

**Verify:**
1. Display shows time correctly
2. Screens auto-cycle every 3s
3. Nav encoder switches screens
4. Mode button cycles UI modes (blue dot → green dot → yellow dot)
5. Pause button freezes auto-cycle
6. Game of Life displays with age coloring
7. Reset button reinitializes Game of Life

## Configuration Migration Table

| Old Location | Old Method | New Method |
|-------------|------------|------------|
| Lines 729-799 | 25 globals | Component internal state |
| Lines 867-951 | `adjust_setting_value` script | `component->adjust_setting()` |
| Lines 852-865 | `check_ui_timeout` script | Automatic in `loop()` |
| Lines 995-2375 | 1380-line display lambda | `component->render()` |
| Lines 64-99 | Nav encoder CW inline | `next_screen()` |
| Lines 100-135 | Nav encoder CCW inline | `prev_screen()` |
| Lines 230-350 | 5 number entities | Component config |
| Lines 351-455 | 9 select entities | Component config |
| Lines 456-497 | 6 switch entities | Component config `screens:` |

## Before/After Comparison

### Before: Monolithic YAML
```
Total lines: 2394
- Hardware config: 200 lines
- Globals: 70 lines
- Scripts: 100 lines
- Number entities: 120 lines
- Select entities: 105 lines
- Switch entities: 42 lines
- Display lambda: 1380 lines
- Button handlers: 377 lines
```

### After: Component-Based
```
Total lines: ~250
- Hardware config: 200 lines (unchanged)
- Component config: 30 lines
- Display lambda: 30 lines
- Button handlers: 40 lines

Removed:
- 70 lines of globals → component state
- 100 lines of scripts → component methods
- 247 lines of entities → component config
- 1380 lines of lambda → component render()
```

**Result: 90% reduction in YAML size!**

## Troubleshooting Migration

### "undefined reference to `id(life_matrix_component)`"

**Cause:** Component not added to YAML yet.

**Fix:** Add the `life_matrix:` configuration block.

### Display is blank

**Cause:** Time not valid or component not calling render.

**Fix:** Check `time.is_valid()` in display lambda, ensure SNTP synced.

### "No matching function for call to `set_ui_mode`"

**Cause:** Need to cast int to enum.

**Fix:** Use `(life_matrix::UIMode)mode` when calling from lambda.

### Screens don't auto-cycle

**Cause:** Component not registered screens or paused.

**Fix:** Ensure `screens:` block present, check pause state.

### Game of Life doesn't appear

**Cause:** Screen 5 not enabled or game not initialized.

**Fix:** Enable in `screens.game_of_life.enabled: true`, check logs.

### Compilation errors about missing headers

**Cause:** Component files not in `esphome/components/life_matrix/`.

**Fix:** Verify directory structure and all 3 files present.

## Post-Migration Checklist

- [ ] Backup original YAML created
- [ ] Component config block added
- [ ] Display lambda simplified
- [ ] Button handlers updated
- [ ] Globals removed
- [ ] Scripts removed (or kept if reused)
- [ ] Entities consolidated
- [ ] Interval timer removed
- [ ] Compilation successful
- [ ] Flash successful
- [ ] Display shows screens
- [ ] Auto-cycle works
- [ ] Encoders/buttons work
- [ ] Game of Life functional
- [ ] UI modes cycle correctly
- [ ] Pause works
- [ ] Performance acceptable (< 50ms loop time)

## Rollback Procedure

If migration fails:

```bash
# Restore backup
cp /config/esphome/life-matrix-backup.yaml /config/esphome/life-matrix.yaml

# Reflash
esphome run life-matrix.yaml
```

## Next Steps

After successful migration:

1. **Test thoroughly** - run for 24h to ensure stability
2. **Monitor performance** - check loop time, heap memory
3. **Tune configuration** - adjust cycle times, colors, etc.
4. **Remove backup** - once confident in new implementation
5. **Share feedback** - report issues or improvements
6. **Contribute** - submit PRs for enhancements

## Support

- Check README.md for API reference
- Check CHANGELOG.md for known issues
- Open GitHub issue for bugs
- Join ESPHome Discord #custom-components

---

Happy migrating! 🚀
