"""Constants for the Dangbei Projector integration."""
from __future__ import annotations

from dataclasses import dataclass

from homeassistant.const import Platform

DOMAIN = "dangbei"

CONF_DEVICE_ID = "device_id"
CONF_TO_ID = "to_id"
CONF_FROM_ID = "from_id"
CONF_MSG_TYPE = "msg_type"
CONF_BLUETOOTH_MAC = "bluetooth_mac"
CONF_DEVICE_NAME = "device_name"
CONF_DEVICE_MODEL = "device_model"
CONF_HOST_NAME = "host_name"
CONF_MAC = "mac"
CONF_WIFI_MAC = "wifi_mac"
CONF_ROM_VERSION = "rom_version"
CONF_SN = "sn"
CONF_POWER_OFF_CONFIRM = "power_off_confirm"
CONF_POWER_OFF_CONFIRM_DELAY = "power_off_confirm_delay"
CONF_WOL_HOST = "wol_host"
CONF_WOL_PORT = "wol_port"
CONF_WOL_TOKEN = "wol_token"
CONF_WOL_ID = "wol_id"
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
WOL_DEVICE_MODEL = "ESP32 Dangbei WOL"
WOL_DEVICE_NAME = "Dangbei WOL"

COMMAND_FROM = 900

MIN_STATUS_POLL_INTERVAL = 5
MAX_STATUS_POLL_INTERVAL = 60
FAST_STATUS_POLL_INTERVAL = 1
TURN_ON_CONFIRM_TIMEOUT = 30
TURN_OFF_CONFIRM_TIMEOUT = 45
REQUIRED_CONFIRM_MATCHES = 2


@dataclass(frozen=True, slots=True)
class CommandSpec:
    """Describes a single projector command."""

    value: str
    command_type: str = "Operation"
    params: str = ""


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
CMD_SIDE_MENU = "side_menu"
CMD_FIND_REMOTE = "find_remote"
CMD_SCREENSHOT = "screenshot"

COMMANDS: dict[str, CommandSpec] = {
    CMD_UP: CommandSpec("1"),
    CMD_DOWN: CommandSpec("2"),
    CMD_LEFT: CommandSpec("3"),
    CMD_RIGHT: CommandSpec("4"),
    CMD_OK: CommandSpec("5"),
    CMD_BACK: CommandSpec("6"),
    CMD_HOME: CommandSpec("7"),
    CMD_MENU: CommandSpec("8"),
    CMD_VOLUME_UP: CommandSpec("9"),
    CMD_VOLUME_DOWN: CommandSpec("10"),
    CMD_POWER_OFF: CommandSpec("11"),
    CMD_SIDE_MENU: CommandSpec("12"),
    CMD_FIND_REMOTE: CommandSpec("101"),
    CMD_SCREENSHOT: CommandSpec("111"),
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
    CMD_SIDE_MENU,
    CMD_FIND_REMOTE,
    CMD_SCREENSHOT,
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
    CMD_SIDE_MENU: "mdi:view-split-vertical",
    CMD_FIND_REMOTE: "mdi:remote-tv",
    CMD_SCREENSHOT: "mdi:camera",
}

PLATFORMS: list[Platform] = [
    Platform.BUTTON,
    Platform.REMOTE,
    Platform.BINARY_SENSOR,
]
