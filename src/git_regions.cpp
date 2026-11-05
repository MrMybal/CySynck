#include "git_regions.hpp"
#include <algorithm>
#include <array>
#include <string_view>

namespace cysynck {
namespace {
constexpr size_t region_limit = 256;
constexpr std::array<std::string_view, 4> markers = {"<<<<<<< LOCAL", "||||||| ANCESTOR",
                                                     "=======", ">>>>>>> PEER"};

bool source_markers(const std::string &text) {
    size_t start = 0;
    while (start < text.size()) {
        auto line = std::string_view(text).substr(start);
        for (auto marker : markers)
            if (line.starts_with(marker.substr(0, 7)))
                return true;
        auto end = text.find('\n', start);
        if (end == text.npos)
            break;
        start = end + 1;
    }
    return false;
}
} // namespace

Json git_conflict_regions(const Json &draft) {
    auto unavailable = [](const char *reason) {
        return Json{{"available", false},
                    {"reason", reason},
                    {"count", 0},
                    {"token", nullptr},
                    {"segments", Json::array()}};
    };
    if (draft.at("kind") != "conflict" || !draft.at("candidate").is_string())
        return unavailable("unsupported");
    for (auto key : {"ancestor", "local", "peer"})
        if (source_markers(draft.at("texts").at(key).get_ref<const std::string &>()))
            return unavailable("source_markers");
    const auto &text = draft.at("candidate").get_ref<const std::string &>();
    Json segments = Json::array(), region;
    size_t start = 0, body = 0, count = 0;
    int phase = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        size_t next = end == text.npos ? text.size() : end + 1;
        auto line = std::string_view(text).substr(start, next - start);
        if (line.ends_with('\n'))
            line.remove_suffix(1);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        int marker = -1;
        for (int i = 0; i < 4; ++i)
            if (line == markers[i])
                marker = i;
        if (marker >= 0) {
            if (marker != phase)
                return unavailable("invalid_proposal");
            if (phase == 0) {
                if (count >= region_limit)
                    return unavailable("region_limit");
                if (start != body)
                    segments.push_back(
                        {{"kind", "context"}, {"text", text.substr(body, start - body)}});
                region = {{"kind", "conflict"}, {"id", count++}};
            } else {
                region[phase == 1   ? "local"
                       : phase == 2 ? "ancestor"
                                    : "peer"] = text.substr(body, start - body);
                if (phase == 3)
                    segments.push_back(region);
            }
            phase = (phase + 1) % 4;
            body = next;
        }
        start = next;
    }
    if (phase != 0 || count == 0 ||
        std::min(count, size_t(127)) != draft.at("conflicts").get<size_t>())
        return unavailable("invalid_proposal");
    if (body < text.size())
        segments.push_back({{"kind", "context"}, {"text", text.substr(body)}});
    Json context{{"format", "cysynck-diff3-regions-v1"}, {"candidate", sha256(text)}};
    for (auto key : {"path", "base", "head", "merge_base", "local_ref", "tracking_ref"})
        context[key] = draft.at(key);
    return {{"available", true},
            {"reason", ""},
            {"count", count},
            {"token", sha256(context.dump())},
            {"segments", segments}};
}

std::string compose_git_regions(const Json &regions, const Json &choices) {
    if (!regions.at("available").get<bool>())
        throw std::runtime_error("Conflict-region choices are unavailable for this file proposal");
    if (!choices.is_array() || choices.size() != regions.at("count").get<size_t>() ||
        choices.size() > region_limit)
        throw std::runtime_error("Choose local, peer or both for every conflict region");
    for (const auto &choice : choices)
        if (!choice.is_string() || (choice != "local" && choice != "peer" && choice != "both"))
            throw std::runtime_error("Choose local, peer or both for every conflict region");
    std::string result;
    for (const auto &part : regions.at("segments")) {
        if (part.at("kind") == "context")
            result += part.at("text").get_ref<const std::string &>();
        else {
            const auto &choice = choices.at(part.at("id").get<size_t>());
            if (choice == "local" || choice == "both")
                result += part.at("local").get_ref<const std::string &>();
            if (choice == "peer" || choice == "both")
                result += part.at("peer").get_ref<const std::string &>();
        }
        if (result.size() > 512 * 1024)
            throw std::runtime_error("Composed Git conflict text exceeds 512 KiB");
    }
    return result;
}
} // namespace cysynck
