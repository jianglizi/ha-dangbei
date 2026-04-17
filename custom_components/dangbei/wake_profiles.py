"""Wake-profile validation and normalization helpers."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Mapping

from .const import (
    CONF_WOL_WAKE_CUSTOM_FORMAT,
    CONF_WOL_WAKE_CUSTOM_HEX,
    CONF_WOL_WAKE_PROFILE,
    DEFAULT_WOL_WAKE_CUSTOM_FORMAT,
    DEFAULT_WOL_WAKE_PROFILE,
    MAX_WOL_WAKE_FULL_ADV_BYTES,
    MAX_WOL_WAKE_MANUFACTURER_DATA_BYTES,
    WOL_WAKE_CUSTOM_FORMAT_FULL_ADV,
    WOL_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA,
    WOL_WAKE_PROFILE_CUSTOM,
    WOL_WAKE_PROFILE_D5X_PRO,
    WOL_WAKE_PROFILE_F3_AIR,
)

VALID_WAKE_PROFILES = {
    WOL_WAKE_PROFILE_D5X_PRO,
    WOL_WAKE_PROFILE_F3_AIR,
    WOL_WAKE_PROFILE_CUSTOM,
}
VALID_CUSTOM_FORMATS = {
    WOL_WAKE_CUSTOM_FORMAT_FULL_ADV,
    WOL_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA,
}


class WakeConfigError(ValueError):
    """Raised when wake-profile input is invalid."""

    def __init__(self, field: str, error_key: str) -> None:
        super().__init__(error_key)
        self.field = field
        self.error_key = error_key


@dataclass(frozen=True, slots=True)
class WakeConfiguration:
    """Normalized wake-profile settings."""

    profile: str
    custom_format: str
    custom_hex: str

    def as_dict(self) -> dict[str, str]:
        """Return the configuration in config-entry / API shape."""
        return {
            CONF_WOL_WAKE_PROFILE: self.profile,
            CONF_WOL_WAKE_CUSTOM_FORMAT: self.custom_format,
            CONF_WOL_WAKE_CUSTOM_HEX: self.custom_hex,
        }


def normalize_hex_string(value: str | None) -> str:
    """Collapse whitespace and lowercase the remaining hex string."""
    if not value:
        return ""
    return "".join(value.split()).lower()


def normalize_wake_configuration(values: Mapping[str, Any]) -> WakeConfiguration:
    """Normalize and validate wake-profile settings from a config form."""
    profile = str(
        values.get(CONF_WOL_WAKE_PROFILE, DEFAULT_WOL_WAKE_PROFILE)
    ).strip().lower()
    if profile not in VALID_WAKE_PROFILES:
        raise WakeConfigError(CONF_WOL_WAKE_PROFILE, "invalid_wake_profile")

    custom_format = str(
        values.get(CONF_WOL_WAKE_CUSTOM_FORMAT, DEFAULT_WOL_WAKE_CUSTOM_FORMAT)
    ).strip().lower()
    if custom_format not in VALID_CUSTOM_FORMATS:
        raise WakeConfigError(
            CONF_WOL_WAKE_CUSTOM_FORMAT, "invalid_wake_custom_format"
        )

    custom_hex = normalize_hex_string(values.get(CONF_WOL_WAKE_CUSTOM_HEX))

    if profile != WOL_WAKE_PROFILE_CUSTOM:
        return WakeConfiguration(
            profile=profile,
            custom_format=custom_format,
            custom_hex=custom_hex,
        )

    if not custom_hex:
        raise WakeConfigError(CONF_WOL_WAKE_CUSTOM_HEX, "missing_wake_custom_hex")
    if len(custom_hex) % 2 != 0:
        raise WakeConfigError(CONF_WOL_WAKE_CUSTOM_HEX, "invalid_wake_custom_hex")
    if not _is_hex_string(custom_hex):
        raise WakeConfigError(CONF_WOL_WAKE_CUSTOM_HEX, "invalid_wake_custom_hex")

    byte_limit = (
        MAX_WOL_WAKE_FULL_ADV_BYTES
        if custom_format == WOL_WAKE_CUSTOM_FORMAT_FULL_ADV
        else MAX_WOL_WAKE_MANUFACTURER_DATA_BYTES
    )
    if len(custom_hex) // 2 > byte_limit:
        raise WakeConfigError(CONF_WOL_WAKE_CUSTOM_HEX, "invalid_wake_custom_hex")

    return WakeConfiguration(
        profile=profile,
        custom_format=custom_format,
        custom_hex=custom_hex,
    )


def wake_profile_options() -> dict[str, str]:
    """Return config-flow select options for wake profiles."""
    return {
        WOL_WAKE_PROFILE_D5X_PRO: "D5X Pro",
        WOL_WAKE_PROFILE_F3_AIR: "F3 Air",
        WOL_WAKE_PROFILE_CUSTOM: "Custom",
    }


def wake_custom_format_options() -> dict[str, str]:
    """Return config-flow select options for custom wake formats."""
    return {
        WOL_WAKE_CUSTOM_FORMAT_MANUFACTURER_DATA: "Manufacturer data only",
        WOL_WAKE_CUSTOM_FORMAT_FULL_ADV: "Full advertising data",
    }


def _is_hex_string(value: str) -> bool:
    return all(char in "0123456789abcdef" for char in value)
