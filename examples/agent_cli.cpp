#include "ben_gear/ben_gear.hpp"

#include <filesystem>
#include <iostream>

int main() {
    auto settings = ben_gear::load_config(std::filesystem::current_path());
    if (settings.api_key.empty()) {
        std::cout << "Set BEN_GEAR_API_KEY before running this example.\n";
        return 0;
    }

    ben_gear::Agent agent(std::move(settings));
    auto result = agent.run("用一句话介绍 BenGear");
    std::cout << result.text << '\n';
}
