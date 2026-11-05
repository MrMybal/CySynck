#include "plugin_metadata.hpp"
#include <regex>
#include <set>
#include <stdexcept>

namespace cysynck {
namespace {
void keys(const Json &value, const std::set<std::string> &allowed) {
    if (!value.is_object())
        throw std::runtime_error("Plugin metadata requires an object");
    for (const auto &[key, _] : value.items())
        if (!allowed.contains(key))
            throw std::runtime_error("Unknown plugin metadata field: " + key);
}
std::string text(const Json &value, size_t maximum, bool required = false) {
    auto result = value.get<std::string>();
    if (result.size() > maximum || (required && result.empty()) || result.find('\0') != result.npos)
        throw std::runtime_error("Plugin text is empty, contains NUL or exceeds its limit");
    (void)Json(result).dump();
    return result;
}
int64_t integer(const Json &value) {
    if (!value.is_number_integer() ||
        (value.is_number_unsigned() && value.get<uint64_t>() > 1000000000ull))
        throw std::runtime_error("Plugin integers must be within -1000000000 and 1000000000");
    auto number = value.get<int64_t>();
    if (number < -1000000000 || number > 1000000000)
        throw std::runtime_error("Plugin integer exceeds its supported range");
    return number;
}
void field_value(const Json &field, const Json &value) {
    auto type = field.at("type").get<std::string>();
    if (type == "boolean") {
        if (!value.is_boolean())
            throw std::runtime_error("Plugin option requires a Boolean");
    } else if (type == "integer") {
        auto number = integer(value);
        if (number < field.at("minimum").get<int64_t>() ||
            number > field.at("maximum").get<int64_t>())
            throw std::runtime_error("Plugin option is outside its declared range");
    } else if (type == "string") {
        (void)text(value, field.at("max_length").get<size_t>());
    } else
        throw std::runtime_error("Unsupported plugin option type");
}
Json fields(const Json &input) {
    if (!input.is_array() || input.size() > 32)
        throw std::runtime_error("Plugin modes support at most 32 option fields");
    Json result = Json::array();
    std::set<std::string> names;
    for (auto field : input) {
        keys(field, {"id", "label", "label_fr", "description", "description_fr", "type", "default",
                     "minimum", "maximum", "max_length"});
        auto name = text(field.at("id"), 64, true);
        if (!std::regex_match(name, std::regex("[A-Za-z][A-Za-z0-9_]{0,63}")) ||
            !names.insert(name).second)
            throw std::runtime_error("Invalid or duplicate plugin option identifier");
        field["label"] = text(field.at("label"), 160, true);
        for (auto key : {"label_fr", "description", "description_fr"})
            if (field.contains(key))
                field[key] = text(field.at(key), std::string(key) == "label_fr" ? 160 : 2048);
        auto type = field.at("type").get<std::string>();
        if (type == "integer") {
            if (field.contains("max_length"))
                throw std::runtime_error("Integer options cannot have max_length");
            field["minimum"] = integer(field.value("minimum", Json(-1000000000)));
            field["maximum"] = integer(field.value("maximum", Json(1000000000)));
            if (field.at("minimum").get<int64_t>() > field.at("maximum").get<int64_t>())
                throw std::runtime_error("Invalid plugin option range");
        } else if (type == "string") {
            if (field.contains("minimum") || field.contains("maximum"))
                throw std::runtime_error("String options cannot have numeric ranges");
            auto maximum = integer(field.value("max_length", Json(1024)));
            if (maximum < 1 || maximum > 4096)
                throw std::runtime_error("Plugin string limits must be between 1 and 4096 bytes");
            field["max_length"] = maximum;
        } else if (type != "boolean" || field.contains("minimum") || field.contains("maximum") ||
                   field.contains("max_length"))
            throw std::runtime_error("Invalid plugin option type or constraints");
        field_value(field, field.at("default"));
        result.push_back(field);
    }
    return result;
}
} // namespace
std::string plugin_text(const char *value, size_t maximum, bool required) {
    if (!value) {
        if (required)
            throw std::runtime_error("Plugin metadata string is missing");
        return {};
    }
    size_t length = 0;
    while (length <= maximum && value[length])
        ++length;
    if (length > maximum)
        throw std::runtime_error("Plugin string exceeds its limit");
    return text(std::string(value, length), maximum, required);
}
Json describe_plugin(const cysynck_plugin_v1 *api, const char *descriptor) {
    if (!api || api->abi_version != 1 || api->mode_count > 100 || (!api->modes && api->mode_count))
        throw std::runtime_error("Plugin ABI mismatch");
    auto id = plugin_text(api->id, 128, true);
    if (!std::regex_match(id, std::regex("[A-Za-z0-9][A-Za-z0-9_.-]{0,127}")))
        throw std::runtime_error("Invalid plugin identifier");
    Json meta = Json::object();
    if (descriptor) {
        auto encoded = plugin_text(descriptor, 65536, true);
        meta = Json::parse(encoded, [](int depth, Json::parse_event_t, Json &) {
            if (depth > 12)
                throw std::runtime_error("Plugin descriptor nesting exceeds its limit");
            return true;
        });
        keys(meta,
             {"schema", "name", "name_fr", "version", "description", "description_fr", "modes"});
        if (!meta.at("schema").is_number_integer() || meta.at("schema") != 1)
            throw std::runtime_error("Unsupported plugin descriptor schema");
    }
    Json result{{"id", id},
                {"name", text(meta.value("name", Json(id)), 160, true)},
                {"version", text(meta.value("version", Json("")), 80)},
                {"description", text(meta.value("description", Json("")), 4096)},
                {"descriptor", descriptor != nullptr},
                {"abi_version", 1},
                {"before_events", api->before_event != nullptr},
                {"after_events", api->after_event != nullptr},
                {"modes", Json::array()}};
    for (auto key : {"name_fr", "description_fr"})
        if (meta.contains(key))
            result[key] = text(meta.at(key), std::string(key) == "name_fr" ? 160 : 4096);
    auto descriptions = meta.value("modes", Json::object());
    if (!descriptions.is_object())
        throw std::runtime_error("Plugin mode descriptors require an object");
    std::set<std::string> names;
    for (size_t index = 0; index < api->mode_count; ++index) {
        const auto &mode = api->modes[index];
        auto name = plugin_text(mode.id, 80, true);
        if (!std::regex_match(name, std::regex("[A-Za-z0-9][A-Za-z0-9_-]{0,79}")) ||
            !names.insert(name).second || !mode.run)
            throw std::runtime_error("Invalid or duplicate plugin mode");
        auto extra = descriptions.value(name, Json::object());
        keys(extra, {"fields", "label_fr", "description_fr"});
        Json item{{"id", name},
                  {"plugin", id},
                  {"label", plugin_text(mode.label, 160, true)},
                  {"description", plugin_text(mode.description, 4096)},
                  {"fields", fields(extra.value("fields", Json::array()))}};
        (void)validate_mode_options(item, Json::object());
        for (auto key : {"label_fr", "description_fr"})
            if (extra.contains(key))
                item[key] = text(extra.at(key), std::string(key) == "label_fr" ? 160 : 4096);
        result["modes"].push_back(item);
    }
    for (const auto &[name, _] : descriptions.items())
        if (!names.contains(name))
            throw std::runtime_error("Descriptor names an unknown plugin mode");
    return result;
}
Json validate_mode_options(const Json &mode, const Json &values) {
    if (!values.is_object() || values.size() > 32 || values.dump().size() > 32768)
        throw std::runtime_error("Mode options require an object of at most 32 fields and 32 KiB");
    Json result = Json::object();
    for (const auto &field : mode.value("fields", Json::array())) {
        auto name = field.at("id").get<std::string>();
        auto value = values.value(name, field.at("default"));
        try {
            field_value(field, value);
        } catch (const std::exception &error) {
            throw std::runtime_error("Invalid mode option " + name + ": " + error.what());
        }
        result[name] = value;
    }
    for (const auto &[name, _] : values.items())
        if (!result.contains(name))
            throw std::runtime_error("Unknown mode option: " + name);
    if (result.dump().size() > 32768)
        throw std::runtime_error("Resolved mode options exceed 32 KiB");
    return result;
}
} // namespace cysynck
