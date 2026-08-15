"""Zustände, die entweder zutreffen oder nicht."""

from __future__ import annotations

from typing import Any

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .coordinator import FloorHeatingCoordinator
from .entity import FloorHeatingEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: FloorHeatingCoordinator = hass.data[DOMAIN][entry.entry_id]
    state = coordinator.data or {}

    entities: list[BinarySensorEntity] = [FloorHeatingCalibrationRunning(coordinator)]
    for ch in state.get("channels", []):
        entities.append(FloorHeatingManualHold(coordinator, ch["id"]))
    for room in state.get("rooms", []):
        entities.append(FloorHeatingRoomStale(coordinator, room["id"]))

    async_add_entities(entities)


class FloorHeatingManualHold(FloorHeatingEntity, BinarySensorEntity):
    """Ein Heizkreis steht im Handbetrieb und wird nicht geregelt."""

    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator: FloorHeatingCoordinator, channel: int) -> None:
        super().__init__(coordinator, f"manual{channel}")
        self._channel = channel
        self._attr_name = f"Heizkreis {channel} Handbetrieb"

    @property
    def is_on(self) -> bool | None:
        ch = self.coordinator.channel(self._channel)
        return None if ch is None else bool(ch.get("manual"))


class FloorHeatingRoomStale(FloorHeatingEntity, BinarySensorEntity):
    """Für einen Raum fehlt ein brauchbarer Messwert, die Regelung setzt aus."""

    _attr_device_class = BinarySensorDeviceClass.PROBLEM

    def __init__(self, coordinator: FloorHeatingCoordinator, room_id: int) -> None:
        super().__init__(coordinator, f"room{room_id}_stale")
        self._room_id = room_id

    @property
    def name(self) -> str:
        room = self.coordinator.room(self._room_id)
        base = room["name"] if room else "Raum"
        return f"{base} ohne Messwert"

    @property
    def is_on(self) -> bool | None:
        room = self.coordinator.room(self._room_id)
        if room is None:
            return None
        return not room.get("temp_valid", False)

    @property
    def extra_state_attributes(self) -> dict[str, Any] | None:
        room = self.coordinator.room(self._room_id)
        if room is None:
            return None
        return {
            "thermometer_zugeordnet": room.get("sensor_set"),
            "messwert_alter_s": room.get("temp_age_s"),
        }


class FloorHeatingCalibrationRunning(FloorHeatingEntity, BinarySensorEntity):
    """Eine Messfahrt läuft."""

    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator: FloorHeatingCoordinator) -> None:
        super().__init__(coordinator, "calibrating")
        self._attr_name = "Messfahrt läuft"

    @property
    def is_on(self) -> bool | None:
        calib = (self.coordinator.data or {}).get("calib")
        return None if calib is None else calib.get("state") == "running"

    @property
    def extra_state_attributes(self) -> dict[str, Any] | None:
        calib = (self.coordinator.data or {}).get("calib")
        if not calib:
            return None
        attrs: dict[str, Any] = {
            "zustand": calib.get("state"),
            "heizkreis": calib.get("channel"),
            "meldung": calib.get("message"),
        }
        if calib.get("suggestion"):
            attrs["vorschlag"] = calib["suggestion"]
        return attrs
