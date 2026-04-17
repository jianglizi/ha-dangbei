"""Button entities for Dangbei remote keys."""
from __future__ import annotations

from homeassistant.components.button import ButtonEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import DangbeiRuntimeData
from .const import BUTTON_COMMANDS, COMMAND_ICONS, DOMAIN
from .device_info import projector_device_info


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up non-power remote buttons for a Dangbei projector."""
    runtime: DangbeiRuntimeData = hass.data[DOMAIN][entry.entry_id]
    async_add_entities(
        [DangbeiButton(entry, runtime.client, command) for command in BUTTON_COMMANDS]
    )


class DangbeiButton(ButtonEntity):
    """One button per remote key, appearing under the projector device."""

    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, entry: ConfigEntry, client, command: str) -> None:
        self._client = client
        self._command = command
        self._attr_unique_id = f"{entry.entry_id}_{command}"
        self._attr_translation_key = command
        self._attr_name = None
        self._attr_icon = COMMAND_ICONS.get(command)
        self._attr_device_info = projector_device_info(entry)

    async def async_press(self) -> None:
        """Send the mapped projector command."""
        await self._client.async_send_command(self._command)
