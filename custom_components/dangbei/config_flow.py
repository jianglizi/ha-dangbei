"""Config flow for the Dangbei Projector integration."""
from __future__ import annotations

import logging
from typing import Any

import voluptuous as vol

from homeassistant.config_entries import ConfigEntry, ConfigFlow, OptionsFlow
from homeassistant.const import CONF_HOST, CONF_PORT
from homeassistant.core import callback
from homeassistant.data_entry_flow import FlowResult
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
    DEFAULT_POWER_OFF_CONFIRM,
    DEFAULT_POWER_OFF_CONFIRM_DELAY,
    DEFAULT_PORT,
    DEFAULT_TO_ID,
    DOMAIN,
)

_LOGGER = logging.getLogger(__name__)

USER_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_HOST): str,
        vol.Required(CONF_DEVICE_ID): str,
        vol.Optional(CONF_PORT, default=DEFAULT_PORT): int,
        vol.Optional(CONF_TO_ID, default=DEFAULT_TO_ID): str,
        vol.Optional(CONF_FROM_ID, default=DEFAULT_FROM_ID): str,
        vol.Optional(CONF_MSG_TYPE, default=DEFAULT_MSG_TYPE): str,
        vol.Optional(
            CONF_POWER_OFF_CONFIRM, default=DEFAULT_POWER_OFF_CONFIRM
        ): bool,
        vol.Optional(
            CONF_POWER_OFF_CONFIRM_DELAY, default=DEFAULT_POWER_OFF_CONFIRM_DELAY
        ): vol.All(vol.Coerce(float), vol.Range(min=0.0, max=10.0)),
    }
)


class DangbeiConfigFlow(ConfigFlow, domain=DOMAIN):
    """Handle the initial setup via UI."""

    VERSION = 1

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        errors: dict[str, str] = {}

        if user_input is not None:
            user_input = {
                **user_input,
                CONF_HOST: user_input[CONF_HOST].strip(),
                CONF_DEVICE_ID: user_input[CONF_DEVICE_ID].strip(),
                CONF_TO_ID: user_input.get(CONF_TO_ID, DEFAULT_TO_ID).strip(),
                CONF_FROM_ID: user_input.get(CONF_FROM_ID, DEFAULT_FROM_ID).strip(),
                CONF_MSG_TYPE: user_input.get(CONF_MSG_TYPE, DEFAULT_MSG_TYPE).strip(),
            }

            host = user_input[CONF_HOST]
            port = user_input.get(CONF_PORT, DEFAULT_PORT)
            await self.async_set_unique_id(f"{host}:{port}")
            self._abort_if_unique_id_configured()

            session = async_get_clientsession(self.hass)
            probe = DangbeiClient(
                session=session,
                host=host,
                port=port,
                device_id=user_input[CONF_DEVICE_ID],
                to_id=user_input.get(CONF_TO_ID, DEFAULT_TO_ID),
                from_id=user_input.get(CONF_FROM_ID, DEFAULT_FROM_ID),
                msg_type=user_input.get(CONF_MSG_TYPE, DEFAULT_MSG_TYPE),
                power_off_confirm=user_input.get(
                    CONF_POWER_OFF_CONFIRM, DEFAULT_POWER_OFF_CONFIRM
                ),
                power_off_confirm_delay=user_input.get(
                    CONF_POWER_OFF_CONFIRM_DELAY, DEFAULT_POWER_OFF_CONFIRM_DELAY
                ),
            )

            try:
                await probe.async_test_connection()
            except Exception as err:  # noqa: BLE001
                _LOGGER.info("Dangbei probe failed for %s:%s: %s", host, port, err)
                errors["base"] = "cannot_connect"

            if not errors:
                return self.async_create_entry(
                    title=f"Dangbei ({host})",
                    data=user_input,
                )

        return self.async_show_form(
            step_id="user",
            data_schema=USER_SCHEMA,
            errors=errors,
        )

    @staticmethod
    @callback
    def async_get_options_flow(entry: ConfigEntry) -> OptionsFlow:
        return DangbeiOptionsFlow()


class DangbeiOptionsFlow(OptionsFlow):
    """Allow editing a subset of settings after setup."""

    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        if user_input is not None:
            return self.async_create_entry(title="", data=user_input)

        data = {**self.config_entry.data, **self.config_entry.options}

        schema = vol.Schema(
            {
                vol.Optional(
                    CONF_POWER_OFF_CONFIRM,
                    default=data.get(
                        CONF_POWER_OFF_CONFIRM, DEFAULT_POWER_OFF_CONFIRM
                    ),
                ): bool,
                vol.Optional(
                    CONF_POWER_OFF_CONFIRM_DELAY,
                    default=data.get(
                        CONF_POWER_OFF_CONFIRM_DELAY,
                        DEFAULT_POWER_OFF_CONFIRM_DELAY,
                    ),
                ): vol.All(vol.Coerce(float), vol.Range(min=0.0, max=10.0)),
            }
        )
        return self.async_show_form(step_id="init", data_schema=schema)
