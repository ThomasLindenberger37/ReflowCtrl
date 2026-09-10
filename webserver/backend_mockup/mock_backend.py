"""FastAPI backend for the reflow oven web interface."""

import asyncio
import math
import time
from collections import deque
from contextlib import asynccontextmanager, suppress
from datetime import datetime
from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field, model_validator

AMBIENT_TEMPERATURE = 23.5
SAFE_START_TEMPERATURE = 40.0
PROFILE_DURATION = 220
FRONTEND_DIR = Path(__file__).resolve().parent.parent / "frontend"


class DebugTerminal:
    """Bounded string buffer that mimics a small serial output."""

    def __init__(self, max_lines: int = 250) -> None:
        self._lines: deque[dict] = deque(maxlen=max_lines)
        self._next_id = 1

    def write(self, message: object) -> None:
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        for text_line in str(message).splitlines() or [""]:
            self._lines.append(
                {
                    "id": self._next_id,
                    "text": f"[{timestamp}] {text_line}",
                }
            )
            self._next_id += 1

    def read_after(self, cursor: int) -> dict:
        lines = [line for line in self._lines if line["id"] > cursor]
        next_cursor = lines[-1]["id"] if lines else max(cursor, self._next_id - 1)
        return {"lines": lines, "next_cursor": next_cursor}


debug_terminal = DebugTerminal()


def debug_print(message: object) -> None:
    """Write a string to the web terminal and the backend console."""
    debug_terminal.write(message)
    print(message, flush=True)


class Config(BaseModel):
    wifi_ssid: str = Field(max_length=64)
    wifi_password: str = Field(max_length=128)
    mqtt_broker: str = Field(max_length=255)
    mqtt_port: int = Field(ge=1, le=65535)
    mqtt_username: str = Field(default="", max_length=128)
    mqtt_password: str = Field(default="", max_length=128)
    mqtt_enabled: bool = False
    spi_sck_pin: int = Field(default=14, ge=0, le=16)
    spi_mosi_pin: int = Field(default=13, ge=0, le=16)
    spi_miso_pin: int = Field(default=12, ge=0, le=16)
    relay_pin: int = Field(default=5, ge=0, le=16)
    button_pin: int = Field(default=4, ge=0, le=16)
    led_pin: int = Field(default=2, ge=0, le=16)
    setup_complete: bool = False

    @model_validator(mode="after")
    def validate_unique_pins(self):
        pins = {
            "SPI SCK": self.spi_sck_pin,
            "SPI MOSI": self.spi_mosi_pin,
            "SPI MISO": self.spi_miso_pin,
            "relay": self.relay_pin,
            "button": self.button_pin,
            "LED": self.led_pin,
        }
        if len(set(pins.values())) != len(pins):
            raise ValueError("Every hardware function must use a unique GPIO pin")
        return self


class SoakProfile(BaseModel):
    start_temperature_c: float = Field(gt=0, le=300)
    end_temperature_c: float = Field(gt=0, le=300)
    duration_s: float = Field(gt=0, le=1800)


class ReflowStageProfile(BaseModel):
    liquidus_temperature_c: float = Field(gt=0, le=350)
    peak_temperature_c: float = Field(gt=0, le=350)
    time_above_liquidus_s: float = Field(gt=0, le=600)


class CoolingProfile(BaseModel):
    max_cooling_rate_c_per_s: float = Field(gt=0, le=20)


class ReflowProfile(BaseModel):
    name: str = Field(min_length=1, max_length=64)
    max_ramp_rate_c_per_s: float = Field(gt=0, le=20)
    soak: SoakProfile
    reflow: ReflowStageProfile
    cooling: CoolingProfile

    @model_validator(mode="after")
    def validate_temperature_order(self):
        temperatures = (
            self.soak.start_temperature_c,
            self.soak.end_temperature_c,
            self.reflow.liquidus_temperature_c,
            self.reflow.peak_temperature_c,
        )
        if not all(
            left < right for left, right in zip(temperatures, temperatures[1:], strict=False)
        ):
            raise ValueError("Temperatures must satisfy soak start < soak end < liquidus < peak")
        soak_rate = (
            self.soak.end_temperature_c - self.soak.start_temperature_c
        ) / self.soak.duration_s
        if soak_rate > self.max_ramp_rate_c_per_s:
            raise ValueError("Soak heating rate exceeds the maximum ramp rate")
        peak_delta = self.reflow.peak_temperature_c - self.reflow.liquidus_temperature_c
        minimum_peak_time = peak_delta * (
            (1 / self.max_ramp_rate_c_per_s) + (1 / self.cooling.max_cooling_rate_c_per_s)
        )
        if self.reflow.time_above_liquidus_s < minimum_peak_time:
            raise ValueError(
                "Time above liquidus is too short for the configured heating and cooling rates"
            )
        return self


class ActiveProfileSelection(BaseModel):
    id: str = Field(min_length=1, max_length=32)


class OvenController:
    """Small in-memory controller matching the embedded REST data model."""

    def __init__(self) -> None:
        self.running = False
        self.state = "idle"
        self.temperature = AMBIENT_TEMPERATURE
        self.target_temperature = 0.0
        self.heater = False
        self.elapsed_seconds = 0
        self._started_at = 0.0
        self._last_update = time.monotonic()
        self._lock = asyncio.Lock()

    @staticmethod
    def profile(elapsed: int) -> tuple[str, float]:
        if elapsed < 50:
            return "preheat", 25.0 + (125.0 * elapsed / 50.0)
        if elapsed < 100:
            return "soak", 150.0 + (30.0 * (elapsed - 50) / 50.0)
        if elapsed < 145:
            return "reflow", 180.0 + (55.0 * (elapsed - 100) / 45.0)
        if elapsed < PROFILE_DURATION:
            return "cooling", max(35.0, 235.0 - (200.0 * (elapsed - 145) / 75.0))
        return "complete", 0.0

    async def start(self) -> dict:
        async with self._lock:
            if self.running:
                raise HTTPException(status_code=409, detail="Reflow process already running")
            if self.temperature > SAFE_START_TEMPERATURE:
                debug_print(
                    f"SAFETY: Start blocked at {self.temperature:.1f} °C "
                    f"(limit {SAFE_START_TEMPERATURE:.1f} °C)"
                )
                raise HTTPException(
                    status_code=409,
                    detail=f"Oven must cool to {SAFE_START_TEMPERATURE:.1f} °C before starting",
                )
            self.running = True
            self.state = "preheat"
            self.elapsed_seconds = 0
            self.target_temperature = 25.0
            self.heater = True
            self._started_at = time.monotonic()
            self._last_update = self._started_at
            debug_print("PROCESS: Reflow profile started")
            return self._status()

    async def stop(self) -> dict:
        async with self._lock:
            self.running = False
            self.state = "idle"
            self.target_temperature = 0.0
            self.heater = False
            self.elapsed_seconds = 0
            self._last_update = time.monotonic()
            debug_print("PROCESS: Stop requested, heater disabled")
            return self._status()

    async def update(self) -> None:
        async with self._lock:
            now = time.monotonic()
            delta = min(now - self._last_update, 2.0)
            self._last_update = now

            if self.running:
                self.elapsed_seconds = min(int(now - self._started_at), PROFILE_DURATION)
                previous_state = self.state
                self.state, self.target_temperature = self.profile(self.elapsed_seconds)
                if self.state != previous_state:
                    debug_print(f"STATE: {previous_state} -> {self.state}")
                self.heater = self.state not in {"cooling", "complete"} and (
                    self.temperature < self.target_temperature + 1.0
                )

                if self.heater:
                    # A deliberately simple thermal model for the local backend.
                    heating_rate = 3.2 * max(0.25, 1.0 - self.temperature / 320.0)
                    self.temperature += heating_rate * delta
                else:
                    cooling_rate = 0.015 * max(0.0, self.temperature - AMBIENT_TEMPERATURE)
                    self.temperature -= cooling_rate * delta

                if self.state == "complete":
                    self.running = False
                    self.heater = False
            else:
                # Let the oven slowly return to room temperature.
                difference = self.temperature - AMBIENT_TEMPERATURE
                self.temperature -= difference * min(0.02 * delta, 1.0)
                if math.isclose(self.temperature, AMBIENT_TEMPERATURE, abs_tol=0.05):
                    self.temperature = AMBIENT_TEMPERATURE

            debug_print(f"TEMPERATURE: {self.temperature:.1f} °C")

    async def status(self) -> dict:
        async with self._lock:
            return self._status()

    def _status(self) -> dict:
        return {
            "state": self.state,
            "temperature": round(self.temperature, 1),
            "target_temperature": round(self.target_temperature, 1),
            "heater": self.heater,
            "elapsed_seconds": self.elapsed_seconds,
        }


oven = OvenController()
config = Config(
    wifi_ssid="MyWifi",
    wifi_password="",
    mqtt_broker="192.168.1.20",
    mqtt_port=1883,
)
profile_defaults: dict[str, ReflowProfile] = {
    "builtin-hxp602": ReflowProfile(
        name="HXP-602",
        max_ramp_rate_c_per_s=2.0,
        soak=SoakProfile(start_temperature_c=140, end_temperature_c=155, duration_s=90),
        reflow=ReflowStageProfile(
            liquidus_temperature_c=165,
            peak_temperature_c=180,
            time_above_liquidus_s=40,
        ),
        cooling=CoolingProfile(max_cooling_rate_c_per_s=3.0),
    ),
    "builtin-sac": ReflowProfile(
        name="SAC Lead-Free",
        max_ramp_rate_c_per_s=2.0,
        soak=SoakProfile(start_temperature_c=150, end_temperature_c=175, duration_s=100),
        reflow=ReflowStageProfile(
            liquidus_temperature_c=217,
            peak_temperature_c=240,
            time_above_liquidus_s=55,
        ),
        cooling=CoolingProfile(max_cooling_rate_c_per_s=3.0),
    ),
}
custom_template = ReflowProfile(
    name="Custom 1",
    max_ramp_rate_c_per_s=2.0,
    soak=SoakProfile(start_temperature_c=140, end_temperature_c=160, duration_s=90),
    reflow=ReflowStageProfile(
        liquidus_temperature_c=217, peak_temperature_c=235, time_above_liquidus_s=45
    ),
    cooling=CoolingProfile(max_cooling_rate_c_per_s=3.0),
)
profile_slots = [
    {"id": profile_id, "type": "builtin", "occupied": True, "configuration": profile}
    for profile_id, profile in profile_defaults.items()
] + [
    {
        "id": f"custom-{number}",
        "type": "custom",
        "occupied": False,
        "configuration": custom_template.model_copy(update={"name": f"Custom {number}"}),
    }
    for number in range(1, 7)
]
active_profile_id = "builtin-hxp602"


async def controller_loop() -> None:
    while True:
        await oven.update()
        await asyncio.sleep(1)


@asynccontextmanager
async def lifespan(_: FastAPI):
    debug_print("BOOT: Reflow Controller backend ready")
    debug_print("SERIAL: Debug terminal initialized")
    task = asyncio.create_task(controller_loop())
    yield
    task.cancel()
    with suppress(asyncio.CancelledError):
        await task


app = FastAPI(title="Reflow Oven API", version="1.0.0", lifespan=lifespan)


@app.get("/api/status")
async def get_status():
    return await oven.status()


@app.post("/api/start")
async def start_reflow():
    return await oven.start()


@app.post("/api/stop")
async def stop_reflow():
    return await oven.stop()


@app.get("/api/config", response_model=Config)
async def get_config():
    return config


@app.get("/api/setup/status")
async def get_setup_status():
    return {"setup_required": not config.setup_complete}


@app.put("/api/config", response_model=Config)
async def update_config(new_config: Config):
    global config
    config = new_config
    debug_print("CONFIG: Settings updated")
    return config


@app.get("/api/profiles")
async def get_profiles():
    return {
        "version": 1,
        "active_profile_id": active_profile_id,
        "profiles": [
            {**slot, "configuration": slot["configuration"].model_dump()} for slot in profile_slots
        ],
    }


@app.put("/api/profiles/active")
async def select_active_profile(selection: ActiveProfileSelection):
    global active_profile_id
    if oven.running:
        raise HTTPException(status_code=409, detail="Profile cannot be changed during reflow")
    slot = next((item for item in profile_slots if item["id"] == selection.id), None)
    if not slot or not slot["occupied"]:
        raise HTTPException(status_code=404, detail="Profile not found")
    active_profile_id = selection.id
    debug_print(f"PROFILE: Active profile set to {active_profile_id}")
    return {"selected": True}


def find_profile_slot(profile_id: str) -> dict:
    slot = next((item for item in profile_slots if item["id"] == profile_id), None)
    if not slot:
        raise HTTPException(status_code=404, detail="Profile not found")
    return slot


def profile_preview(profile: ReflowProfile) -> dict:
    ambient = 25.0
    points = [{"time_s": 0.0, "temperature_c": ambient, "phase": "ramp-up"}]
    time_value = 0.0

    ramp_up_duration = (profile.soak.start_temperature_c - ambient) / profile.max_ramp_rate_c_per_s
    ramp_to_reflow_duration = (
        profile.reflow.liquidus_temperature_c - profile.soak.end_temperature_c
    ) / profile.max_ramp_rate_c_per_s
    peak_delta = profile.reflow.peak_temperature_c - profile.reflow.liquidus_temperature_c
    minimum_peak_ascent = peak_delta / profile.max_ramp_rate_c_per_s
    minimum_peak_descent = peak_delta / profile.cooling.max_cooling_rate_c_per_s
    spare_time = profile.reflow.time_above_liquidus_s - minimum_peak_ascent - minimum_peak_descent
    peak_ascent_duration = minimum_peak_ascent + spare_time / 2
    peak_descent_duration = minimum_peak_descent + spare_time / 2
    cooling_duration = (
        profile.reflow.liquidus_temperature_c - ambient
    ) / profile.cooling.max_cooling_rate_c_per_s
    total_duration = (
        ramp_up_duration
        + profile.soak.duration_s
        + ramp_to_reflow_duration
        + peak_ascent_duration
        + peak_descent_duration
        + cooling_duration
    )
    sample_period = max(1.0, total_duration / (160 - 1 - 6))

    def segment(start: float, end: float, duration: float, phase: str) -> None:
        nonlocal time_value
        segment_start = time_value
        sample_count = max(1, math.ceil(duration / sample_period))
        for sample in range(1, sample_count + 1):
            progress = sample / sample_count
            time_value = segment_start + duration * progress
            points.append(
                {
                    "time_s": time_value,
                    "temperature_c": start + (end - start) * progress,
                    "phase": phase,
                }
            )

    segment(
        ambient,
        profile.soak.start_temperature_c,
        ramp_up_duration,
        "ramp-up",
    )
    segment(
        profile.soak.start_temperature_c,
        profile.soak.end_temperature_c,
        profile.soak.duration_s,
        "soak",
    )
    segment(
        profile.soak.end_temperature_c,
        profile.reflow.liquidus_temperature_c,
        ramp_to_reflow_duration,
        "ramp-to-reflow",
    )
    segment(
        profile.reflow.liquidus_temperature_c,
        profile.reflow.peak_temperature_c,
        peak_ascent_duration,
        "above-liquidus",
    )
    segment(
        profile.reflow.peak_temperature_c,
        profile.reflow.liquidus_temperature_c,
        peak_descent_duration,
        "peak-and-descent",
    )
    segment(
        profile.reflow.liquidus_temperature_c,
        ambient,
        cooling_duration,
        "cooling",
    )
    return {
        "valid": True,
        "warnings": [],
        "errors": [],
        "summary": {
            "duration_s": time_value,
            "peak_temperature_c": profile.reflow.peak_temperature_c,
            "time_above_liquidus_s": profile.reflow.time_above_liquidus_s,
            "max_ramp_rate_c_per_s": profile.max_ramp_rate_c_per_s,
        },
        "points": points,
    }


@app.post("/api/profiles/preview")
async def preview_profile(profile: ReflowProfile):
    return profile_preview(profile)


@app.get("/api/profiles/{profile_id}")
async def get_profile(profile_id: str):
    slot = find_profile_slot(profile_id)
    return {**slot, "configuration": slot["configuration"].model_dump()}


@app.put("/api/profiles/{profile_id}")
async def update_profile(profile_id: str, profile: ReflowProfile):
    slot = find_profile_slot(profile_id)
    slot["configuration"] = profile
    slot["occupied"] = True
    return {**slot, "configuration": profile.model_dump()}


@app.post("/api/profiles/{profile_id}/reset")
async def reset_profile(profile_id: str):
    slot = find_profile_slot(profile_id)
    if slot["type"] == "builtin":
        slot["configuration"] = profile_defaults[profile_id].model_copy(deep=True)
        slot["occupied"] = True
    else:
        number = int(profile_id.removeprefix("custom-"))
        slot["configuration"] = custom_template.model_copy(update={"name": f"Custom {number}"})
        slot["occupied"] = False
    return {**slot, "configuration": slot["configuration"].model_dump()}


@app.delete("/api/profiles/{profile_id}")
async def clear_profile(profile_id: str):
    global active_profile_id
    slot = find_profile_slot(profile_id)
    if slot["type"] != "custom":
        raise HTTPException(status_code=405, detail="Built-in profiles cannot be deleted")
    await reset_profile(profile_id)
    if active_profile_id == profile_id:
        active_profile_id = "builtin-hxp602"
    return {"cleared": True}


@app.get("/api/logs")
async def get_debug_logs(after: int = 0):
    return debug_terminal.read_after(max(0, after))


@app.post("/api/characterize")
async def characterize_oven():
    debug_print("CHARACTERIZE: Oven characterization requested")
    return {"status": "accepted"}


# Keep this last so /api routes are matched before the static file handler.
app.mount("/", StaticFiles(directory=FRONTEND_DIR, html=True), name="frontend")
