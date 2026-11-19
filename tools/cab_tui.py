#!/usr/bin/env python3
"""CabControl Bluetooth diagnostics, identification name and firmware update (macOS, Textual + bleak).

Usage (from the repository root; uv creates tools/.venv on first run):
  uv run --project tools cab-tui                   # scan and pick a cabinet
  uv run --project tools cab-tui --name "Cab 3"    # connect to the first match

Keys: Enter connects, b batch-updates several cabinets, n sets the name,
u updates the firmware (updates and naming need include/ota_secret.h),
Escape disconnects/goes back, q quits.
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import hmac
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ble_flash import (  # noqa: E402
    DEFAULT_FIRMWARE,
    DEFAULT_SECRET,
    INFO_UUID,
    NONCE_SIZE,
    SERVICE_UUID,
    STATE_READY,
    STATUS_UUID,
    FlashError,
    Image,
    load_image,
    load_secret,
    parse_status,
    transfer,
    version_text,
)

# Must match src/bluetooth.cpp, include/diagnostics.h and include/identity_core.h.
DIAG_UUID = "c0de0102-cab0-4c71-9a6e-3f1d2b7e8a10"
NAME_UUID = "c0de0103-cab0-4c71-9a6e-3f1d2b7e8a10"
NAME_AUTH_UUID = "c0de0104-cab0-4c71-9a6e-3f1d2b7e8a10"
NAME_SET_UUID = "c0de0105-cab0-4c71-9a6e-3f1d2b7e8a10"
RESET_UUID = "c0de0106-cab0-4c71-9a6e-3f1d2b7e8a10"

DIAG_FORMAT_VERSION = 2
DIAG_FORMAT = "<BIHHHBBBBBBHB"
NAME_PROTOCOL_VERSION = 1
NAME_AUTH_LABEL = b"CabControl-NAME-v1"
NAME_MAX_LEN = 15
NAME_SET_HEADER_SIZE = 1
NAME_RESULT_OK = 1
NAME_RESULTS = {
    0: "no result",
    1: "OK",
    2: "AUTH (secret mismatch)",
    3: "INVALID name",
    4: "STORAGE (flash write failed)",
    5: "BAD_COMMAND",
}

# Advertised manufacturer data, see src/bluetooth.cpp: [format][major][minor][patch].
ADV_COMPANY_ID = 0xFFFF
ADV_FORMAT_VERSION = 1

LEVELS = ["UNKNOWN", "TIMEOUT", "OFF", "HALF", "FULL"]
POWER_ERRORS = ["NONE", "DC_IN_LOW", "DC_ADJ_ERR", "DC_OUT_ERR"]
INPUT_NAMES = ["OFF", "HALF", "FULL", "AUX"]
FLAG_DC_ON, FLAG_DAC_PRESENT, FLAG_ACK_PENDING, FLAG_OLED_PRESENT = 0x01, 0x02, 0x04, 0x08


def enum_name(names: list[str], value: int) -> str:
    return names[value] if value < len(names) else f"#{value}"


@dataclass
class Diagnostics:
    uptime_s: int
    dc_in_v: float
    dc_adj_v: float
    dc_out_v: float
    flags: int
    power_error: int
    latched_power_error: int
    flap_percent: int
    level: int
    timeout_level: int
    time_left_s: int
    inputs: int
    format_version: int

    @property
    def dc_on(self) -> bool:
        return bool(self.flags & FLAG_DC_ON)

    @property
    def dac_present(self) -> bool:
        return bool(self.flags & FLAG_DAC_PRESENT)

    @property
    def ack_pending(self) -> bool:
        return bool(self.flags & FLAG_ACK_PENDING)

    @property
    def oled_present(self) -> bool:
        return bool(self.flags & FLAG_OLED_PRESENT)

    def input_active(self, index: int) -> bool:
        bit_set = bool(self.inputs & (1 << index))
        return bit_set if self.format_version == 2 else not bit_set


def parse_diagnostics(raw: bytes) -> Diagnostics:
    raw = bytes(raw)
    if len(raw) < struct.calcsize(DIAG_FORMAT) or raw[0] not in (1, DIAG_FORMAT_VERSION):
        raise FlashError(f"Unsupported diagnostics format: {raw[:1].hex() or 'empty'}")
    (_, uptime, dc_in, dc_adj, dc_out, flags, error, latched, flap, level,
     timeout_level, time_left, inputs) = struct.unpack_from(DIAG_FORMAT, raw)
    return Diagnostics(uptime, dc_in / 1000, dc_adj / 1000, dc_out / 1000, flags,
                       error, latched, flap, level, timeout_level, time_left, inputs, raw[0])


# Batch update: how long to wait for the device to drop the link after READY,
# and for it to advertise the new version once SFU has installed it.
REBOOT_DISCONNECT_TIMEOUT = 10.0
INSTALL_TIMEOUT = 60.0


def advertised_version(manufacturer_data: dict[int, bytes]) -> tuple[int, int, int] | None:
    """Firmware version from the advertisement; None for firmware that predates it."""
    data = bytes(manufacturer_data.get(ADV_COMPANY_ID, b""))
    if len(data) < 4 or data[0] != ADV_FORMAT_VERSION:
        return None
    return (data[1], data[2], data[3])


def firmware_text(version: tuple[int, ...] | None) -> str:
    return "?" if version is None else version_text(version)


@dataclass
class ScannedDevice:
    device: Any
    name: str
    firmware: tuple[int, int, int] | None
    rssi: int

    @property
    def address(self) -> str:
        return self.device.address


def parse_advertisement(device, adv) -> ScannedDevice | None:
    """Returns the cabinet behind a scan result, or None if it is not one."""
    if SERVICE_UUID not in [u.lower() for u in adv.service_uuids]:
        return None
    return ScannedDevice(device, adv.local_name or device.name or "?",
                         advertised_version(adv.manufacturer_data), adv.rssi)


def validate_name(name: str) -> str | None:
    """Returns why the device would reject the name, or None if it is valid."""
    if not name:
        return "Name must not be empty"
    if len(name) > NAME_MAX_LEN:
        return f"Name must be at most {NAME_MAX_LEN} characters"
    if any(not 0x20 <= ord(c) <= 0x7E for c in name):
        return "Name may only contain printable ASCII characters"
    if name != name.strip(" "):
        return "Name must not start or end with a space"
    return None


def build_name_body(secret: bytes, nonce: bytes, name: str) -> bytes:
    encoded = name.encode("ascii")
    signed = bytes([len(encoded)]) + encoded.ljust(NAME_MAX_LEN, b"\0")
    tag = hmac.new(secret, NAME_AUTH_LABEL + nonce + signed, hashlib.sha256).digest()
    return signed + tag


async def read_name_auth(client) -> tuple[int, bytes]:
    auth = bytes(await client.read_gatt_char(NAME_AUTH_UUID))
    if len(auth) < 2 + NONCE_SIZE or auth[0] != NAME_PROTOCOL_VERSION:
        raise FlashError(f"Unsupported name protocol on device: {auth[:1].hex()}")
    return auth[1], auth[2:2 + NONCE_SIZE]


async def set_name(client, secret: bytes, name: str) -> None:
    error = validate_name(name)
    if error:
        raise FlashError(error)
    _, nonce = await read_name_auth(client)
    body = build_name_body(secret, nonce, name)
    # ArduinoBLE has no long-write support, so every write must fit the MTU.
    fragment_size = client.mtu_size - 3 - NAME_SET_HEADER_SIZE
    for offset in range(0, len(body), fragment_size):
        await client.write_gatt_char(
            NAME_SET_UUID, bytes([offset]) + body[offset:offset + fragment_size],
            response=True)
    result, _ = await read_name_auth(client)
    if result != NAME_RESULT_OK:
        raise FlashError(f"Device rejected the name: {NAME_RESULTS.get(result, result)}")


def format_duration(seconds: int) -> str:
    days, rest = divmod(seconds, 86400)
    hours, rest = divmod(rest, 3600)
    minutes, secs = divmod(rest, 60)
    text = f"{hours:02d}:{minutes:02d}:{secs:02d}"
    return f"{days}d {text}" if days else text


def diagnostics_rows(diag: Diagnostics) -> list[tuple[str, str, str]]:
    """(section, label, value) rows shown in the device screen."""
    latched = enum_name(POWER_ERRORS, diag.latched_power_error)
    if diag.ack_pending:
        latched += " (waiting for ack on device)"
    if diag.format_version == 2:
        states = [
            ("Control", "Physical door position", enum_name(["TOP", "HALF", "DOWN"], diag.timeout_level)),
            ("Control", "Flap state", enum_name(["CLOSED", "HALF", "FULL", "TIMEOUT"], diag.level)),
            ("Control", "IO error", "UNEXPECTED_IO" if diag.flags & 0x10 else "NONE"),
        ]
        inputs = ["Upper", "Lower"]
    else:
        states = [("Control", "Level", enum_name(LEVELS, diag.level)),
                  ("Control", "Timed out level", enum_name(LEVELS, diag.timeout_level))]
        inputs = INPUT_NAMES
    rows = [
        *states,
        ("Control", "Flap command", f"{diag.flap_percent} %"),
        ("Power", "DC in", f"{diag.dc_in_v:.2f} V"),
        ("Power", "DC adj", f"{diag.dc_adj_v:.2f} V"),
        ("Power", "DC out", f"{diag.dc_out_v:.2f} V"),
        ("Power", "DC output", "on" if diag.dc_on else "SHUT DOWN"),
        ("Power", "Error", enum_name(POWER_ERRORS, diag.power_error)),
        ("Power", "Last error", latched),
        ("Control", "Timeout in", format_duration(diag.time_left_s)
         if diag.time_left_s else "-"),
        ("Inputs", "Active", " ".join(
            name for i, name in enumerate(inputs) if diag.input_active(i)) or "none"),
        ("System", "Uptime", format_duration(diag.uptime_s)),
        ("System", "DAC", "present" if diag.dac_present else "MISSING"),
        ("System", "OLED", "present" if diag.oled_present else "MISSING"),
    ]
    return rows


# --- Textual UI -------------------------------------------------------------

from textual import on, work  # noqa: E402
from textual.app import App, ComposeResult  # noqa: E402
from textual.binding import Binding  # noqa: E402
from textual.containers import Vertical  # noqa: E402
from textual.screen import ModalScreen, Screen  # noqa: E402
from textual.widgets import (  # noqa: E402
    DataTable,
    Footer,
    Header,
    Input,
    Label,
    ProgressBar,
    Static,
)

ClientFactory = Callable[..., Any]
ScannerFactory = Callable[..., Any]


def default_client_factory(device, disconnected_callback):
    from bleak import BleakClient
    return BleakClient(device, disconnected_callback=disconnected_callback)


def default_scanner_factory(detection_callback):
    from bleak import BleakScanner
    return BleakScanner(detection_callback=detection_callback, service_uuids=[SERVICE_UUID])


class NameScreen(ModalScreen[str | None]):
    BINDINGS = [Binding("escape", "cancel", "Cancel")]
    DEFAULT_CSS = """
    NameScreen { align: center middle; }
    NameScreen > Vertical {
        width: 50; height: auto; padding: 1 2; border: thick $accent; background: $surface;
    }
    NameScreen #name-error { color: $error; height: auto; }
    """

    def __init__(self, current: str) -> None:
        super().__init__()
        self.current = current

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label(f"Identification name (max {NAME_MAX_LEN} characters)")
            yield Input(value=self.current, max_length=NAME_MAX_LEN, id="name-input")
            yield Label("", id="name-error")

    @on(Input.Submitted)
    def submit(self, event: Input.Submitted) -> None:
        error = validate_name(event.value)
        if error:
            self.query_one("#name-error", Label).update(error)
            return
        self.dismiss(event.value)

    def action_cancel(self) -> None:
        self.dismiss(None)


def describe_image(path_text: str, device_version: tuple[int, ...] | None
                   ) -> tuple[Image | None, str]:
    """Loads the image and returns (image if it can be flashed, description)."""
    try:
        image = load_image(Path(path_text).expanduser())
    except (FlashError, OSError) as error:
        return None, str(error)
    text = f"Image {version_text(image.version)}, {len(image.data) // 1024} KiB"
    if device_version is None:
        return image, text
    text += f"  (device runs {version_text(device_version)})"
    if image.version <= device_version:
        return None, text + "\nNot newer than the device firmware; bump PARAMS_VERSION_*."
    return image, text


class FirmwareScreen(ModalScreen[Image | None]):
    BINDINGS = [Binding("escape", "cancel", "Cancel")]
    DEFAULT_CSS = """
    FirmwareScreen { align: center middle; }
    FirmwareScreen > Vertical {
        width: 80; height: auto; padding: 1 2; border: thick $accent; background: $surface;
    }
    FirmwareScreen #fw-info { height: auto; margin-top: 1; }
    FirmwareScreen .invalid { color: $error; }
    """

    def __init__(self, path: Path, device_version: tuple[int, ...] | None) -> None:
        super().__init__()
        self.path = path
        self.device_version = device_version
        self.image: Image | None = None

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label("Firmware image (Enter to flash, Escape to cancel)")
            yield Input(value=str(self.path), id="fw-path")
            yield Label("", id="fw-info")

    def on_mount(self) -> None:
        self.check(str(self.path))

    @on(Input.Changed, "#fw-path")
    def path_changed(self, event: Input.Changed) -> None:
        self.check(event.value)

    def check(self, path_text: str) -> None:
        self.image, text = describe_image(path_text, self.device_version)
        info = self.query_one("#fw-info", Label)
        info.update(text)
        info.set_class(self.image is None, "invalid")

    @on(Input.Submitted, "#fw-path")
    def submit(self, event: Input.Submitted) -> None:
        # Re-read so a rebuild since the dialog opened is picked up.
        self.check(event.value)
        if self.image is not None:
            self.dismiss(self.image)

    def action_cancel(self) -> None:
        self.dismiss(None)


class FlashScreen(ModalScreen[bool]):
    """Runs the upload; dismisses True once the device verified the image."""

    BINDINGS = [Binding("escape", "close", "Abort / close")]
    DEFAULT_CSS = """
    FlashScreen { align: center middle; }
    FlashScreen > Vertical {
        width: 70; height: auto; padding: 1 2; border: thick $accent; background: $surface;
    }
    FlashScreen ProgressBar { margin: 1 0; }
    FlashScreen #flash-log { height: auto; }
    FlashScreen #flash-rate { height: 1; color: $text-muted; }
    """

    def __init__(self, client, secret: bytes, image: Image, cab_name: str) -> None:
        super().__init__()
        self.client = client
        self.secret = secret
        self.image = image
        self.cab_name = cab_name
        self.lines: list[str] = []
        self.done = False

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label(f"Updating [b]{self.cab_name}[/b] to "
                        f"{version_text(self.image.version)}")
            yield ProgressBar(total=len(self.image.data), show_eta=True, id="flash-bar")
            yield Static("", id="flash-rate")
            yield Static("", id="flash-log")
            yield Label("Escape aborts the update", id="flash-hint")

    def on_mount(self) -> None:
        self.run_transfer()

    def log_line(self, message: str) -> None:
        self.lines.append(message)
        self.query_one("#flash-log", Static).update("\n".join(self.lines))

    def on_progress(self, sent: int, total: int, rate: float) -> None:
        self.query_one("#flash-bar", ProgressBar).update(progress=sent)
        self.query_one("#flash-rate", Static).update(f"{sent}/{total} B  {rate:.1f} KiB/s")

    @work(exclusive=True, group="flash")
    async def run_transfer(self) -> None:
        try:
            await transfer(self.client, self.secret, self.image,
                           self.on_progress, self.log_line)
        except Exception as error:  # noqa: BLE001 - shown to the user
            self.finish(False, f"[b red]Failed:[/] {error}")
            return
        self.finish(True, "The device reboots now; rescan in a few seconds.")
        self.dismiss(True)

    def finish(self, ok: bool, message: str) -> None:
        self.done = True
        self.log_line(message)
        self.query_one("#flash-hint", Label).update("Escape closes")

    def action_close(self) -> None:
        if not self.done:
            # Cancelling the worker makes transfer() send ABORT to the device.
            self.workers.cancel_group(self, "flash")
            self.app.notify("Firmware update aborted", severity="warning")
        self.dismiss(False)


class ResetScreen(ModalScreen[bool]):
    BINDINGS = [Binding("y", "confirm", "Reboot"), Binding("escape,n", "cancel", "Cancel")]

    def __init__(self, target: str) -> None:
        super().__init__()
        self.target = target

    def compose(self) -> ComposeResult:
        yield Static(f"Reboot {self.target}?\nOn startup the door is assumed HALF "
                     "and suction returns to FULL.\nPress Y to reboot or Escape to cancel.",
                     markup=False)

    def action_confirm(self) -> None:
        self.dismiss(True)

    def action_cancel(self) -> None:
        self.dismiss(False)


async def request_reboot(client, secret: bytes) -> None:
    challenge = bytes(await client.read_gatt_char(RESET_UUID))
    if len(challenge) != 17:
        raise FlashError("Invalid reboot challenge")
    tag = hmac.digest(secret, b"CabControl-RESET-v1" + challenge[1:], "sha256")[:16]
    await client.write_gatt_char(RESET_UUID, tag, response=True)
    reply = bytes(await client.read_gatt_char(RESET_UUID))
    if len(reply) != 17 or reply[0] != 1:
        reason = {2: "secret mismatch", 3: "update or reboot in progress"}.get(
            reply[0] if reply else None, "invalid response")
        raise FlashError(f"Reboot refused: {reason}")


class DeviceScreen(Screen):
    BINDINGS = [
        Binding("n", "set_name", "Set name"),
        Binding("u", "update_firmware", "Update firmware"),
        Binding("r", "reboot", "Reboot"),
        Binding("escape", "disconnect", "Disconnect"),
        Binding("q", "app.quit", "Quit"),
    ]
    DEFAULT_CSS = """
    DeviceScreen #device { height: auto; padding: 0 1; }
    DeviceScreen #diag { height: 1fr; }
    DeviceScreen #age { height: 1; padding: 0 1; color: $text-muted; }
    """

    def __init__(self, device, label: str) -> None:
        super().__init__()
        self.device = device
        self.label = label
        self.client = None
        self.cab_name = label
        self.firmware = "?"
        self.firmware_version: tuple[int, ...] | None = None
        self.expect_reboot = False
        self.reset_accepted = False
        self.update_status = "?"
        self.last_update: float | None = None
        self.leaving = False

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static(f"Connecting to {self.label}...", id="device")
        table = DataTable(id="diag", cursor_type="none", zebra_stripes=True)
        table.add_column("Section", key="section")
        table.add_column("Signal", key="label")
        table.add_column("Value", key="value")
        yield table
        yield Static("", id="age")
        yield Footer()

    def on_mount(self) -> None:
        self.set_interval(1.0, self.refresh_age)
        self.connect()

    @work(exclusive=True, group="connection")
    async def connect(self) -> None:
        app: CabTuiApp = self.app  # type: ignore[assignment]
        self.client = app.client_factory(self.device, self.on_disconnected)
        try:
            await self.client.connect()
            info = bytes(await self.client.read_gatt_char(INFO_UUID))
            if len(info) >= 4:
                self.firmware_version = tuple(info[1:4])
                self.firmware = version_text(self.firmware_version)
            self.on_status(None, await self.client.read_gatt_char(STATUS_UUID))
            self.on_name(None, await self.client.read_gatt_char(NAME_UUID))
            self.on_diag(None, await self.client.read_gatt_char(DIAG_UUID))
            await self.client.start_notify(DIAG_UUID, self.on_diag)
            await self.client.start_notify(STATUS_UUID, self.on_status)
            await self.client.start_notify(NAME_UUID, self.on_name)
        except Exception as error:  # noqa: BLE001 - surface every BLE failure
            self.app.notify(f"Connection failed: {error}", severity="error")
            await self.close()

    def on_diag(self, _characteristic, data) -> None:
        try:
            diag = parse_diagnostics(data)
        except FlashError as error:
            self.app.notify(str(error), severity="error")
            return
        self.last_update = time.monotonic()
        table = self.query_one("#diag", DataTable)
        for section, label, value in diagnostics_rows(diag):
            key = f"{section}/{label}"
            if key in table.rows:
                table.update_cell(key, "value", value)
            else:
                table.add_row(section, label, value, key=key)
        self.refresh_age()

    def on_status(self, _characteristic, data) -> None:
        status = parse_status(data)
        # The device reboots ~1 s after READY; this may arrive before the
        # flash dialog has finished.
        if status.state == STATE_READY:
            self.expect_reboot = True
        self.update_status = status.describe()
        self.refresh_device()

    def on_name(self, _characteristic, data) -> None:
        self.cab_name = bytes(data).decode("ascii", "replace")
        self.refresh_device()

    def refresh_device(self) -> None:
        address = getattr(self.device, "address", self.device)
        self.query_one("#device", Static).update(
            f"[b]{self.cab_name}[/b]  {address}\n"
            f"Firmware {self.firmware}   Update: {self.update_status}")
        self.app.sub_title = self.cab_name

    def refresh_age(self) -> None:
        if self.last_update is None:
            return
        age = time.monotonic() - self.last_update
        text = f"Updated {age:.0f} s ago"
        self.query_one("#age", Static).update(text + ("  (stale)" if age > 3 else ""))

    def on_disconnected(self, _client) -> None:
        if not self.leaving:
            if self.reset_accepted:
                self.app.notify(f"{self.cab_name} accepted the reboot. Reconnect after startup.")
            elif self.expect_reboot:
                self.app.notify(f"{self.cab_name} is rebooting to install the update. "
                                "Rescan in a few seconds.", timeout=10)
            else:
                self.app.notify(f"{self.cab_name} disconnected", severity="warning")
            self.leaving = True
            self.leave()

    def leave(self) -> None:
        while self.app.screen is not self:
            self.app.pop_screen()
        self.app.pop_screen()

    async def close(self) -> None:
        if self.leaving:
            return
        self.leaving = True
        if self.client is not None:
            try:
                await self.client.disconnect()
            except Exception:  # noqa: BLE001 - already gone
                pass
        self.leave()

    async def action_disconnect(self) -> None:
        await self.close()

    @work(exclusive=True, group="reboot")
    async def action_reboot(self) -> None:
        from bleak.exc import BleakError

        client = self.client
        if client is None or not client.is_connected or self.reset_accepted:
            return
        if client.services.get_characteristic(RESET_UUID) is None:
            self.app.notify("This firmware does not support remote reboot.", severity="warning")
            return
        if not await self.app.push_screen_wait(ResetScreen(f"{self.cab_name} ({self.device.address})")):
            return
        if self.leaving or not client.is_connected:
            return
        try:
            await request_reboot(client, self.app.load_secret())
        except (FlashError, BleakError, OSError, ValueError) as error:
            self.app.notify(f"Reboot not confirmed: {error}", severity="error")
            return
        self.reset_accepted = True
        self.app.notify("Reboot accepted; waiting for the cabinet to disconnect.")

    @work(exclusive=True, group="name")
    async def action_set_name(self) -> None:
        app: CabTuiApp = self.app  # type: ignore[assignment]
        try:
            secret = app.load_secret()
        except FlashError as error:
            self.app.notify(str(error), severity="error")
            return
        name = await self.app.push_screen_wait(NameScreen(self.cab_name))
        if name is None or self.client is None:
            return
        try:
            await set_name(self.client, secret, name)
        except Exception as error:  # noqa: BLE001 - shown to the user
            self.app.notify(str(error), severity="error")
            return
        self.on_name(None, await self.client.read_gatt_char(NAME_UUID))
        self.app.notify(f"Name set to {name!r}. The advertised name updates after "
                        "disconnecting.")


    @work(exclusive=True, group="firmware")
    async def action_update_firmware(self) -> None:
        app: CabTuiApp = self.app  # type: ignore[assignment]
        if self.client is None or not self.client.is_connected:
            self.app.notify("Not connected", severity="error")
            return
        try:
            secret = app.load_secret()
        except FlashError as error:
            self.app.notify(str(error), severity="error")
            return
        image = await self.app.push_screen_wait(
            FirmwareScreen(app.args.firmware, self.firmware_version))
        if image is None or self.client is None or self.leaving:
            return
        if await self.app.push_screen_wait(
                FlashScreen(self.client, secret, image, self.cab_name)):
            self.expect_reboot = True


class BatchSelectScreen(Screen):
    """Picks the image and the cabinets for a batch update; keeps scanning."""

    BINDINGS = [
        Binding("space", "toggle", "Toggle"),
        Binding("a", "select_outdated", "Select outdated"),
        Binding("s", "start", "Start update"),
        Binding("escape", "back", "Back"),
    ]
    DEFAULT_CSS = """
    BatchSelectScreen #batch-path-label { padding: 0 1; }
    BatchSelectScreen #batch-image { height: auto; padding: 0 1; }
    BatchSelectScreen #batch-image.invalid { color: $error; }
    BatchSelectScreen #batch-devices { height: 1fr; }
    BatchSelectScreen #batch-hint { height: auto; padding: 0 1; color: $text-muted; }
    """

    def __init__(self, secret: bytes, firmware_path: Path,
                 devices: dict[str, ScannedDevice]) -> None:
        super().__init__()
        self.secret = secret
        self.firmware_path = firmware_path
        self.devices = dict(devices)
        self.image: Image | None = None
        self.selected: set[str] = set()
        # Rows the user toggled keep their choice; others follow "outdated".
        self.touched: set[str] = set()
        self.scanner = None

    def compose(self) -> ComposeResult:
        yield Header()
        yield Label("Firmware image (edit and press Enter to reload)", id="batch-path-label")
        yield Input(value=str(self.firmware_path), id="batch-path")
        yield Static("", id="batch-image")
        table = DataTable(id="batch-devices", cursor_type="row")
        table.add_column("Update", key="selected")
        table.add_column("Name", key="name")
        table.add_column("Firmware", key="firmware")
        table.add_column("Status", key="status")
        table.add_column("Address", key="address")
        table.add_column("RSSI", key="rssi")
        yield table
        yield Static("Enter/space toggles a cabinet, a selects all outdated, s starts. "
                     "Up to two cabinets update at once.", id="batch-hint")
        yield Footer()

    async def on_mount(self) -> None:
        self.app.sub_title = "Batch update"
        self.load_image(str(self.firmware_path))
        self.query_one("#batch-devices", DataTable).focus()
        app: CabTuiApp = self.app  # type: ignore[assignment]
        self.scanner = app.scanner_factory(self.on_detected)
        try:
            await self.scanner.start()
        except Exception as error:  # noqa: BLE001 - e.g. Bluetooth off/denied
            self.scanner = None
            self.app.notify(f"Scan failed: {error}", severity="error")

    async def on_unmount(self) -> None:
        await self.stop_scan()

    async def stop_scan(self) -> None:
        if self.scanner is not None:
            scanner, self.scanner = self.scanner, None
            try:
                await scanner.stop()
            except Exception:  # noqa: BLE001 - already stopped
                pass

    def status(self, device: ScannedDevice) -> str:
        if self.image is None:
            return "-"
        if device.firmware is None:
            return "unknown"
        return "outdated" if device.firmware < self.image.version else "up to date"

    def selectable(self, device: ScannedDevice) -> bool:
        return self.status(device) != "up to date"

    def refresh_row(self, address: str) -> None:
        device = self.devices[address]
        if not self.selectable(device):
            self.selected.discard(address)
        elif address not in self.touched:
            if self.status(device) == "outdated":
                self.selected.add(address)
            else:
                self.selected.discard(address)
        # Escape the opening bracket so the table doesn't parse [x] as markup.
        mark = r"\[x]" if address in self.selected else (
            "[ ]" if self.selectable(device) else " - ")
        values = {"selected": mark, "name": device.name,
                  "firmware": firmware_text(device.firmware),
                  "status": self.status(device), "address": address,
                  "rssi": str(device.rssi)}
        table = self.query_one("#batch-devices", DataTable)
        if address in table.rows:
            for column, value in values.items():
                table.update_cell(address, column, value)
        else:
            table.add_row(*values.values(), key=address)

    def refresh_rows(self) -> None:
        for address in self.devices:
            self.refresh_row(address)
        self.query_one("#batch-hint", Static).update(
            f"{len(self.selected)} selected. Enter/space toggles a cabinet, a selects "
            "all outdated, s starts. Up to two cabinets update at once.")

    def on_detected(self, device, adv) -> None:
        scanned = parse_advertisement(device, adv)
        if scanned is None:
            return
        self.devices[scanned.address] = scanned
        self.refresh_rows()

    def load_image(self, path_text: str) -> None:
        self.image, text = describe_image(path_text, None)
        info = self.query_one("#batch-image", Static)
        info.update(text)
        info.set_class(self.image is None, "invalid")
        self.refresh_rows()

    @on(Input.Submitted, "#batch-path")
    def path_submitted(self, event: Input.Submitted) -> None:
        self.load_image(event.value)
        self.query_one("#batch-devices", DataTable).focus()

    def toggle(self, address: str) -> None:
        device = self.devices[address]
        if not self.selectable(device):
            self.app.notify(f"{device.name} already runs {firmware_text(device.firmware)}",
                            severity="warning")
            return
        self.touched.add(address)
        self.selected ^= {address}
        self.refresh_rows()

    @on(DataTable.RowSelected, "#batch-devices")
    def row_selected(self, event: DataTable.RowSelected) -> None:
        if event.row_key.value in self.devices:
            self.toggle(event.row_key.value)

    def action_toggle(self) -> None:
        table = self.query_one("#batch-devices", DataTable)
        if table.row_count:
            row_key = table.coordinate_to_cell_key(table.cursor_coordinate).row_key
            self.toggle(row_key.value)

    def action_select_outdated(self) -> None:
        self.touched = set(self.devices)
        self.selected = {address for address, device in self.devices.items()
                         if self.status(device) == "outdated"}
        self.refresh_rows()

    async def action_start(self) -> None:
        # Re-read so a rebuild since the screen opened is picked up.
        self.load_image(self.query_one("#batch-path", Input).value)
        if self.image is None:
            self.app.notify("Choose a valid firmware image first", severity="error")
            return
        if not self.selected:
            self.app.notify("Select at least one cabinet", severity="error")
            return
        table = self.query_one("#batch-devices", DataTable)
        targets = [self.devices[key.value] for key in table.rows
                   if key.value in self.selected]
        await self.stop_scan()
        self.app.switch_screen(BatchScreen(self.secret, self.image, targets))

    async def action_back(self) -> None:
        await self.stop_scan()
        self.app.pop_screen()


class BatchScreen(Screen):
    """Runs two update slots, keeping each until the install is confirmed."""

    BINDINGS = [Binding("escape", "stop", "Abort / back")]
    DEFAULT_CSS = """
    BatchScreen #batch-title { height: auto; padding: 0 1; }
    BatchScreen #batch-results { height: 1fr; }
    BatchScreen #batch-current { height: 1; padding: 0 1; }
    BatchScreen ProgressBar { padding: 0 1; }
    BatchScreen #batch-log { height: 1; padding: 0 1; color: $text-muted; }
    BatchScreen #batch-hint { height: 1; padding: 0 1; }
    """

    def __init__(self, secret: bytes, image: Image, targets: list[ScannedDevice]) -> None:
        super().__init__()
        self.secret = secret
        self.image = image
        self.targets = targets
        self.results: dict[str, bool] = {}
        self.started: set[str] = set()
        self.confirmations: dict[str, asyncio.Event] = {}
        self.last_seen: dict[str, tuple[int, int, int] | None] = {}
        self.done = False
        self.stopping = False

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static(f"Updating {len(self.targets)} cabinet(s) to "
                     f"[b]{version_text(self.image.version)}[/b]", id="batch-title")
        table = DataTable(id="batch-results", cursor_type="none")
        table.add_column("Name", key="name")
        table.add_column("Address", key="address")
        table.add_column("From", key="from")
        table.add_column("Result", key="result", width=48)
        yield table
        yield Static("", id="batch-current")
        yield ProgressBar(total=len(self.targets), show_eta=False, id="batch-bar")
        yield Static("", id="batch-log")
        yield Static("Escape aborts the batch", id="batch-hint")
        yield Footer()

    def on_mount(self) -> None:
        self.app.sub_title = "Batch update"
        table = self.query_one("#batch-results", DataTable)
        for target in self.targets:
            table.add_row(target.name, target.address, firmware_text(target.firmware),
                          "queued", key=target.address)
        self.run_batch()

    def set_result(self, address: str, text: str) -> None:
        self.query_one("#batch-results", DataTable).update_cell(address, "result", text)

    def log_line(self, message: str) -> None:
        self.query_one("#batch-log", Static).update(message)

    @work(exclusive=True, group="batch")
    async def run_batch(self) -> None:
        pending = iter(self.targets)

        async def run_slot() -> None:
            for target in pending:
                self.started.add(target.address)
                try:
                    await self.update_one(target)
                except Exception as error:  # noqa: BLE001 - recorded, batch continues
                    self.results[target.address] = False
                    self.set_result(target.address, f"[b red]FAILED[/]: {error}")
                else:
                    self.results[target.address] = True
                    self.set_result(target.address,
                                    f"[b green]updated[/] to {version_text(self.image.version)}")
                self.query_one("#batch-bar", ProgressBar).update(progress=len(self.results))

        app: CabTuiApp = self.app  # type: ignore[assignment]
        scanner = app.scanner_factory(self.on_advertisement)
        try:
            try:
                await scanner.start()
            except Exception as error:
                for target in self.targets:
                    self.results[target.address] = False
                    self.set_result(target.address, f"Scan failed: {error}")
            else:
                slots = [asyncio.create_task(run_slot()) for _ in range(2)]
                try:
                    await asyncio.gather(*slots)
                finally:
                    for slot in slots:
                        if not slot.done() and not self.stopping:
                            slot.cancel()
                    await asyncio.gather(*slots, return_exceptions=True)
        except asyncio.CancelledError:
            for target in self.targets:
                if target.address not in self.results:
                    self.set_result(target.address, "[b yellow]aborted[/]"
                                    if target.address in self.started else "skipped")
            raise
        finally:
            try:
                await scanner.stop()
            except Exception as error:
                self.app.notify(f"Could not stop batch scan: {error}", severity="warning")
            self.finish()

    def on_advertisement(self, device, adv) -> None:
        event = self.confirmations.get(device.address)
        if event is not None and (scanned := parse_advertisement(device, adv)) is not None:
            self.last_seen[device.address] = scanned.firmware
            if scanned.firmware == self.image.version:
                event.set()

    async def update_one(self, target: ScannedDevice) -> None:
        app: CabTuiApp = self.app  # type: ignore[assignment]
        address = target.address
        def on_progress(sent: int, total: int, rate: float) -> None:
            self.set_result(address, f"uploading {sent * 100 // total}%  {rate:.1f} KiB/s")

        disconnected = asyncio.Event()
        client = app.client_factory(target.device, lambda _client: disconnected.set())
        self.set_result(address, "connecting")
        try:
            await client.connect()
            self.set_result(address, "uploading")
            await transfer(client, self.secret, self.image, on_progress,
                           lambda message: self.log_line(f"{target.name}: {message}"))
            confirmed = self.confirmations[address] = asyncio.Event()
            # The device drops the link ~1 s after READY to reboot into SFU.
            self.set_result(address, "rebooting")
            try:
                await asyncio.wait_for(disconnected.wait(), REBOOT_DISCONNECT_TIMEOUT)
            except asyncio.TimeoutError:
                pass
        finally:
            if client.is_connected:
                try:
                    await client.disconnect()
                except Exception:  # noqa: BLE001 - already gone
                    pass

        self.set_result(address, "installing, waiting for it to come back")
        try:
            await asyncio.wait_for(confirmed.wait(), INSTALL_TIMEOUT)
        except asyncio.TimeoutError:
            seen = self.last_seen.get(address)
            raise FlashError("uploaded, but not seen after reboot" if seen is None else
                             f"uploaded, but runs {firmware_text(seen)} after reboot")
        finally:
            self.confirmations.pop(address, None)

    def finish(self) -> None:
        self.done = True
        updated = sum(self.results.values())
        failed = len(self.results) - updated
        skipped = len(self.targets) - len(self.results)
        summary = f"{updated} updated, {failed} failed" + (
            f", {skipped} aborted/skipped" if skipped else "")
        self.query_one("#batch-current", Static).update(f"Done: {summary}")
        self.query_one("#batch-hint", Static).update("Escape returns to the scan list")
        self.app.notify(f"Batch update: {summary}",
                        severity="information" if updated == len(self.targets) else "warning")

    def action_stop(self) -> None:
        if not self.done:
            if not self.stopping:
                self.stopping = True
                self.workers.cancel_group(self, "batch")
                self.app.notify("Stopping active updates", severity="warning")
            return
        self.app.pop_screen()


class ScanScreen(Screen):
    BINDINGS = [
        Binding("b", "batch_update", "Batch update"),
        Binding("r", "rescan", "Rescan"),
        Binding("q", "app.quit", "Quit"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self.scanner = None
        self.devices: dict[str, ScannedDevice] = {}

    def compose(self) -> ComposeResult:
        yield Header()
        yield Static("Scanning for CabControl cabinets... select one and press Enter, "
                     "or press b to update several.",
                     id="hint")
        table = DataTable(id="devices", cursor_type="row")
        table.add_column("Name", key="name")
        table.add_column("Firmware", key="firmware")
        table.add_column("Address", key="address")
        table.add_column("RSSI", key="rssi")
        yield table
        yield Footer()

    async def on_screen_resume(self) -> None:
        self.app.sub_title = "Scan"
        await self.start_scan()

    async def on_screen_suspend(self) -> None:
        await self.stop_scan()

    async def start_scan(self) -> None:
        app: CabTuiApp = self.app  # type: ignore[assignment]
        await self.stop_scan()
        self.devices.clear()
        self.query_one("#devices", DataTable).clear()
        self.scanner = app.scanner_factory(self.on_detected)
        try:
            await self.scanner.start()
        except Exception as error:  # noqa: BLE001 - e.g. Bluetooth off/denied
            self.scanner = None
            self.app.notify(f"Scan failed: {error}", severity="error")

    async def stop_scan(self) -> None:
        if self.scanner is not None:
            scanner, self.scanner = self.scanner, None
            try:
                await scanner.stop()
            except Exception:  # noqa: BLE001 - already stopped
                pass

    def on_detected(self, device, adv) -> None:
        scanned = parse_advertisement(device, adv)
        if scanned is None:
            return
        address, firmware = scanned.address, firmware_text(scanned.firmware)
        table = self.query_one("#devices", DataTable)
        if address in self.devices:
            table.update_cell(address, "name", scanned.name)
            table.update_cell(address, "firmware", firmware)
            table.update_cell(address, "rssi", str(scanned.rssi))
        else:
            table.add_row(scanned.name, firmware, address, str(scanned.rssi), key=address)
        self.devices[address] = scanned
        app: CabTuiApp = self.app  # type: ignore[assignment]
        if app.wants_auto_connect(address, scanned.name):
            app.auto_connect_done = True
            self.open_device(address)

    def open_device(self, address: str) -> None:
        scanned = self.devices[address]
        self.app.push_screen(DeviceScreen(scanned.device, scanned.name))

    def action_batch_update(self) -> None:
        app: CabTuiApp = self.app  # type: ignore[assignment]
        try:
            secret = app.load_secret()
        except FlashError as error:
            self.app.notify(str(error), severity="error")
            return
        self.app.push_screen(BatchSelectScreen(secret, app.args.firmware, self.devices))

    @on(DataTable.RowSelected, "#devices")
    def row_selected(self, event: DataTable.RowSelected) -> None:
        if event.row_key.value in self.devices:
            self.open_device(event.row_key.value)

    async def action_rescan(self) -> None:
        await self.start_scan()


class CabTuiApp(App):
    TITLE = "CabControl"

    def __init__(self, args: argparse.Namespace,
                 client_factory: ClientFactory = default_client_factory,
                 scanner_factory: ScannerFactory = default_scanner_factory) -> None:
        super().__init__()
        self.args = args
        self.client_factory = client_factory
        self.scanner_factory = scanner_factory
        self.auto_connect_done = False

    def on_mount(self) -> None:
        self.push_screen(ScanScreen())

    def wants_auto_connect(self, address: str, name: str) -> bool:
        if self.auto_connect_done:
            return False
        if self.args.address:
            return address.lower() == self.args.address.lower()
        return self.args.name is not None and name == self.args.name

    def load_secret(self) -> bytes:
        return load_secret(self.args.secret)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--name", help="connect to the first cabinet with this name")
    parser.add_argument("--address", help="connect to this BLE address/UUID")
    parser.add_argument("--secret", type=Path, default=DEFAULT_SECRET,
                        help="secret header, needed to set the name and update "
                             "firmware (default: include/ota_secret.h)")
    parser.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE,
                        help="default firmware image for updates "
                             "(default: .pio/build/nanorp2040connect/firmware.bin)")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    CabTuiApp(parse_args(argv)).run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
