#include "ReflectionGen.h"
#include "Meta.h"
#include "ParseTask.h"
#include "ReflectionParser.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <regex>
#include <sol/sol.hpp>
#include <thread>
#include <unordered_set>

static std::atomic_uint64_t gCurrentClassIndex { 0 };
static void BindScript(sol::state& lua)
{
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::coroutine,
        sol::lib::string, sol::lib::os, sol::lib::math, sol::lib::table,
        sol::lib::debug, sol::lib::bit32, sol::lib::io, sol::lib::ffi,
        sol::lib::jit, sol::lib::utf8);

    auto refGen = lua["ReflectionGen"].get_or_create<sol::table>();

    refGen.new_usertype<ParseTask>("ParseTask",
        "inputFile", &ParseTask::inputFile,
        "outputFile", &ParseTask::outputFile
        //
    );
    refGen.new_usertype<Namespace>("Namespace",
        "name", &Namespace::name,
        "parent", &Namespace::parent,
        "isStruct", &Namespace::isStruct,
        "children", &Namespace::children,
        "GetFullName", &Namespace::GetFullName
        //
    );
    refGen.new_usertype<NamedObject>("NamedObject",
        "name", &NamedObject::name,
        "type", &NamedObject::type
        //
    );
    refGen.new_usertype<EnumValue>("EnumValue",
        "name", &EnumValue::name,
        "value", &EnumValue::value
        //
    );
    refGen.new_usertype<ClassMeta>("ClassMeta",
        "name", &ClassMeta::name,
        "annotations", &ClassMeta::annotations,
        "namespace", &ClassMeta::namespace_,
        "constructors", &ClassMeta::constructors,
        "methods", &ClassMeta::methods,
        "fields", &ClassMeta::fields,
        "GetFullName", &ClassMeta::GetFullName
        //
    );
    refGen.new_usertype<FieldMeta>("FieldMeta",
        "name", &FieldMeta::name,
        "type", &FieldMeta::type,
        "annotations", &FieldMeta::annotations,
        "namespace", &FieldMeta::namespace_,
        "isStatic", &FieldMeta::isStatic,
        "GetFullName", &FieldMeta::GetFullName
        //
    );
    refGen.new_usertype<ConstructorMeta>("ConstructorMeta",
        "name", &ConstructorMeta::name,
        "type", &ConstructorMeta::type,
        "annotations", &ConstructorMeta::annotations,
        "namespace", &ConstructorMeta::namespace_,
        "arguments", &ConstructorMeta::arguments,
        "GetFullName", &ConstructorMeta::GetFullName
        //
    );
    refGen.new_usertype<MethodMeta>("MethodMeta",
        "name", &MethodMeta::name,
        "type", &MethodMeta::type,
        "annotations", &MethodMeta::annotations,
        "namespace", &MethodMeta::namespace_,
        "isStatic", &MethodMeta::isStatic,
        "returnType", &MethodMeta::returnType,
        "arguments", &MethodMeta::arguments,
        "GetFullName", &MethodMeta::GetFullName
        //
    );
    refGen.new_usertype<EnumMeta>("EnumMeta",
        "name", &EnumMeta::name,
        "type", &EnumMeta::type,
        "annotations", &EnumMeta::annotations,
        "namespace", &EnumMeta::namespace_,
        "isClass", &EnumMeta::isClass,
        "underlyingType", &EnumMeta::underlyingType,
        "values", &EnumMeta::values,
        "GetFullName", &EnumMeta::GetFullName
        //
    );
    refGen.new_usertype<ParseState>("ParseResult",
        "classes", &ParseState::classes_,
        "enums", &ParseState::enums_
        //
    );
    auto fileUtils = lua["FileUtils"].get_or_create<sol::table>();
    fileUtils["MakeDirsForFile"] = [](const std::string& filePath) {
        std::filesystem::path p(filePath);
        p = p.parent_path();
        std::filesystem::create_directories(p);
    };
    auto miscUtils = lua["MiscUtils"].get_or_create<sol::table>();
    miscUtils["FetchAddClassId"] = []() {                       // Used to generating class id
        return std::to_string(gCurrentClassIndex.fetch_add(1)); //
    };
}

static int DoScript(sol::state& lua, const std::string& scriptPath)
{
    auto pr = lua.do_file(scriptPath, sol::load_mode::any);
    if (pr.valid()) {
        return 0;
    } else {
        sol::error err = pr;
        std::cout << "Failed to do script " << scriptPath.c_str()
                  << ": " << err.what() << std::endl;
        return 1;
    }
}

static bool GetCompilerOptions(sol::state& lua, std::vector<std::string>& result)
{
    result.clear();

    //    auto refGen = lua["ReflectionGen"];
    //    auto config = refGen["Config"];
    //    if (!config.valid()) {
    //        std::cerr << "Failed to parse config: " << "'ReflectionGen.Config.CompilerOptions' not found" << std::endl;
    //        return false;
    //    }
    //    auto configTable = config.get<sol::table>();
    //    auto compilerOptionsProxy = configTable["CompileOptions"];
    //    if (!compilerOptionsProxy.valid()) {
    //        std::cerr << "Failed to parse config: " << "'ReflectionGen.Config.CompilerOptions' not found" << std::endl;
    //        return false;
    //    }
    auto table = lua["ReflectionGenConfig"]["CompilerOptions"].get<sol::table>();

    //    auto table = compilerOptionsProxy.get<sol::table>();
    result.reserve(table.size());
    for (auto& kv : table) {
        auto opt = kv.second.as<sol::optional<std::string>>();
        if (!opt.has_value()) {
            std::cerr << "Failed to parse config: 'ReflectionGenConfig.CompilerOptions' should be an array of string" << std::endl;
            return false;
        }
        result.push_back(opt.value());
    }
    return true;
}

static void AddCompilerArgs(std::vector<const char*>& dst, const std::vector<std::string>& newArgs)
{
    for (auto& s : newArgs) {
        if (dst.end() == std::find_if(dst.begin(), dst.end(), [&s](const char* str) {
                return str == s;
            })) {
            dst.push_back(s.c_str());
        }
    }
}

static void AddCompilerArgs(std::vector<const char*>& dst, const std::vector<const char*>& newArgs)
{
    for (auto& s : newArgs) {
        if (dst.end() == std::find_if(dst.begin(), dst.end(), [&s](const char* str) {
                return strcmp(str, s) == 0;
            })) {
            dst.push_back(s);
        }
    }
}

class WorkThread {
public:
    explicit WorkThread(ParseTaskQueue& taskQueue, const std::vector<const char*>& compilerFlags)
        : taskQueue_ { taskQueue }
        , compilerArgs_ { compilerFlags }
    {
    }
    ~WorkThread()
    {
        isThreadRunning_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool Initialize(const std::string& scriptFile)
    {
        BindScript(lua_);

        if (0 != DoScript(lua_, scriptFile)) {
            return false;
        }

        if (!GetCompilerOptions(lua_, compilerArgsFromLua_)) {
            return false;
        }
        std::vector<const char*> args;
        AddCompilerArgs(args, compilerArgsFromLua_);
        AddCompilerArgs(args, compilerArgs_);
        compilerArgs_ = std::move(args);

        isThreadRunning_ = true;
        thread_ = std::thread([this]() {
            ThreadRoutine();
        });

        return true;
    }

private:
    void ThreadRoutine()
    {
        while (isThreadRunning_) {
            int ret = 0;
            auto* task = taskQueue_.Pop();
            if (task == nullptr) {
                break;
            }
            const std::string& codeFile = task->inputFile;
            ReflectionParser parser { codeFile };
            std::cout << "Parsing file: " << codeFile << std::endl;

            if (!parser.Initialize(compilerArgs_)) {
                ret = -1;
                goto END;
            }

            if (!parser.Parse()) {
                ret = -2;
                goto END;
            }

            ret = parser.TraverseClasses([this, &task](const ParseState& result) {
                auto pr = lua_["ReflectionGenCallback"]["OnFileParsed"](result, task);
                if (pr.valid()) {
                    return 0;
                } else {
                    sol::error err = pr;
                    std::cout << "Failed to callback 'OnFileParsed'"
                              << ": " << err.what();
                    return 1;
                }
            });
        END:
            if (ret != 0) {
                std::cerr << "Failed to parse " << codeFile << std::endl;
            }
        }
    }

private:
    ParseTaskQueue& taskQueue_;
    std::thread thread_ {};
    std::atomic_bool isThreadRunning_ { false };
    sol::state lua_ {};
    std::vector<const char*> compilerArgs_ {}; ///< All compiler args from lua and command line
    std::vector<std::string> compilerArgsFromLua_ {};
};

struct FilterContext {
    std::vector<std::regex> includeRegexes;
    std::vector<std::regex> excludeRegexes;

    FilterContext(const std::vector<std::string>& includeStrings, const std::vector<std::string>& excludeStrings)
    {
        includeRegexes.reserve(includeStrings.size());
        for (auto& exp : includeStrings) {
            includeRegexes.emplace_back(exp, std::regex_constants::ECMAScript);
        }
        excludeRegexes.reserve(excludeStrings.size());
        for (auto& exp : excludeStrings) {
            excludeRegexes.emplace_back(exp, std::regex_constants::ECMAScript);
        }
    }

    bool ShouldFilterOut(const std::string& s) const
    {
        for (auto& regex : includeRegexes) {
            if (!std::regex_search(s, regex, std::regex_constants::match_any)) {
                return true;
            }
        }
        for (auto& regex : excludeRegexes) {
            if (std::regex_search(s, regex, std::regex_constants::match_any)) {
                return true;
            }
        }
        return false;
    }
};

static void PopulateParseTaskVectorFiles(std::vector<ParseTask>& parseTask, std::vector<std::string>&& files, const FilterContext& filter)
{
    parseTask.reserve(parseTask.size() + files.size());
    for (auto& f : files) {
        if (filter.ShouldFilterOut(f)) {
            continue;
        }
        parseTask.push_back(ParseTask { .inputFile = std::move(f) });
    }
    files.clear();
}
static void PopulateParseTaskVectorDirs(std::vector<ParseTask>& parseTask, const std::vector<std::string>& dirs, const FilterContext& filter)
{
    static const std::unordered_set<std::string> kExtNamesToBeIncluded {
        ".h", ".hpp", ".cpp", ".cxx", ".C"
    };
    for (auto& dir : dirs) {
        for (auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            auto extName = entry.path().extension().string();
            if (!kExtNamesToBeIncluded.count(extName)) {
                continue;
            }
            std::string pathString = entry.path().string();
            if (filter.ShouldFilterOut(pathString)) {
                continue;
            }
            parseTask.push_back(ParseTask { .inputFile = std::move(pathString) });
        }
    };
}

int ReflectionGen::Run()
{
    std::vector<ParseTask> parseTasks;
    {
        FilterContext filterContext {
            config_.includeRegexes,
            config_.excludeRegexes,
        };
        PopulateParseTaskVectorFiles(parseTasks, std::move(config_.files), filterContext);
        PopulateParseTaskVectorDirs(parseTasks, config_.dirs, filterContext);
    }
    auto workThreadsCount = config_.workThreadsCount;

    ParseTaskQueue taskQueue(workThreadsCount * 2);

    std::vector<std::unique_ptr<WorkThread>> workThreads;
    workThreads.resize(workThreadsCount);
    for (auto& t : workThreads) {
        t = std::make_unique<WorkThread>(taskQueue, config_.clangParams);
        if (!t->Initialize(config_.scriptFile)) {
            std::cerr << "Failed to initialize work thread" << std::endl;
            return 2;
        }
    }

    for (auto& item : parseTasks) {
        std::filesystem::path inputPath(item.inputFile);
        std::filesystem::path outputPath {};
#ifdef _WIN32
        if (inputPath.is_absolute()) {
            outputPath = std::filesystem::path(config_.outputDir + '/' + item.inputFile.substr(3)).string(); // remove the disk label
        } else
#endif
        {
            outputPath = std::filesystem::path(config_.outputDir + '/' + item.inputFile);
        }

        item.outputFile = outputPath.string();
        taskQueue.Push(&item);
    }
    for (uint32_t i = 0; i < workThreadsCount; ++i) {
        taskQueue.Push(nullptr); // to notify the work thread exit
    }

    return 0;
}