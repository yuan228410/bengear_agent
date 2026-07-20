#include "config/loader.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    const auto workspace = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
    const auto settings = ben_gear::config::load_config(workspace);

    std::cout << "provider=" << ben_gear::config::provider_name(settings.llm.provider) << '\n'
              << "base_url=" << settings.llm.base_url << '\n'
              << "api_url=" << (settings.llm.api_url.empty() ? "<default>" : settings.llm.api_url) << '\n'
              << "model=" << settings.llm.model << '\n'
              << "max_tokens=" << settings.llm.max_tokens << '\n'
              << "temperature=" << settings.llm.temperature << '\n'
              << "api_key=" << (settings.llm.api_key.empty() ? "<empty>" : "<set>") << '\n';
}
