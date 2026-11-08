#pragma once
#include <cysynck/core.hpp>
namespace cysynck {
struct EndpointIdentity {
    std::string project, peer, wireguard_key, signing_key, device_id;
    int listen_port = 0;
};
std::string sign_endpoint_record(const EndpointIdentity &identity, const std::string &endpoint,
                                 const std::string &source, int64_t sequence, int64_t issued_at,
                                 int64_t expires_at, const fs::path &private_signing_key);
// expected contains independently pinned identities, never values copied from the envelope.
// Returns validated metadata and a canonical hash for a private anti-replay checkpoint.
Json verify_endpoint_record(const std::string &bytes, const EndpointIdentity &expected, int64_t now,
                            const fs::path &scratch_parent);
} // namespace cysynck
