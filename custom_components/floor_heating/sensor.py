"""Messwerte: Raumthermometer, Vorlauffühler, Schaltschrank und Diagnose."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import (
    PERCENTAGE,
    EntityCategory,
    UnitOfElectricPotential,
    UnitOfInformation,
    UnitOfTemperature,
    UnitOfTime,
)
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .coordinator import FloorHeatingCoordinator
from .entity import FloorHeatingEntity


@dataclass(frozen=True, kw_only=True)
class FloorHeatingSensorDescription(SensorEntityDescription):
    """Beschreibung eines Messwerts samt Zugriff auf den Zustand."""

    value: Callable[[dict[str, Any]], Any]


DIAGNOSTICS: tuple[FloorHeatingSensorDescription, ...] = (
    FloorHeatingSensorDescription(
        key="uptime",
        name="Laufzeit",
        native_unit_of_measurement=UnitOfTime.SECONDS,
        device_class=SensorDeviceClass.DURATION,
        state_class=SensorStateClass.MEASUREMENT,
        entity_category=EntityCategory.DIAGNOSTIC,
        value=lambda s: s.get("uptime_s"),
    ),
    FloorHeatingSensorDescription(
        key="heap",
        name="Freier Speicher",
        native_unit_of_measurement=UnitOfInformation.BYTES,
        device_class=SensorDeviceClass.DATA_SIZE,
        state_class=SensorStateClass.MEASUREMENT,
        entity_category=EntityCategory.DIAGNOSTIC,
        entity_registry_enabled_default=False,
        value=lambda s: s.get("heap"),
    ),
    FloorHeatingSensorDescription(
        key="rssi",
        name="WLAN-Empfang",
        native_unit_of_measurement="dBm",
        device_class=SensorDeviceClass.SIGNAL_STRENGTH,
        state_class=SensorStateClass.MEASUREMENT,
        entity_category=EntityCategory.DIAGNOSTIC,
        value=lambda s: s.get("net", {}).get("rssi"),
    ),
    FloorHeatingSensorDescription(
        key="box_temp",
        name="Schaltschrank Temperatur",
        native_unit_of_measurement=UnitOfTemperature.CELSIUS,
        device_class=SensorDeviceClass.TEMPERATURE,
        state_class=SensorStateClass.MEASUREMENT,
        entity_category=EntityCategory.DIAGNOSTIC,
        value=lambda s: (
            s["local_sensors"]["hdc1080"]["temp_c"]
            if s.get("local_sensors", {}).get("hdc1080", {}).get("valid")
            else None
        ),
    ),
    FloorHeatingSensorDescription(
        key="box_hum",
        name="Schaltschrank Luftfeuchtigkeit",
        native_unit_of_measurement=PERCENTAGE,
        device_class=SensorDeviceClass.HUMIDITY,
        state_class=SensorStateClass.MEASUREMENT,
        entity_category=EntityCategory.DIAGNOSTIC,
        value=lambda s: (
            s["local_sensors"]["hdc1080"]["humidity"]
            if s.get("local_sensors", {}).get("hdc1080", {}).get("valid")
            else None
        ),
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: FloorHeatingCoordinator = hass.data[DOMAIN][entry.entry_id]
    state = coordinator.data or {}

    entities: list[SensorEntity] = [
        FloorHeatingDiagnosticSensor(coordinator, d) for d in DIAGNOSTICS
    ]

    # Je Raum die Werte des zugeordneten Thermometers.
    for room in state.get("rooms", []):
        entities.append(FloorHeatingRoomSensor(coordinator, room["id"], "humidity"))
        entities.append(FloorHeatingRoomSensor(coordinator, room["id"], "battery"))

    # Je Messgruppe die Gegenspannung — für die Fehlersuche an den Antrieben.
    for group in range(1, len(state.get("bemf_mv", [])) + 1):
        entities.append(FloorHeatingBemfSensor(coordinator, group))

    # Vorlauffühler am 1-Wire-Bus, sofern angeschlossen.
    for index, probe in enumerate(state.get("local_sensors", {}).get("ds18b20", [])):
        entities.append(FloorHeatingProbeSensor(coordinator, index, probe.get("address", "")))

    async_add_entities(entities)


class FloorHeatingDiagnosticSensor(FloorHeatingEntity, SensorEntity):
    """Messwert, der sich unmittelbar aus dem Zustand ergibt."""

    entity_description: FloorHeatingSensorDescription

    def __init__(
        self, coordinator: FloorHeatingCoordinator, description: FloorHeatingSensorDescription
    ) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description

    @property
    def native_value(self) -> Any:
        return self.entity_description.value(self.coordinator.data or {})


class FloorHeatingRoomSensor(FloorHeatingEntity, SensorEntity):
    """Nebenwerte des Raumthermometers."""

    _attr_state_class = SensorStateClass.MEASUREMENT

    KINDS = {
        "humidity": ("Luftfeuchtigkeit", PERCENTAGE, SensorDeviceClass.HUMIDITY),
        "battery": ("Batterie Thermometer", PERCENTAGE, SensorDeviceClass.BATTERY),
    }

    def __init__(self, coordinator: FloorHeatingCoordinator, room_id: int, kind: str) -> None:
        super().__init__(coordinator, f"room{room_id}_{kind}")
        self._room_id = room_id
        self._kind = kind
        label, unit, device_class = self.KINDS[kind]
        self._label = label
        self._attr_native_unit_of_measurement = unit
        self._attr_device_class = device_class
        if kind == "battery":
            self._attr_entity_category = EntityCategory.DIAGNOSTIC

    @property
    def name(self) -> str:
        room = self.coordinator.room(self._room_id)
        return f"{room['name']} {self._label}" if room else self._label

    @property
    def native_value(self) -> Any:
        room = self.coordinator.room(self._room_id)
        if not room or not room.get("temp_valid"):
            return None
        return room.get(self._kind)


class FloorHeatingBemfSensor(FloorHeatingEntity, SensorEntity):
    """Gegenspannung einer Messgruppe."""

    _attr_native_unit_of_measurement = UnitOfElectricPotential.MILLIVOLT
    _attr_device_class = SensorDeviceClass.VOLTAGE
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_entity_category = EntityCategory.DIAGNOSTIC
    _attr_entity_registry_enabled_default = False

    def __init__(self, coordinator: FloorHeatingCoordinator, group: int) -> None:
        super().__init__(coordinator, f"bemf{group}")
        self._group = group
        self._attr_name = f"Gegenspannung Messgruppe {group}"

    @property
    def native_value(self) -> Any:
        values = (self.coordinator.data or {}).get("bemf_mv", [])
        if len(values) < self._group:
            return None
        return values[self._group - 1]


class FloorHeatingProbeSensor(FloorHeatingEntity, SensorEntity):
    """Vorlauffühler am 1-Wire-Bus."""

    _attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS
    _attr_device_class = SensorDeviceClass.TEMPERATURE
    _attr_state_class = SensorStateClass.MEASUREMENT

    def __init__(self, coordinator: FloorHeatingCoordinator, index: int, address: str) -> None:
        # Die Busadresse ist dauerhaft und eindeutig; die Reihenfolge am Bus
        # ist es nicht.
        super().__init__(coordinator, f"probe_{address or index}")
        self._index = index
        self._attr_name = f"Vorlauf {index + 1}"

    @property
    def native_value(self) -> Any:
        probes = (self.coordinator.data or {}).get("local_sensors", {}).get("ds18b20", [])
        if len(probes) <= self._index:
            return None
        probe = probes[self._index]
        return probe["temp_c"] if probe.get("valid") else None
