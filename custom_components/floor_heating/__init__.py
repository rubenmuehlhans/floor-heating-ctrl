"""Integration der Ventilsteuerung für die Fußbodenheizung."""

from __future__ import annotations

import voluptuous as vol

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.exceptions import HomeAssistantError

from .api import FloorHeatingApi, FloorHeatingError
from .const import (
    ATTR_CHANNEL,
    ATTR_DIRECTION,
    CONF_HOST,
    DOMAIN,
    SERVICE_CALIBRATE,
    SERVICE_CALIBRATE_ABORT,
    SERVICE_CALIBRATE_ACCEPT,
    SERVICE_FORCE,
    SERVICE_RELEASE,
)
from .coordinator import FloorHeatingCoordinator

# Die Verteilerplatine bringt Ventile und Räume mit, das Heizungsgerät statt
# dessen Pumpen und Messstellen. Beide teilen sich Diagnose und Neustart.
PLATFORMS_MANIFOLD: list[Platform] = [
    Platform.BINARY_SENSOR,
    Platform.BUTTON,
    Platform.CLIMATE,
    Platform.COVER,
    Platform.SENSOR,
]

PLATFORMS_HEAT: list[Platform] = [
    Platform.BINARY_SENSOR,
    Platform.BUTTON,
    Platform.SELECT,
    Platform.SENSOR,
]

CHANNEL_SCHEMA = vol.Schema(
    {vol.Required(ATTR_CHANNEL): vol.All(vol.Coerce(int), vol.Range(min=1, max=11))}
)

FORCE_SCHEMA = CHANNEL_SCHEMA.extend(
    {vol.Required(ATTR_DIRECTION): vol.In(["open", "close", "stop"])}
)


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Gerät einrichten."""
    api = FloorHeatingApi(entry.data[CONF_HOST], async_get_clientsession(hass))
    coordinator = FloorHeatingCoordinator(hass, entry, api)
    await coordinator.async_config_entry_first_refresh()

    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = coordinator
    platforms = PLATFORMS_HEAT if coordinator.is_heat else PLATFORMS_MANIFOLD
    entry.runtime_data = platforms
    await hass.config_entries.async_forward_entry_setups(entry, platforms)

    # Die Dienste betreffen Ventile und Messfahrten; ohne Verteilerplatine
    # gibt es nichts, worauf sie wirken koennten.
    if not coordinator.is_heat:
        _register_services(hass)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Gerät entfernen."""
    platforms = getattr(entry, "runtime_data", None) or PLATFORMS_MANIFOLD
    unloaded = await hass.config_entries.async_unload_platforms(entry, platforms)
    if unloaded:
        hass.data[DOMAIN].pop(entry.entry_id)
        if not hass.data[DOMAIN]:
            hass.data.pop(DOMAIN)
            for service in (
                SERVICE_CALIBRATE,
                SERVICE_CALIBRATE_ACCEPT,
                SERVICE_CALIBRATE_ABORT,
                SERVICE_FORCE,
                SERVICE_RELEASE,
            ):
                hass.services.async_remove(DOMAIN, service)
    return unloaded


def _register_services(hass: HomeAssistant) -> None:
    """Dienste anmelden.

    Sie wirken auf alle eingerichteten Geräte. Wer mehrere Anlagen betreibt,
    ruft sie über die jeweilige Entität auf oder richtet je Etage eine eigene
    Automatisierung ein.
    """
    if hass.services.has_service(DOMAIN, SERVICE_CALIBRATE):
        return

    def coordinators() -> list[FloorHeatingCoordinator]:
        return list(hass.data.get(DOMAIN, {}).values())

    async def _run(call: ServiceCall, action) -> None:
        for coordinator in coordinators():
            try:
                await action(coordinator)
            except FloorHeatingError as err:
                raise HomeAssistantError(str(err)) from err
            await coordinator.async_request_refresh()

    async def handle_calibrate(call: ServiceCall) -> None:
        channel = call.data[ATTR_CHANNEL]
        await _run(call, lambda c: c.api.async_calibrate_start(channel))

    async def handle_accept(call: ServiceCall) -> None:
        await _run(call, lambda c: c.api.async_calibrate_accept())

    async def handle_abort(call: ServiceCall) -> None:
        await _run(call, lambda c: c.api.async_calibrate_abort())

    async def handle_force(call: ServiceCall) -> None:
        channel = call.data[ATTR_CHANNEL]
        direction = call.data[ATTR_DIRECTION]
        await _run(call, lambda c: c.api.async_channel_cmd(channel, direction))

    async def handle_release(call: ServiceCall) -> None:
        channel = call.data[ATTR_CHANNEL]
        await _run(call, lambda c: c.api.async_channel_cmd(channel, "auto"))

    hass.services.async_register(DOMAIN, SERVICE_CALIBRATE, handle_calibrate, CHANNEL_SCHEMA)
    hass.services.async_register(DOMAIN, SERVICE_CALIBRATE_ACCEPT, handle_accept, vol.Schema({}))
    hass.services.async_register(DOMAIN, SERVICE_CALIBRATE_ABORT, handle_abort, vol.Schema({}))
    hass.services.async_register(DOMAIN, SERVICE_FORCE, handle_force, FORCE_SCHEMA)
    hass.services.async_register(DOMAIN, SERVICE_RELEASE, handle_release, CHANNEL_SCHEMA)
