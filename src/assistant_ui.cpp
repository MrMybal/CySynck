#include "assistant_ui.hpp"
#include "assistant.hpp"
#include <cymcp/chat_ui.hpp>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <future>
#include <sstream>

namespace cysynck::assistant {
namespace {
bool application_provider(const std::string &id) {
    // These factories consume ProviderConfig::tools in the pinned SDK. Other
    // native agents can connect through our external MCP endpoint instead.
    return id == "codex" || id == "openai" || id == "anthropic" || id == "openrouter" ||
           id == "ollama" || id == "openai-compatible" || id == "llama.cpp" || id == "lmstudio";
}
} // namespace
struct Panel::Impl {
    Core &core;
    Host host;
    std::shared_ptr<cymcp::ProviderRegistry> providers =
        std::make_shared<cymcp::ProviderRegistry>();
    cymcp::ChatController controller{providers};
    std::string attachment;
    cymcp::ChatPanel chat;
    std::string provider = "openai-compatible", model, endpoint = "http://127.0.0.1:8080/v1";
    std::string key_env = "CYSYNCK_AI_API_KEY", executable, history_id = "main", error;
    std::string remote_url, remote_env = "CYSYNCK_REMOTE_MCP_TOKEN", remote_tools;
    bool restore = false, history = true, writes = false, full = false;
    bool remote_pending = false;
    int port = 0, listening = 0;
    std::future<std::vector<cymcp::DiscoveredConnection>> discovery;
    std::vector<cymcp::DiscoveredConnection> discovered;
    std::future<void> remote;
    std::future<void> endpoint_stop;
    cymcp::Cancellation discovery_cancel;
    cymcp::Cancellation remote_cancel;
    cymcp::ProviderConfig detected;
    bool has_detected = false;
    explicit Impl(Core &value, std::shared_ptr<DesktopBridge> desktop)
        : core(value), host(value, Options::load(value.state()), true, std::move(desktop)),
          chat(controller, [this] {
              cymcp::ChatPanelOptions options;
              options.pick_attachments = [this] {
                  return attachment.empty() ? std::vector<fs::path>{}
                                            : std::vector<fs::path>{fs::u8path(attachment)};
              };
              options.require_editable_history = true;
              options.edit_message = [this](auto id, const std::string &text) {
                  controller.edit_message(id, text);
              };
              options.delete_message = [this](auto id) { controller.delete_turn(id); };
              return options;
          }()) {}
    ~Impl() {
        controller.cancel();
        discovery_cancel.cancel();
        remote_cancel.cancel();
        if (discovery.valid())
            discovery.wait();
        if (remote.valid())
            remote.wait();
        if (endpoint_stop.valid())
            endpoint_stop.wait();
        host.stop();
    }
    void draw(bool french) {
        auto tr = [french](const char *en, const char *fr) { return french ? fr : en; };
        controller.poll();
        if (endpoint_stop.valid() &&
            endpoint_stop.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                endpoint_stop.get();
            } catch (const std::exception &e) {
                error = cymcp::redact_error_message(e.what());
            }
            listening = 0;
        }
        if (discovery.valid() &&
            discovery.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                discovered = discovery.get();
            } catch (const std::exception &e) {
                error = cymcp::redact_error_message(e.what());
            }
        }
        if (remote.valid() &&
            remote.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                remote.get();
            } catch (const std::exception &e) {
                error = cymcp::redact_error_message(e.what());
            }
            remote_pending = false;
        }
        if (ImGui::CollapsingHeader(tr("Assistant settings / MCP", "Réglages de l’assistant / MCP"),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("assistant-connection-settings", ImVec2(0, 300),
                              ImGuiChildFlags_Borders);
            if (ImGui::Checkbox(
                    tr("Allow application actions", "Autoriser les actions de l’application"),
                    &writes))
                host.allow_writes(writes);
            ImGui::TextWrapped(
                "%s",
                tr("Read-only by default. Enabling changes authorizes the chat and connected MCP "
                   "clients. Existing preview tokens, backups and recovery rules still apply.",
                   "Lecture seule par défaut. Cette autorisation s’applique au chat et aux clients "
                   "MCP connectés. Les aperçus, sauvegardes et règles de récupération restent "
                   "obligatoires."));
            ImGui::BeginDisabled(discovery.valid());
            if (ImGui::Button(tr("Detect providers", "Détecter les fournisseurs"))) {
                discovery_cancel = {};
                auto context = cymcp::CallContext{};
                context.cancellation = discovery_cancel;
                discovery = std::async(std::launch::async, [registry = providers, context] {
                    return registry->discover({}, context);
                });
            }
            ImGui::EndDisabled();
            if (discovery.valid()) {
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Cancel detection", "Annuler la recherche")))
                    discovery_cancel.cancel();
            }
            ImGui::SameLine();
            if (ImGui::BeginCombo(tr("Provider", "Fournisseur"), provider.c_str())) {
                for (const auto &item : providers->describe()) {
                    auto id = item.at("id").get<std::string>();
                    if (!application_provider(id))
                        continue;
                    if (ImGui::Selectable(id.c_str(), provider == id)) {
                        provider = id;
                        detected = {};
                        has_detected = false;
                        endpoint =
                            item.value("defaults", Json::object()).value("base_url", std::string());
                    }
                }
                ImGui::EndCombo();
            }
            for (std::size_t n = 0; n < discovered.size(); ++n) {
                auto &item = discovered[n];
                if (!item.available() || !application_provider(item.provider_id))
                    continue;
                ImGui::PushID(static_cast<int>(n));
                if (ImGui::SmallButton(item.name.c_str())) {
                    provider = item.provider_id;
                    detected = item.config;
                    has_detected = true;
                    endpoint = detected.settings.value("base_url", std::string());
                    executable = cymcp::path_utf8(detected.process.executable);
                    model = cymcp::default_model(item.models);
                }
                ImGui::PopID();
            }
            if (provider != "codex") {
                ImGui::InputText(tr("Endpoint", "Adresse du service"), &endpoint);
                ImGui::InputText(
                    tr("API key environment variable", "Variable d’environnement de la clé API"),
                    &key_env);
            } else
                ImGui::InputText(
                    tr("Native CLI executable (optional)", "Exécutable CLI natif (facultatif)"),
                    &executable);
            ImGui::InputText(tr("Model", "Modèle"), &model);
            if (ImGui::BeginCombo(tr("Available models", "Modèles disponibles"), model.c_str())) {
                for (const auto &item : controller.state().models) {
                    auto id = item.is_string() ? item.get<std::string>()
                                               : item.value("id", std::string());
                    if (!id.empty() && ImGui::Selectable(id.c_str(), model == id)) {
                        model = id;
                        if (controller.state().connected && !controller.state().busy)
                            controller.select_model(model);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox(tr("Keep private history", "Conserver l’historique privé"), &history);
            ImGui::SameLine();
            ImGui::Checkbox(tr("Restore saved session", "Restaurer la session enregistrée"),
                            &restore);
            ImGui::InputText(tr("History ID", "Identifiant d’historique"), &history_id);
            ImGui::InputText(tr("Attachment path", "Chemin de pièce jointe"), &attachment);
            ImGui::Separator();
            ImGui::BeginDisabled(listening != 0);
            ImGui::InputInt(
                tr("Local MCP port (0 = automatic)", "Port MCP local (0 = automatique)"), &port);
            ImGui::Checkbox(tr("Full MCP catalogue", "Catalogue MCP complet"), &full);
            if (ImGui::Button(tr("Start local MCP", "Démarrer le MCP local"))) {
                try {
                    listening = host.listen(port, full);
                    error.clear();
                } catch (const std::exception &e) {
                    error = cymcp::redact_error_message(e.what());
                }
            }
            ImGui::EndDisabled();
            if (listening > 0) {
                ImGui::Text("http://127.0.0.1:%d/mcp", listening);
                ImGui::TextWrapped("%s: %s", tr("Bearer token file", "Fichier du jeton Bearer"),
                                   path_string(core.state() / "api-token").c_str());
                if (ImGui::Button(tr("Stop MCP", "Arrêter MCP"))) {
                    listening = -1;
                    endpoint_stop = std::async(std::launch::async, [this] { host.stop(); });
                }
            }
            if (ImGui::TreeNode(
                    tr("External application tools", "Outils d’applications externes"))) {
                ImGui::InputText("URL MCP", &remote_url);
                ImGui::InputText(
                    tr("Token environment variable", "Variable d’environnement du jeton"),
                    &remote_env);
                ImGui::InputText(tr("Allowed tool names (comma separated)",
                                    "Outils autorisés (séparés par des virgules)"),
                                 &remote_tools);
                ImGui::BeginDisabled(remote_pending || controller.state().busy);
                if (ImGui::Button(
                        tr("Connect selected tools", "Connecter les outils sélectionnés"))) {
                    std::vector<std::string> names;
                    std::stringstream input(remote_tools);
                    std::string item;
                    while (std::getline(input, item, ',')) {
                        auto first = item.find_first_not_of(" \t");
                        auto last = item.find_last_not_of(" \t");
                        if (first != item.npos)
                            names.push_back(item.substr(first, last - first + 1));
                    }
                    auto token = std::getenv(remote_env.c_str());
                    std::string secret = token ? token : "";
                    remote_pending = true;
                    remote_cancel = {};
                    remote = std::async(std::launch::async, [this, url = remote_url, secret, names,
                                                             cancel = remote_cancel] {
                        host.connect_remote(url, secret, names, cancel);
                    });
                }
                ImGui::EndDisabled();
                if (remote_pending &&
                    ImGui::SmallButton(tr("Cancel connection", "Annuler la connexion")))
                    remote_cancel.cancel();
                ImGui::TreePop();
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(controller.state().busy);
            if (ImGui::Button(tr("Connect / new chat", "Connexion / nouvelle conversation"))) {
                try {
                    cymcp::ChatConnection connection;
                    connection.provider_id = provider;
                    connection.model = model;
                    connection.provider = has_detected ? detected : cymcp::ProviderConfig{};
                    if (provider == "codex") {
                        connection.provider.settings["allow_all_actions"] = false;
                        connection.provider.settings["sandbox"] = "read-only";
                        connection.provider.settings["approval_policy"] = "never";
                        connection.provider.approvals = cymcp::Policy::deny_all();
                    }
                    if (!endpoint.empty())
                        connection.provider.settings["base_url"] = endpoint;
                    connection.provider.settings["deferred_tools"] = host.options().compact;
                    if (!executable.empty())
                        connection.provider.process.executable = fs::u8path(executable);
                    if (const auto *key = std::getenv(key_env.c_str()))
                        connection.provider.api_key = key;
                    connection.provider.tools = host.tools();
                    connection.context.max_bytes = host.options().context_bytes;
                    connection.performance = host.performance();
                    connection.system_prompt =
                        "You assist the user of CySynck. Use the registered CySynck tools for all "
                        "application actions. Discover operations and inspect their schemas before "
                        "calling them. Respect write permissions and preview/plan tokens. Sending "
                        "messages/files requires an explicit user request. Never confirm a peer's "
                        "key or fingerprint from discovery alone: require independent user "
                        "verification before trust changes. Never "
                        "retry a mutation after an error, cancellation or expired result without "
                        "inspecting durable application state and an explicit user request. Ask "
                        "before requesting an "
                        "authorization change. Native shell/OS actions are not authorized by the "
                        "application.";
                    if (history) {
                        connection.history_directory = core.state() / "assistant-history";
                        connection.history_id = history_id;
                    }
                    connection.restore = restore;
                    connection.automatic_model = model.empty();
                    controller.open(std::move(connection));
                    error.clear();
                } catch (const std::exception &e) {
                    error = cymcp::redact_error_message(e.what());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Refresh models", "Actualiser les modèles"))) {
                try {
                    controller.refresh_models();
                } catch (const std::exception &e) {
                    error = cymcp::redact_error_message(e.what());
                }
            }
            ImGui::EndDisabled();
            if (controller.state().busy && ImGui::Button(tr("Cancel", "Annuler")))
                controller.cancel();
        }
        if (!error.empty())
            ImGui::TextWrapped("%s", error.c_str());
        // SDK poll/Markdown rendering stays on the same native ImGui thread.
        chat.draw("cysynck-assistant");
    }
};
Panel::Panel(Core &core, std::shared_ptr<DesktopBridge> desktop)
    : impl_(std::make_unique<Impl>(core, std::move(desktop))) {}
Panel::~Panel() = default;
void Panel::draw(bool french) { impl_->draw(french); }
void Panel::changed(const Json &events) { impl_->host.observe(events); }
int Panel::listen(int port, bool allow_write) {
    impl_->writes = allow_write;
    impl_->host.allow_writes(allow_write);
    impl_->listening = impl_->host.listen(port, impl_->full);
    return impl_->listening;
}
} // namespace cysynck::assistant
