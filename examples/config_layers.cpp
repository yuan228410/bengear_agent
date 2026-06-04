#include "ben_gear/config/loader.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    const auto workspace = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
    const auto settings = ben_gear::config::load_config(workspace);

    std::cout << "provider=" << ben_gear::config::provider_name(settings.provider) << '\n'
              << "base_url=" << settings.base_url << '\n'
              << "api_url=" << (settings.api_url.empty() ? "<default>" : settings.api_url) << '\n'
              << "model=" << settings.model << '\n'
              << "max_tokens=" << settings.max_tokens << '\n'
              << "temperature=" << settings.temperature << '\n'
              << "api_key=" << (settings.api_key.empty() ? "<empty>" : "<set>") << '\n';
}
