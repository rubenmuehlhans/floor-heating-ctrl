"""Schaltflächen für einmalige Handlungen."""

from __future__ import annotations

from homeassistant.components.button import ButtonEntity
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

    entities: list[ButtonEntity] = [FloorHeatingRestartButton(coordinator)]
    for room in state.get("rooms", []):
        entities.append(FloorHeatingCheckNowButton(coordinator, room["id"]))
    for ch in state.get("channels", []):
        entities.append(FloorHeatingReleaseButton(coordinator, ch["id"]))

    async_add_entities(entities)


class FloorHeatingCheckNowButton(FloorHeatingEntity, ButtonEntity):
    """Regelung des Raums sofort neu bewerten, statt das Prüfintervall abzuwarten."""

    def __init__(self, coordinator: FloorHeatingCoordinator, room_id: int) -> None:
        super().__init__(coordinator, f"room{room_id}_check")
        self._room_id = room_id

    @property
    def name(self) -> str:
        room = self.coordinator.room(self._room_id)
        base = room["name"] if room else "Raum"
        return f"{base} jetzt prüfen"

    async def async_press(self) -> None:
        await self.coordinator.async_command(
            self.coordinator.api.async_room_check_now(self._room_id)
        )


class FloorHeatingReleaseButton(FloorHeatingEntity, ButtonEntity):
    """Handbetrieb eines Heizkreises aufheben."""

    _attr_entity_category = EntityCategory.CONFIG
    _attr_entity_registry_enabled_default = False

    def __init__(self, coordinator: FloorHeatingCoordinator, channel: int) -> None:
        super().__init__(coordinator, f"release{channel}")
        self._channel = channel
        self._attr_name = f"Heizkreis {channel} an die Regelung"

    async def async_press(self) -> None:
        await self.coordinator.async_command(
            self.coordinator.api.async_channel_cmd(self._channel, "auto")
        )


class FloorHeatingRestartButton(FloorHeatingEntity, ButtonEntity):
    """Gerät neu starten."""

    _attr_entity_category = EntityCategory.CONFIG
    _attr_name = "Neu starten"

    def __init__(self, coordinator: FloorHeatingCoordinator) -> None:
        super().__init__(coordinator, "restart")

    async def async_press(self) -> None:
        await self.coordinator.api.async_restart()
