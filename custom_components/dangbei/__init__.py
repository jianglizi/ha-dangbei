"""The Dangbei Projector integration."""
from __future__ import annotations

import logging
from dataclasses import dataclass

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST, CONF_PORT
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .client import DangbeiClient, DangbeiWolClient
from .const import (
    CONF_DEVICE_ID,
    CONF_FROM_ID,
    CONF_MSG_TYPE,
    CONF_POWER_OFF_CONFIRM,
    CONF_POWER_OFF_CONFIRM_DELAY,
    CONF_STATUS_POLL_INTERVAL,
    CONF_TO_ID,
    CONF_WOL_HOST,
    CONF_WOL_PORT,
    CONF_WOL_TOKEN,
    CONF_WOL_WAKE_CUSTOM_FORMAT,
    CONF_WOL_WAKE_CUSTOM_HEX,
    CONF_WOL_WAKE_PROFILE,
    DEFAULT_FROM_ID,
    DEFAULT_MSG_TYPE,
    DEFAULT_PORT,
    DEFAULT_POWER_OFF_CONFIRM,
    DEFAULT_POWER_OFF_CONFIRM_DELAY,
    DEFAULT_STATUS_POLL_INTERVAL,
    DEFAULT_TO_ID,
    DEFAULT_WOL_PORT,
    DEFAULT_WOL_WAKE_CUSTOM_FORMAT,
    DEFAULT_WOL_WAKE_PROFILE,
    DOMAIN,
    PLATFORMS,
)
from .coordinators import Esp32OnlineCoordinator, ProjectorPowerCoordinator

_LOGGER = logging.getLogger(__name__)

ENTRY_VERSION = 2


@dataclass
class DangbeiRuntimeData:
    """Holds per-entry runtime objects."""

    client: DangbeiClient
    wol_client: DangbeiWolClient | None
    projector_coordinator: ProjectorPowerCoordinator
    esp32_coordinator: Esp32OnlineCoordinator | None


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up Dangbei Projector from a config entry."""
    data = {**entry.data, **entry.options}
    session = async_get_clientsession(hass)

    client = DangbeiClient(
        session=session,
        host=data[CONF_HOST],
        port=data.get(CONF_PORT, DEFAULT_PORT),
        device_id=data[CONF_DEVICE_ID],
        to_id=data.get(CONF_TO_ID, DEFAULT_TO_ID),
        from_id=data.get(CONF_FROM_ID, DEFAULT_FROM_ID),
        msg_type=data.get(CONF_MSG_TYPE, DEFAULT_MSG_TYPE),
        power_off_confirm=data.get(CONF_POWER_OFF_CONFIRM, DEFAULT_POWER_OFF_CONFIRM),
        power_off_confirm_delay=data.get(
            CONF_POWER_OFF_CONFIRM_DELAY, DEFAULT_POWER_OFF_CONFIRM_DELAY
        ),
    )

    wol_host = data.get(CONF_WOL_HOST)
    wol_client: DangbeiWolClient | None = None
    if wol_host:
        wol_client = DangbeiWolClient(
            session=session,
            host=wol_host,
            port=data.get(CONF_WOL_PORT, DEFAULT_WOL_PORT),
            token=data.get(CONF_WOL_TOKEN) or None,
        )

    try:
        await client.async_connect()
    except Exception as err:  # noqa: BLE001
        _LOGGER.debug(
            "Dangbei WebSocket not reachable at %s on setup (%s); "
            "projector is probably off. Will retry on demand.",
            client.url,
            err,
        )

    poll_interval = int(
        data.get(CONF_STATUS_POLL_INTERVAL, DEFAULT_STATUS_POLL_INTERVAL)
    )
    projector_coordinator = ProjectorPowerCoordinator(hass, client, poll_interval)
    await projector_coordinator.async_config_entry_first_refresh()

    esp32_coordinator: Esp32OnlineCoordinator | None = None
    if wol_client is not None:
        esp32_coordinator = Esp32OnlineCoordinator(hass, wol_client, poll_interval)
        await esp32_coordinator.async_config_entry_first_refresh()

    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = DangbeiRuntimeData(
        client=client,
        wol_client=wol_client,
        projector_coordinator=projector_coordinator,
        esp32_coordinator=esp32_coordinator,
    )

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    entry.async_on_unload(entry.add_update_listener(_async_update_listener))
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded:
        runtime: DangbeiRuntimeData = hass.data[DOMAIN].pop(entry.entry_id)
        await runtime.projector_coordinator.async_shutdown()
        await runtime.client.async_close()
        if not hass.data[DOMAIN]:
            hass.data.pop(DOMAIN)
    return unloaded


async def async_migrate_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Migrate old entries to include wake-profile defaults."""
    if entry.version >= ENTRY_VERSION:
        return True

    _LOGGER.info("Migrating Dangbei config entry from version %s", entry.version)
    data = {**entry.data}
    data.setdefault(CONF_WOL_WAKE_PROFILE, DEFAULT_WOL_WAKE_PROFILE)
    data.setdefault(CONF_WOL_WAKE_CUSTOM_FORMAT, DEFAULT_WOL_WAKE_CUSTOM_FORMAT)
    data.setdefault(CONF_WOL_WAKE_CUSTOM_HEX, "")
    hass.config_entries.async_update_entry(entry, data=data, version=ENTRY_VERSION)
    return True


async def _async_update_listener(hass: HomeAssistant, entry: ConfigEntry) -> None:
    """Reload entry on options change."""
    await hass.config_entries.async_reload(entry.entry_id)
