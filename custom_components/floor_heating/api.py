"""Zugriff auf die JSON-Schnittstelle der Ventilsteuerung.

Bewusst frei von Home-Assistant-Abhängigkeiten, damit sich der Zugriff auch
ohne laufende Installation prüfen lässt (siehe tools/check_api.py im
Firmware-Repo).
"""

from __future__ import annotations

import asyncio
import logging
from typing import Any

import aiohttp

_LOGGER = logging.getLogger(__name__)

TIMEOUT = aiohttp.ClientTimeout(total=10)


class FloorHeatingError(Exception):
    """Das Gerät war nicht erreichbar oder hat die Anfrage abgelehnt."""


class FloorHeatingAuthError(FloorHeatingError):
    """Platzhalter: die Firmware kennt derzeit keine Anmeldung."""


class FloorHeatingApi:
    """Schmaler Zugriff auf die Schnittstelle eines Geräts."""

    def __init__(self, host: str, session: aiohttp.ClientSession) -> None:
        self._host = host.rstrip("/")
        self._session = session
        self._etag: str | None = None
        self._cached: dict[str, Any] | None = None

    @property
    def host(self) -> str:
        return self._host

    def _url(self, path: str) -> str:
        return f"http://{self._host}{path}"

    async def _request(
        self, method: str, path: str, *, json: Any = None, headers: dict[str, str] | None = None
    ) -> tuple[int, Any, dict[str, str]]:
        try:
            async with self._session.request(
                method, self._url(path), json=json, headers=headers, timeout=TIMEOUT
            ) as resp:
                if resp.status == 304:
                    return 304, None, dict(resp.headers)
                body: Any = None
                if resp.content_type == "application/json":
                    body = await resp.json()
                else:
                    body = await resp.text()
                if resp.status >= 400:
                    detail = body.get("error") if isinstance(body, dict) else str(body)[:120]
                    raise FloorHeatingError(f"{resp.status}: {detail}")
                return resp.status, body, dict(resp.headers)
        except asyncio.TimeoutError as err:
            raise FloorHeatingError("Zeitüberschreitung") from err
        except aiohttp.ClientError as err:
            raise FloorHeatingError(str(err)) from err

    async def async_get_state(self) -> dict[str, Any]:
        """Gerätezustand holen.

        Nutzt den Änderungszähler des Geräts: hat sich nichts geändert und
        fährt kein Ventil, antwortet es mit 304 und der zuletzt geholte Stand
        gilt weiter.
        """
        headers = {"If-None-Match": self._etag} if self._etag else None
        status, body, resp_headers = await self._request("GET", "/api/state", headers=headers)

        if status == 304 and self._cached is not None:
            return self._cached

        if not isinstance(body, dict):
            raise FloorHeatingError("Unerwartete Antwort auf /api/state")

        self._etag = resp_headers.get("ETag")
        self._cached = body
        return body

    async def async_get_config(self) -> dict[str, Any]:
        _, body, _ = await self._request("GET", "/api/config")
        if not isinstance(body, dict):
            raise FloorHeatingError("Unerwartete Antwort auf /api/config")
        return body

    # -- Räume ---------------------------------------------------------

    async def async_set_room_target(self, room_id: int, target_c: float) -> None:
        await self._request("POST", f"/api/room/{room_id}/target", json={"target_c": target_c})

    async def async_set_room_mode(self, room_id: int, heat: bool) -> None:
        await self._request(
            "POST", f"/api/room/{room_id}/mode", json={"mode": "heat" if heat else "off"}
        )

    async def async_room_check_now(self, room_id: int) -> None:
        await self._request("POST", f"/api/room/{room_id}/check", json={})

    # -- Heizkreise ----------------------------------------------------

    async def async_channel_cmd(self, channel: int | str, cmd: str, **kwargs: Any) -> None:
        payload: dict[str, Any] = {"cmd": cmd}
        payload.update(kwargs)
        await self._request("POST", f"/api/channel/{channel}/cmd", json=payload)

    async def async_set_channel_position(self, channel: int, position: float) -> None:
        """Stellung 0 bis 1. Die Regelung überschreibt sie beim nächsten Durchlauf."""
        await self.async_channel_cmd(channel, "position", position=position)

    # -- Messfahrt -----------------------------------------------------

    async def async_calibrate_start(self, channel: int) -> None:
        await self._request("POST", f"/api/calib/{channel}/start", json={})

    async def async_calibrate_accept(self) -> None:
        await self._request("POST", "/api/calib/accept", json={})

    async def async_calibrate_abort(self) -> None:
        await self._request("POST", "/api/calib/abort", json={})

    # -- System --------------------------------------------------------

    async def async_restart(self) -> None:
        await self._request("POST", "/api/system/restart", json={})
