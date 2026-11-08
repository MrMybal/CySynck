#pragma once
#include "endpoint_record.hpp"
namespace cysynck {
EndpointIdentity network_endpoint_identity(const Json &pinned_profile, const std::string &project);
// Updates a private checkpoint only after signature, identity, expiry and replay checks.
Json accept_network_endpoint(const std::string &bytes, const EndpointIdentity &identity,
                             Json &checkpoint, int64_t now, const fs::path &scratch,
                             Json *private_history = nullptr);
// Compact floors survive project revocation/re-pairing and never become endpoint
// candidates. Existing legacy cached records can be migrated without losing trust removal.
void retain_endpoint_checkpoint(Json &private_history, const EndpointIdentity &identity,
                                 const Json &checkpoint);
// Uses the observed direct IP and the pinned WG UDP listener, never the sync/relay port.
std::string direct_network_endpoint(const Json &pinned_profile, const Json &connection);
Json resolved_network_endpoint(const Json &pinned_profile, const Json &checkpoints,
                               const Json &connection, int64_t now);
std::string effective_network_endpoint(const Json &peer, int64_t now);
} // namespace cysynck
