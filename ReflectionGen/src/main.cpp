#include "CLI11.hpp"
#include "ReflectionGen.h"
#include "StringUtils.h"
#include <algorithm>
#include <iostream>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<const char*> clangParams;
    {
        std::string_view dashDash { "--" };
        for (int i = 1; i < argc; ++i) {
            if (dashDash == argv[i]) {
                clangParams.reserve(argc - i - 1);
                auto oldArgc = argc;
                argc = i;
                for (++i; i < oldArgc; ++i) {
                    clangParams.push_back(argv[i]);
                }
                break;
            }
        }
    }

    CLI::App app { "ReflectionGen" };

    std::string scriptFile;
    std::vector<std::string> includeRegexes;
    std::vector<std::string> excludeRegexes;
    std::vector<std::string> concatenatedClangParamsList;
    std::vector<std::string> dirs;
    std::vector<std::string> files;
    std::string outputDir;
    uint32_t workThreadsCount = std::max(std::thread::hardware_concurrency() / 2, 1U);
    app.add_option("-s,--script", scriptFile, "The script used to process the parse result")
        ->required()
        ->expected(1);
    app.add_option("-d,--dir", dirs, "A list of directories in which the files will be parsed");
    app.add_option("-f,--file", files, "A list of files which will be parsed");
    app.add_option("--include", includeRegexes, "A list of regex to filter in files, so that only they will not be processed");
    app.add_option("--exclude", excludeRegexes, "A list of regex to filter out files, so that they will not be processed");
    app.add_option("--clang-params", concatenatedClangParamsList,
        "A list of command params to be passed to libclang. The first letter will be used as delimiter,"
        " which will then divide the rest into a list of arguments, which will be passed to libclang. e.g. ';-Idir1;-Idir2',"
        " then we will pass '-Idir1' and '-Idir2' to libclang.");
    app.add_option("-o,--output", outputDir, "The output directory to put the result, the final path will be: '${outputDir}/${inputFilePath}'")
        ->required()
        ->expected(1);
    app.add_option<uint32_t>("-j,--jobs", workThreadsCount, "Concurrent parsing.");

    CLI11_PARSE(app, argc, argv);

    for (auto& params : concatenatedClangParamsList) {
        if (params.empty()) {
            std::cerr << "You specified an empty string for --clang-params option, which is illegal, we will ignore it" << std::endl;
            continue;
        }
        std::vector<std::string_view> list = StringUtils::Split(params, params[0]);
        auto delimiter = params[0];
        for (auto& c : params) {
            if (c == delimiter) {
                c = '\0';
            }
        }
        for (size_t i = 1; i < list.size(); ++i) {
            clangParams.push_back(list[i].data());
        }
    }

    ReflectionGenConfig config {
        .scriptFile = std::move(scriptFile),
        .includeRegexes = std::move(includeRegexes),
        .excludeRegexes = std::move(excludeRegexes),
        .dirs = std::move(dirs),
        .files = std::move(files),
        .outputDir = std::move(outputDir),
        .workThreadsCount = workThreadsCount,
        .clangParams = std::move(clangParams),
    };
    ReflectionGen gen { std::move(config) };
    return gen.Run();
}