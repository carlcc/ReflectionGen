#include "ReflectionGen.h"
#include "Meta.h"
#include "ParseTask.h"
#include "ReflectionParser.h"
#include "StringUtils.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <regex>
#include <sol/sol.hpp>
#include <thread>
#include <unordered_set>

static std::atomic_uint64_t gCurrentClassIndex { 0 };
static std::mutex gScriptGlobalMutex {};

static inline uint64_t GetSteadyTimeMicros()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

static void BindScript(sol::state& lua)
{
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::coroutine,
        sol::lib::string, sol::lib::os, sol::lib::math, sol::lib::table,
        sol::lib::debug, sol::lib::bit32, sol::lib::io, sol::lib::ffi,
        sol::lib::jit, sol::lib::utf8);

    auto refGen = lua["ReflectionGen"].get_or_create<sol::table>();

    refGen.new_usertype<ParseTask>("ParseTask",
        "scriptParams", &ParseTask::scriptParams,
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
        "isAbstract", &ClassMeta::isAbstract,
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
    miscUtils["NextClassId"] = []() { // Used to generating class id
        // 1 year = 365 * 24*3600*1000*1000 = 31536000000000 = 0x00001CAE8C13E000 us
        // Thus we take the high 12 bits as an atomic counter, and the rest 52 bits as timestamp,
        // this will make a unique ID, even you run this program multiple times,
        // unless you generate more than 4095 IDs in one us or run multiple instances of this program simultaneously.
        auto id = (gCurrentClassIndex.fetch_add(1) << 52U) | GetSteadyTimeMicros();
        return std::to_string(id); //
    };
    miscUtils["DoExclusively"] = [](const std::function<void()>& job) {
        std::unique_lock<std::mutex> lck(gScriptGlobalMutex);
        job();
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

std::atomic_int gWorkThreadIdCounter { 0 };
class WorkThread {
public:
    explicit WorkThread(const ReflectionGenConfig& config, ParseTaskQueue& taskQueue)
        : config_ { config }
        , threadId_ { gWorkThreadIdCounter++ }
        , taskQueue_ { taskQueue }
    {
    }
    ~WorkThread()
    {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool Initialize()
    {
        BindScript(lua_);

        if (0 != DoScript(lua_, config_.scriptFile)) {
            return false;
        }

        if (!GetCompilerOptions(lua_, compilerArgsFromLua_)) {
            return false;
        }

        isThreadRunning_ = true;
        thread_ = std::thread([this]() {
            ThreadRoutine();
        });

        return true;
    }

private:
    void ThreadRoutine()
    {
        std::vector<const char*> compilerArgs;
        AddCompilerArgs(compilerArgs, compilerArgsFromLua_);
        AddCompilerArgs(compilerArgs, config_.clangParams);

        if (config_.debug) {
            std::stringstream ss;
            ss << "This is thread " << threadId_ << ", the clang params are: \n";
            for (size_t index = 0; index < compilerArgs.size(); ++index) {
                ss << "arg[" << index << "] = " << compilerArgs[index] << "\n";
            }
            std::cout << ss.str() << std::flush;
        }

        while (isThreadRunning_) {
            int ret = 0;
            auto* task = taskQueue_.Pop();
            if (task == nullptr) {
                break;
            }
            const std::string& codeFile = task->inputFile;
            ReflectionParser parser { codeFile };

            if (!parser.Initialize(compilerArgs)) {
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
    const ReflectionGenConfig& config_;
    int threadId_ {};
    ParseTaskQueue& taskQueue_;
    std::thread thread_ {};
    std::atomic_bool isThreadRunning_ { false };
    sol::state lua_ {};
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
    if (!CheckPaths()) {
        return 2;
    }

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
        t = std::make_unique<WorkThread>(config_, taskQueue);
        if (!t->Initialize()) {
            std::cerr << "Failed to initialize work thread" << std::endl;
            return 2;
        }
    }

    int retCode = 0;
    std::filesystem::path relativeDir(config_.relativeDir);
    std::error_code ec;
    for (auto& item : parseTasks) {
        std::filesystem::path inputPath(item.inputFile);
        auto relativeInputPath = std::filesystem::relative(inputPath, relativeDir, ec);
        if (ec.operator bool()) {
            std::cerr << "Failed to calculate the path of '" << item.inputFile << "' relative to '" << config_.relativeDir << ": " << ec.message() << std::endl;
            retCode = 1;
            break;
        }
        std::filesystem::path outputPath {};
        outputPath = std::filesystem::path(config_.outputDir + '/' + relativeInputPath.string()).lexically_normal();

        item.scriptParams = &config_.scriptParams;
        item.outputFile = outputPath.string();
        taskQueue.Push(&item);
    }
    for (uint32_t i = 0; i < workThreadsCount; ++i) {
        taskQueue.Push(nullptr); // to notify the work thread exit
    }

    return retCode;
}

static bool Contains(const std::filesystem::path& parent, const std::filesystem::path& child)
{
    auto parentNormal = std::filesystem::absolute(parent).lexically_normal();
    auto childNormal = std::filesystem::absolute(child).lexically_normal();
    return StringUtils::StartsWith(childNormal.string(), parentNormal.string());
}

bool ReflectionGen::CheckPaths()
{
    std::filesystem::path relativeDir(config_.relativeDir);
    for (auto& f : config_.files) {
        if (!Contains(relativeDir, f)) {
            std::cerr << "Input file/dir (" << f << ") is not RELATIVE_DIR (" << config_.relativeDir << ") or inside it." << std::endl;
            return false;
        }
    }
    for (auto& d : config_.dirs) {
        if (!Contains(relativeDir, d)) {
            std::cerr << "Input file/dir (" << d << ") is not RELATIVE_DIR (" << config_.relativeDir << ") or inside it." << std::endl;
            return false;
        }
    }
    return true;
}
