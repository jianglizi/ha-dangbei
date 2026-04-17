"""Remote entity for the Dangbei projector."""
from __future__ import annotations

import asyncio
from typing import Any, Iterable

from homeassistant.components.remote import RemoteEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from . import DangbeiRuntimeData
from .client import DangbeiClient, DangbeiWolClient
from .const import CMD_POWER_OFF, COMMAND_VALUE_MAP, DOMAIN
from .device_info import projector_device_info


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up the projector power/remote entity."""
    runtime: DangbeiRuntimeData = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([DangbeiRemote(entry, runtime)])


class DangbeiRemote(CoordinatorEntity[bool], RemoteEntity):
    """Remote entity exposing send_command plus power actions."""

    _attr_has_entity_name = True
    _attr_name = None
    _attr_translation_key = "remote"

    def __init__(self, entry: ConfigEntry, runtime: DangbeiRuntimeData) -> None:
        super().__init__(runtime.projector_coordinator)
        self._client: DangbeiClient = runtime.client
        self._wol_client: DangbeiWolClient | None = runtime.wol_client
        self._power_coordinator = runtime.projector_coordinator
        self._attr_unique_id = f"{entry.entry_id}_remote"
        self._attr_device_info = projector_device_info(entry)

    @property
    def is_on(self) -> bool:
        """Reflect the effective power state shown to the user."""
        return self._power_coordinator.effective_state

    @callback
    def _handle_coordinator_update(self) -> None:
        self.async_write_ha_state()

    async def async_turn_on(self, **kwargs: Any) -> None:
        """Wake the projector through the ESP32 companion."""
        if self._wol_client is None:
            raise HomeAssistantError(
                "Power-on requires a configured ESP32 wake-up device."
            )
        try:
            await self._wol_client.async_wakeup()
        except Exception as err:
            raise HomeAssistantError(
                f"Failed to trigger wake-up on ESP32: {err}"
            ) from err

        await self._power_coordinator.async_begin_transition(True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        """Send the projector power-off command and enter a fast confirm window."""
        await self._client.async_send_command(CMD_POWER_OFF)
        await self._power_coordinator.async_begin_transition(False)

    async def async_send_command(
        self, command: Iterable[str] | str, **kwargs: Any
    ) -> None:
        """Send one or more remote commands to the projector."""
        num_repeats = int(kwargs.get("num_repeats", 1) or 1)
        delay_secs = float(kwargs.get("delay_secs", 0.4) or 0.0)
        hold_secs = float(kwargs.get("hold_secs", 0.0) or 0.0)

        commands = [command] if isinstance(command, str) else list(command)
        saw_power_off = False

        for _ in range(num_repeats):
            for index, cmd in enumerate(commands):
                cmd = cmd.strip().lower()
                if cmd not in COMMAND_VALUE_MAP:
                    raise ValueError(
                        f"Unknown Dangbei command '{cmd}'. "
                        f"Valid: {sorted(COMMAND_VALUE_MAP)}"
                    )
                await self._client.async_send_command(cmd)
                saw_power_off = saw_power_off or cmd == CMD_POWER_OFF
                if hold_secs:
                    await asyncio.sleep(hold_secs)
                if delay_secs and index < len(commands) - 1:
                    await asyncio.sleep(delay_secs)

        if saw_power_off:
            await self._power_coordinator.async_begin_transition(False)
