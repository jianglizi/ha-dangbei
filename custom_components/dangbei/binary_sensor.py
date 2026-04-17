"""Binary sensors for projector and companion ESP32 availability."""
from __future__ import annotations

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from . import DangbeiRuntimeData
from .const import CONF_WOL_HOST, DOMAIN
from .device_info import esp32_device_info, projector_device_info


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up connectivity sensors for the projector and optional ESP32."""
    runtime: DangbeiRuntimeData = hass.data[DOMAIN][entry.entry_id]
    entities: list[BinarySensorEntity] = [DangbeiOnlineSensor(entry, runtime)]

    if runtime.esp32_coordinator is not None:
        wol_host = ({**entry.data, **entry.options}).get(CONF_WOL_HOST)
        entities.append(DangbeiEsp32OnlineSensor(entry, runtime, wol_host))

    async_add_entities(entities)


class DangbeiOnlineSensor(CoordinatorEntity[bool], BinarySensorEntity):
    """True when the projector WebSocket port accepts connections."""

    _attr_has_entity_name = True
    _attr_name = None
    _attr_translation_key = "online"
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY

    def __init__(self, entry: ConfigEntry, runtime: DangbeiRuntimeData) -> None:
        super().__init__(runtime.projector_coordinator)
        self._projector_coordinator = runtime.projector_coordinator
        self._attr_unique_id = f"{entry.entry_id}_online"
        self._attr_device_info = projector_device_info(entry)

    @property
    def is_on(self) -> bool:
        """Reflect the last probed projector state."""
        return self._projector_coordinator.actual_state


class DangbeiEsp32OnlineSensor(CoordinatorEntity[bool], BinarySensorEntity):
    """True when the companion ESP32 HTTP API is reachable."""

    _attr_has_entity_name = True
    _attr_name = None
    _attr_translation_key = "esp32_online"
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY

    def __init__(
        self,
        entry: ConfigEntry,
        runtime: DangbeiRuntimeData,
        wol_host: str | None,
    ) -> None:
        assert runtime.esp32_coordinator is not None
        super().__init__(runtime.esp32_coordinator)
        self._esp32_coordinator = runtime.esp32_coordinator
        self._attr_unique_id = f"{entry.entry_id}_esp32_online"
        self._attr_device_info = esp32_device_info(entry, wol_host)

    @property
    def is_on(self) -> bool:
        """Reflect whether the ESP32 API is reachable."""
        return bool(self._esp32_coordinator.data)
