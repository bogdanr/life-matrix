# Life Matrix ESPHome Component

A time-passage visualization component for ESPHome and HUB75 LED matrix displays. Renders year, month, day, and hour progress as pixel art — with a Game of Life panel as a bonus screen.

## Screens

1. **Year View** — Calendar heatmap of all 365 days with event markers and a breathing "today" indicator
2. **Month View** — Progress bar with gradient showing current month progress
3. **Day View** — 24h visualization split into Sleep / Work / Life segments with rainbow coloring
4. **Hour View** — Spiral fill with multiple color schemes (seasonal, gradient, rainbow, single)
5. **Game of Life** — Conway's cellular automaton with age-based coloring and smart auto-reset

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

See [`example-minimal.yaml`](example-minimal.yaml) for a working configuration. The essentials:

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

  # Visual styling
  color_scheme: "Time Segments"   # Single Color | Gradient | Time Segments | Rainbow
  gradient_type: "Red-Blue"       # Red-Blue | Green-Yellow | Cyan-Magenta | Purple-Orange | Blue-Yellow
  fill_direction: "Bottom to Top" # Bottom to Top | Top to Bottom
  text_area_position: "Top"       # Top | Bottom | None
```

## Home Assistant Integration

The component exposes entities for full remote control via Home Assistant:

- **Switches** — toggle individual screens on/off
- **Selects** — color scheme, gradient type, fill direction, marker style, Game of Life speed
- **Numbers** — brightness, bed time, work hours, screen cycle time
- **Text inputs** — year events (comma-separated dates), time override for testing

See the full configuration in the project repository for examples.

## Future

- [ ] Interactive Game of Life (cursor + draw)
- [ ] Pomodoro timer screen
- [ ] Accelerometer shake-to-reset
- [ ] Pixel art / drawing mode

## License

GPLv3 — See [LICENSE](LICENSE) · [Issues](https://github.com/bogdanr/life-matrix/issues)
