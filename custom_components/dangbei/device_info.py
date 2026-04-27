"""Shared device-info helpers for Dangbei entities."""
from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST
from homeassistant.helpers.device_registry import DeviceInfo

from .const import (
    CONF_DEVICE_MODEL,
    CONF_DEVICE_NAME,
    CONF_ROM_VERSION,
    DEVICE_MANUFACTURER,
    DOMAIN,
    WOL_DEVICE_MODEL,
    WOL_DEVICE_NAME,
)


def projector_device_info(entry: ConfigEntry) -> DeviceInfo:
    """Build the projector device info for an entry."""
    data = {**entry.data, **entry.options}
    name = data.get(CONF_DEVICE_NAME) or entry.title or f"Dangbei ({data[CONF_HOST]})"
    model = data.get(CONF_DEVICE_MODEL) or "Projector"
    sw_version = data.get(CONF_ROM_VERSION) or None
    return DeviceInfo(
        identifiers={(DOMAIN, entry.entry_id)},
        name=name,
        manufacturer=DEVICE_MANUFACTURER,
        model=model,
        sw_version=sw_version,
    )


def esp32_device_info(entry: ConfigEntry, wol_host: str | None) -> DeviceInfo:
    """Build the companion ESP32 device info for an entry."""
    return DeviceInfo(
        identifiers={(DOMAIN, f"{entry.entry_id}_esp32")},
        name=wol_host or WOL_DEVICE_NAME,
        manufacturer=DEVICE_MANUFACTURER,
        model=WOL_DEVICE_MODEL,
    )
