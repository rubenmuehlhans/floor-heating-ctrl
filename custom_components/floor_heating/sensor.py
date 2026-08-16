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

from .const import DOMAIN, ROLE_LABELS
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

    if coordinator.is_heat:
        # Je zugeordneter Messstelle ein Fühler.
        for probe in state.get("probes", []):
            if probe.get("role"):
                entities.append(HeatProbeSensor(coordinator, probe["role"]))
        if coordinator.probe("abgas"):
            entities.append(HeatBurnerRuntime(coordinator))
            entities.append(HeatBurnerStarts(coordinator))
            entities.append(HeatOilToday(coordinator))
        if coordinator.probe("puffer"):
            entities.append(HeatChargeLevel(coordinator))
            entities.append(HeatChargePhase(coordinator))
        for c in state.get("circuits", []):
            entities.append(HeatCircuitSpread(coordinator, c["id"]))
        async_add_entities(entities)
        return

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


# ---------------------------------------------------------------------
# Heizungsgerät
# ---------------------------------------------------------------------


class HeatProbeSensor(FloorHeatingEntity, SensorEntity):
    """Eine zugeordnete Messstelle des Heizungsgeräts."""

    _attr_device_class = SensorDeviceClass.TEMPERATURE
    _attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_suggested_display_precision = 1

    def __init__(self, coordinator: FloorHeatingCoordinator, role: str) -> None:
        super().__init__(coordinator, f"probe_{role}")
        self._role = role
        self._attr_name = ROLE_LABELS.get(role, role)

    @property
    def native_value(self) -> float | None:
        return (self.coordinator.probe(self._role) or {}).get("temp_c")

    @property
    def extra_state_attributes(self) -> dict:
        p = self.coordinator.probe(self._role) or {}
        return {
            "kennung": p.get("rom"),
            "korrektur_k": p.get("offset_k"),
            "alter_s": p.get("age_s"),
            "verworfene_messungen": p.get("errors"),
        }


class HeatBurnerRuntime(FloorHeatingEntity, SensorEntity):
    """Laufzeit des Brenners seit Mitternacht."""

    _attr_device_class = SensorDeviceClass.DURATION
    _attr_native_unit_of_measurement = UnitOfTime.SECONDS
    _attr_state_class = SensorStateClass.TOTAL_INCREASING
    _attr_name = "Brennerlaufzeit heute"

    def __init__(self, coordinator: FloorHeatingCoordinator) -> None:
        super().__init__(coordinator, "burner_runtime")

    @property
    def native_value(self) -> int | None:
        return ((self.coordinator.data or {}).get("burner") or {}).get("runtime_today_s")

    @property
    def extra_state_attributes(self) -> dict:
        b = (self.coordinator.data or {}).get("burner") or {}
        return {"gestern_s": b.get("runtime_yesterday_s")}


class HeatBurnerStarts(FloorHeatingEntity, SensorEntity):
    """Anzahl der Brennerstarts seit Mitternacht."""

    _attr_state_class = SensorStateClass.TOTAL_INCREASING
    _attr_name = "Brennerstarts heute"

    def __init__(self, coordinator: FloorHeatingCoordinator) -> None:
        super().__init__(coordinator, "burner_starts")

    @property
    def native_value(self) -> int | None:
        return ((self.coordinator.data or {}).get("burner") or {}).get("starts_today")

    @property
    def extra_state_attributes(self) -> dict:
        b = (self.coordinator.data or {}).get("burner") or {}
        return {"gestern": b.get("starts_yesterday")}


class HeatOilToday(FloorHeatingEntity, SensorEntity):
    """Schätzung aus Laufzeit und Düsendurchsatz — keine Messung."""

    _attr_native_unit_of_measurement = "L"
    _attr_state_class = SensorStateClass.TOTAL_INCREASING
    _attr_suggested_display_precision = 2
    _attr_name = "Ölverbrauch heute"

    def __init__(self, coordinator: FloorHeatingCoordinator) -> None:
        super().__init__(coordinator, "oil_today")

    @property
    def native_value(self) -> float | None:
        return ((self.coordinator.data or {}).get("burner") or {}).get("litres_today")


class HeatChargeLevel(FloorHeatingEntity, SensorEntity):
    """Füllstand des Pufferspeichers, geschätzt aus dem Pufferfühler."""

    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_suggested_display_precision = 0
    _attr_icon = "mdi:storage-tank"
    _attr_name = "Füllstand Pufferspeicher"

    def __init__(self, coordinator: FloorHeatingCoordinator) -> None:
        super().__init__(coordinator, "charge_level")

    @property
    def native_value(self) -> float | None:
        level = ((self.coordinator.data or {}).get("charge") or {}).get("level")
        return None if level is None else level * 100.0

    @property
    def extra_state_attributes(self) -> dict:
        c = (self.coordinator.data or {}).get("charge") or {}
        return {
            "nur_schaetzung": c.get("limited"),
            "kesselwerte_vom_nachbargeraet": c.get("kessel_remote"),
        }


class HeatChargePhase(FloorHeatingEntity, SensorEntity):
    """Ruhe, Ladung oder geladen."""

    _attr_icon = "mdi:water-boiler"
    _attr_name = "Ladezustand"

    def __init__(self, coordinator: FloorHeatingCoordinator) -> None:
        super().__init__(coordinator, "charge_phase")

    @property
    def native_value(self) -> str | None:
        return ((self.coordinator.data or {}).get("charge") or {}).get("phase")

    @property
    def extra_state_attributes(self) -> dict:
        c = (self.coordinator.data or {}).get("charge") or {}
        return {"spreizung_kessel_k": c.get("spread_k"), "seit_s": c.get("since_s")}


class HeatCircuitSpread(FloorHeatingEntity, SensorEntity):
    """Vorlauf minus Rücklauf: der Beleg, dass Wärme abgegeben wird."""

    _attr_native_unit_of_measurement = UnitOfTemperature.KELVIN
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_suggested_display_precision = 1

    def __init__(self, coordinator: FloorHeatingCoordinator, circuit_id: int) -> None:
        super().__init__(coordinator, f"circuit{circuit_id}_spread")
        self._id = circuit_id
        c = coordinator.circuit(circuit_id) or {}
        self._attr_name = f"{c.get('name') or f'Heizkreis {circuit_id}'} Spreizung"

    @property
    def native_value(self) -> float | None:
        return (self.coordinator.circuit(self._id) or {}).get("spread_k")

    @property
    def extra_state_attributes(self) -> dict:
        c = self.coordinator.circuit(self._id) or {}
        return {"vorlauf_c": c.get("vl_c"), "ruecklauf_c": c.get("rl_c")}
