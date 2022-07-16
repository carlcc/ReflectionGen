#pragma once
#include <string>
#include <vector>

struct ReflectionGenConfig {
    std::string scriptFile {};
    std::vector<std::string> includeRegexes {};
    std::vector<std::string> excludeRegexes {};
    std::vector<std::string> dirs {};
    std::vector<std::string> files {};
    std::string outputDir {};
    uint32_t workThreadsCount {};
    std::vector<const char*> clangParams {};
};

class ReflectionGen {
public:
    explicit ReflectionGen(ReflectionGenConfig&& cfg)
        : config_ { std::move(cfg) }
    {
    }
    int Run();

private:
    ReflectionGenConfig config_;
};
