"""Constants for the Dangbei Projector integration."""
from __future__ import annotations

from homeassistant.const import Platform

DOMAIN = "dangbei"

CONF_DEVICE_ID = "device_id"
CONF_TO_ID = "to_id"
CONF_FROM_ID = "from_id"
CONF_MSG_TYPE = "msg_type"
CONF_POWER_OFF_CONFIRM = "power_off_confirm"
CONF_POWER_OFF_CONFIRM_DELAY = "power_off_confirm_delay"
CONF_WOL_HOST = "wol_host"
CONF_WOL_PORT = "wol_port"
CONF_WOL_TOKEN = "wol_token"
CONF_WOL_ID = "wol_id"
CONF_WOL_WAKE_PROFILE = "wol_wake_profile"
CONF_WOL_WAKE_CUSTOM_FORMAT = "wol_wake_custom_format"
CONF_WOL_WAKE_CUSTOM_HEX = "wol_wake_custom_hex"
CONF_STATUS_POLL_INTERVAL = "status_poll_interval"

DEFAULT_PORT = 6689
DEFAULT_TO_ID = "fczAs/bZ2lc="
DEFAULT_FROM_ID = "tYbuTOjizjQ="
DEFAULT_MSG_TYPE = "HB7FxtN64oc="
DEFAULT_POWER_OFF_CONFIRM = True
DEFAULT_POWER_OFF_CONFIRM_DELAY = 2.0
DEFAULT_WOL_PORT = 80
DEFAULT_STATUS_POLL_INTERVAL = 10
DEVICE_MANUFACTURER = "Dangbei"
DEVICE_MODEL = "Projector"
WOL_DEVICE_MODEL = "ESP32 WOL"
WOL_DEVICE_NAME = "Dangbei WOL"

WOL_WAKE_PROFILE_D5X_PRO = "d5x_pro"
WOL_WAKE_PROFILE_F3_AIR = "f3_air"
WOL_WAKE_PROFILE_CUSTOM = "custom"
WOL_WAKE_CUSTOM_FORMAT_FULL_ADV = "full_adv"
WOL_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA = "manufacturer_data"
DEFAULT_WOL_WAKE_PROFILE = WOL_WAKE_PROFILE_D5X_PRO
DEFAULT_WOL_WAKE_CUSTOM_FORMAT = WOL_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA
MAX_WOL_WAKE_FULL_ADV_BYTES = 31
MAX_WOL_WAKE_MANUFACTURER_DATA_BYTES = 16

MIN_STATUS_POLL_INTERVAL = 5
MAX_STATUS_POLL_INTERVAL = 60
FAST_STATUS_POLL_INTERVAL = 1
TURN_ON_CONFIRM_TIMEOUT = 30
TURN_OFF_CONFIRM_TIMEOUT = 45
REQUIRED_CONFIRM_MATCHES = 2

CMD_UP = "up"
CMD_DOWN = "down"
CMD_LEFT = "left"
CMD_RIGHT = "right"
CMD_OK = "ok"
CMD_BACK = "back"
CMD_HOME = "home"
CMD_MENU = "menu"
CMD_VOLUME_UP = "volume_up"
CMD_VOLUME_DOWN = "volume_down"
CMD_POWER_OFF = "power_off"

COMMAND_VALUE_MAP: dict[str, str] = {
    CMD_UP: "1",
    CMD_DOWN: "2",
    CMD_LEFT: "3",
    CMD_RIGHT: "4",
    CMD_OK: "5",
    CMD_BACK: "6",
    CMD_HOME: "7",
    CMD_MENU: "8",
    CMD_VOLUME_UP: "9",
    CMD_VOLUME_DOWN: "10",
    CMD_POWER_OFF: "11",
}

BUTTON_COMMANDS: tuple[str, ...] = (
    CMD_UP,
    CMD_DOWN,
    CMD_LEFT,
    CMD_RIGHT,
    CMD_OK,
    CMD_BACK,
    CMD_HOME,
    CMD_MENU,
    CMD_VOLUME_UP,
    CMD_VOLUME_DOWN,
)

COMMAND_ICONS: dict[str, str] = {
    CMD_UP: "mdi:arrow-up-bold",
    CMD_DOWN: "mdi:arrow-down-bold",
    CMD_LEFT: "mdi:arrow-left-bold",
    CMD_RIGHT: "mdi:arrow-right-bold",
    CMD_OK: "mdi:checkbox-marked-circle",
    CMD_BACK: "mdi:keyboard-return",
    CMD_HOME: "mdi:home",
    CMD_MENU: "mdi:menu",
    CMD_VOLUME_UP: "mdi:volume-plus",
    CMD_VOLUME_DOWN: "mdi:volume-minus",
}

PLATFORMS: list[Platform] = [
    Platform.BUTTON,
    Platform.REMOTE,
    Platform.BINARY_SENSOR,
]
