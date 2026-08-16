"""Betriebsart der Heizkreispumpen."""

from __future__ import annotations

from homeassistant.components.select import SelectEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .api import FloorHeatingError
from .const import DOMAIN, PUMP_MODES
from .coordinator import FloorHeatingCoordinator
from .entity import FloorHeatingEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: FloorHeatingCoordinator = hass.data[DOMAIN][entry.entry_id]
    if not coordinator.is_heat:
        return
    state = coordinator.data or {}
    async_add_entities(
        FloorHeatingPumpMode(coordinator, c["id"]) for c in state.get("circuits", [])
    )


class FloorHeatingPumpMode(FloorHeatingEntity, SelectEntity):
    """Automatik, dauerhaft ein oder dauerhaft aus."""

    _attr_options = PUMP_MODES
    _attr_icon = "mdi:pump"

    def __init__(self, coordinator: FloorHeatingCoordinator, circuit_id: int) -> None:
        super().__init__(coordinator, f"circuit{circuit_id}_mode")
        self._id = circuit_id
        self._attr_name = f"{self._circuit_name} Betriebsart"

    @property
    def _circuit(self) -> dict:
        return self.coordinator.circuit(self._id) or {}

    @property
    def _circuit_name(self) -> str:
        return self._circuit.get("name") or f"Heizkreis {self._id}"

    @property
    def current_option(self) -> str | None:
        return self._circuit.get("mode")

    @property
    def extra_state_attributes(self) -> dict:
        c = self._circuit
        return {
            "pumpe_laeuft": c.get("on"),
            "grund": c.get("reason"),
            "bedarf": c.get("demand"),
            "weg_zum_relais": c.get("path"),
        }

    async def async_select_option(self, option: str) -> None:
        try:
            await self.coordinator.api.async_set_pump_mode(self._id, option)
        except FloorHeatingError as err:
            raise HomeAssistantError(str(err)) from err
        await self.coordinator.async_request_refresh()
