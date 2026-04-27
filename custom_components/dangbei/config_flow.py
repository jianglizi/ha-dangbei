"""Config flow for the Dangbei Projector integration."""
from __future__ import annotations

import asyncio
import logging
from typing import Any

import voluptuous as vol
from zeroconf import ServiceStateChange

from homeassistant.components import zeroconf
from homeassistant.config_entries import ConfigEntry, ConfigFlow, OptionsFlow
from homeassistant.const import CONF_HOST, CONF_PORT
from homeassistant.core import callback
from homeassistant.data_entry_flow import FlowResult
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .client import (
    CannotDiscover,
    DangbeiWolClient,
    DangbeiWolClientError,
    discover_device,
)
from .const import (
    CONF_BLUETOOTH_MAC,
    CONF_DEVICE_ID,
    CONF_DEVICE_MODEL,
    CONF_DEVICE_NAME,
    CONF_FROM_ID,
    CONF_HOST_NAME,
    CONF_MAC,
    CONF_MSG_TYPE,
    CONF_POWER_OFF_CONFIRM,
    CONF_POWER_OFF_CONFIRM_DELAY,
    CONF_ROM_VERSION,
    CONF_SN,
    CONF_STATUS_POLL_INTERVAL,
    CONF_TO_ID,
    CONF_WIFI_MAC,
    CONF_WOL_HOST,
    CONF_WOL_ID,
    CONF_WOL_PORT,
    CONF_WOL_TOKEN,
    DEFAULT_FROM_ID,
    DEFAULT_MSG_TYPE,
    DEFAULT_PORT,
    DEFAULT_POWER_OFF_CONFIRM,
    DEFAULT_POWER_OFF_CONFIRM_DELAY,
    DEFAULT_STATUS_POLL_INTERVAL,
    DEFAULT_TO_ID,
    DEFAULT_WOL_PORT,
    DOMAIN,
    MAX_STATUS_POLL_INTERVAL,
    MIN_STATUS_POLL_INTERVAL,
)

_LOGGER = logging.getLogger(__name__)


class DangbeiConfigFlow(ConfigFlow, domain=DOMAIN):
    """Handle the initial setup via UI."""

    VERSION = 3

    def __init__(self) -> None:
        self._discovered_info: dict[str, Any] | None = None
        self._discovered_projector: dict[str, Any] | None = None
        self._discovered_projectors: list[dict[str, Any]] = []
        self._discovered_wol: dict[str, Any] | None = None

    async def _scan_projectors(self) -> list[dict[str, Any]]:
        """Actively scan the LAN for projectors via mDNS (_db_controller._tcp)."""
        import socket as _socket
        from zeroconf.asyncio import AsyncServiceBrowser, AsyncServiceInfo

        results: dict[str, dict[str, Any]] = {}
        pending: set[asyncio.Task] = set()
        aiozc = await zeroconf.async_get_async_instance(self.hass)

        async def _handle_service(service_type: str, name: str) -> None:
            info = AsyncServiceInfo(service_type, name)
            if not await info.async_request(aiozc.zeroconf, 3000):
                _LOGGER.debug("mDNS: no info returned for %s", name)
                return

            props: dict[str, str] = {}
            for key, val in (info.properties or {}).items():
                k = key.decode("utf-8", errors="ignore") if isinstance(key, bytes) else str(key)
                v = val.decode("utf-8", errors="ignore") if isinstance(val, bytes) else str(val)
                props[k] = v

            device_id = props.get("deviceId") or props.get("dId") or ""
            if not device_id:
                return

            host = ""
            if info.server:
                host = info.server.rstrip(".")
            if not host and info.addresses:
                for addr in info.addresses:
                    if len(addr) == 4:
                        host = _socket.inet_ntoa(addr)
                        break

            port = info.port or DEFAULT_PORT
            model = props.get("deviceModel") or props.get("dModel") or ""
            dev_name = props.get("deviceName") or props.get("dName") or ""

            if device_id not in results:
                results[device_id] = {
                    "device_id": device_id,
                    "host": host,
                    "port": port,
                    "model": model,
                    "name": dev_name,
                }
                _LOGGER.warning(
                    "mDNS scan found projector %s (%s) at %s:%s",
                    dev_name, model, host, port,
                )

        # Signal.fire() is synchronous — handler must be a plain function.
        # Schedule async work via ensure_future so it runs on the event loop.
        def on_service_change(
            zeroconf: Any,  # noqa: ARG001
            service_type: str,
            name: str,
            state_change: ServiceStateChange,
        ) -> None:
            if state_change not in (ServiceStateChange.Added, ServiceStateChange.Updated):
                return
            task = asyncio.ensure_future(_handle_service(service_type, name))
            pending.add(task)
            task.add_done_callback(pending.discard)

        _LOGGER.warning("mDNS scan: starting scan for _db_controller._tcp")
        browser = AsyncServiceBrowser(
            aiozc.zeroconf,
            "_db_controller._tcp.local.",
            handlers=[on_service_change],
        )
        try:
            await asyncio.sleep(4)
            if pending:
                await asyncio.gather(*pending, return_exceptions=True)
        finally:
            await browser.async_cancel()

        _LOGGER.warning("mDNS scan: finished, found %d projector(s)", len(results))
        return list(results.values())

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        errors: dict[str, str] = {}

        # Scan for projectors on first show
        if not self._discovered_projectors and user_input is None:
            self._discovered_projectors = await self._scan_projectors()

        if user_input is not None:
            selected = user_input.get("projector_select", "manual")
            port = user_input.get(CONF_PORT, DEFAULT_PORT)

            if selected != "manual":
                # User selected a discovered projector
                proj = next(
                    (p for p in self._discovered_projectors if p["device_id"] == selected),
                    None,
                )
                if proj:
                    host = proj["host"]
                    port = proj["port"]
                else:
                    errors["base"] = "cannot_connect"
            else:
                host = user_input.get(CONF_HOST, "").strip()
                if not host:
                    errors["base"] = "host_required"

            if not errors:
                try:
                    info = await discover_device(
                        async_get_clientsession(self.hass), host, port
                    )
                except CannotDiscover as err:
                    _LOGGER.info("Dangbei 112 discovery failed for %s:%s: %s", host, port, err)
                    errors["base"] = "cannot_connect"
                except Exception as err:  # noqa: BLE001
                    _LOGGER.exception("Unexpected error during Dangbei discovery")
                    errors["base"] = "cannot_connect"

            if not errors:
                device_id = info.get("device_id", "")
                if device_id:
                    await self.async_set_unique_id(device_id)
                    self._abort_if_unique_id_configured()

                info["_host"] = host
                info["_port"] = port
                self._discovered_info = info
                self.context["title_placeholders"] = {
                    "device_name": info.get("device_name") or host,
                    "device_model": info.get("device_model") or "",
                }
                return await self.async_step_confirm()

        # Build select options
        options: dict[str, str] = {}
        for proj in self._discovered_projectors:
            label = f"{proj['host']} - {proj['model'] or proj['name'] or proj['device_id'][:8]}"
            options[proj["device_id"]] = label
        options["manual"] = "Manual input (type IP below)"

        previous = user_input or {}
        schema: dict[Any, Any] = {
            vol.Required(
                "projector_select",
                default=previous.get("projector_select", "manual"),
            ): vol.In(options),
            vol.Optional(
                CONF_HOST,
                default=previous.get(CONF_HOST, ""),
            ): str,
            vol.Optional(
                CONF_PORT,
                default=previous.get(CONF_PORT, DEFAULT_PORT),
            ): int,
        }

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(schema),
            errors=errors,
        )

    async def async_step_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Show discovered device info and ask for confirmation."""
        if user_input is not None:
            return await self.async_step_setup()

        info = self._discovered_info or {}
        desc = (
            f"**{info.get('device_name', '')}** ({info.get('device_model', '')})\n"
            f"- Device ID: `{info.get('device_id', '')}`\n"
            f"- Bluetooth MAC: `{info.get('bluetooth_mac', '')}`\n"
            f"- MAC: `{info.get('mac', '')}`\n"
            f"- ROM: `{info.get('rom_version', '')}`"
        )
        return self.async_show_form(
            step_id="confirm",
            data_schema=vol.Schema({}),
            description_placeholders={"info": desc},
        )

    async def async_step_setup(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Optional ESP32 configuration before entry creation."""
        errors: dict[str, str] = {}

        if user_input is not None:
            wol_host = (user_input.get(CONF_WOL_HOST) or "").strip()
            wol_port = user_input.get(CONF_WOL_PORT, DEFAULT_WOL_PORT)
            wol_token = (user_input.get(CONF_WOL_TOKEN) or "").strip()
            power_off_confirm = user_input.get(
                CONF_POWER_OFF_CONFIRM, DEFAULT_POWER_OFF_CONFIRM
            )
            power_off_confirm_delay = user_input.get(
                CONF_POWER_OFF_CONFIRM_DELAY, DEFAULT_POWER_OFF_CONFIRM_DELAY
            )

            info = self._discovered_info or {}
            data: dict[str, Any] = {
                CONF_HOST: info.get("_host", ""),
                CONF_PORT: info.get("_port", DEFAULT_PORT),
                CONF_DEVICE_ID: info["device_id"],
                CONF_BLUETOOTH_MAC: info.get("bluetooth_mac", ""),
                CONF_DEVICE_NAME: info.get("device_name", ""),
                CONF_DEVICE_MODEL: info.get("device_model", ""),
                CONF_HOST_NAME: info.get("host_name", ""),
                CONF_MAC: info.get("mac", ""),
                CONF_WIFI_MAC: info.get("wifi_mac", ""),
                CONF_ROM_VERSION: info.get("rom_version", ""),
                CONF_SN: info.get("sn", ""),
                CONF_TO_ID: DEFAULT_TO_ID,
                CONF_FROM_ID: DEFAULT_FROM_ID,
                CONF_MSG_TYPE: DEFAULT_MSG_TYPE,
                CONF_POWER_OFF_CONFIRM: power_off_confirm,
                CONF_POWER_OFF_CONFIRM_DELAY: power_off_confirm_delay,
            }

            if wol_host:
                data[CONF_WOL_HOST] = wol_host
                data[CONF_WOL_PORT] = wol_port
                if wol_token:
                    data[CONF_WOL_TOKEN] = wol_token

                session = async_get_clientsession(self.hass)
                wol_client = DangbeiWolClient(
                    session=session,
                    host=wol_host,
                    port=wol_port,
                    token=wol_token or None,
                )
                try:
                    wol_info = await wol_client.async_test()
                    wol_id = str(
                        wol_info.get("id") or wol_info.get("device_id") or wol_host
                    )
                    data[CONF_WOL_ID] = wol_id

                    # Auto-push bluetooth MAC to ESP32
                    bt_mac = info.get("bluetooth_mac", "")
                    if bt_mac:
                        try:
                            await wol_client.async_push_wake_config(
                                bluetooth_mac=bt_mac
                            )
                            _LOGGER.info(
                                "Pushed bluetooth_mac=%s to ESP32 at %s",
                                bt_mac,
                                wol_host,
                            )
                        except DangbeiWolClientError as err:
                            _LOGGER.warning(
                                "Failed to push wake config to ESP32: %s", err
                            )
                except DangbeiWolClientError as err:
                    _LOGGER.info("ESP32 probe failed for %s: %s", wol_host, err)
                    errors["base"] = "wol_cannot_connect"

            if not errors:
                return self.async_create_entry(
                    title=f"Dangbei ({data[CONF_HOST]})",
                    data=data,
                )

        return self.async_show_form(
            step_id="setup",
            data_schema=vol.Schema(
                {
                    vol.Optional(
                        CONF_WOL_HOST,
                        default=(user_input or self._discovered_wol or {}).get(
                            CONF_WOL_HOST, ""
                        ),
                    ): str,
                    vol.Optional(
                        CONF_WOL_PORT,
                        default=(user_input or self._discovered_wol or {}).get(
                            CONF_WOL_PORT, DEFAULT_WOL_PORT
                        ),
                    ): int,
                    vol.Optional(
                        CONF_WOL_TOKEN,
                        default=(user_input or {}).get(CONF_WOL_TOKEN, ""),
                    ): str,
                    vol.Optional(
                        CONF_POWER_OFF_CONFIRM,
                        default=(
                            (user_input or {}).get(
                                CONF_POWER_OFF_CONFIRM, DEFAULT_POWER_OFF_CONFIRM
                            )
                        ),
                    ): bool,
                    vol.Optional(
                        CONF_POWER_OFF_CONFIRM_DELAY,
                        default=(
                            (user_input or {}).get(
                                CONF_POWER_OFF_CONFIRM_DELAY,
                                DEFAULT_POWER_OFF_CONFIRM_DELAY,
                            )
                        ),
                    ): vol.All(vol.Coerce(float), vol.Range(min=0.0, max=10.0)),
                }
            ),
            errors=errors,
        )

    async def async_step_zeroconf(
        self, discovery_info: zeroconf.ZeroconfServiceInfo
    ) -> FlowResult:
        """Handle mDNS discovery for both projectors and ESP32 devices."""
        svc_type = (discovery_info.type or "").lower()
        if "_db_controller._tcp" in svc_type:
            return await self._async_zeroconf_projector(discovery_info)
        if "_dangbei-wol._tcp" in svc_type:
            return await self._async_zeroconf_esp32(discovery_info)
        return self.async_abort(reason="not_dangbei_device")

    async def _async_zeroconf_projector(
        self, discovery_info: zeroconf.ZeroconfServiceInfo
    ) -> FlowResult:
        """Handle projector discovered via mDNS (_db_controller._tcp)."""
        host = discovery_info.host or ""
        port = discovery_info.port or DEFAULT_PORT
        properties = discovery_info.properties or {}

        device_id = (
            _decode_discovery_value(properties.get("dId"))
            or _decode_discovery_value(properties.get(b"dId"))
            or _decode_discovery_value(properties.get("deviceId"))
            or _decode_discovery_value(properties.get(b"deviceId"))
        )

        if not device_id:
            _LOGGER.debug("mDNS projector at %s:%s missing deviceId, ignoring", host, port)
            return self.async_abort(reason="incomplete_device_info")

        # Check if already configured
        existing = self._find_entry_by_device_id(device_id)
        if existing is not None:
            # Update host/port in case they changed
            updated_data = {**existing.data}
            updated_data[CONF_HOST] = host
            updated_data[CONF_PORT] = port
            self.hass.config_entries.async_update_entry(existing, data=updated_data)
            return self.async_abort(reason="already_configured")

        device_name = (
            _decode_discovery_value(properties.get("dName"))
            or _decode_discovery_value(properties.get(b"dName"))
            or _decode_discovery_value(properties.get("deviceName"))
            or _decode_discovery_value(properties.get(b"deviceName"))
            or host
        )
        device_model = (
            _decode_discovery_value(properties.get("dModel"))
            or _decode_discovery_value(properties.get(b"dModel"))
            or _decode_discovery_value(properties.get("deviceModel"))
            or _decode_discovery_value(properties.get(b"deviceModel"))
        )
        mac = (
            _decode_discovery_value(properties.get("mac"))
            or _decode_discovery_value(properties.get(b"mac"))
        )
        rom_version = (
            _decode_discovery_value(properties.get("romV"))
            or _decode_discovery_value(properties.get(b"romV"))
            or _decode_discovery_value(properties.get("romVersion"))
            or _decode_discovery_value(properties.get(b"romVersion"))
        )
        sn = (
            _decode_discovery_value(properties.get("sn"))
            or _decode_discovery_value(properties.get(b"sn"))
        )

        self._discovered_projector = {
            "_host": host,
            "_port": port,
            "device_id": device_id,
            "device_name": device_name,
            "device_model": device_model,
            "mac": mac,
            "rom_version": rom_version,
            "sn": sn,
        }

        _LOGGER.info(
            "Discovered Dangbei projector %s (%s) at %s:%s, deviceId=%s",
            device_name, device_model, host, port, device_id,
        )

        self.context["title_placeholders"] = {
            "device_name": device_name,
            "device_model": device_model,
            "host": host,
        }
        return await self.async_step_projector_confirm()

    async def async_step_projector_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Confirm discovered projector, then run 112 handshake for full info."""
        if user_input is not None:
            info = self._discovered_projector or {}
            host = info.get("_host", "")
            port = info.get("_port", DEFAULT_PORT)

            # Run 112 handshake to get full device info (bluetoothMac, etc.)
            try:
                full_info = await discover_device(
                    async_get_clientsession(self.hass), host, port
                )
                # Merge: mDNS info as base, 112 handshake overrides/enriches
                info.update(full_info)
                info["_host"] = host
                info["_port"] = port
            except CannotDiscover as err:
                _LOGGER.warning(
                    "112 handshake failed after mDNS discovery for %s: %s", host, err
                )
                # Continue with mDNS-only info (no bluetoothMac)

            self._discovered_info = info
            device_id = info.get("device_id", "")
            if device_id:
                await self.async_set_unique_id(device_id)
                self._abort_if_unique_id_configured()
            return await self.async_step_setup()

        info = self._discovered_projector or {}
        desc = (
            f"**{info.get('device_name', '')}** ({info.get('device_model', '')})\n"
            f"- IP: `{info.get('_host', '')}:{info.get('_port', '')}`\n"
            f"- Device ID: `{info.get('device_id', '')}`\n"
            f"- MAC: `{info.get('mac', '')}`\n"
            f"- ROM: `{info.get('rom_version', '')}`"
        )
        return self.async_show_form(
            step_id="projector_confirm",
            data_schema=vol.Schema({}),
            description_placeholders={"info": desc},
        )

    async def _async_zeroconf_esp32(
        self, discovery_info: zeroconf.ZeroconfServiceInfo
    ) -> FlowResult:
        """Handle ESP32 wake-up device discovered via mDNS (_dangbei-wol._tcp)."""
        host = discovery_info.host or ""
        port = discovery_info.port or DEFAULT_WOL_PORT
        properties = discovery_info.properties or {}
        wol_id = (
            _decode_discovery_value(properties.get("id"))
            or _decode_discovery_value(properties.get(b"id"))
            or host
        )
        _LOGGER.info(
            "Discovered Dangbei WOL device id=%s at %s:%s", wol_id, host, port
        )

        existing_entry = self._find_entry_by_wol_id(wol_id)
        if existing_entry is not None:
            updated_data = {**existing_entry.data}
            updated_data[CONF_WOL_ID] = wol_id
            updated_data[CONF_WOL_HOST] = host
            updated_data[CONF_WOL_PORT] = port
            self.hass.config_entries.async_update_entry(
                existing_entry, data=updated_data
            )
            return self.async_abort(reason="already_configured")

        self._discovered_wol = {
            CONF_WOL_ID: wol_id,
            CONF_WOL_HOST: host,
            CONF_WOL_PORT: port,
        }
        self.context["title_placeholders"] = {"host": host, "id": wol_id}
        return await self.async_step_zeroconf_confirm()

    async def async_step_zeroconf_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Confirm the discovered ESP32 wake-up device before opening the form."""
        if user_input is not None:
            return await self._async_step_user_impl()

        return self.async_show_form(
            step_id="zeroconf_confirm",
            data_schema=vol.Schema({}),
            description_placeholders=self.context.get("title_placeholders"),
        )

    def _find_entry_by_device_id(self, device_id: str) -> ConfigEntry | None:
        if not device_id:
            return None
        for entry in self.hass.config_entries.async_entries(DOMAIN):
            data = {**entry.data, **entry.options}
            if data.get(CONF_DEVICE_ID) == device_id:
                return entry
        return None

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
            wol_host = (user_input.get(CONF_WOL_HOST) or "").strip()
            wol_port = user_input.get(CONF_WOL_PORT, DEFAULT_WOL_PORT)
            wol_token = (user_input.get(CONF_WOL_TOKEN) or "").strip()
            power_off_confirm = user_input.get(
                CONF_POWER_OFF_CONFIRM, DEFAULT_POWER_OFF_CONFIRM
            )
            power_off_confirm_delay = user_input.get(
                CONF_POWER_OFF_CONFIRM_DELAY, DEFAULT_POWER_OFF_CONFIRM_DELAY
            )
            poll_interval = user_input.get(
                CONF_STATUS_POLL_INTERVAL, DEFAULT_STATUS_POLL_INTERVAL
            )

            # Advanced overrides
            to_id = (user_input.get(CONF_TO_ID) or "").strip() or DEFAULT_TO_ID
            from_id = (user_input.get(CONF_FROM_ID) or "").strip() or DEFAULT_FROM_ID
            msg_type = (user_input.get(CONF_MSG_TYPE) or "").strip() or DEFAULT_MSG_TYPE
            device_id_override = (user_input.get(CONF_DEVICE_ID) or "").strip()

            result_data: dict[str, Any] = {
                **self.config_entry.data,
                CONF_POWER_OFF_CONFIRM: power_off_confirm,
                CONF_POWER_OFF_CONFIRM_DELAY: power_off_confirm_delay,
                CONF_STATUS_POLL_INTERVAL: poll_interval,
                CONF_TO_ID: to_id,
                CONF_FROM_ID: from_id,
                CONF_MSG_TYPE: msg_type,
            }

            if device_id_override:
                result_data[CONF_DEVICE_ID] = device_id_override

            if wol_host:
                result_data[CONF_WOL_HOST] = wol_host
                result_data[CONF_WOL_PORT] = wol_port
                if wol_token:
                    result_data[CONF_WOL_TOKEN] = wol_token
                else:
                    result_data.pop(CONF_WOL_TOKEN, None)

                # Try pushing bluetooth MAC to ESP32
                bt_mac = result_data.get(CONF_BLUETOOTH_MAC, "")
                if bt_mac:
                    session = async_get_clientsession(self.hass)
                    wol_client = DangbeiWolClient(
                        session=session,
                        host=wol_host,
                        port=wol_port,
                        token=wol_token or None,
                    )
                    try:
                        await wol_client.async_push_wake_config(
                            bluetooth_mac=bt_mac
                        )
                    except DangbeiWolClientError as err:
                        _LOGGER.warning(
                            "Failed to push wake config to ESP32: %s", err
                        )
            else:
                result_data.pop(CONF_WOL_HOST, None)
                result_data.pop(CONF_WOL_PORT, None)
                result_data.pop(CONF_WOL_TOKEN, None)
                result_data.pop(CONF_WOL_ID, None)

            if not errors:
                return self.async_create_entry(title="", data=result_data)

        return self.async_show_form(
            step_id="init",
            data_schema=vol.Schema(
                {
                    vol.Optional(
                        CONF_WOL_HOST,
                        default=defaults.get(CONF_WOL_HOST, ""),
                    ): str,
                    vol.Optional(
                        CONF_WOL_PORT,
                        default=defaults.get(CONF_WOL_PORT, DEFAULT_WOL_PORT),
                    ): int,
                    vol.Optional(
                        CONF_WOL_TOKEN,
                        default=defaults.get(CONF_WOL_TOKEN, ""),
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
                            CONF_POWER_OFF_CONFIRM_DELAY,
                            DEFAULT_POWER_OFF_CONFIRM_DELAY,
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
                    vol.Optional(
                        CONF_DEVICE_ID,
                        default=defaults.get(CONF_DEVICE_ID, ""),
                    ): str,
                    vol.Optional(
                        CONF_TO_ID,
                        default=defaults.get(CONF_TO_ID, DEFAULT_TO_ID),
                    ): str,
                    vol.Optional(
                        CONF_FROM_ID,
                        default=defaults.get(CONF_FROM_ID, DEFAULT_FROM_ID),
                    ): str,
                    vol.Optional(
                        CONF_MSG_TYPE,
                        default=defaults.get(CONF_MSG_TYPE, DEFAULT_MSG_TYPE),
                    ): str,
                }
            ),
            errors=errors,
        )


def _decode_discovery_value(value: Any) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="ignore").strip()
    if isinstance(value, str):
        return value.strip()
    return ""
