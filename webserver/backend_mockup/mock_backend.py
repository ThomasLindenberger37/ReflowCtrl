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


class ReflowProfile(BaseModel):
    name: str = Field(min_length=1, max_length=64)
    max_ramp_rate_c_per_s: float = Field(gt=0, le=20)
    soak: SoakProfile
    reflow: ReflowStageProfile

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
        minimum_peak_time = (
            self.reflow.peak_temperature_c - self.reflow.liquidus_temperature_c
        ) / self.max_ramp_rate_c_per_s
        if self.reflow.time_above_liquidus_s <= minimum_peak_time:
            raise ValueError(
                "Time above liquidus is too short for the configured peak and ramp rate"
            )
        return self


class ActiveProfileSelection(BaseModel):
    name: str = Field(min_length=1, max_length=64)


class ProfileUpdate(BaseModel):
    original_name: str = Field(min_length=1, max_length=64)
    profile: ReflowProfile


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
profiles: dict[str, ReflowProfile] = {
    "HXP-602": ReflowProfile(
        name="HXP-602",
        max_ramp_rate_c_per_s=2.0,
        soak=SoakProfile(start_temperature_c=120, end_temperature_c=150, duration_s=90),
        reflow=ReflowStageProfile(
            liquidus_temperature_c=173,
            peak_temperature_c=195,
            time_above_liquidus_s=45,
        ),
    ),
    "SAC305": ReflowProfile(
        name="SAC305",
        max_ramp_rate_c_per_s=2.0,
        soak=SoakProfile(start_temperature_c=150, end_temperature_c=180, duration_s=90),
        reflow=ReflowStageProfile(
            liquidus_temperature_c=217,
            peak_temperature_c=245,
            time_above_liquidus_s=60,
        ),
    ),
    "Custom 1": ReflowProfile(
        name="Custom 1",
        max_ramp_rate_c_per_s=1.8,
        soak=SoakProfile(start_temperature_c=125, end_temperature_c=155, duration_s=100),
        reflow=ReflowStageProfile(
            liquidus_temperature_c=183,
            peak_temperature_c=215,
            time_above_liquidus_s=50,
        ),
    ),
    "Custom 2": ReflowProfile(
        name="Custom 2",
        max_ramp_rate_c_per_s=1.5,
        soak=SoakProfile(start_temperature_c=110, end_temperature_c=145, duration_s=110),
        reflow=ReflowStageProfile(
            liquidus_temperature_c=170,
            peak_temperature_c=200,
            time_above_liquidus_s=55,
        ),
    ),
}
active_profile_name = "HXP-602"


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
        "active_profile": active_profile_name,
        "profiles": [profile.model_dump() for profile in profiles.values()],
    }


@app.put("/api/profiles/active")
async def select_active_profile(selection: ActiveProfileSelection):
    global active_profile_name
    if oven.running:
        raise HTTPException(status_code=409, detail="Profile cannot be changed during reflow")
    if selection.name not in profiles:
        raise HTTPException(status_code=404, detail="Profile not found")
    active_profile_name = selection.name
    debug_print(f"PROFILE: Active profile set to {active_profile_name}")
    return {"active_profile": active_profile_name}


@app.put("/api/profile", response_model=ReflowProfile)
async def update_profile(update: ProfileUpdate):
    global active_profile_name
    profile_name = update.original_name
    profile = update.profile
    if profile_name not in profiles:
        raise HTTPException(status_code=404, detail="Profile not found")
    if oven.running and profile_name == active_profile_name:
        raise HTTPException(status_code=409, detail="Active profile cannot be edited during reflow")
    if profile.name != profile_name and profile.name in profiles:
        raise HTTPException(status_code=409, detail="A profile with this name already exists")

    del profiles[profile_name]
    profiles[profile.name] = profile
    if active_profile_name == profile_name:
        active_profile_name = profile.name
    debug_print(f"PROFILE: {profile_name} saved as {profile.name}")
    return profile


@app.post("/api/profile", response_model=ReflowProfile, status_code=201)
async def create_profile(profile: ReflowProfile):
    if len(profiles) >= 10:
        raise HTTPException(status_code=409, detail="At most 10 profiles are allowed")
    if profile.name in profiles:
        raise HTTPException(status_code=409, detail="A profile with this name already exists")
    profiles[profile.name] = profile
    debug_print(f"PROFILE: {profile.name} created")
    return profile


@app.get("/api/logs")
async def get_debug_logs(after: int = 0):
    return debug_terminal.read_after(max(0, after))


@app.post("/api/characterize")
async def characterize_oven():
    debug_print("CHARACTERIZE: Oven characterization requested")
    return {"status": "accepted"}


# Keep this last so /api routes are matched before the static file handler.
app.mount("/", StaticFiles(directory=FRONTEND_DIR, html=True), name="frontend")
