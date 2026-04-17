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

DEFAULT_PORT = 6689
DEFAULT_TO_ID = "fczAs/bZ2lc="
DEFAULT_FROM_ID = "tYbuTOjizjQ="
DEFAULT_MSG_TYPE = "HB7FxtN64oc="
DEFAULT_POWER_OFF_CONFIRM = True
DEFAULT_POWER_OFF_CONFIRM_DELAY = 2.0
DEVICE_MANUFACTURER = "Dangbei"
DEVICE_MODEL = "Projector"

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

COMMAND_LABELS: dict[str, str] = {
    CMD_UP: "Up",
    CMD_DOWN: "Down",
    CMD_LEFT: "Left",
    CMD_RIGHT: "Right",
    CMD_OK: "OK",
    CMD_BACK: "Back",
    CMD_HOME: "Home",
    CMD_MENU: "Menu",
    CMD_VOLUME_UP: "Volume Up",
    CMD_VOLUME_DOWN: "Volume Down",
    CMD_POWER_OFF: "Power Off",
}

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
    CMD_POWER_OFF: "mdi:power",
}

PLATFORMS: list[Platform] = [Platform.BUTTON, Platform.REMOTE]
