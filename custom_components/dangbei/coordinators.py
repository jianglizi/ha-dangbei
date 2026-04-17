"""Coordinators for projector and ESP32 availability."""
from __future__ import annotations

import asyncio
import logging
from datetime import timedelta

from homeassistant.core import HomeAssistant
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator

from .client import DangbeiClient, DangbeiWolClient
from .const import (
    DOMAIN,
    FAST_STATUS_POLL_INTERVAL,
    MIN_STATUS_POLL_INTERVAL,
    REQUIRED_CONFIRM_MATCHES,
    TURN_OFF_CONFIRM_TIMEOUT,
    TURN_ON_CONFIRM_TIMEOUT,
)

_LOGGER = logging.getLogger(__name__)


class ProjectorPowerCoordinator(DataUpdateCoordinator[bool]):
    """Track actual projector power and temporary target state after actions."""

    def __init__(
        self,
        hass: HomeAssistant,
        client: DangbeiClient,
        poll_interval: int,
    ) -> None:
        self._client = client
        self._pending_target: bool | None = None
        self._pending_matches = 0
        self._confirmation_task: asyncio.Task[None] | None = None
        super().__init__(
            hass,
            _LOGGER,
            name=f"{DOMAIN}_projector_online",
            update_interval=timedelta(
                seconds=max(poll_interval, MIN_STATUS_POLL_INTERVAL)
            ),
            update_method=self._async_update_actual_state,
            always_update=True,
        )

    @property
    def actual_state(self) -> bool:
        """Return the most recently probed projector state."""
        return bool(self.data)

    @property
    def pending_target(self) -> bool | None:
        """Return the temporary target state during a confirmation window."""
        return self._pending_target

    @property
    def effective_state(self) -> bool:
        """Return the user-facing power state, with pending target override."""
        if self._pending_target is not None:
            return self._pending_target
        return self.actual_state

    async def async_begin_transition(self, target: bool) -> None:
        """Enter a fast confirmation window after a power action."""
        self._pending_target = target
        self._pending_matches = 0
        self._cancel_confirmation_task()
        self.async_update_listeners()
        timeout = TURN_ON_CONFIRM_TIMEOUT if target else TURN_OFF_CONFIRM_TIMEOUT
        self._confirmation_task = self.hass.async_create_task(
            self._async_run_confirmation_window(timeout)
        )

    async def async_shutdown(self) -> None:
        """Cancel any background confirmation task."""
        self._cancel_confirmation_task()

    async def _async_update_actual_state(self) -> bool:
        actual_state = await self._client.async_probe_alive()
        self._reconcile_pending_target(actual_state)
        return actual_state

    def _reconcile_pending_target(self, actual_state: bool) -> None:
        if self._pending_target is None:
            return

        if actual_state == self._pending_target:
            self._pending_matches += 1
            if self._pending_matches >= REQUIRED_CONFIRM_MATCHES:
                self._pending_target = None
                self._pending_matches = 0
        else:
            self._pending_matches = 0

    async def _async_run_confirmation_window(self, timeout_seconds: int) -> None:
        current_task = asyncio.current_task()
        try:
            await self.async_request_refresh()

            deadline = self.hass.loop.time() + timeout_seconds
            while self._pending_target is not None and self.hass.loop.time() < deadline:
                await asyncio.sleep(FAST_STATUS_POLL_INTERVAL)
                await self.async_request_refresh()

            if self._pending_target is not None:
                self._pending_target = None
                self._pending_matches = 0
                self.async_update_listeners()
        except asyncio.CancelledError:
            raise
        finally:
            if self._confirmation_task is current_task:
                self._confirmation_task = None

    def _cancel_confirmation_task(self) -> None:
        if self._confirmation_task is not None:
            self._confirmation_task.cancel()
            self._confirmation_task = None


class Esp32OnlineCoordinator(DataUpdateCoordinator[bool]):
    """Track whether the companion ESP32 HTTP API is reachable."""

    def __init__(
        self,
        hass: HomeAssistant,
        wol_client: DangbeiWolClient,
        poll_interval: int,
    ) -> None:
        self._wol_client = wol_client
        super().__init__(
            hass,
            _LOGGER,
            name=f"{DOMAIN}_esp32_online",
            update_interval=timedelta(
                seconds=max(poll_interval, MIN_STATUS_POLL_INTERVAL)
            ),
            update_method=self._async_update_online,
            always_update=True,
        )

    async def _async_update_online(self) -> bool:
        try:
            await self._wol_client.async_get_info()
        except Exception:  # noqa: BLE001
            return False
        return True
