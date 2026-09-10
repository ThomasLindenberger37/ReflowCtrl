# Reflow Controller

A static, responsive web interface embedded in the ESP32 firmware, with a
FastAPI mock for local browser development.

## Getting started

Python 3.10 or newer is recommended.

```bash
python -m venv .venv
source .venv/bin/activate
python -m pip install -r backend_mockup/requirements.txt
python -m uvicorn backend_mockup.mock_backend:app --reload
```

Then open <http://127.0.0.1:8000> in a browser.

On Windows PowerShell, activate the virtual environment with `.venv\Scripts\Activate.ps1`.

If activation does not take effect in your shell, run the commands explicitly through the environment on Linux or WSL:

```bash
.venv/bin/python -m pip install -r backend_mockup/requirements.txt
.venv/bin/python -m uvicorn backend_mockup.mock_backend:app --reload
```

## API

The complete request/response contract, validation rules, and `curl` examples are documented in [API.md](API.md).

- `GET /api/status` – read the current process status
- `POST /api/start` – start the reflow profile
- `POST /api/stop` – stop the profile
- `GET /api/config` – read the network and MQTT configuration
- `GET /api/setup/status` – let the backend report whether installation is required
- `PUT /api/config` – update the in-memory configuration
- `GET /api/profiles` – list the two built-in and six custom profile slots
- `GET /api/profiles/{id}` – read one profile slot
- `PUT /api/profiles/{id}` – validate and persist a profile
- `PUT /api/profiles/active` – persist the active profile ID
- `POST /api/profiles/{id}/reset` – restore a slot to its factory state
- `DELETE /api/profiles/{id}` – clear a custom slot
- `POST /api/profiles/preview` – validate a profile and generate its target curve
- `GET /api/logs?after=<id>` – read new lines from the debug terminal
- `POST /api/characterize` – entry point for oven characterization

The local backend takes about 3 minutes and 40 seconds to progress through Preheat, Soak, Reflow, Cooling, and Complete. Configuration values and process state are kept in memory only.

On first use, the installation wizard collects WiFi credentials followed by the hardware pin mapping. MQTT is the optional final step; leaving the broker empty stores `mqtt_enabled: false`, while entering a broker enables MQTT. The configuration model stores three SPI pins (`SCK`, `MOSI`, and `MISO`) plus one GPIO each for the heater relay, push button, and status LED. GPIO values must be unique and between 0 and 16. The wizard can be reopened with the settings button.

The ESP32 serves the UI on port 80 at `http://reflow-ctrl.local`. The embedded
backend currently implements `GET /api/status` for the measured temperature and
`GET /api/logs` for the terminal output, plus oven characterization and persistent
characterization configuration. In the characterization dialog, use **Load CSV / JSON**
and **Save to controller** to retain the analyzed thermal behavior in ESP32 flash.
The saved analysis is restored when the dialog opens, including after a controller
restart. Raw CSV recordings remain browser-only. Profile management is available;
process controls and settings remain disabled until their firmware APIs exist. The legacy FastAPI mock
does not implement the embedded characterization API. The embedded status and terminal polling have no external
web dependencies; the chart stays empty until Chart.js is bundled locally.

Every ESP-IDF log line is mirrored to UART and a fixed-size RAM ring buffer. The
expandable debug terminal polls that buffer via `GET /api/logs`; no external log
server is required.

The local Python mock retains up to 250 lines in memory. Custom mock messages can
be added with `debug_print("My message")` from `backend_mockup/mock_backend.py` or
other imported Python code.

## Profile editor

The profile editor stores only compact profile parameters. Validation and curve generation run in
the ESP32 backend so the preview uses the same calculations as process control. Characterization
data remains part of the oven configuration and is never copied into individual profiles.

Characterization UI checks: `node --test tests/characterization_ui_test.js` (from the repository root).
