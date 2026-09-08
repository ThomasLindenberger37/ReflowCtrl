# Reflow Controller HTTP API

Version: `1.0.0`

This document describes the HTTP/JSON contract used between the static web interface and the controller backend. The same contract can be implemented by the current FastAPI backend and a later ESP-IDF HTTP server.

## Connection

Default local base URL:

```text
http://127.0.0.1:8000
```

All application endpoints use the `/api` prefix. Requests with a JSON body must include:

```http
Content-Type: application/json
```

The current API has no authentication, authorization, TLS, or session mechanism. It is intended for use on a trusted local network. WiFi and MQTT passwords are returned as plain JSON by the configuration endpoint; this must be considered when deploying the controller.

FastAPI also provides interactive documentation while the Python backend is running:

- Swagger UI: `GET /docs`
- OpenAPI document: `GET /openapi.json`

## Common responses

Successful requests return JSON with HTTP `200 OK`.

Application conflicts use HTTP `409 Conflict`:

```json
{
  "detail": "Reflow process already running"
}
```

Invalid JSON data uses HTTP `422 Unprocessable Entity`. FastAPI returns a `detail` array describing the invalid fields:

```json
{
  "detail": [
    {
      "type": "greater_than_equal",
      "loc": ["body", "relay_pin"],
      "msg": "Input should be greater than or equal to 0",
      "input": -1
    }
  ]
}
```

## Process control

### Read oven status

```http
GET /api/status
```

Example:

```bash
curl http://127.0.0.1:8000/api/status
```

Response:

```json
{
  "state": "idle",
  "temperature": 23.5,
  "target_temperature": 0.0,
  "heater": false,
  "elapsed_seconds": 0
}
```

Fields:

| Field | Type | Meaning |
|---|---:|---|
| `state` | string | `idle`, `preheat`, `soak`, `reflow`, `cooling`, `complete`, or `error` |
| `temperature` | number | Current measured temperature in °C |
| `target_temperature` | number | Current controller setpoint in °C |
| `heater` | boolean | Whether the heater output is active |
| `elapsed_seconds` | integer | Seconds elapsed since process start |

Recommended polling interval: once per second. The browser stores chart history locally; this response contains only the current sample.

### Start reflow

```http
POST /api/start
```

No request body is required.

```bash
curl -X POST http://127.0.0.1:8000/api/start
```

The response uses the same object as `GET /api/status`.

Possible errors:

| Status | Condition |
|---:|---|
| `409` | A reflow process is already running |
| `409` | The measured temperature is above the safe start limit of `40.0 °C` |

Hot-oven error:

```json
{
  "detail": "Oven must cool to 40.0 °C before starting"
}
```

### Stop reflow

```http
POST /api/stop
```

No request body is required.

```bash
curl -X POST http://127.0.0.1:8000/api/stop
```

The heater is disabled, the state changes to `idle`, and elapsed time is reset. The response uses the status object.

## Installation and configuration

### Read setup requirement

```http
GET /api/setup/status
```

```bash
curl http://127.0.0.1:8000/api/setup/status
```

Response before installation:

```json
{
  "setup_required": true
}
```

The backend is authoritative for this decision. The frontend automatically opens the installation wizard only when `setup_required` is `true`.

### Read configuration

```http
GET /api/config
```

```bash
curl http://127.0.0.1:8000/api/config
```

Response:

```json
{
  "wifi_ssid": "Workshop",
  "wifi_password": "secret",
  "mqtt_broker": "192.168.1.20",
  "mqtt_port": 1883,
  "mqtt_username": "oven",
  "mqtt_password": "secret",
  "mqtt_enabled": true,
  "spi_sck_pin": 18,
  "spi_mosi_pin": 23,
  "spi_miso_pin": 19,
  "relay_pin": 26,
  "button_pin": 27,
  "led_pin": 2,
  "setup_complete": true
}
```

### Replace configuration

```http
PUT /api/config
```

The endpoint replaces the complete configuration. Clients should read the current object, modify it, and send all fields back.

```bash
curl -X PUT http://127.0.0.1:8000/api/config \
  -H 'Content-Type: application/json' \
  --data '{
    "wifi_ssid": "Workshop",
    "wifi_password": "secret",
    "mqtt_broker": "192.168.1.20",
    "mqtt_port": 1883,
    "mqtt_username": "oven",
    "mqtt_password": "secret",
    "mqtt_enabled": true,
    "spi_sck_pin": 18,
    "spi_mosi_pin": 23,
    "spi_miso_pin": 19,
    "relay_pin": 26,
    "button_pin": 27,
    "led_pin": 2,
    "setup_complete": true
  }'
```

To disable MQTT:

```json
{
  "mqtt_broker": "",
  "mqtt_port": 1883,
  "mqtt_username": "",
  "mqtt_password": "",
  "mqtt_enabled": false
}
```

The remaining configuration fields must still be included in the `PUT` request.

Validation rules:

| Field | Rule |
|---|---|
| `wifi_ssid` | Maximum 64 characters |
| `wifi_password` | Maximum 128 characters |
| `mqtt_broker` | Maximum 255 characters; may be empty when MQTT is disabled |
| `mqtt_port` | Integer from 1 through 65535 |
| `mqtt_username` | Maximum 128 characters |
| `mqtt_password` | Maximum 128 characters |
| All GPIO fields | Integer from 0 through 16 |
| GPIO mapping | All six assigned pins must be unique |
| `setup_complete` | Set to `true` when the installation wizard finishes |

## Reflow profiles

Only compact profile parameters are transferred. The generated one-second preview curve is calculated in the browser and is never sent through the API.

Profile object:

```json
{
  "name": "HXP-602",
  "max_ramp_rate_c_per_s": 2.0,
  "soak": {
    "start_temperature_c": 120,
    "end_temperature_c": 150,
    "duration_s": 90
  },
  "reflow": {
    "liquidus_temperature_c": 173,
    "peak_temperature_c": 195,
    "time_above_liquidus_s": 45
  }
}
```

Validation rules:

```text
0 < soak start < soak end < liquidus < peak
0 < maximum ramp rate <= 20 °C/s
0 < soak duration <= 1800 s
0 < time above liquidus <= 600 s
soak heating rate <= maximum ramp rate
time above liquidus > minimum time required to ramp from liquidus to peak
```

### List profiles

```http
GET /api/profiles
```

```bash
curl http://127.0.0.1:8000/api/profiles
```

Response:

```json
{
  "active_profile": "HXP-602",
  "profiles": [
    {
      "name": "HXP-602",
      "max_ramp_rate_c_per_s": 2.0,
      "soak": {
        "start_temperature_c": 120.0,
        "end_temperature_c": 150.0,
        "duration_s": 90.0
      },
      "reflow": {
        "liquidus_temperature_c": 173.0,
        "peak_temperature_c": 195.0,
        "time_above_liquidus_s": 45.0
      }
    }
  ]
}
```

### Select active profile

```http
PUT /api/profiles/active
```

```bash
curl -X PUT http://127.0.0.1:8000/api/profiles/active \
  -H 'Content-Type: application/json' \
  --data '{"name":"SAC305"}'
```

Response:

```json
{
  "active_profile": "SAC305"
}
```

Possible errors:

| Status | Condition |
|---:|---|
| `404` | Profile name does not exist |
| `409` | A reflow process is currently running |

### Update or rename profile

```http
PUT /api/profile
```

`original_name` identifies the existing profile. The `name` in the nested profile
object becomes its new name, so the same operation can update or rename a profile.

```bash
curl -X PUT 'http://127.0.0.1:8000/api/profile' \
  -H 'Content-Type: application/json' \
  --data '{
    "original_name": "Custom 1",
    "profile": {
      "name": "Lead Free Custom",
      "max_ramp_rate_c_per_s": 1.8,
      "soak": {
        "start_temperature_c": 140,
        "end_temperature_c": 175,
        "duration_s": 100
      },
      "reflow": {
        "liquidus_temperature_c": 217,
        "peak_temperature_c": 242,
        "time_above_liquidus_s": 55
      }
    }
  }'
```

The response is the saved compact profile object.

Possible errors:

| Status | Condition |
|---:|---|
| `404` | Path profile does not exist |
| `409` | The active profile is edited while reflow is running |
| `409` | The new name already belongs to another profile |
| `422` | Profile data violates a validation rule |

### Create profile

```http
POST /api/profile
```

The request body is one compact profile object in the same format returned by
`GET /api/profiles`. The ESP8266 persists it immediately in SPIFFS and returns
the created profile with status `201 Created`.

Possible errors:

| Status | Condition |
|---:|---|
| `409` | Ten profiles already exist |
| `409` | The profile name already exists |
| `422` | Profile data violates a validation rule |

## Debug terminal

### Read new log lines

```http
GET /api/logs?after={cursor}
```

The endpoint supports incremental polling. Start with `after=0`, render the returned lines, and use `next_cursor` in the next request.

```bash
curl 'http://127.0.0.1:8000/api/logs?after=0'
```

Response:

```json
{
  "lines": [
    {
      "id": 1,
      "text": "[14:25:32.184] BOOT: Reflow Controller backend ready"
    },
    {
      "id": 2,
      "text": "[14:25:33.185] TEMPERATURE: 23.5 °C"
    }
  ],
  "next_cursor": 2
}
```

The ESP8266 retains the latest 24 complete log lines in a fixed-size RAM ring buffer and returns up
to four lines per request. The browser should poll approximately once per second while the
Developer Tools panel is open. Log output is mirrored: every line remains visible on UART0 and is
also available through this endpoint.

Actions that can already be exercised through the UI but do not yet have a hardware or persistent
implementation emit a line beginning with `NOT_IMPLEMENTED`. Their REST handlers remain safe and
responsive so the complete embedded frontend can be tested without energizing the heater output.

To produce a web-terminal line from Python backend code:

```python
from backend.main import debug_print

debug_print("Sensor initialized")
```

## Oven characterization

```http
POST /api/characterization/start
```

```bash
curl -X POST http://reflow-ctrl.local/api/characterization/start
```

The controller publishes filtered temperatures at 200 ms intervals and executes the automatic
baseline, heat, coast, final-heat, and cooldown sequence. Heater output is forced off for a user
stop, sensor error, invalid temperature, 250 °C safety limit, recording error, OTA update, or the
20-minute characterization timeout.

### Stop immediately

```http
POST /api/characterization/stop
```

### Current status

```http
GET /api/characterization/status
```

The JSON response contains `running`, `phase`, `elapsed_ms`, `temperature`, `heater_output`, and
an optional `error` stop reason.

### Incremental live CSV preview

```http
GET /api/characterization/samples?after={cursor}
```

This cursor-based endpoint returns only new CSV data lines and `next_cursor`. The ESP stores only
the latest 16 lines in a fixed-size transfer buffer. The browser accumulates the received lines
and creates the downloadable CSV locally; raw CSV samples are not persisted on the ESP.

## Static frontend

The backend also serves the static frontend:

```http
GET /
GET /index.html
GET /style.css
GET /app.js
GET /reflow-profile.js
```

These are not API endpoints and return static files rather than JSON.

### Persistent characterization configuration (ESP32)

`GET /api/characterization/configuration` returns the saved version-1
`oven-characterization` analysis object, or JSON `null` if none exists.
Responses are not cached. Opening the characterization dialog restores it automatically.

`PUT /api/characterization/configuration` replaces the saved analysis with the JSON
request body (maximum 8192 bytes). The controller validates the version, type,
curves, summary, and data quality before writing a single NVS blob and committing it.
Success returns `{"saved":true}` only after commit. Invalid documents return HTTP 400;
storage failures return HTTP 500. Both contain an English `detail` message.
An incomplete upload closes the connection without saving.

Use **Load CSV / JSON** to analyze a CSV or import an existing configuration JSON,
then **Save to controller**. The saved analysis survives browser and controller
restarts; a subsequent save replaces it. Erasing NVS also erases this configuration.
Storage does not yet apply the analysis to process control. This endpoint is
implemented in the ESP32 firmware; the legacy Python mock does not implement it.
