"""Gemeinsame Grundlage aller Entitäten."""

from __future__ import annotations

from typing import Any

from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN, MANUFACTURER
from .coordinator import FloorHeatingCoordinator


class FloorHeatingEntity(CoordinatorEntity[FloorHeatingCoordinator]):
    """Bindet eine Entität an das Gerät und an die Datenversorgung."""

    _attr_has_entity_name = True

    def __init__(self, coordinator: FloorHeatingCoordinator, key: str) -> None:
        super().__init__(coordinator)
        dev: dict[str, Any] = coordinator.device_info_raw
        self._device_id = dev.get("id") or coordinator.api.host
        self._attr_unique_id = f"{self._device_id}_{key}"

        site = dev.get("site") or ""
        name = f"Fußbodenheizung {site}".strip()

        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, self._device_id)},
            name=name,
            manufacturer=MANUFACTURER,
            model=dev.get("model") or "ESP32 Ventilsteuerung",
            sw_version=(coordinator.data or {}).get("version"),
            configuration_url=f"http://{coordinator.api.host}/",
            connections=(
                {("mac", dev["mac"])} if dev.get("mac") else set()
            ),
        )

    @property
    def available(self) -> bool:
        return super().available and self.coordinator.data is not None
