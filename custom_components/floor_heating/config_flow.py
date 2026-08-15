"""Einrichtungsdialog: Adresse eingeben, Gerät prüfen, fertig."""

from __future__ import annotations

from typing import Any

import voluptuous as vol

from homeassistant.config_entries import ConfigFlow, ConfigFlowResult
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import FloorHeatingApi, FloorHeatingError
from .const import CONF_HOST, DOMAIN

SCHEMA = vol.Schema({vol.Required(CONF_HOST): str})


class FloorHeatingConfigFlow(ConfigFlow, domain=DOMAIN):
    """Führt durch das Hinzufügen eines Geräts."""

    VERSION = 1

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        errors: dict[str, str] = {}

        if user_input is not None:
            host = user_input[CONF_HOST].strip()
            api = FloorHeatingApi(host, async_get_clientsession(self.hass))
            try:
                state = await api.async_get_state()
            except FloorHeatingError:
                errors["base"] = "cannot_connect"
            else:
                device = state.get("device") or {}
                device_id = device.get("id")
                if not device_id:
                    # Ältere Firmware liefert keine Kennung. Ohne sie liesse
                    # sich das Gerät nach einem Adresswechsel nicht
                    # wiedererkennen.
                    errors["base"] = "firmware_too_old"
                else:
                    await self.async_set_unique_id(device_id)
                    self._abort_if_unique_id_configured(updates={CONF_HOST: host})
                    site = device.get("site") or host
                    return self.async_create_entry(
                        title=f"Fußbodenheizung {site}".strip(),
                        data={CONF_HOST: host},
                    )

        return self.async_show_form(step_id="user", data_schema=SCHEMA, errors=errors)
