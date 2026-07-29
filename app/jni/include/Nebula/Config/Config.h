#pragma once

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace Nebula {

class Config final {
public:
    static Config& Get();

    void SetPath(std::string path);
    [[nodiscard]] const std::string& GetPath() const;
    bool Load();
    bool Save() const;

    [[nodiscard]] bool GetBool(
        const std::string& key, bool defaultValue = false) const;
    [[nodiscard]] int GetInt(
        const std::string& key, int defaultValue = 0) const;
    [[nodiscard]] float GetFloat(
        const std::string& key, float defaultValue = 0.0F) const;

    void SetBool(const std::string& key, bool value);
    void SetInt(const std::string& key, int value);
    void SetFloat(const std::string& key, float value);

private:
    Config() = default;

    mutable std::mutex mutex_;
    std::string path_;
    nlohmann::json values_ = nlohmann::json::object();
};

} // namespace Nebula
