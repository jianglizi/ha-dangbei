"""WebSocket client for the Dangbei projector."""
from __future__ import annotations

import asyncio
import json
import logging
from typing import Any

import aiohttp

from .const import (
    CMD_OK,
    CMD_POWER_OFF,
    COMMAND_VALUE_MAP,
)

_LOGGER = logging.getLogger(__name__)

RECONNECT_BACKOFF_INITIAL = 1.0
RECONNECT_BACKOFF_MAX = 30.0
CONNECT_TIMEOUT = 5.0
SEND_TIMEOUT = 5.0


class DangbeiWolClientError(Exception):
    """Raised when the ESP32 wake-up device cannot satisfy a request."""


class DangbeiWolUnsupportedError(DangbeiWolClientError):
    """Raised when the ESP32 firmware lacks a required endpoint."""


class DangbeiWolValidationError(DangbeiWolClientError):
    """Raised when the ESP32 rejects a wake-profile payload."""


class DangbeiClient:
    """Maintain a WebSocket connection to the projector and send remote commands."""

    def __init__(
        self,
        session: aiohttp.ClientSession,
        host: str,
        port: int,
        device_id: str,
        to_id: str,
        from_id: str,
        msg_type: str,
        power_off_confirm: bool,
        power_off_confirm_delay: float,
    ) -> None:
        self._session = session
        self._host = host
        self._port = port
        self._device_id = device_id
        self._to_id = to_id
        self._from_id = from_id
        self._msg_type = msg_type
        self._power_off_confirm = power_off_confirm
        self._power_off_confirm_delay = power_off_confirm_delay

        self._ws: aiohttp.ClientWebSocketResponse | None = None
        self._send_lock = asyncio.Lock()
        self._connect_lock = asyncio.Lock()
        self._closed = False

    @property
    def url(self) -> str:
        return f"ws://{self._host}:{self._port}"

    @property
    def connected(self) -> bool:
        return self._ws is not None and not self._ws.closed

    async def async_connect(self) -> None:
        """Open the WebSocket. Raises aiohttp/asyncio errors on failure."""
        async with self._connect_lock:
            if self.connected:
                return
            _LOGGER.debug("Connecting to Dangbei WebSocket at %s", self.url)
            self._ws = await asyncio.wait_for(
                self._session.ws_connect(self.url, heartbeat=30),
                timeout=CONNECT_TIMEOUT,
            )
            _LOGGER.info("Connected to Dangbei projector at %s", self.url)

    async def async_close(self) -> None:
        """Close the WebSocket permanently."""
        self._closed = True
        if self._ws is not None and not self._ws.closed:
            await self._ws.close()
        self._ws = None

    def _build_payload(self, value: str) -> dict[str, Any]:
        return {
            "sn": "",
            "data": {
                "command": {
                    "value": value,
                    "params": "",
                    "command": "Operation",
                    "from": 901,
                },
                "toDeviceId": self._device_id,
            },
            "toId": self._to_id,
            "fromId": self._from_id,
            "type": self._msg_type,
        }

    async def _ensure_connected(self) -> None:
        if self.connected:
            return
        backoff = RECONNECT_BACKOFF_INITIAL
        attempt = 0
        while not self.connected and not self._closed:
            attempt += 1
            try:
                await self.async_connect()
                return
            except (aiohttp.ClientError, asyncio.TimeoutError, OSError) as err:
                if attempt >= 3:
                    raise
                _LOGGER.warning(
                    "Reconnect attempt %s to %s failed: %s; retrying in %.1fs",
                    attempt,
                    self.url,
                    err,
                    backoff,
                )
                await asyncio.sleep(backoff)
                backoff = min(backoff * 2, RECONNECT_BACKOFF_MAX)

    async def _send_value(self, value: str) -> None:
        async with self._send_lock:
            await self._ensure_connected()
            assert self._ws is not None
            payload = self._build_payload(value)
            try:
                await asyncio.wait_for(
                    self._ws.send_json(payload), timeout=SEND_TIMEOUT
                )
            except (aiohttp.ClientError, asyncio.TimeoutError, ConnectionResetError) as err:
                _LOGGER.warning("Send failed (%s); reconnecting and retrying once", err)
                await self._reset_connection()
                await self._ensure_connected()
                assert self._ws is not None
                await asyncio.wait_for(
                    self._ws.send_json(payload), timeout=SEND_TIMEOUT
                )
            _LOGGER.debug("Sent command value=%s payload=%s", value, json.dumps(payload))

    async def _reset_connection(self) -> None:
        if self._ws is not None and not self._ws.closed:
            try:
                await self._ws.close()
            except Exception:  # noqa: BLE001
                pass
        self._ws = None

    async def async_send_command(self, command: str) -> None:
        """Send a named command (e.g. "up", "ok", "power_off")."""
        if command not in COMMAND_VALUE_MAP:
            raise ValueError(f"Unknown Dangbei command: {command}")
        value = COMMAND_VALUE_MAP[command]
        await self._send_value(value)

        if command == CMD_POWER_OFF and self._power_off_confirm:
            await asyncio.sleep(self._power_off_confirm_delay)
            await self._send_value(COMMAND_VALUE_MAP[CMD_OK])

    async def async_test_connection(self) -> None:
        """Used by the config flow to verify the WebSocket is reachable."""
        ws = await asyncio.wait_for(
            self._session.ws_connect(self.url, heartbeat=None),
            timeout=CONNECT_TIMEOUT,
        )
        await ws.close()

    async def async_probe_alive(self, timeout: float = 2.0) -> bool:
        """Return True if the projector's WebSocket port accepts connections.

        The projector only listens on port 6689 when powered on, so a successful
        handshake is treated as "projector is on".
        """
        try:
            ws = await asyncio.wait_for(
                self._session.ws_connect(self.url, heartbeat=None),
                timeout=timeout,
            )
        except (aiohttp.ClientError, asyncio.TimeoutError, OSError):
            return False
        try:
            await ws.close()
        except Exception:  # noqa: BLE001
            pass
        return True


class DangbeiWolClient:
    """HTTP client for the companion esp32-dangbei-wol firmware."""

    def __init__(
        self,
        session: aiohttp.ClientSession,
        host: str,
        port: int = 80,
        token: str | None = None,
    ) -> None:
        self._session = session
        self._host = host
        self._port = port
        self._token = token.strip() if token else ""

    @property
    def base_url(self) -> str:
        return f"http://{self._host}:{self._port}"

    def _headers(self) -> dict[str, str]:
        if self._token:
            return {"Authorization": f"Bearer {self._token}"}
        return {}

    async def _async_request(
        self,
        method: str,
        path: str,
        *,
        expect_json: bool,
        json_body: dict[str, Any] | None = None,
    ) -> dict[str, Any] | None:
        try:
            async with self._session.request(
                method,
                f"{self.base_url}{path}",
                headers=self._headers(),
                json=json_body,
                timeout=aiohttp.ClientTimeout(total=5),
            ) as resp:
                body = await resp.text()
        except (aiohttp.ClientError, asyncio.TimeoutError, OSError) as err:
            raise DangbeiWolClientError(str(err)) from err

        if resp.status in (404, 501):
            detail = body.strip() or "Unsupported by current ESP32 firmware"
            raise DangbeiWolUnsupportedError(detail)
        if resp.status == 400:
            detail = body.strip() or "Invalid wake-up configuration"
            raise DangbeiWolValidationError(detail)
        if resp.status not in (200, 202):
            detail = body.strip() or f"HTTP {resp.status}"
            raise DangbeiWolClientError(detail)

        if not expect_json:
            return None

        try:
            data = json.loads(body) if body else {}
        except json.JSONDecodeError as err:
            raise DangbeiWolClientError("Invalid JSON response from ESP32") from err

        if not isinstance(data, dict):
            raise DangbeiWolClientError("Unexpected response from ESP32")
        return data

    async def async_wakeup(self) -> None:
        """Trigger a BLE wake-up broadcast on the ESP32."""
        await self._async_request("POST", "/api/wakeup", expect_json=False)

    async def async_get_info(self) -> dict[str, Any]:
        response = await self._async_request("GET", "/api/info", expect_json=True)
        assert response is not None
        return response

    async def async_set_wakeup_config(
        self,
        *,
        profile: str,
        custom_format: str,
        custom_hex: str,
    ) -> dict[str, Any]:
        """Persist the active wake-profile configuration on the ESP32."""
        response = await self._async_request(
            "POST",
            "/api/wakeup_config",
            expect_json=True,
            json_body={
                "profile": profile,
                "custom_format": custom_format,
                "custom_hex": custom_hex,
            },
        )
        assert response is not None
        return response

    async def async_test(self) -> dict[str, Any]:
        """Used by the config flow to verify the ESP32 is reachable."""
        return await self.async_get_info()
