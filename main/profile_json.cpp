#include "profile_json.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

#include "cJSON.h"

namespace reflowCtrl {
namespace {

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

const cJSON* field(const cJSON* object, const char* name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

bool read_number(const cJSON* object, const char* name, double& result) {
    const cJSON* value = field(object, name);
    if (!cJSON_IsNumber(value)) {
        return false;
    }
    result = value->valuedouble;
    return true;
}

std::optional<ProfileConfiguration> parse_configuration(const cJSON* root) {
    if (!cJSON_IsObject(root) || !cJSON_IsString(field(root, "name"))) {
        return std::nullopt;
    }
    const cJSON* soak = field(root, "soak");
    const cJSON* reflow = field(root, "reflow");
    const cJSON* cooling = field(root, "cooling");
    ProfileConfiguration result;
    result.name = field(root, "name")->valuestring;
    if (!read_number(root, "max_ramp_rate_c_per_s", result.max_ramp_rate_c_per_s)
        || !read_number(soak, "start_temperature_c", result.soak_start_temperature_c)
        || !read_number(soak, "end_temperature_c", result.soak_end_temperature_c)
        || !read_number(soak, "duration_s", result.soak_duration_s)
        || !read_number(reflow, "liquidus_temperature_c", result.liquidus_temperature_c)
        || !read_number(reflow, "peak_temperature_c", result.peak_temperature_c)
        || !read_number(reflow, "time_above_liquidus_s", result.time_above_liquidus_s)
        || !read_number(cooling, "max_cooling_rate_c_per_s", result.max_cooling_rate_c_per_s)) {
        return std::nullopt;
    }
    return result;
}

cJSON* configuration_json(const ProfileConfiguration& profile) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", profile.name.c_str());
    cJSON_AddNumberToObject(root, "max_ramp_rate_c_per_s", profile.max_ramp_rate_c_per_s);
    cJSON* soak = cJSON_AddObjectToObject(root, "soak");
    cJSON_AddNumberToObject(soak, "start_temperature_c", profile.soak_start_temperature_c);
    cJSON_AddNumberToObject(soak, "end_temperature_c", profile.soak_end_temperature_c);
    cJSON_AddNumberToObject(soak, "duration_s", profile.soak_duration_s);
    cJSON* reflow = cJSON_AddObjectToObject(root, "reflow");
    cJSON_AddNumberToObject(reflow, "liquidus_temperature_c", profile.liquidus_temperature_c);
    cJSON_AddNumberToObject(reflow, "peak_temperature_c", profile.peak_temperature_c);
    cJSON_AddNumberToObject(reflow, "time_above_liquidus_s", profile.time_above_liquidus_s);
    cJSON* cooling = cJSON_AddObjectToObject(root, "cooling");
    cJSON_AddNumberToObject(cooling, "max_cooling_rate_c_per_s", profile.max_cooling_rate_c_per_s);
    return root;
}

cJSON* slot_json(const ProfileSlot& slot) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", slot.id.c_str());
    cJSON_AddStringToObject(root, "type", slot.type == ProfileType::BuiltIn ? "builtin" : "custom");
    cJSON_AddBoolToObject(root, "occupied", slot.occupied);
    cJSON_AddItemToObject(root, "configuration", configuration_json(slot.configuration));
    return root;
}

std::string print_json(cJSON* value) {
    Json owner(value, &cJSON_Delete);
    std::unique_ptr<char, decltype(&cJSON_free)> text(cJSON_PrintUnformatted(value), &cJSON_free);
    return text ? text.get() : std::string{};
}

Json parse_json(std::string_view json) {
    return {cJSON_ParseWithLength(json.data(), json.size()), &cJSON_Delete};
}

void add_rate_curve(std::vector<CharacterizationRatePoint>& target, const cJSON* root,
                    const char* name) {
    const cJSON* array = field(root, name);
    const cJSON* point = nullptr;
    cJSON_ArrayForEach(point, array) {
        double temperature = 0.0;
        double rate = 0.0;
        if (read_number(point, "temperatureC", temperature)
            && read_number(point, "rateCPerSecond", rate)) {
            target.push_back({temperature, rate});
        }
    }
}

void append_json_string(std::string& output, std::string_view value) {
    constexpr char HEX_DIGITS[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
            case '"':
                output.append("\\\"");
                break;
            case '\\':
                output.append("\\\\");
                break;
            case '\b':
                output.append("\\b");
                break;
            case '\f':
                output.append("\\f");
                break;
            case '\n':
                output.append("\\n");
                break;
            case '\r':
                output.append("\\r");
                break;
            case '\t':
                output.append("\\t");
                break;
            default:
                if (character < 0x20) {
                    output.append("\\u00");
                    output.push_back(HEX_DIGITS[character >> 4]);
                    output.push_back(HEX_DIGITS[character & 0x0f]);
                } else {
                    output.push_back(static_cast<char>(character));
                }
        }
    }
    output.push_back('"');
}

void append_json_number(std::string& output, double value) {
    char number[32]{};
    const int length = std::snprintf(number, sizeof(number), "%.6g", value);
    if (length > 0) {
        output.append(number, static_cast<std::size_t>(length));
    }
}

void append_issues(std::string& output, const ProfilePreview& preview,
                   ValidationSeverity severity) {
    bool first = true;
    for (const auto& issue : preview.issues) {
        if (issue.severity != severity) {
            continue;
        }
        output.append(first ? "{" : ",{");
        output.append("\"field\":");
        append_json_string(output, issue.field);
        output.append(",\"message\":");
        append_json_string(output, issue.message);
        output.push_back('}');
        first = false;
    }
}

}  // namespace

std::optional<ProfileConfiguration> parse_profile(const std::string_view json) {
    const Json root = parse_json(json);
    return root ? parse_configuration(root.get()) : std::nullopt;
}

std::optional<ProfileCollection> parse_profile_collection(const std::string_view json) {
    const Json root = parse_json(json);
    const cJSON* version = root ? field(root.get(), "version") : nullptr;
    const cJSON* active = root ? field(root.get(), "active_profile_id") : nullptr;
    const cJSON* profiles = root ? field(root.get(), "profiles") : nullptr;
    if (!cJSON_IsNumber(version) || version->valueint != ProfileCollection::SCHEMA_VERSION
        || !cJSON_IsString(active) || !cJSON_IsArray(profiles)
        || cJSON_GetArraySize(profiles) != 8) {
        return std::nullopt;
    }
    ProfileCollection result;
    result.active_profile_id = active->valuestring;
    for (int index = 0; index < 8; ++index) {
        const cJSON* item = cJSON_GetArrayItem(profiles, index);
        const cJSON* id = field(item, "id");
        const cJSON* type = field(item, "type");
        const cJSON* occupied = field(item, "occupied");
        if (!cJSON_IsString(id) || !cJSON_IsString(type) || !cJSON_IsBool(occupied)) {
            return std::nullopt;
        }
        auto& slot = result.profiles[static_cast<std::size_t>(index)];
        slot.id = id->valuestring;
        slot.type = std::strcmp(type->valuestring, "builtin") == 0 ? ProfileType::BuiltIn
                                                                   : ProfileType::Custom;
        slot.occupied = cJSON_IsTrue(occupied);
        const auto configuration = parse_configuration(field(item, "configuration"));
        if (!configuration) {
            return std::nullopt;
        }
        slot.configuration = *configuration;
    }
    static const ProfileCollection defaults = default_profile_collection();
    for (std::size_t index = 0; index < result.profiles.size(); ++index) {
        if (result.profiles[index].id != defaults.profiles[index].id
            || result.profiles[index].type != defaults.profiles[index].type
            || (index < 2 && !result.profiles[index].occupied)) {
            return std::nullopt;
        }
    }
    return result;
}

std::string serialize_profile(const ProfileSlot& profile) {
    return print_json(slot_json(profile));
}

std::string serialize_profile_collection(const ProfileCollection& profiles) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", profiles.version);
    cJSON_AddStringToObject(root, "active_profile_id", profiles.active_profile_id.c_str());
    cJSON* array = cJSON_AddArrayToObject(root, "profiles");
    for (const auto& profile : profiles.profiles) {
        cJSON_AddItemToArray(array, slot_json(profile));
    }
    return print_json(root);
}

std::string serialize_profile_preview(const ProfilePreview& preview) {
    std::string output;
    output.reserve(512 + preview.points.size() * 64);
    output.append(preview.valid ? "{\"valid\":true,\"warnings\":["
                                : "{\"valid\":false,\"warnings\":[");
    append_issues(output, preview, ValidationSeverity::Warning);
    output.append("],\"errors\":[");
    append_issues(output, preview, ValidationSeverity::Error);
    output.append("],\"summary\":{\"duration_s\":");
    append_json_number(output, preview.duration_s);
    output.append(",\"peak_temperature_c\":");
    append_json_number(output, preview.peak_temperature_c);
    output.append(",\"time_above_liquidus_s\":");
    append_json_number(output, preview.time_above_liquidus_s);
    output.append(",\"max_ramp_rate_c_per_s\":");
    append_json_number(output, preview.max_ramp_rate_c_per_s);
    output.append("},\"points\":[");
    for (std::size_t index = 0; index < preview.points.size(); ++index) {
        const auto& point = preview.points[index];
        output.append(index == 0 ? "{\"time_s\":" : ",{\"time_s\":");
        append_json_number(output, point.time_s);
        output.append(",\"temperature_c\":");
        append_json_number(output, point.temperature_c);
        output.append(",\"phase\":");
        append_json_string(output, point.phase);
        output.push_back('}');
    }
    output.append("]}");
    return output;
}

std::optional<OvenCapabilities> parse_oven_capabilities(const std::string_view json) {
    const Json root = parse_json(json);
    if (!root) {
        return std::nullopt;
    }
    OvenCapabilities result;
    if (!read_number(root.get(), "ambientTemperatureC", result.ambient_temperature_c)) {
        return std::nullopt;
    }
    const cJSON* equilibrium = field(root.get(), "estimatedEquilibriumTemperatureC");
    if (cJSON_IsNumber(equilibrium)) {
        result.maximum_temperature_c = equilibrium->valuedouble;
    } else {
        const cJSON* summary = field(root.get(), "summary");
        double maximum = 0.0;
        if (read_number(summary, "maximumTemperatureC", maximum)) {
            result.maximum_temperature_c = maximum;
        }
    }
    add_rate_curve(result.heating_rates, root.get(), "heatingRate");
    add_rate_curve(result.cooling_rates, root.get(), "passiveCoolingRate");
    return result;
}

}  // namespace reflowCtrl
