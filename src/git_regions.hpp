#pragma once
#include <cysynck/core.hpp>

namespace cysynck {
// Only accepts the internally generated, bounded diff3 file proposal.
Json git_conflict_regions(const Json &draft);
std::string compose_git_regions(const Json &regions, const Json &choices);
} // namespace cysynck
