"""Button entities for each Dangbei remote key."""
from __future__ import annotations

from homeassistant.components.button import ButtonEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .client import DangbeiClient
from .const import (
    COMMAND_ICONS,
    COMMAND_LABELS,
    COMMAND_VALUE_MAP,
    DEVICE_MANUFACTURER,
    DEVICE_MODEL,
    DOMAIN,
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    client: DangbeiClient = hass.data[DOMAIN][entry.entry_id]
    async_add_entities(
        DangbeiButton(entry, client, cmd) for cmd in COMMAND_VALUE_MAP
    )


class DangbeiButton(ButtonEntity):
    """One button per remote key, appearing under the projector device."""

    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(
        self, entry: ConfigEntry, client: DangbeiClient, command: str
    ) -> None:
        self._entry = entry
        self._client = client
        self._command = command
        self._attr_unique_id = f"{entry.entry_id}_{command}"
        self._attr_translation_key = command
        self._attr_name = COMMAND_LABELS[command]
        self._attr_icon = COMMAND_ICONS.get(command)
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)},
            name=entry.title or f"Dangbei ({entry.data[CONF_HOST]})",
            manufacturer=DEVICE_MANUFACTURER,
            model=DEVICE_MODEL,
        )

    async def async_press(self) -> None:
        await self._client.async_send_command(self._command)
