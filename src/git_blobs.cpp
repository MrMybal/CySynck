#include "git_blobs.hpp"
#include "git_lfs.hpp"
#include <algorithm>

namespace cysynck {
std::map<std::string, std::string> git_small_blobs(const Json &project, const Json &entries) {
    if (!entries.is_array() || entries.size() > 10000)
        throw std::runtime_error("Small Git blob inspection is limited to 10000 entries");
    std::map<std::string, uintmax_t> sizes;
    for (const auto &entry : entries) {
        auto id = entry.at("oid").get<std::string>();
        const auto &bytes = entry.at("bytes");
        if ((id.size() != 40 && id.size() != 64) ||
            id.find_first_not_of("0123456789abcdef") != id.npos || !bytes.is_number_integer() ||
            (!bytes.is_number_unsigned() && bytes.get<int64_t>() < 0))
            throw std::runtime_error("Invalid small Git blob metadata");
        auto size = bytes.get<uintmax_t>();
        if (size >= 1024)
            continue;
        auto [it, inserted] = sizes.emplace(id, size);
        if (!inserted && it->second != size)
            throw std::runtime_error("Inconsistent small Git blob sizes");
    }
    std::map<std::string, std::string> result;
    for (auto first = sizes.begin(); first != sizes.end();) {
        auto last = first;
        std::string input;
        for (size_t count = 0; last != sizes.end() && count < 256; ++count, ++last)
            input += last->first + '\n';
        auto output = offline_git(project, {"cat-file", "--batch"}, input);
        size_t position = 0;
        for (auto it = first; it != last; ++it) {
            auto header = it->first + " blob " + std::to_string(it->second) + '\n';
            if (output.compare(position, header.size(), header) != 0)
                throw std::runtime_error("Small Git blob header differs from its inventory");
            position += header.size();
            auto size = static_cast<size_t>(it->second);
            if (position >= output.size() || size >= output.size() - position ||
                output[position + size] != '\n')
                throw std::runtime_error("Incomplete small Git blob batch");
            result.emplace(it->first, output.substr(position, size));
            position += size + 1;
        }
        if (position != output.size())
            throw std::runtime_error("Unexpected small Git blob batch data");
        first = last;
    }
    return result;
}
} // namespace cysynck
