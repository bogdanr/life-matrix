# Life Matrix ESPHome Component

A sophisticated time visualization and Conway's Game of Life display component for ESPHome, designed for HUB75 LED matrix displays.

## Features

### 6 Visualization Screens

1. **Year View** - Calendar heatmap showing activity across all 365 days
2. **Month View** - Progress bar with gradient showing current month progress
3. **Day View** - Sleep/Work/Life visualization with rainbow segments
4. **Hour View** - Current hour progress with customizable color schemes
5. **Habits View** - Placeholder for future habit tracking features
6. **Game of Life** - Conway's cellular automaton with age-based coloring

### Game of Life Highlights

- **Complex Patterns**: Famous methuselahs (R-pentomino, Acorn, Diehard)
- **Glider Generation**: Self-organizing patterns that move across the grid
- **Age-Based Coloring**:
  - Cyan (young cells, age < 5)
  - Green (middle age, 5-15)
  - Yellow (mature, 15-30)
  - Rainbow (ancient cells, age 30+)
- **Smart Auto-Reset**: Detects extinction, low population, and stable patterns
- **Demo Mode**: Shows rules and pattern examples for 5 seconds on startup

### UI Features

- **3 UI Modes**:
  - Auto Cycle: Automatically rotates through enabled screens
  - Manual Browse: Navigate screens with encoder
  - Settings: Adjust per-screen settings
- **Rotary Encoder Support**: Navigate screens and adjust settings
- **Pause/Resume**: Freeze auto-cycling
- **Visual Indicators**: Mode and pause status shown on display

## Hardware Requirements

- **ESP32** (ESP32-S3 recommended for performance)
- **HUB75 RGB LED Matrix** (tested on 64×64 dual panels = 32×120 after rotation)
- **2× Rotary Encoders** with push buttons (optional but recommended)
- **RTC/SNTP Time Source** (required for time visualizations)

### Tested Hardware

- Adafruit Matrix Portal S3
- 64×64 HUB75 RGB LED panels (2x chained)

## Installation

### Method 1: Local Component (Development)

1. Copy the `life_matrix` folder to your ESPHome config directory under `esphome/components/`
2. Add the component to your device YAML (see Configuration section)

### Method 2: External Component (GitHub)

```yaml
external_components:
  - source: github://username/esphome-life-matrix
    components: [ life_matrix ]
```

## Configuration

### Minimal Configuration

```yaml
time:
  - platform: sntp
    id: sntp_time

font:
  - file: "fonts/monobit.ttf"
    id: font_sm
    size: 8

display:
  - platform: hub75
    id: matrix_display
    board: adafruit-matrix-portal-s3
    panel_width: 64
    panel_height: 32
    layout_cols: 2
    layout: HORIZONTAL
    rotation: 90
    lambda: |-
      auto time = id(sntp_time).now();
      if (time.is_valid()) {
        id(life_matrix_component)->render(it, time);
      }

life_matrix:
  id: life_matrix_component
  time_id: sntp_time
  font_small: font_sm
```

### Full Configuration Options

```yaml
life_matrix:
  id: life_matrix_component
  time_id: sntp_time             # Required: time component ID
  font_small: font_sm            # Optional: small font for text
  font_medium: font_md           # Optional: medium font (future use)

  # Grid dimensions (should match your display after rotation)
  grid_width: 32                 # Default: 32
  grid_height: 120               # Default: 120

  # Screen management
  screen_cycle_time: 3s          # Default: 3s, auto-cycle interval

  # Enable/disable individual screens
  screens:
    year:
      enabled: true              # Default: true
    month:
      enabled: true
    day:
      enabled: true
    hour:
      enabled: true
    habits:
      enabled: false             # Default: true (placeholder screen)
    game_of_life:
      enabled: true

  # Game of Life configuration
  game_of_life:
    update_interval: 200ms       # Default: 200ms, cell update speed
    complex_patterns: true       # Default: true, use methuselahs
    auto_reset_on_stable: true   # Default: true, reset when stable
    stability_timeout: 60s       # Default: 60s, wait before reset
    demo_mode: false             # Default: false, show rules on boot

  # Time segments for day view (24-hour format)
  time_segments:
    bed_time_hour: 22            # Default: 22 (10 PM)
    work_start_hour: 9           # Default: 9 (9 AM)
    work_end_hour: 17            # Default: 17 (5 PM)

  # Visual styling
  color_scheme: "Single Color"   # Options: Single Color, Gradient, Time Segments, Rainbow
  gradient_type: "Red-Blue"      # Options: Red-Blue, Green-Yellow, Cyan-Magenta, Purple-Orange
  text_area_position: "Top"      # Options: Top, Bottom, None
  fill_direction: "Bottom to Top" # Options: Bottom to Top, Top to Bottom
```

## Screen Descriptions

### Year View

A calendar heatmap displaying all 365 days of the current year. Each column represents a day, with 12 months arranged vertically.

**Features:**
- Activity-based coloring (can integrate with external data)
- Event markers for important dates
- Breathing "today" indicator
- Month boundary markers

### Month View

Simple progress bar showing how much of the current month has elapsed.

**Colors:** Gradient from red-magenta to yellow-orange

### Day View

24-hour visualization divided into three segments:

- **Sleep** (dark gray): Calculated from bed time + 8 hours
- **Work** (orange): User-configurable work hours (skipped on weekends)
- **Life** (rainbow): Everything else, with each segment getting its own full rainbow cycle

### Hour View

Current hour progress with multiple display modes:

- **Time Segments**: Spiral filling with seasonal colors
- **Gradient**: Smooth color transition
- **Rainbow**: Full spectrum
- **Single Color**: Monochrome fill

### Game of Life

Conway's cellular automaton with:
- Toroidal topology (wraps at edges)
- Classic rules: birth on 3, survival on 2-3
- Age tracking for colorful visualization
- Famous patterns: R-pentomino (1103 gens), Acorn (5206 gens), Gliders, Diehard

## Button/Encoder Integration

### Navigation Encoder (enc1)

```yaml
sensor:
  - platform: rotary_encoder
    id: enc1
    pin_a: GPIO10
    pin_b: GPIO11
    on_clockwise:
      - lambda: id(life_matrix_component)->next_screen();
    on_anticlockwise:
      - lambda: id(life_matrix_component)->prev_screen();

binary_sensor:
  - platform: gpio
    id: enc1_sw
    pin:
      number: GPIO8
      mode: INPUT_PULLUP
      inverted: true
    on_press:
      - lambda: |-
          // Cycle UI mode
          int mode = (int)id(life_matrix_component)->get_ui_mode();
          mode = (mode + 1) % 3;
          id(life_matrix_component)->set_ui_mode((life_matrix::UIMode)mode);
```

### Value Encoder (enc2)

```yaml
sensor:
  - platform: rotary_encoder
    id: enc2
    pin_a: GPIO3
    pin_b: GPIO18
    on_clockwise:
      - lambda: id(life_matrix_component)->adjust_setting(+1);
    on_anticlockwise:
      - lambda: id(life_matrix_component)->adjust_setting(-1);
```

### Buttons

```yaml
binary_sensor:
  - platform: gpio
    id: button_up
    pin: GPIO6
    on_press:
      - lambda: id(life_matrix_component)->toggle_pause();

  - platform: gpio
    id: button_down
    pin: GPIO7
    on_press:
      - lambda: id(life_matrix_component)->reset_game_of_life();
```

## API Reference

### Component Methods

#### Screen Management

- `void next_screen()` - Switch to next enabled screen
- `void prev_screen()` - Switch to previous enabled screen
- `int get_current_screen_id()` - Get active screen ID (0-5)
- `void set_screen_cycle_time(float seconds)` - Set auto-cycle interval

#### UI State

- `void set_ui_mode(UIMode mode)` - Set mode (AUTO_CYCLE=0, MANUAL_BROWSE=1, SETTINGS=2)
- `UIMode get_ui_mode()` - Get current UI mode
- `void toggle_pause()` - Toggle auto-cycle pause
- `void set_paused(bool paused)` - Set pause state

#### Game of Life

- `void reset_game_of_life()` - Clear and reinitialize grid
- `void initialize_game_of_life(PatternType pattern)` - Initialize with specific pattern
- `int get_population()` - Get count of living cells
- `int get_generation()` - Get current generation number
- `bool is_stable()` - Check if pattern has stabilized

#### Settings

- `void adjust_setting(int direction)` - Adjust current setting (+1/-1)
- `void set_settings_cursor(int pos)` - Set settings menu cursor position

#### Configuration

- `void set_grid_dimensions(int width, int height)` - Set grid size
- `void set_time_segments(const TimeSegmentsConfig &config)` - Configure day view segments
- `void set_game_update_interval(int ms)` - Set Game of Life speed

## Performance

- **Update Rate**: 20 FPS (50ms display refresh)
- **Loop Time**: < 50ms (monitor with `debug` component)
- **Memory**: ~50KB heap free on ESP32-S3
- **Game of Life**: Default 200ms update (configurable 50ms-1000ms)

## Troubleshooting

### Display shows "No Views"

- Check that at least one screen is enabled in `screens:` configuration
- Verify `register_screen()` calls in setup

### Time not syncing

- Ensure WiFi is connected
- Check SNTP server accessibility
- Verify timezone setting in `time:` component

### Game of Life resets too frequently

- Increase `stability_timeout` (default 60s)
- Disable `auto_reset_on_stable`
- Check for low population (< 58 cells triggers reset)

### Encoders not working

- Check physical connections (encoders sensitive to loose wires)
- Verify GPIO pin numbers match hardware
- Add pull-up resistors if needed

### Display glitches during OTA

- This is normal - OTA updates pause rendering
- Component automatically detects OTA and shows "OTA" message

## Future Enhancements

- [ ] Habits tracker screen with streak visualization
- [ ] Year view with Home Assistant calendar integration
- [ ] Accelerometer shake-to-reset
- [ ] Pixel art / drawing mode
- [ ] Tilt physics effects
- [ ] Pomodoro timer screen

## License

MIT License - See LICENSE file for details

## Contributing

Pull requests welcome! Please ensure:
- Code follows ESPHome style guidelines
- Test on real hardware before submitting
- Update documentation for new features

## Credits

Developed for the Adafruit Matrix Portal S3 as part of the Life Matrix project.

**Pattern References:**
- R-pentomino: Discovered by John Conway
- Acorn: Discovered by Charles Corderman (1970)
- Glider: Discovered by Richard K. Guy (1970)
- Diehard: Discovered by Achim Flammenkamp (before 1997)

## Support

- GitHub Issues: [Project Issue Tracker]
- ESPHome Discord: `#custom-components`
- Documentation: This README and inline code comments
