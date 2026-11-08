#pragma once
#include <cysynck/core.hpp>
#include <string_view>

namespace cysynck::cyrevision {
using Document = nlohmann::ordered_json;
Document parse(const std::string &bytes);
Document identity(const Document &value);
Document bundle_manifest(const Document &value);
Document content_inventory(const Document &value);
std::string normalize_guid(const std::string &value);
std::string encode_string(const std::string &value);
std::string canonical_bundle(const Document &value, bool legacy_windows = false);
std::string canonical_inventory(const Document &value, bool legacy_windows = false);
std::string base64_encode(std::string_view value);
std::string base64_decode(const std::string &value, size_t expected_size);
std::string fingerprint(const std::string &public_key);
std::string openssl_tool();
void generate_key(const fs::path &private_key);
std::string public_key(const fs::path &private_key);
std::string sign(const fs::path &private_key, const std::string &payload);
bool verify(const std::string &public_key, const std::string &payload, const std::string &signature,
            const fs::path &scratch_parent);
bool verify_bundle(const Document &document, const std::string &expected_public_key,
                   const fs::path &scratch_parent);
std::string signed_bundle(const Document &manifest, const fs::path &private_key,
                          bool legacy_windows = false);
bool verify_inventory(const Document &document, const std::string &expected_public_key,
                      const fs::path &scratch_parent);
std::string signed_inventory(const Document &inventory, const fs::path &private_key,
                             bool legacy_windows = false);
} // namespace cysynck::cyrevision
