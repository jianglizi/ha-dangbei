"""Shared device-info helpers for Dangbei entities."""
from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST
from homeassistant.helpers.device_registry import DeviceInfo

from .const import DEVICE_MANUFACTURER, DEVICE_MODEL, DOMAIN, WOL_DEVICE_MODEL, WOL_DEVICE_NAME


def projector_device_info(entry: ConfigEntry) -> DeviceInfo:
    """Build the projector device info for an entry."""
    return DeviceInfo(
        identifiers={(DOMAIN, entry.entry_id)},
        name=entry.title or f"Dangbei ({entry.data[CONF_HOST]})",
        manufacturer=DEVICE_MANUFACTURER,
        model=DEVICE_MODEL,
    )


def esp32_device_info(entry: ConfigEntry, wol_host: str | None) -> DeviceInfo:
    """Build the companion ESP32 device info for an entry."""
    return DeviceInfo(
        identifiers={(DOMAIN, f"{entry.entry_id}_esp32")},
        name=wol_host or WOL_DEVICE_NAME,
        manufacturer=DEVICE_MANUFACTURER,
        model=WOL_DEVICE_MODEL,
    )
