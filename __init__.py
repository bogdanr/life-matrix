import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, time as time_, font, light, sensor
from esphome.const import (
    CONF_ID,
    CONF_TIME_ID,
    CONF_UPDATE_INTERVAL,
)
from esphome import automation

CONF_DISPLAY = "display"
CONF_STATUS_LED = "status_led"
CONF_FONT_SMALL = "font_small"
CONF_FONT_MEDIUM = "font_medium"
CONF_GRID_WIDTH = "grid_width"
CONF_GRID_HEIGHT = "grid_height"
CONF_SCREEN_CYCLE_TIME = "screen_cycle_time"
CONF_GOL_FINAL_GENERATION_SENSOR = "gol_final_generation_sensor"
CONF_GOL_FINAL_POPULATION_SENSOR = "gol_final_population_sensor"
CONF_SCREENS = "screens"
CONF_YEAR = "year"
CONF_MONTH = "month"
CONF_DAY = "day"
CONF_HOUR = "hour"
CONF_HABITS = "habits"
CONF_GAME_OF_LIFE = "game_of_life"
CONF_ENABLED = "enabled"
CONF_COMPLEX_PATTERNS = "complex_patterns"
CONF_AUTO_RESET_ON_STABLE = "auto_reset_on_stable"
CONF_STABILITY_TIMEOUT = "stability_timeout"
CONF_DEMO_MODE = "demo_mode"
CONF_TIME_SEGMENTS = "time_segments"
CONF_BED_TIME_HOUR = "bed_time_hour"
CONF_WORK_START_HOUR = "work_start_hour"
CONF_WORK_END_HOUR = "work_end_hour"
CONF_STYLE = "style"
CONF_GRADIENT_TYPE = "gradient_type"
CONF_TEXT_AREA_POSITION = "text_area_position"
CONF_FILL_DIRECTION = "fill_direction"

life_matrix_ns = cg.esphome_ns.namespace("life_matrix")
LifeMatrix = life_matrix_ns.class_("LifeMatrix", cg.Component)

# Enums
DisplayStyle = life_matrix_ns.enum("DisplayStyle")
DISPLAY_STYLES = {
    "Single Color": 0,
    "Gradient": 1,
    "Time Segments": 2,
    "Rainbow": 3,
}

SCREEN_SCHEMA = cv.Schema({
    cv.Optional(CONF_ENABLED, default=True): cv.boolean,
})

GAME_OF_LIFE_SCHEMA = cv.Schema({
    cv.Optional(CONF_UPDATE_INTERVAL, default="200ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_COMPLEX_PATTERNS, default=False): cv.boolean,
    cv.Optional(CONF_AUTO_RESET_ON_STABLE, default=True): cv.boolean,
    cv.Optional(CONF_STABILITY_TIMEOUT, default="60s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_DEMO_MODE, default=False): cv.boolean,
})

TIME_SEGMENTS_SCHEMA = cv.Schema({
    cv.Optional(CONF_BED_TIME_HOUR, default=22): cv.int_range(min=0, max=23),
    cv.Optional(CONF_WORK_START_HOUR, default=9): cv.int_range(min=0, max=23),
    cv.Optional(CONF_WORK_END_HOUR, default=17): cv.int_range(min=0, max=23),
})

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LifeMatrix),
    cv.Optional(CONF_DISPLAY): cv.use_id(display.Display),
    cv.Required(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
    cv.Optional(CONF_STATUS_LED): cv.use_id(light.LightState),

    # Optional fonts
    cv.Optional(CONF_FONT_SMALL): cv.use_id(font.Font),
    cv.Optional(CONF_FONT_MEDIUM): cv.use_id(font.Font),

    # Optional sensors
    cv.Optional(CONF_GOL_FINAL_GENERATION_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_GOL_FINAL_POPULATION_SENSOR): cv.use_id(sensor.Sensor),

    # Grid dimensions
    cv.Optional(CONF_GRID_WIDTH, default=32): cv.int_range(min=8, max=256),
    cv.Optional(CONF_GRID_HEIGHT, default=120): cv.int_range(min=8, max=256),

    # Screen management
    cv.Optional(CONF_SCREEN_CYCLE_TIME, default="3s"): cv.positive_time_period_seconds,
    cv.Optional(CONF_SCREENS): cv.Schema({
        cv.Optional(CONF_YEAR): SCREEN_SCHEMA,
        cv.Optional(CONF_MONTH): SCREEN_SCHEMA,
        cv.Optional(CONF_DAY): SCREEN_SCHEMA,
        cv.Optional(CONF_HOUR): SCREEN_SCHEMA,
        cv.Optional(CONF_HABITS): SCREEN_SCHEMA,
        cv.Optional(CONF_GAME_OF_LIFE): SCREEN_SCHEMA,
    }),

    # Game of Life settings
    cv.Optional(CONF_GAME_OF_LIFE): GAME_OF_LIFE_SCHEMA,

    # Time segments
    cv.Optional(CONF_TIME_SEGMENTS): TIME_SEGMENTS_SCHEMA,

    # Visual styling
    cv.Optional(CONF_STYLE, default="Single Color"): cv.enum(DISPLAY_STYLES, upper=False),
    cv.Optional(CONF_GRADIENT_TYPE, default="Red-Blue"): cv.string,
    cv.Optional(CONF_TEXT_AREA_POSITION, default="Top"): cv.one_of("Top", "Bottom", "None", upper=False),
    cv.Optional(CONF_FILL_DIRECTION, default="Bottom to Top"): cv.one_of("Bottom to Top", "Top to Bottom", upper=False),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Display reference (optional - component receives DisplayBuffer in render())
    if CONF_DISPLAY in config:
        disp = await cg.get_variable(config[CONF_DISPLAY])
        cg.add(var.set_display(disp))

    time_comp = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time(time_comp))

    # Status LED
    if CONF_STATUS_LED in config:
        led = await cg.get_variable(config[CONF_STATUS_LED])
        cg.add(var.set_status_led(led))

    # Optional sensors
    if CONF_GOL_FINAL_GENERATION_SENSOR in config:
        sens = await cg.get_variable(config[CONF_GOL_FINAL_GENERATION_SENSOR])
        cg.add(var.set_gol_final_generation_sensor(sens))

    if CONF_GOL_FINAL_POPULATION_SENSOR in config:
        sens = await cg.get_variable(config[CONF_GOL_FINAL_POPULATION_SENSOR])
        cg.add(var.set_gol_final_population_sensor(sens))

    # Optional fonts
    if CONF_FONT_SMALL in config:
        font_small = await cg.get_variable(config[CONF_FONT_SMALL])
        cg.add(var.set_font_small(font_small))

    if CONF_FONT_MEDIUM in config:
        font_medium = await cg.get_variable(config[CONF_FONT_MEDIUM])
        cg.add(var.set_font_medium(font_medium))

    # Grid dimensions
    cg.add(var.set_grid_dimensions(config[CONF_GRID_WIDTH], config[CONF_GRID_HEIGHT]))

    # Screen cycle time
    cg.add(var.set_screen_cycle_time(config[CONF_SCREEN_CYCLE_TIME]))

    # Register screens
    if CONF_SCREENS in config:
        screens_config = config[CONF_SCREENS]
        if CONF_YEAR in screens_config:
            cg.add(var.register_screen(0, screens_config[CONF_YEAR][CONF_ENABLED]))
        if CONF_MONTH in screens_config:
            cg.add(var.register_screen(1, screens_config[CONF_MONTH][CONF_ENABLED]))
        if CONF_DAY in screens_config:
            cg.add(var.register_screen(2, screens_config[CONF_DAY][CONF_ENABLED]))
        if CONF_HOUR in screens_config:
            cg.add(var.register_screen(3, screens_config[CONF_HOUR][CONF_ENABLED]))
        if CONF_HABITS in screens_config:
            cg.add(var.register_screen(4, screens_config[CONF_HABITS][CONF_ENABLED]))
        if CONF_GAME_OF_LIFE in screens_config:
            cg.add(var.register_screen(5, screens_config[CONF_GAME_OF_LIFE][CONF_ENABLED]))
    else:
        # Default: enable all screens
        for screen_id in range(6):
            cg.add(var.register_screen(screen_id, True))

    # Game of Life configuration
    if CONF_GAME_OF_LIFE in config:
        gol_config = config[CONF_GAME_OF_LIFE]
        cg.add(var.set_game_update_interval(gol_config[CONF_UPDATE_INTERVAL]))
        cg.add(var.set_demo_mode(gol_config[CONF_DEMO_MODE]))

    # Time segments
    if CONF_TIME_SEGMENTS in config:
        ts_config = config[CONF_TIME_SEGMENTS]
        # For now, we'll need to pass this via individual setters
        # A proper struct would require more C++ code generation

    # Visual styling
    cg.add(var.set_style(config[CONF_STYLE]))
    cg.add(var.set_text_area_position(config[CONF_TEXT_AREA_POSITION]))
    cg.add(var.set_fill_direction(config[CONF_FILL_DIRECTION]))
    cg.add(var.set_gradient_type(config[CONF_GRADIENT_TYPE]))
