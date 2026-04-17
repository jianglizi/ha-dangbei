"""The Dangbei Projector integration."""
from __future__ import annotations

import logging

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST, CONF_PORT
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .client import DangbeiClient
from .const import (
    CONF_DEVICE_ID,
    CONF_FROM_ID,
    CONF_MSG_TYPE,
    CONF_POWER_OFF_CONFIRM,
    CONF_POWER_OFF_CONFIRM_DELAY,
    CONF_TO_ID,
    DEFAULT_FROM_ID,
    DEFAULT_MSG_TYPE,
    DEFAULT_PORT,
    DEFAULT_POWER_OFF_CONFIRM,
    DEFAULT_POWER_OFF_CONFIRM_DELAY,
    DEFAULT_TO_ID,
    DOMAIN,
    PLATFORMS,
)

_LOGGER = logging.getLogger(__name__)


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

    try:
        await client.async_connect()
    except Exception as err:  # noqa: BLE001
        _LOGGER.warning(
            "Dangbei WebSocket not reachable at %s (%s). "
            "Will retry on first command; projector may be powered off.",
            client.url,
            err,
        )

    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = client

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)

    entry.async_on_unload(entry.add_update_listener(_async_update_listener))

    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded:
        client: DangbeiClient = hass.data[DOMAIN].pop(entry.entry_id)
        await client.async_close()
        if not hass.data[DOMAIN]:
            hass.data.pop(DOMAIN)
    return unloaded


async def _async_update_listener(hass: HomeAssistant, entry: ConfigEntry) -> None:
    """Reload entry on options change."""
    await hass.config_entries.async_reload(entry.entry_id)
