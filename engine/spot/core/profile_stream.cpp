#include "spot/core/profile_stream.h"

#include <nlohmann/json.hpp>

#include <cmath>

namespace Spot {

std::string EncodeProfileFrame(const ProfileFrame& frame) {
    nlohmann::json value = {{"version", 2},
                            {"number", frame.number},
                            {"duration", frame.duration.count()},
                            {"dropped", frame.droppedSamples},
                            {"samples", nlohmann::json::array()},
                            {"counters", nlohmann::json::array()}};
    for (const auto& sample : frame.samples) {
        value["samples"].push_back(
            {sample.name, sample.depth, sample.start.count(), sample.duration.count(), sample.thread});
    }
    for (const auto& counter : frame.counters) {
        value["counters"].push_back({counter.name, counter.value});
    }
    return std::string(ProfileStreamPrefix) + value.dump();
}

std::optional<ProfileFrame> DecodeProfileFrame(std::string_view line) {
    const auto start = line.find(ProfileStreamPrefix);
    if (start == std::string_view::npos || line.size() > 4 * 1024 * 1024) {
        return {};
    }
    line.remove_prefix(start + ProfileStreamPrefix.size());
    try {
        const auto value = nlohmann::json::parse(line.begin(), line.end());
        if (!value.is_object() || value.at("version") != 2 || !value.at("number").is_number_unsigned() ||
            !value.at("duration").is_number_integer() || !value.at("dropped").is_number_unsigned() ||
            !value.at("samples").is_array() || !value.at("counters").is_array() ||
            value.at("samples").size() > Profiler::MaxSamplesPerFrame || value.at("counters").size() > 1024) {
            return {};
        }
        ProfileFrame frame;
        frame.number = value.at("number").get<std::uint64_t>();
        frame.duration = std::chrono::nanoseconds(value.at("duration").get<std::int64_t>());
        frame.droppedSamples = value.at("dropped").get<std::size_t>();
        if (frame.duration.count() < 0) {
            return {};
        }
        // Validate the entire record before interning names from external input.
        for (const auto& sample : value.at("samples")) {
            if (!sample.is_array() || sample.size() != 5 || !sample[0].is_string() ||
                sample[0].get_ref<const std::string&>().size() > 256 || !sample[1].is_number_unsigned() ||
                sample[1].get<std::uint64_t>() > 1024 || !sample[2].is_number_integer() ||
                !sample[3].is_number_integer() || sample[2].get<std::int64_t>() < 0 ||
                sample[3].get<std::int64_t>() < 0 || !sample[4].is_number_unsigned() ||
                sample[4].get<std::uint64_t>() > 1024) {
                return {};
            }
        }
        for (const auto& counter : value.at("counters")) {
            if (!counter.is_array() || counter.size() != 2 || !counter[0].is_string() ||
                counter[0].get_ref<const std::string&>().size() > 256 || !counter[1].is_number() ||
                !std::isfinite(counter[1].get<double>())) {
                return {};
            }
        }
        for (const auto& sample : value.at("samples")) {
            frame.samples.push_back({.name = Profiler::Intern(sample[0].get_ref<const std::string&>()),
                                     .depth = sample[1].get<std::uint32_t>(),
                                     .start = std::chrono::nanoseconds(sample[2].get<std::int64_t>()),
                                     .duration = std::chrono::nanoseconds(sample[3].get<std::int64_t>()),
                                     .thread = sample[4].get<std::uint32_t>()});
        }
        for (const auto& counter : value.at("counters")) {
            frame.counters.push_back(
                {Profiler::Intern(counter[0].get_ref<const std::string&>()), counter[1].get<double>()});
        }
        return frame;
    } catch (const nlohmann::json::exception&) {
        return {};
    }
}

} // namespace Spot
