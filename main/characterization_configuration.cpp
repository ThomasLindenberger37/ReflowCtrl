#include "characterization_configuration.hpp"

#include <cmath>
#include <cstring>
#include <memory>

#include "cJSON.h"

namespace reflowCtrl {
namespace {

const cJSON* field(const cJSON* object, const char* name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

bool number(const cJSON* object, const char* name, bool nullable = false) {
    const cJSON* value = field(object, name);
    return (nullable && cJSON_IsNull(value))
           || (cJSON_IsNumber(value) && std::isfinite(value->valuedouble));
}

bool count(const cJSON* object, const char* name) {
    const cJSON* value = field(object, name);
    return number(object, name) && value->valuedouble >= 0
           && std::floor(value->valuedouble) == value->valuedouble;
}

bool curve(const cJSON* object, const char* name, bool overshoot = false) {
    const cJSON* points = field(object, name);
    if (!cJSON_IsArray(points) || cJSON_GetArraySize(points) > 64) {
        return false;
    }
    const cJSON* point = nullptr;
    cJSON_ArrayForEach(point, points) {
        if (!cJSON_IsObject(point) || !number(point, "temperatureC")) {
            return false;
        }
        if (overshoot) {
            if (!number(point, "overshootC") || !number(point, "timeToPeakSeconds")
                || !cJSON_IsString(field(point, "phase"))) {
                return false;
            }
        } else if (!number(point, "rateCPerSecond") || !count(point, "sampleCount")) {
            return false;
        }
    }
    return true;
}

// Bound parser recursion on the HTTP task stack, including unknown JSON fields.
bool bounded_nesting(std::string_view json) {
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char character : json) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                in_string = false;
            }
        } else if (character == '"') {
            in_string = true;
        } else if (character == '{' || character == '[') {
            if (++depth > 8) {
                return false;
            }
        } else if (character == '}' || character == ']') {
            if (--depth < 0) {
                return false;
            }
        }
    }
    return depth == 0 && !in_string;
}

bool valid_document(const cJSON* root) {
    const cJSON* version = field(root, "formatVersion");
    const cJSON* type = field(root, "type");
    const cJSON* summary = field(root, "summary");
    const cJSON* quality = field(root, "dataQuality");
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(version) || version->valuedouble != 1
        || !cJSON_IsString(type) || std::strcmp(type->valuestring, "oven-characterization") != 0
        || !number(root, "ambientTemperatureC")
        || !number(root, "estimatedEquilibriumTemperatureC", true) || !curve(root, "heatingRate")
        || !curve(root, "passiveCoolingRate") || !curve(root, "coastOvershoot", true)
        || !cJSON_IsObject(summary) || !number(summary, "minimumTemperatureC")
        || !number(summary, "maximumTemperatureC")
        || !number(summary, "maximumHeatingRateCPerSecond", true)
        || !number(summary, "maximumObservedOvershootC", true)
        || !number(summary, "totalDurationSeconds") || !cJSON_IsObject(quality)
        || !count(quality, "inputSamples") || !count(quality, "usedSamples")) {
        return false;
    }
    const cJSON* warnings = field(quality, "warnings");
    if (!cJSON_IsArray(warnings)) {
        return false;
    }
    const cJSON* warning = nullptr;
    cJSON_ArrayForEach(warning, warnings) {
        if (!cJSON_IsString(warning)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool is_valid_characterization_configuration(std::string_view json) {
    if (json.empty() || json.size() > MAX_CHARACTERIZATION_CONFIGURATION_SIZE
        || json.find('\0') != std::string_view::npos || !bounded_nesting(json)) {
        return false;
    }
    const char* end = nullptr;
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLengthOpts(json.data(), json.size(), &end, false), &cJSON_Delete);
    if (!root || !valid_document(root.get())) {
        return false;
    }
    const std::string_view trailing(end, json.data() + json.size() - end);
    return trailing.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

}  // namespace reflowCtrl
