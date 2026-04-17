"""Remote entity for the Dangbei Projector."""
from __future__ import annotations

import asyncio
from typing import Any, Iterable

from homeassistant.components.remote import RemoteEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .client import DangbeiClient
from .const import (
    CMD_POWER_OFF,
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
    async_add_entities([DangbeiRemote(entry, client)])


class DangbeiRemote(RemoteEntity):
    """Remote entity exposing send_command and turn_off."""

    _attr_has_entity_name = True
    _attr_name = "Remote"
    _attr_translation_key = "remote"
    _attr_should_poll = False

    def __init__(self, entry: ConfigEntry, client: DangbeiClient) -> None:
        self._entry = entry
        self._client = client
        self._attr_unique_id = f"{entry.entry_id}_remote"
        self._attr_is_on = True
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)},
            name=entry.title or f"Dangbei ({entry.data[CONF_HOST]})",
            manufacturer=DEVICE_MANUFACTURER,
            model=DEVICE_MODEL,
        )

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self._client.async_send_command(CMD_POWER_OFF)

    async def async_send_command(
        self, command: Iterable[str] | str, **kwargs: Any
    ) -> None:
        num_repeats = int(kwargs.get("num_repeats", 1) or 1)
        delay_secs = float(kwargs.get("delay_secs", 0.4) or 0.0)
        hold_secs = float(kwargs.get("hold_secs", 0.0) or 0.0)

        commands = [command] if isinstance(command, str) else list(command)
        for _ in range(num_repeats):
            for idx, cmd in enumerate(commands):
                cmd = cmd.strip().lower()
                if cmd not in COMMAND_VALUE_MAP:
                    raise ValueError(
                        f"Unknown Dangbei command '{cmd}'. "
                        f"Valid: {sorted(COMMAND_VALUE_MAP)}"
                    )
                await self._client.async_send_command(cmd)
                if hold_secs:
                    await asyncio.sleep(hold_secs)
                if delay_secs and idx < len(commands) - 1:
                    await asyncio.sleep(delay_secs)
