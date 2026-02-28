import esphome.codegen as cg
import esphome.config_validation as cv
import logging
import io
import os
import requests
from PIL import Image
from urllib.parse import urlparse

from esphome.components import display, time as time_, font, light, sensor
from esphome.const import (
    CONF_ID,
    CONF_TIME_ID,
    CONF_UPDATE_INTERVAL,
    CONF_FILE,
    CONF_RAW_DATA_ID,
)
from esphome import automation
from esphome.core import CORE, HexInt, EsphomeError

_LOGGER = logging.getLogger(__name__)

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
CONF_LIFESPAN = "lifespan"
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

# Icon configuration keys
CONF_ICONS = "icons"
CONF_LAMEID = "lameid"
CONF_URL = "url"
CONF_ICON_ID = "icon_id"
CONF_ICON_CACHE = "icon_cache"

MAX_ICONS = 50
ICON_WIDTH = 8
ICON_HEIGHT = 8

# Lifespan config keys
CONF_LS_BIRTHDAY        = "birthday"
CONF_LS_MOVED_OUT       = "moved_out"
CONF_LS_SCHOOL_YEARS    = "school_years"
CONF_LS_KIDS            = "kids"
CONF_LS_PARENTS         = "parents"
CONF_LS_SIBLINGS        = "siblings"
CONF_LS_PARTNER_RANGES  = "partner_ranges"
CONF_LS_MARRIAGE_RANGES = "marriage_ranges"
CONF_LS_MILESTONES      = "milestones"
CONF_LS_RETIREMENT      = "retirement"
CONF_LS_LIFE_EXPECTANCY = "life_expectancy_age"
CONF_LS_PHASE_CYCLE     = "phase_cycle_time"

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

LIFESPAN_SCHEMA = cv.Schema({
    cv.Required(CONF_LS_BIRTHDAY):                          cv.string,
    cv.Optional(CONF_LS_MOVED_OUT):                         cv.int_range(min=14, max=40),
    cv.Optional(CONF_LS_SCHOOL_YEARS):                      cv.int_range(min=8, max=25),
    cv.Optional(CONF_LS_KIDS):                              cv.string,
    cv.Optional(CONF_LS_PARENTS):                           cv.string,
    cv.Optional(CONF_LS_SIBLINGS):                          cv.string,
    cv.Optional(CONF_LS_PARTNER_RANGES):                    cv.string,
    cv.Optional(CONF_LS_MARRIAGE_RANGES):                   cv.string,
    cv.Optional(CONF_LS_MILESTONES):                        cv.string,
    cv.Optional(CONF_LS_RETIREMENT):                        cv.int_range(min=50, max=90),
    cv.Optional(CONF_LS_LIFE_EXPECTANCY, default=90):       cv.int_range(min=40, max=110),
    # Integer seconds; 0 disables phase highlighting
    cv.Optional(CONF_LS_PHASE_CYCLE, default=3):            cv.int_range(min=0, max=30),
})

ICON_SCHEMA = cv.Schema({
    cv.Required(CONF_ICON_ID): cv.string,
    cv.Exclusive(CONF_FILE, "source"): cv.file_,
    cv.Exclusive(CONF_URL, "source"): cv.url,
    cv.Exclusive(CONF_LAMEID, "source"): cv.string,
    cv.GenerateID(CONF_RAW_DATA_ID): cv.declare_id(cg.uint16),
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
        cv.Optional(CONF_LIFESPAN): SCREEN_SCHEMA,
    }),

    # Game of Life settings
    cv.Optional(CONF_GAME_OF_LIFE): GAME_OF_LIFE_SCHEMA,

    # Time segments
    cv.Optional(CONF_TIME_SEGMENTS): TIME_SEGMENTS_SCHEMA,

    # Lifespan view (biographical data — YAML only, not exposed to HA dashboard)
    cv.Optional(CONF_LIFESPAN): LIFESPAN_SCHEMA,

    # Visual styling
    cv.Optional(CONF_STYLE, default="Single Color"): cv.enum(DISPLAY_STYLES, upper=False),
    cv.Optional(CONF_GRADIENT_TYPE, default="Red-Blue"): cv.string,
    cv.Optional(CONF_TEXT_AREA_POSITION, default="Top"): cv.one_of("Top", "Bottom", "None", upper=False),
    cv.Optional(CONF_FILL_DIRECTION, default="Bottom to Top"): cv.one_of("Bottom to Top", "Top to Bottom", upper=False),

    # Icon configuration
    cv.Optional(CONF_ICONS): cv.All(
        cv.ensure_list(ICON_SCHEMA),
        cv.Length(max=MAX_ICONS),
    ),
    cv.Optional(CONF_ICON_CACHE, default=True): cv.boolean,
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
        if CONF_LIFESPAN in screens_config:
            cg.add(var.register_screen(5, screens_config[CONF_LIFESPAN][CONF_ENABLED]))
        if CONF_GAME_OF_LIFE in screens_config:
            cg.add(var.register_screen(6, screens_config[CONF_GAME_OF_LIFE][CONF_ENABLED]))
    else:
        # Default: enable screens 0–4, then lifespan (5) if configured, then GoL (6)
        for screen_id in range(5):
            cg.add(var.register_screen(screen_id, True))
        if CONF_LIFESPAN in config:
            cg.add(var.register_screen(5, True))
        cg.add(var.register_screen(6, True))

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

    # Lifespan view configuration
    if CONF_LIFESPAN in config:
        ls = config[CONF_LIFESPAN]
        cg.add(var.set_lifespan_birthday(ls[CONF_LS_BIRTHDAY]))
        if CONF_LS_MOVED_OUT in ls:
            cg.add(var.set_lifespan_moved_out_age(ls[CONF_LS_MOVED_OUT]))
        if CONF_LS_SCHOOL_YEARS in ls:
            cg.add(var.set_lifespan_school_years(ls[CONF_LS_SCHOOL_YEARS]))
        if CONF_LS_KIDS in ls:
            cg.add(var.set_lifespan_kids(ls[CONF_LS_KIDS]))
        if CONF_LS_PARENTS in ls:
            cg.add(var.set_lifespan_parents(ls[CONF_LS_PARENTS]))
        if CONF_LS_SIBLINGS in ls:
            cg.add(var.set_lifespan_siblings(ls[CONF_LS_SIBLINGS]))
        if CONF_LS_PARTNER_RANGES in ls:
            cg.add(var.set_lifespan_partner_ranges(ls[CONF_LS_PARTNER_RANGES]))
        if CONF_LS_MARRIAGE_RANGES in ls:
            cg.add(var.set_lifespan_marriage_ranges(ls[CONF_LS_MARRIAGE_RANGES]))
        if CONF_LS_MILESTONES in ls:
            cg.add(var.set_lifespan_milestones(ls[CONF_LS_MILESTONES]))
        if CONF_LS_RETIREMENT in ls:
            cg.add(var.set_lifespan_retirement_age(ls[CONF_LS_RETIREMENT]))
        cg.add(var.set_lifespan_life_expectancy(ls[CONF_LS_LIFE_EXPECTANCY]))
        cg.add(var.set_lifespan_phase_cycle(float(ls[CONF_LS_PHASE_CYCLE])))

    # Visual styling
    cg.add(var.set_style(config[CONF_STYLE]))
    cg.add(var.set_text_area_position(config[CONF_TEXT_AREA_POSITION]))
    cg.add(var.set_fill_direction(config[CONF_FILL_DIRECTION]))
    cg.add(var.set_gradient_type(config[CONF_GRADIENT_TYPE]))

    # Icon processing
    if CONF_ICONS in config:
        _LOGGER.info("Processing icons for life-matrix...")
        use_cache = config.get(CONF_ICON_CACHE, True)
        
        for icon_conf in config[CONF_ICONS]:
            icon_id = icon_conf[CONF_ICON_ID]
            image = None
            
            if CONF_FILE in icon_conf:
                path = CORE.relative_config_path(icon_conf[CONF_FILE])
                try:
                    image = Image.open(path)
                except Exception as e:
                    raise EsphomeError(f"Could not load icon file {path}: {e}")
                    
            elif CONF_LAMEID in icon_conf:
                lameid = icon_conf[CONF_LAMEID]
                cache_path = CORE.relative_config_path(f".cache/life_matrix_icons/{lameid}")
                
                if use_cache and os.path.isfile(cache_path):
                    try:
                        image = Image.open(cache_path)
                        _LOGGER.info(f"Loaded icon '{icon_id}' from cache")
                    except Exception as e:
                        raise EsphomeError(f"Could not load cached icon {cache_path}: {e}")
                else:
                    url = f"https://developer.lametric.com/content/apps/icon_thumbs/{lameid}"
                    try:
                        r = requests.get(url, timeout=10.0)
                        r.raise_for_status()
                        image = Image.open(io.BytesIO(r.content))
                        if use_cache:
                            os.makedirs(os.path.dirname(cache_path), exist_ok=True)
                            with open(cache_path, "wb") as f:
                                f.write(r.content)
                            _LOGGER.info(f"Downloaded and cached icon '{icon_id}' (lameid: {lameid})")
                    except Exception as e:
                        raise EsphomeError(f"Could not download LaMetric icon {lameid}: {e}")
                        
            elif CONF_URL in icon_conf:
                url = icon_conf[CONF_URL]
                parsed = urlparse(url)
                cache_path = CORE.relative_config_path(f".cache/life_matrix_icons/{os.path.basename(parsed.path)}")
                
                if use_cache and os.path.isfile(cache_path):
                    try:
                        image = Image.open(cache_path)
                        _LOGGER.info(f"Loaded icon '{icon_id}' from cache")
                    except Exception as e:
                        raise EsphomeError(f"Could not load cached icon {cache_path}: {e}")
                else:
                    try:
                        r = requests.get(url, timeout=10.0)
                        r.raise_for_status()
                        image = Image.open(io.BytesIO(r.content))
                        if use_cache:
                            os.makedirs(os.path.dirname(cache_path), exist_ok=True)
                            with open(cache_path, "wb") as f:
                                f.write(r.content)
                            _LOGGER.info(f"Downloaded and cached icon '{icon_id}'")
                    except Exception as e:
                        raise EsphomeError(f"Could not download icon from {url}: {e}")
            
            if image is None:
                continue
            
            # Determine number of frames
            n_frames = 1
            if hasattr(image, "n_frames"):
                n_frames = min(image.n_frames, 64)  # Cap at 64 frames
            
            # Get default frame duration (in ms)
            default_duration = 100  # Default 100ms per frame
            try:
                default_duration = image.info.get("duration", 100)
            except:
                pass
            
            # Process each frame - collect all data
            all_frame_data = []
            frame_durations = []
            
            for frame_idx in range(n_frames):
                # Seek to frame
                if n_frames > 1:
                    image.seek(frame_idx)
                
                # Make a copy for this frame (important for animated GIFs)
                frame = image.copy()
                
                # Resize to 8x8 if needed
                width, height = frame.size
                if width != ICON_WIDTH or height != ICON_HEIGHT:
                    frame = frame.resize((ICON_WIDTH, ICON_HEIGHT), Image.Resampling.LANCZOS)
                
                # Convert to RGBA if needed
                if frame.mode != "RGBA":
                    frame = frame.convert("RGBA")
                
                # Convert to RGB565 array with transparency marker
                pixels = list(frame.getdata())
                for r, g, b, a in pixels:
                    if a < 128:  # Transparent pixel
                        all_frame_data.append(0xF81F)  # Magenta as transparent marker
                    else:
                        # Convert to RGB565
                        r5 = (r >> 3) & 0x1F
                        g6 = (g >> 2) & 0x3F
                        b5 = (b >> 3) & 0x1F
                        all_frame_data.append((r5 << 11) | (g6 << 5) | b5)
                
                # Get frame duration
                duration = default_duration
                if hasattr(image, "info") and "duration" in image.info:
                    try:
                        duration = image.info["duration"]
                    except:
                        pass
                frame_durations.append(duration)
            
            # Create single progmem array for all frames
            rhs = [HexInt(x) for x in all_frame_data]
            prog_arr = cg.progmem_array(icon_conf[CONF_RAW_DATA_ID], rhs)
            
            # Register icon with component - pass array, frame count, and durations
            frame_durations_int = [int(d) for d in frame_durations]
            cg.add(var.register_icon_frames(icon_id, prog_arr, n_frames, frame_durations_int))
            
            if n_frames > 1:
                _LOGGER.info(f"Registered animated icon '{icon_id}' ({n_frames} frames)")
            else:
                _LOGGER.info(f"Registered static icon '{icon_id}'")
