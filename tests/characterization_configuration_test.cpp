#include "characterization_configuration.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

constexpr auto CONFIGURATION = R"({
    "formatVersion":1,"type":"oven-characterization","ambientTemperatureC":26.32,
    "heatingRate":[{"temperatureC":37.5,"rateCPerSecond":0.3512,"sampleCount":337}],
    "passiveCoolingRate":[],"coastOvershoot":[],"estimatedEquilibriumTemperatureC":null,
    "summary":{"minimumTemperatureC":24.95,"maximumTemperatureC":241.56,
    "maximumHeatingRateCPerSecond":0.3512,"maximumObservedOvershootC":null,
    "totalDurationSeconds":817.4},
    "dataQuality":{"inputSamples":4798,"usedSamples":3946,"warnings":["Sampling gap"]}
})";

TEST(CharacterizationConfiguration, AcceptsAnalysisWithMissingCoolingAndOvershoot) {
    EXPECT_TRUE(reflowCtrl::is_valid_characterization_configuration(CONFIGURATION));
    EXPECT_TRUE(
        reflowCtrl::is_valid_characterization_configuration(std::string(CONFIGURATION) + " \n\t"));
}

TEST(CharacterizationConfiguration, RejectsMalformedOrTrailingData) {
    for (const auto* invalid : {"", "null", "{}", "{", "[]"}) {
        EXPECT_FALSE(reflowCtrl::is_valid_characterization_configuration(invalid));
    }
    EXPECT_FALSE(
        reflowCtrl::is_valid_characterization_configuration(std::string(CONFIGURATION) + "{}"));
    EXPECT_FALSE(reflowCtrl::is_valid_characterization_configuration(std::string(CONFIGURATION)
                                                                     + std::string(1, '\0')));
}

TEST(CharacterizationConfiguration, RejectsUnsupportedSchemaAndInvalidNumbers) {
    for (const auto* replacement : {"2", "null", "\"1\""}) {
        std::string json = CONFIGURATION;
        json.replace(json.find("\"formatVersion\":1") + 16, 1, replacement);
        EXPECT_FALSE(reflowCtrl::is_valid_characterization_configuration(json));
    }
    for (const auto* replacement : {"null", "\"warm\"", "1e999"}) {
        std::string json = CONFIGURATION;
        json.replace(json.find("26.32"), 5, replacement);
        EXPECT_FALSE(reflowCtrl::is_valid_characterization_configuration(json));
    }
}

TEST(CharacterizationConfiguration, RejectsOversizedOrDeeplyNestedDocuments) {
    EXPECT_FALSE(reflowCtrl::is_valid_characterization_configuration(std::string(8193, ' ')));
    std::string json = CONFIGURATION;
    json.insert(1, "\"extra\":" + std::string(20, '[') + "0" + std::string(20, ']') + ",");
    EXPECT_FALSE(reflowCtrl::is_valid_characterization_configuration(json));
}

}  // namespace
