#include "help.hpp"
#include <httplib.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace cysynck;
static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
static void set_root(const std::string &value) {
#ifdef _WIN32
    _putenv_s("CYSYNCK_DOC_ROOT", value.c_str());
#else
    if (value.empty())
        unsetenv("CYSYNCK_DOC_ROOT");
    else
        setenv("CYSYNCK_DOC_ROOT", value.c_str(), 1);
#endif
}
int main(int argc, char **argv) {
    const auto previous = env("CYSYNCK_DOC_ROOT");
    try {
        require(argc == 2, "Expected the packaged documentation location");
        set_root(argv[1]);
        Core core(fs::current_path() / "help-http-test-data" / random_id());
        const auto token = core.api_token();
        HttpServer server(core);
        const auto port = server.bind("127.0.0.1", 0);
        std::jthread listener([&] { server.listen(); });
        try {
            httplib::Client client("127.0.0.1", port);
            client.set_connection_timeout(2);
            client.set_read_timeout(10);
            const httplib::Headers auth{{"Authorization", "Bearer " + token}};
            auto response = client.Get("/api/help/archive");
            require(response && response->status == 401,
                    "Help archives must require authentication");
            // Authentication precedes body parsing. Keep this exact-status probe
            // bodyless so Windows cannot reset a connection with unread POST bytes.
            response = client.Post("/api/help", "", "application/json");
            require(response && response->status == 401,
                    "Help locations must require authentication");
            response = client.Post("/api/help", auth, R"({"context":"Backups","language":"fr"})",
                                   "application/json");
            require(response && response->status == 200, "Authenticated contextual help failed");
            const auto location = Json::parse(response->body);
            require(location.at("language") == "fr" && location.at("page") == "backups" &&
                        location.at("section") == "verifier-et-restaurer" &&
                        location.at("can_open") == true,
                    "Web help lost context, language or loopback eligibility");
            require(location.at("url").get<std::string>().starts_with("file:///"),
                    "The reader must remain a local file URL");
            response = client.Post("/api/help", auth, R"({"context":"Network","language":"zz"})",
                                   "application/json");
            require(response && response->status == 200 &&
                        Json::parse(response->body).at("language") == "en",
                    "Unsupported Web help locale must fall back to English");
            auto foreign = auth;
            foreign.emplace("Origin", "https://foreign.invalid");
            response = client.Post("/api/help/open", foreign, "{}", "application/json");
            require(response && response->status == 403,
                    "Cross-origin browser launching must be refused");
            response = client.Get("/api/help/archive", auth);
            require(response && response->status == 200, "Offline archive download failed");
            require(response->get_header_value("Content-Type").starts_with("application/zip"),
                    "Wrong archive MIME type");
            const auto archive = fs::u8path(argv[1]).parent_path() / "CySynck-Documentation.zip";
            require(response->body == read_file(archive, 32 * 1024 * 1024),
                    "Downloaded archive is incomplete or changed");
            response = client.Get("/Documentation/Documentation.html");
            require(response && response->status == 404,
                    "Do not host the offline reader through HTTP");
            response = client.Get("/help-contexts.json");
            require(response && response->status == 200 &&
                        Json::parse(response->body).contains("Backups"),
                    "Shared Web context catalog missing");
            server.stop();
            listener.join();
        } catch (...) {
            server.stop();
            listener.join();
            throw;
        }
        set_root(previous);
        std::cout << "Web help authentication, origin refusal, context/fallback and offline "
                     "archive passed; no browser was launched\n";
        return 0;
    } catch (const std::exception &error) {
        set_root(previous);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
