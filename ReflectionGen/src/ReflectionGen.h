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
    std::string relativeDir {};
    uint32_t workThreadsCount {};
    std::vector<const char*> clangParams {};
    std::vector<const char*> scriptParams {};
    bool debug { false };
};

class ReflectionGen {
public:
    explicit ReflectionGen(ReflectionGenConfig&& cfg)
        : config_ { std::move(cfg) }
    {
    }
    int Run();

private:
    bool CheckPaths();

private:
    ReflectionGenConfig config_;
};
