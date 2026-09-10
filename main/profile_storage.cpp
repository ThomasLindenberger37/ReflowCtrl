#include "profile_storage.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>

#include "cJSON.h"
#include "characterization_storage.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "profile.hpp"
#include "profile_json.hpp"

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "profiles";
constexpr char NVS_NAMESPACE[] = "profiles";
constexpr char NVS_KEY[] = "document_v1";
constexpr std::size_t MAX_DOCUMENT_SIZE = 16 * 1024;
constexpr std::size_t MAX_REQUEST_SIZE = 2048;

class ProfileService {
   public:
    esp_err_t initialize() {
        std::lock_guard lock(mutex_);
        std::string document;
        const esp_err_t result = load(document);
        if (result == ESP_OK) {
            const auto parsed = parse_profile_collection(document);
            if (parsed) {
                profiles_ = *parsed;
                ensure_valid_active_profile();
                return ESP_OK;
            }
            ESP_LOGE(TAG, "Stored profile document is invalid; restoring factory defaults");
        } else if (result != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Could not load profiles: %s; restoring factory defaults",
                     esp_err_to_name(result));
        }
        profiles_ = default_profile_collection();
        return persist();
    }

    ProfileCollection snapshot() const {
        std::lock_guard lock(mutex_);
        return profiles_;
    }

    std::optional<ProfileSlot> profile(const std::string_view id) const {
        std::lock_guard lock(mutex_);
        const auto iterator = find(id);
        return iterator == profiles_.profiles.end() ? std::nullopt
                                                    : std::optional<ProfileSlot>(*iterator);
    }

    esp_err_t save(const std::string_view id, const ProfileConfiguration& configuration) {
        std::lock_guard lock(mutex_);
        const auto iterator = find(id);
        if (iterator == profiles_.profiles.end()) {
            return ESP_ERR_NOT_FOUND;
        }
        iterator->configuration = configuration;
        iterator->occupied = true;
        return persist();
    }

    esp_err_t clear(const std::string_view id) {
        std::lock_guard lock(mutex_);
        const auto iterator = find(id);
        if (iterator == profiles_.profiles.end()) {
            return ESP_ERR_NOT_FOUND;
        }
        if (iterator->type != ProfileType::Custom) {
            return ESP_ERR_NOT_ALLOWED;
        }
        const auto defaults = default_profile_collection();
        const std::size_t index = static_cast<std::size_t>(iterator - profiles_.profiles.begin());
        *iterator = defaults.profiles[index];
        if (profiles_.active_profile_id == id) {
            profiles_.active_profile_id = "builtin-hxp602";
        }
        return persist();
    }

    esp_err_t restore(const std::string_view id) {
        std::lock_guard lock(mutex_);
        const auto iterator = find(id);
        if (iterator == profiles_.profiles.end()) {
            return ESP_ERR_NOT_FOUND;
        }
        const auto defaults = default_profile_collection();
        const std::size_t index = static_cast<std::size_t>(iterator - profiles_.profiles.begin());
        *iterator = defaults.profiles[index];
        return persist();
    }

    esp_err_t select(const std::string_view id) {
        std::lock_guard lock(mutex_);
        const auto iterator = find(id);
        if (iterator == profiles_.profiles.end() || !iterator->occupied) {
            return ESP_ERR_NOT_FOUND;
        }
        profiles_.active_profile_id = id;
        return persist();
    }

   private:
    using Iterator = std::array<ProfileSlot, 8>::iterator;
    using ConstIterator = std::array<ProfileSlot, 8>::const_iterator;

    Iterator find(const std::string_view id) {
        return std::find_if(profiles_.profiles.begin(), profiles_.profiles.end(),
                            [&](const auto& profile) { return profile.id == id; });
    }

    ConstIterator find(const std::string_view id) const {
        return std::find_if(profiles_.profiles.begin(), profiles_.profiles.end(),
                            [&](const auto& profile) { return profile.id == id; });
    }

    void ensure_valid_active_profile() {
        const auto active = find(profiles_.active_profile_id);
        if (active == profiles_.profiles.end() || !active->occupied) {
            profiles_.active_profile_id = "builtin-hxp602";
        }
    }

    static esp_err_t load(std::string& document) {
        nvs_handle_t handle{};
        esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
        std::size_t size = 0;
        if (result == ESP_OK) {
            result = nvs_get_blob(handle, NVS_KEY, nullptr, &size);
            if (result == ESP_OK && (size == 0 || size > MAX_DOCUMENT_SIZE)) {
                result = ESP_ERR_INVALID_SIZE;
            }
            if (result == ESP_OK) {
                document.resize(size);
                result = nvs_get_blob(handle, NVS_KEY, document.data(), &size);
            }
            nvs_close(handle);
        }
        return result;
    }

    esp_err_t persist() const {
        const std::string document = serialize_profile_collection(profiles_);
        if (document.empty() || document.size() > MAX_DOCUMENT_SIZE) {
            return ESP_ERR_INVALID_SIZE;
        }
        nvs_handle_t handle{};
        esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (result == ESP_OK) {
            result = nvs_set_blob(handle, NVS_KEY, document.data(), document.size());
            if (result == ESP_OK) {
                result = nvs_commit(handle);
            }
            nvs_close(handle);
        }
        return result;
    }

    mutable std::mutex mutex_;
    ProfileCollection profiles_{default_profile_collection()};
};

ProfileService service;

esp_err_t json_response(httpd_req_t* request, const std::string& body,
                        const char* status = "200 OK") {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, body.data(), static_cast<ssize_t>(body.size()));
}

esp_err_t error_response(httpd_req_t* request, const char* status, const char* detail) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "detail", detail);
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> owner(root, &cJSON_Delete);
    std::unique_ptr<char, decltype(&cJSON_free)> text(cJSON_PrintUnformatted(root), &cJSON_free);
    return json_response(request, text ? text.get() : "{}", status);
}

std::optional<std::string> request_body(httpd_req_t* request) {
    if (request->content_len == 0 || request->content_len > MAX_REQUEST_SIZE) {
        return std::nullopt;
    }
    std::string body(request->content_len, '\0');
    std::size_t received = 0;
    while (received < body.size()) {
        const int length = httpd_req_recv(request, body.data() + received, body.size() - received);
        if (length <= 0) {
            return std::nullopt;
        }
        received += static_cast<std::size_t>(length);
    }
    return body;
}

std::optional<OvenCapabilities> capabilities() {
    std::string json;
    return load_saved_characterization(json) == ESP_OK ? parse_oven_capabilities(json)
                                                       : std::nullopt;
}

std::string_view path_id(const httpd_req_t* request) {
    constexpr std::string_view PREFIX = "/api/profiles/";
    const std::string_view path(request->uri);
    return path.starts_with(PREFIX) ? path.substr(PREFIX.size()) : std::string_view{};
}

esp_err_t list_profiles(httpd_req_t* request) {
    return json_response(request, serialize_profile_collection(service.snapshot()));
}

esp_err_t get_profile(httpd_req_t* request) {
    const auto profile = service.profile(path_id(request));
    return profile ? json_response(request, serialize_profile(*profile))
                   : error_response(request, "404 Not Found", "Profile slot not found");
}

esp_err_t preview_profile(httpd_req_t* request) {
    const auto body = request_body(request);
    const auto profile = body ? parse_profile(*body) : std::nullopt;
    if (!profile) {
        return error_response(request, "400 Bad Request", "Invalid profile document");
    }
    return json_response(
        request, serialize_profile_preview(generate_profile_preview(*profile, capabilities())));
}

esp_err_t put_profile(httpd_req_t* request) {
    if (path_id(request) == "active") {
        const auto body = request_body(request);
        std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
            body ? cJSON_ParseWithLength(body->data(), body->size()) : nullptr, &cJSON_Delete);
        const cJSON* id = root ? cJSON_GetObjectItemCaseSensitive(root.get(), "id") : nullptr;
        if (!cJSON_IsString(id)) {
            return error_response(request, "400 Bad Request", "Profile id is required");
        }
        return service.select(id->valuestring) == ESP_OK
                   ? json_response(request, "{\"selected\":true}")
                   : error_response(request, "404 Not Found", "Occupied profile slot not found");
    }
    const auto body = request_body(request);
    const auto profile = body ? parse_profile(*body) : std::nullopt;
    if (!profile) {
        return error_response(request, "400 Bad Request", "Invalid profile document");
    }
    const auto preview = generate_profile_preview(*profile, capabilities());
    if (!preview.valid) {
        return json_response(request, serialize_profile_preview(preview),
                             "422 Unprocessable Entity");
    }
    const esp_err_t result = service.save(path_id(request), *profile);
    if (result != ESP_OK) {
        return error_response(
            request, result == ESP_ERR_NOT_FOUND ? "404 Not Found" : "500 Internal Server Error",
            "Could not save profile");
    }
    return json_response(request, serialize_profile(*service.profile(path_id(request))));
}

esp_err_t reset_profile(httpd_req_t* request) {
    std::string_view id = path_id(request);
    constexpr std::string_view SUFFIX = "/reset";
    if (!id.ends_with(SUFFIX)) {
        return error_response(request, "404 Not Found", "Profile operation not found");
    }
    id.remove_suffix(SUFFIX.size());
    const esp_err_t result = service.restore(id);
    return result == ESP_OK ? json_response(request, serialize_profile(*service.profile(id)))
                            : error_response(request, "404 Not Found", "Profile slot not found");
}

esp_err_t delete_profile(httpd_req_t* request) {
    const esp_err_t result = service.clear(path_id(request));
    if (result == ESP_ERR_NOT_ALLOWED) {
        return error_response(request, "405 Method Not Allowed",
                              "Built-in profiles cannot be deleted");
    }
    return result == ESP_OK ? json_response(request, "{\"cleared\":true}")
                            : error_response(request, "404 Not Found", "Profile slot not found");
}

}  // namespace

esp_err_t register_profile_storage(httpd_handle_t server) {
    ESP_RETURN_ON_ERROR(service.initialize(), TAG, "Could not initialize profile storage");
    const std::array endpoints{
        httpd_uri_t{"/api/profiles", HTTP_GET, &list_profiles, nullptr},
        httpd_uri_t{"/api/profiles/preview", HTTP_POST, &preview_profile, nullptr},
        httpd_uri_t{"/api/profiles/*", HTTP_GET, &get_profile, nullptr},
        httpd_uri_t{"/api/profiles/*", HTTP_PUT, &put_profile, nullptr},
        httpd_uri_t{"/api/profiles/*", HTTP_POST, &reset_profile, nullptr},
        httpd_uri_t{"/api/profiles/*", HTTP_DELETE, &delete_profile, nullptr},
    };
    for (const auto& endpoint : endpoints) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &endpoint), TAG,
                            "Could not register %s", endpoint.uri);
    }
    return ESP_OK;
}

}  // namespace reflowCtrl
