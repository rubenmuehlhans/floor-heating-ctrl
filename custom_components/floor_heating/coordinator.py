"""Datenversorgung: holt den Gerätezustand und verteilt ihn an die Entitäten."""

from __future__ import annotations

import logging
from typing import Any

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import FloorHeatingApi, FloorHeatingError
from .const import DEFAULT_SCAN_INTERVAL, DOMAIN

_LOGGER = logging.getLogger(__name__)


class FloorHeatingCoordinator(DataUpdateCoordinator[dict[str, Any]]):
    """Fragt den Zustand ab und hält ihn für alle Entitäten bereit."""

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry, api: FloorHeatingApi) -> None:
        super().__init__(
            hass,
            _LOGGER,
            name=f"{DOMAIN} {api.host}",
            update_interval=DEFAULT_SCAN_INTERVAL,
        )
        self.api = api
        self.entry = entry

    async def _async_update_data(self) -> dict[str, Any]:
        try:
            return await self.api.async_get_state()
        except FloorHeatingError as err:
            raise UpdateFailed(str(err)) from err

    # -- Zugriff auf Teilbereiche --------------------------------------

    @property
    def device_info_raw(self) -> dict[str, Any]:
        return (self.data or {}).get("device", {})

    def room(self, room_id: int) -> dict[str, Any] | None:
        for room in (self.data or {}).get("rooms", []):
            if room["id"] == room_id:
                return room
        return None

    def channel(self, channel_id: int) -> dict[str, Any] | None:
        for ch in (self.data or {}).get("channels", []):
            if ch["id"] == channel_id:
                return ch
        return None

    async def async_command(self, coro) -> None:
        """Befehl absetzen und den Zustand unmittelbar danach neu holen.

        Ohne die sofortige Abfrage stünde bis zum nächsten Takt der alte Wert
        in der Oberfläche, was wie ein wirkungsloser Befehl aussieht.
        """
        try:
            await coro
        except FloorHeatingError as err:
            raise UpdateFailed(str(err)) from err
        await self.async_request_refresh()
