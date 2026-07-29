#include "Nebula/Config/Config.h"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <utility>

#include "Nebula/Core/Log.h"

namespace {

void EnsureParentDirectories(const std::string& filePath) {
    size_t separator = 0;
    while ((separator = filePath.find('/', separator + 1)) !=
           std::string::npos) {
        const std::string directory = filePath.substr(0, separator);
        if (!directory.empty() &&
            mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
            NEBULA_LOGW("Could not create config directory %s: %d",
                        directory.c_str(), errno);
            return;
        }
    }
}

} // namespace

namespace Nebula {

Config& Config::Get() {
    static Config instance;
    return instance;
}

void Config::SetPath(std::string path) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = std::move(path);
}

const std::string& Config::GetPath() const {
    return path_;
}

bool Config::Load() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (path_.empty()) {
        return false;
    }

    std::ifstream input(path_);
    if (!input.good()) {
        NEBULA_LOGI("No config yet at %s; defaults will be used",
                    path_.c_str());
        return false;
    }

    try {
        nlohmann::json loaded;
        input >> loaded;
        if (!loaded.is_object()) {
            NEBULA_LOGE("Config root must be a JSON object");
            return false;
        }
        values_ = std::move(loaded);
        NEBULA_LOGI("Loaded config from %s", path_.c_str());
        return true;
    } catch (const std::exception& error) {
        NEBULA_LOGE("Could not parse config: %s", error.what());
        return false;
    }
}

bool Config::Save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (path_.empty()) {
        return false;
    }

    EnsureParentDirectories(path_);
    const std::string temporaryPath = path_ + ".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::trunc);
        if (!output.good()) {
            NEBULA_LOGE("Could not open %s for writing",
                        temporaryPath.c_str());
            return false;
        }
        output << values_.dump(2) << '\n';
        output.flush();
        if (!output.good()) {
            NEBULA_LOGE("Could not write config");
            return false;
        }
    }

    if (std::rename(temporaryPath.c_str(), path_.c_str()) != 0) {
        NEBULA_LOGE("Could not replace config file: %d", errno);
        std::remove(temporaryPath.c_str());
        return false;
    }
    return true;
}

bool Config::GetBool(
    const std::string& key, bool defaultValue) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto value = values_.find(key);
    return value != values_.end() && value->is_boolean()
               ? value->get<bool>()
               : defaultValue;
}

int Config::GetInt(
    const std::string& key, int defaultValue) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto value = values_.find(key);
    return value != values_.end() && value->is_number_integer()
               ? value->get<int>()
               : defaultValue;
}

float Config::GetFloat(
    const std::string& key, float defaultValue) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto value = values_.find(key);
    return value != values_.end() && value->is_number()
               ? value->get<float>()
               : defaultValue;
}

void Config::SetBool(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_[key] = value;
}

void Config::SetInt(const std::string& key, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_[key] = value;
}

void Config::SetFloat(const std::string& key, float value) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_[key] = value;
}

} // namespace Nebula
