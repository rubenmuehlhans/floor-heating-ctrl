"""Ein Ventil je Heizkreis.

Öffnen und Schließen sind Notfahrten: sie fahren bis zum Anschlag,
unabhängig von der geschätzten Stellung, und lassen den Kreis danach im
Handbetrieb stehen. Eine gesetzte Stellung überschreibt die Regelung dagegen
beim nächsten Durchlauf — das entspricht dem Verhalten der Firmware und ist in
den Zusatzangaben der Entität ablesbar.
"""

from __future__ import annotations

from typing import Any

from homeassistant.components.cover import (
    ATTR_POSITION,
    CoverDeviceClass,
    CoverEntity,
    CoverEntityFeature,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .coordinator import FloorHeatingCoordinator
from .entity import FloorHeatingEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: FloorHeatingCoordinator = hass.data[DOMAIN][entry.entry_id]
    channels = (coordinator.data or {}).get("channels", [])
    async_add_entities(FloorHeatingCover(coordinator, c["id"]) for c in channels)


class FloorHeatingCover(FloorHeatingEntity, CoverEntity):
    """Ventil eines Heizkreises."""

    _attr_device_class = CoverDeviceClass.DAMPER
    _attr_supported_features = (
        CoverEntityFeature.OPEN
        | CoverEntityFeature.CLOSE
        | CoverEntityFeature.STOP
        | CoverEntityFeature.SET_POSITION
    )

    def __init__(self, coordinator: FloorHeatingCoordinator, channel: int) -> None:
        super().__init__(coordinator, f"cover{channel}")
        self._channel = channel
        self._attr_name = f"Heizkreis {channel}"

    @property
    def _ch(self) -> dict[str, Any] | None:
        return self.coordinator.channel(self._channel)

    @property
    def available(self) -> bool:
        return super().available and self._ch is not None

    @property
    def current_cover_position(self) -> int | None:
        ch = self._ch
        if not ch:
            return None
        return round(ch["position"] * 100)

    @property
    def is_closed(self) -> bool | None:
        pos = self.current_cover_position
        return None if pos is None else pos <= 0

    @property
    def is_opening(self) -> bool:
        ch = self._ch
        return bool(ch and ch.get("op") == "opening")

    @property
    def is_closing(self) -> bool:
        ch = self._ch
        return bool(ch and ch.get("op") == "closing")

    @property
    def extra_state_attributes(self) -> dict[str, Any] | None:
        ch = self._ch
        if not ch:
            return None
        return {
            "messgruppe": ch.get("group"),
            "handbetrieb": ch.get("manual"),
            "stellung_bekannt": ch.get("known"),
            "kalibriert": ch.get("calibrated"),
            "gegenspannung_mv": ch.get("bemf_mv"),
            "wartet_auf_gruppe": ch.get("pending"),
            "letzter_halt": ch.get("last_stop"),
        }

    async def async_open_cover(self, **kwargs: Any) -> None:
        await self.coordinator.async_command(
            self.coordinator.api.async_channel_cmd(self._channel, "open")
        )

    async def async_close_cover(self, **kwargs: Any) -> None:
        await self.coordinator.async_command(
            self.coordinator.api.async_channel_cmd(self._channel, "close")
        )

    async def async_stop_cover(self, **kwargs: Any) -> None:
        await self.coordinator.async_command(
            self.coordinator.api.async_channel_cmd(self._channel, "stop")
        )

    async def async_set_cover_position(self, **kwargs: Any) -> None:
        position = kwargs.get(ATTR_POSITION)
        if position is None:
            return
        await self.coordinator.async_command(
            self.coordinator.api.async_set_channel_position(self._channel, position / 100)
        )
