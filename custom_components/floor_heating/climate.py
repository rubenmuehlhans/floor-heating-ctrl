"""Ein Thermostat je eingerichtetem Raum."""

from __future__ import annotations

from typing import Any

from homeassistant.components.climate import (
    ClimateEntity,
    ClimateEntityFeature,
    HVACAction,
    HVACMode,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import ATTR_TEMPERATURE, UnitOfTemperature
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN, MAX_TEMP, MIN_TEMP, TEMP_STEP
from .coordinator import FloorHeatingCoordinator
from .entity import FloorHeatingEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: FloorHeatingCoordinator = hass.data[DOMAIN][entry.entry_id]
    rooms = (coordinator.data or {}).get("rooms", [])
    async_add_entities(FloorHeatingClimate(coordinator, r["id"]) for r in rooms)


class FloorHeatingClimate(FloorHeatingEntity, ClimateEntity):
    """Raumregelung als Thermostat."""

    _attr_temperature_unit = UnitOfTemperature.CELSIUS
    _attr_hvac_modes = [HVACMode.OFF, HVACMode.HEAT]
    _attr_supported_features = (
        ClimateEntityFeature.TARGET_TEMPERATURE
        | ClimateEntityFeature.TURN_ON
        | ClimateEntityFeature.TURN_OFF
    )
    _attr_min_temp = MIN_TEMP
    _attr_max_temp = MAX_TEMP
    _attr_target_temperature_step = TEMP_STEP

    def __init__(self, coordinator: FloorHeatingCoordinator, room_id: int) -> None:
        super().__init__(coordinator, f"room{room_id}")
        self._room_id = room_id
        self._attr_name = None  # der Gerätename traegt den Raum

    @property
    def _room(self) -> dict[str, Any] | None:
        return self.coordinator.room(self._room_id)

    @property
    def available(self) -> bool:
        return super().available and self._room is not None

    @property
    def name(self) -> str | None:
        room = self._room
        return room["name"] if room else None

    @property
    def current_temperature(self) -> float | None:
        room = self._room
        if not room or not room.get("temp_valid"):
            return None
        return room["temp_c"]

    @property
    def target_temperature(self) -> float | None:
        room = self._room
        return room["target_c"] if room else None

    @property
    def hvac_mode(self) -> HVACMode:
        room = self._room
        if room and room.get("mode") == "heat":
            return HVACMode.HEAT
        return HVACMode.OFF

    @property
    def hvac_action(self) -> HVACAction | None:
        room = self._room
        if not room:
            return None
        if room.get("mode") != "heat":
            return HVACAction.OFF
        if not room.get("temp_valid"):
            # Ohne gültigen Messwert setzt die Regelung aus; die Ventile
            # bleiben stehen, wo sie sind.
            return HVACAction.IDLE
        return HVACAction.HEATING if room.get("target_position", 0) > 0.005 else HVACAction.IDLE

    @property
    def extra_state_attributes(self) -> dict[str, Any] | None:
        room = self._room
        if not room:
            return None
        return {
            "heizkreise": room.get("channels"),
            "zielstellung": round(room.get("target_position", 0) * 100),
            "naechste_pruefung_s": room.get("next_check_s"),
            "messwert_alter_s": room.get("temp_age_s"),
            "luftfeuchte": room.get("humidity"),
            "batterie_thermometer": room.get("battery"),
            "thermometer_zugeordnet": room.get("sensor_set"),
        }

    async def async_set_temperature(self, **kwargs: Any) -> None:
        temp = kwargs.get(ATTR_TEMPERATURE)
        if temp is None:
            return
        await self.coordinator.async_command(
            self.coordinator.api.async_set_room_target(self._room_id, float(temp))
        )

    async def async_set_hvac_mode(self, hvac_mode: HVACMode) -> None:
        await self.coordinator.async_command(
            self.coordinator.api.async_set_room_mode(self._room_id, hvac_mode == HVACMode.HEAT)
        )
