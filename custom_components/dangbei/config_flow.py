"""Config flow for the Dangbei Projector integration."""
from __future__ import annotations

import logging
from typing import Any

import voluptuous as vol

from homeassistant.components import zeroconf
from homeassistant.config_entries import ConfigEntry, ConfigFlow, OptionsFlow
from homeassistant.const import CONF_HOST, CONF_PORT
from homeassistant.core import callback
from homeassistant.data_entry_flow import FlowResult
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .client import (
    DangbeiClient,
    DangbeiWolClient,
    DangbeiWolClientError,
    DangbeiWolUnsupportedError,
)
from .const import (
    CONF_DEVICE_ID,
    CONF_FROM_ID,
    CONF_MSG_TYPE,
    CONF_POWER_OFF_CONFIRM,
    CONF_POWER_OFF_CONFIRM_DELAY,
    CONF_STATUS_POLL_INTERVAL,
    CONF_TO_ID,
    CONF_WOL_HOST,
    CONF_WOL_ID,
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
    MAX_STATUS_POLL_INTERVAL,
    MIN_STATUS_POLL_INTERVAL,
)
from .wake_profiles import (
    WakeConfigError,
    normalize_wake_configuration,
    wake_custom_format_options,
    wake_profile_options,
)

_LOGGER = logging.getLogger(__name__)


def _user_schema(defaults: dict[str, Any]) -> vol.Schema:
    return vol.Schema(
        {
            vol.Required(CONF_HOST, default=defaults.get(CONF_HOST, "")): str,
            vol.Required(
                CONF_DEVICE_ID, default=defaults.get(CONF_DEVICE_ID, "")
            ): str,
            vol.Optional(CONF_PORT, default=defaults.get(CONF_PORT, DEFAULT_PORT)): int,
            vol.Optional(
                CONF_TO_ID, default=defaults.get(CONF_TO_ID, DEFAULT_TO_ID)
            ): str,
            vol.Optional(
                CONF_FROM_ID, default=defaults.get(CONF_FROM_ID, DEFAULT_FROM_ID)
            ): str,
            vol.Optional(
                CONF_MSG_TYPE, default=defaults.get(CONF_MSG_TYPE, DEFAULT_MSG_TYPE)
            ): str,
            vol.Optional(
                CONF_POWER_OFF_CONFIRM,
                default=defaults.get(
                    CONF_POWER_OFF_CONFIRM, DEFAULT_POWER_OFF_CONFIRM
                ),
            ): bool,
            vol.Optional(
                CONF_POWER_OFF_CONFIRM_DELAY,
                default=defaults.get(
                    CONF_POWER_OFF_CONFIRM_DELAY, DEFAULT_POWER_OFF_CONFIRM_DELAY
                ),
            ): vol.All(vol.Coerce(float), vol.Range(min=0.0, max=10.0)),
            vol.Optional(CONF_WOL_HOST, default=defaults.get(CONF_WOL_HOST, "")): str,
            vol.Optional(
                CONF_WOL_PORT, default=defaults.get(CONF_WOL_PORT, DEFAULT_WOL_PORT)
            ): int,
            vol.Optional(
                CONF_WOL_TOKEN, default=defaults.get(CONF_WOL_TOKEN, "")
            ): str,
            vol.Required(
                CONF_WOL_WAKE_PROFILE,
                default=defaults.get(
                    CONF_WOL_WAKE_PROFILE, DEFAULT_WOL_WAKE_PROFILE
                ),
            ): vol.In(wake_profile_options()),
            vol.Required(
                CONF_WOL_WAKE_CUSTOM_FORMAT,
                default=defaults.get(
                    CONF_WOL_WAKE_CUSTOM_FORMAT, DEFAULT_WOL_WAKE_CUSTOM_FORMAT
                ),
            ): vol.In(wake_custom_format_options()),
            vol.Optional(
                CONF_WOL_WAKE_CUSTOM_HEX,
                default=defaults.get(CONF_WOL_WAKE_CUSTOM_HEX, ""),
            ): str,
        }
    )


def _options_schema(defaults: dict[str, Any]) -> vol.Schema:
    return vol.Schema(
        {
            vol.Optional(
                CONF_POWER_OFF_CONFIRM,
                default=defaults.get(CONF_POWER_OFF_CONFIRM, DEFAULT_POWER_OFF_CONFIRM),
            ): bool,
            vol.Optional(
                CONF_POWER_OFF_CONFIRM_DELAY,
                default=defaults.get(
                    CONF_POWER_OFF_CONFIRM_DELAY, DEFAULT_POWER_OFF_CONFIRM_DELAY
                ),
            ): vol.All(vol.Coerce(float), vol.Range(min=0.0, max=10.0)),
            vol.Optional(
                CONF_STATUS_POLL_INTERVAL,
                default=defaults.get(
                    CONF_STATUS_POLL_INTERVAL, DEFAULT_STATUS_POLL_INTERVAL
                ),
            ): vol.All(
                vol.Coerce(int),
                vol.Range(
                    min=MIN_STATUS_POLL_INTERVAL,
                    max=MAX_STATUS_POLL_INTERVAL,
                ),
            ),
            vol.Required(
                CONF_WOL_WAKE_PROFILE,
                default=defaults.get(
                    CONF_WOL_WAKE_PROFILE, DEFAULT_WOL_WAKE_PROFILE
                ),
            ): vol.In(wake_profile_options()),
            vol.Required(
                CONF_WOL_WAKE_CUSTOM_FORMAT,
                default=defaults.get(
                    CONF_WOL_WAKE_CUSTOM_FORMAT, DEFAULT_WOL_WAKE_CUSTOM_FORMAT
                ),
            ): vol.In(wake_custom_format_options()),
            vol.Optional(
                CONF_WOL_WAKE_CUSTOM_HEX,
                default=defaults.get(CONF_WOL_WAKE_CUSTOM_HEX, ""),
            ): str,
        }
    )


class DangbeiConfigFlow(ConfigFlow, domain=DOMAIN):
    """Handle the initial setup via UI."""

    VERSION = 2

    def __init__(self) -> None:
        self._discovered_wol: dict[str, Any] | None = None

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        errors: dict[str, str] = {}

        if user_input is not None:
            user_input = _strip_user_input(user_input)

            try:
                wake_config = normalize_wake_configuration(user_input)
            except WakeConfigError as err:
                errors[err.field] = err.error_key
            else:
                user_input.update(wake_config.as_dict())

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

            if not errors:
                try:
                    await probe.async_test_connection()
                except Exception as err:  # noqa: BLE001
                    _LOGGER.info("Dangbei probe failed for %s:%s: %s", host, port, err)
                    errors["base"] = "cannot_connect"

            wol_host = user_input.get(CONF_WOL_HOST, "").strip()
            wol_info: dict[str, Any] | None = None
            if not errors and wol_host:
                wol_probe = DangbeiWolClient(
                    session=session,
                    host=wol_host,
                    port=user_input.get(CONF_WOL_PORT, DEFAULT_WOL_PORT),
                    token=user_input.get(CONF_WOL_TOKEN) or None,
                )
                try:
                    wol_info = await wol_probe.async_test()
                except DangbeiWolClientError as err:
                    _LOGGER.info("ESP32 WOL probe failed for %s: %s", wol_host, err)
                    errors["base"] = "wol_cannot_connect"
                else:
                    try:
                        wake_response = await wol_probe.async_set_wakeup_config(
                            profile=user_input[CONF_WOL_WAKE_PROFILE],
                            custom_format=user_input[CONF_WOL_WAKE_CUSTOM_FORMAT],
                            custom_hex=user_input[CONF_WOL_WAKE_CUSTOM_HEX],
                        )
                        _apply_wake_response(user_input, wake_response)
                    except DangbeiWolUnsupportedError as err:
                        _LOGGER.info("ESP32 wake config endpoint unsupported: %s", err)
                        errors["base"] = "wol_firmware_too_old"
                    except DangbeiWolClientError as err:
                        _LOGGER.info("ESP32 WOL setup failed for %s: %s", wol_host, err)
                        errors["base"] = "wol_sync_failed"

            if not errors:
                wol_id = ""
                if wol_host:
                    wol_id = str(
                        (wol_info or {}).get(CONF_WOL_ID)
                        or (wol_info or {}).get("id")
                        or (self._discovered_wol or {}).get(CONF_WOL_ID, "")
                    )
                if wol_id:
                    user_input[CONF_WOL_ID] = wol_id
                else:
                    user_input.pop(CONF_WOL_ID, None)
                return self.async_create_entry(
                    title=f"Dangbei ({host})",
                    data=user_input,
                )

        defaults = user_input or self._discovered_wol or {}
        return self.async_show_form(
            step_id="user",
            data_schema=_user_schema(defaults),
            errors=errors,
        )

    async def async_step_zeroconf(
        self, discovery_info: zeroconf.ZeroconfServiceInfo
    ) -> FlowResult:
        """Handle ESP32 wake-up device discovered over mDNS."""
        host = discovery_info.host or ""
        port = discovery_info.port or DEFAULT_WOL_PORT
        properties = discovery_info.properties or {}
        wol_id = (
            _decode_discovery_value(properties.get("id"))
            or _decode_discovery_value(properties.get(b"id"))
            or host
        )
        _LOGGER.info("Discovered Dangbei WOL device id=%s at %s:%s", wol_id, host, port)

        existing_entry = self._find_entry_by_wol_id(wol_id)
        if existing_entry is not None:
            updated_data = {**existing_entry.data}
            updated_data[CONF_WOL_ID] = wol_id
            updated_data[CONF_WOL_HOST] = host
            updated_data[CONF_WOL_PORT] = port
            self.hass.config_entries.async_update_entry(existing_entry, data=updated_data)
            return self.async_abort(reason="already_configured")

        self._discovered_wol = {
            CONF_WOL_ID: wol_id,
            CONF_WOL_HOST: host,
            CONF_WOL_PORT: port,
            CONF_WOL_WAKE_PROFILE: DEFAULT_WOL_WAKE_PROFILE,
            CONF_WOL_WAKE_CUSTOM_FORMAT: DEFAULT_WOL_WAKE_CUSTOM_FORMAT,
            CONF_WOL_WAKE_CUSTOM_HEX: "",
        }
        self.context["title_placeholders"] = {"host": host, "id": wol_id}
        return await self.async_step_zeroconf_confirm()

    async def async_step_zeroconf_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Confirm the discovered ESP32 wake-up device before opening the form."""
        if user_input is not None:
            return await self.async_step_user()

        return self.async_show_form(
            step_id="zeroconf_confirm",
            data_schema=vol.Schema({}),
            description_placeholders=self.context.get("title_placeholders"),
        )

    def _find_entry_by_wol_id(self, wol_id: str) -> ConfigEntry | None:
        if not wol_id:
            return None
        for entry in self.hass.config_entries.async_entries(DOMAIN):
            data = {**entry.data, **entry.options}
            if data.get(CONF_WOL_ID) == wol_id:
                return entry
        return None

    @staticmethod
    @callback
    def async_get_options_flow(entry: ConfigEntry) -> OptionsFlow:
        return DangbeiOptionsFlow()


class DangbeiOptionsFlow(OptionsFlow):
    """Allow editing runtime options after setup."""

    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        errors: dict[str, str] = {}
        defaults = {**self.config_entry.data, **self.config_entry.options}

        if user_input is not None:
            user_input = _strip_user_input(user_input)

            try:
                wake_config = normalize_wake_configuration(user_input)
            except WakeConfigError as err:
                errors[err.field] = err.error_key
            else:
                user_input.update(wake_config.as_dict())

            wol_host = defaults.get(CONF_WOL_HOST, "").strip()
            if not errors and wol_host:
                session = async_get_clientsession(self.hass)
                wol_probe = DangbeiWolClient(
                    session=session,
                    host=wol_host,
                    port=defaults.get(CONF_WOL_PORT, DEFAULT_WOL_PORT),
                    token=defaults.get(CONF_WOL_TOKEN) or None,
                )
                try:
                    wake_response = await wol_probe.async_set_wakeup_config(
                        profile=user_input[CONF_WOL_WAKE_PROFILE],
                        custom_format=user_input[CONF_WOL_WAKE_CUSTOM_FORMAT],
                        custom_hex=user_input[CONF_WOL_WAKE_CUSTOM_HEX],
                    )
                    _apply_wake_response(user_input, wake_response)
                except DangbeiWolUnsupportedError as err:
                    _LOGGER.info("ESP32 wake config endpoint unsupported: %s", err)
                    errors["base"] = "wol_firmware_too_old"
                except DangbeiWolClientError as err:
                    _LOGGER.info("ESP32 wake config sync failed for %s: %s", wol_host, err)
                    errors["base"] = "wol_sync_failed"

            if not errors:
                return self.async_create_entry(title="", data=user_input)

        return self.async_show_form(
            step_id="init",
            data_schema=_options_schema(user_input or defaults),
            errors=errors,
        )


def _strip_user_input(user_input: dict[str, Any]) -> dict[str, Any]:
    cleaned = {**user_input}
    for key in (
        CONF_HOST,
        CONF_DEVICE_ID,
        CONF_TO_ID,
        CONF_FROM_ID,
        CONF_MSG_TYPE,
        CONF_WOL_HOST,
        CONF_WOL_TOKEN,
        CONF_WOL_WAKE_CUSTOM_HEX,
    ):
        if key in cleaned and isinstance(cleaned[key], str):
            cleaned[key] = cleaned[key].strip()

    cleaned.setdefault(CONF_TO_ID, DEFAULT_TO_ID)
    cleaned.setdefault(CONF_FROM_ID, DEFAULT_FROM_ID)
    cleaned.setdefault(CONF_MSG_TYPE, DEFAULT_MSG_TYPE)
    cleaned.setdefault(CONF_WOL_WAKE_PROFILE, DEFAULT_WOL_WAKE_PROFILE)
    cleaned.setdefault(CONF_WOL_WAKE_CUSTOM_FORMAT, DEFAULT_WOL_WAKE_CUSTOM_FORMAT)
    cleaned.setdefault(CONF_WOL_WAKE_CUSTOM_HEX, "")
    return cleaned


def _apply_wake_response(target: dict[str, Any], response: dict[str, Any]) -> None:
    if "wake_profile" in response or "profile" in response:
        target[CONF_WOL_WAKE_PROFILE] = str(
            response.get("wake_profile", response.get("profile", ""))
        )
    if "wake_custom_format" in response or "custom_format" in response:
        target[CONF_WOL_WAKE_CUSTOM_FORMAT] = str(
            response.get("wake_custom_format", response.get("custom_format", ""))
        )
    if "wake_custom_hex" in response or "custom_hex" in response:
        target[CONF_WOL_WAKE_CUSTOM_HEX] = str(
            response.get("wake_custom_hex", response.get("custom_hex", ""))
        )


def _decode_discovery_value(value: Any) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="ignore").strip()
    if isinstance(value, str):
        return value.strip()
    return ""
