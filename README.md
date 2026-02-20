# Life Matrix ESPHome Component

A time-passage visualization component for ESPHome and HUB75 LED matrix displays. Renders year, month, day, and hour progress as pixel art — with biographical lifespan phases and a Game of Life panel.

## Screens

1. **Year View** — Calendar heatmap of all 365 days with event markers and a breathing "today" indicator
2. **Month View** — 4×8 day grid with activity fill, event borders, and current-time progress in today's cell
3. **Day View** — 24h visualization split into Sleep / Work / Life segments with rainbow coloring
4. **Hour View** — Spiral fill with multiple color schemes (seasonal, gradient, rainbow, single)
5. **Lifespan View** — Biographical visualization with life phases (school, career, retirement), relationship markers, and cosmos starfield beyond life expectancy
6. **Game of Life** — Conway's cellular automaton with age-based coloring and smart auto-reset

## Hardware

- **ESP32-S3** (recommended for performance)
- **HUB75 RGB LED Matrix** — tested with 2× 64×32 panels chained horizontally, rotated 90° → 32×120 effective
- **Tested on**: Adafruit Matrix Portal S3
- **Optional**: Rotary encoders, buttons for physical UI

## Installation

### From GitHub

```yaml
external_components:
  - source: github://bogdanr/life-matrix
    components: [ life_matrix ]
```

### Local Development

Copy the `life_matrix` folder to `esphome/components/` in your ESPHome config directory.

## Quick Start

See [`example-minimal.yaml`](example-minimal.yaml) for a basic setup or [`example-full.yaml`](example-full.yaml) for a complete configuration with HA integration and physical UI. The essentials:

```yaml
life_matrix:
  id: life_matrix_component
  time_id: sntp_time
  font_small: font_sm
  grid_width: 32
  grid_height: 120
  screen_cycle_time: 5s
```

Then call `render()` from your display lambda:

```yaml
display:
  - platform: hub75
    # ... your panel config ...
    lambda: |-
      auto time = id(sntp_time).now();
      if (time.is_valid()) {
        id(life_matrix_component)->render(it, time);
      }
```

## Configuration

All options with defaults:

```yaml
life_matrix:
  id: life_matrix_component
  time_id: sntp_time           # Required
  font_small: font_sm          # Required for text
  status_led: status_led       # Optional: NeoPixel for mode indication

  grid_width: 32               # Match your display after rotation
  grid_height: 120
  screen_cycle_time: 5s

  # Screen toggles (all default to true)
  screens:
    year: { enabled: true }
    month: { enabled: true }
    day: { enabled: true }
    hour: { enabled: true }
    lifespan: { enabled: true }
    game_of_life: { enabled: true }

  # Game of Life
  game_of_life:
    update_interval: 200ms
    complex_patterns: false
    auto_reset_on_stable: true
    stability_timeout: 60s

  # Day view time segments (24h format)
  time_segments:
    bed_time_hour: 22
    work_start_hour: 9
    work_end_hour: 17

  # Lifespan biographical data (all optional except birthday)
  lifespan:
    birthday: "1990-05-15"         # Required for lifespan screen
    moved_out: 18                  # Age when you left home (default: 18)
    school_years: 12               # Total school years starting at age 6 (default: 12)
    retirement: 70                 # Retirement age (default: 70)
    life_expectancy_age: 90        # Expected lifespan (default: 90)
    phase_cycle_time: 3            # Phase color animation speed in seconds (0 = static)
    kids: "2018-03-10,2021-07-22"  # Comma-separated birth dates
    parents: "2015-11-03"          # Comma-separated death dates
    siblings: ""
    partner_ranges: "2012-2016"    # Ranges: YYYY-YYYY or YYYY-present
    marriage_ranges: "2016-present"
    milestones: "2012-06-15 Graduated,2016-09-01 New job"  # date label pairs

  # Visual styling
  style: "Time Segments"          # Single Color | Gradient | Time Segments | Rainbow
  gradient_type: "Red-Blue"       # Red-Blue | Green-Yellow | Cyan-Magenta | Purple-Orange | Blue-Yellow
  fill_direction: "Bottom to Top" # Bottom to Top | Top to Bottom
  text_area_position: "Top"       # Top | Bottom | None
```

## Home Assistant Integration

The component exposes entities for full remote control via Home Assistant:

- **Switches** — toggle individual screens on/off (including Lifespan), complex GoL patterns
- **Selects** — style, gradient type, fill direction, marker style, marker color, year day/event style, Game of Life speed
- **Numbers** — brightness, bed time, work hours, screen cycle time, lifespan ages (moved out, school years, retirement, life expectancy, phase cycle time)
- **Text inputs** — year events (comma-separated dates), lifespan biographical data (birthday, kids, parents, siblings, milestones), time override for testing
- **Sensors** — GoL final generation/population, heap free, loop time

See [`example-full.yaml`](example-full.yaml) for a complete working configuration with all entities.

## Future

- [ ] Interactive Game of Life (cursor + draw)
- [ ] Pomodoro timer screen
- [ ] Accelerometer shake-to-reset
- [ ] Pixel art / drawing mode

## License

GPLv3 — See [LICENSE](LICENSE) · [Issues](https://github.com/bogdanr/life-matrix/issues)
