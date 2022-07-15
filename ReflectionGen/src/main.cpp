#include "CLI11.hpp"
#include "ReflectionParser.h"
#include "StringUtils.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <ostream>
#include <queue>
#include <regex>
#include <sol/sol.hpp>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

struct ParseTask {
    std::string file;
};

static std::atomic_uint64_t gCurrentClassIndex { 0 };
static void BindScript(sol::state& lua)
{
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::coroutine,
        sol::lib::string, sol::lib::os, sol::lib::math, sol::lib::table,
        sol::lib::debug, sol::lib::bit32, sol::lib::io, sol::lib::ffi,
        sol::lib::jit, sol::lib::utf8);

    auto refGen = lua["ReflectionGen"].get_or_create<sol::table>();

    refGen.new_usertype<ParseTask>("ParseTask",
        "file", &ParseTask::file
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

class ParseTaskQueue {
public:
    explicit ParseTaskQueue(size_t capacity)
        : capacity_ { capacity }
    {
    }

    void Push(ParseTask* item)
    {
        {
            std::unique_lock<std::mutex> lck(mutex_);
            produceCv_.wait(lck, [this]() { return queue_.size() < capacity_; });
            queue_.push(item);
        }
        consumeCv_.notify_one();
    }

    ParseTask* Pop()
    {
        ParseTask* ret;
        {
            std::unique_lock<std::mutex> lck(mutex_);
            consumeCv_.wait(lck, [this]() { return !queue_.empty(); });
            ret = queue_.front();
            queue_.pop();
        }
        produceCv_.notify_one();
        return ret;
    }

private:
    std::queue<ParseTask*> queue_;
    std::mutex mutex_;
    std::condition_variable produceCv_;
    std::condition_variable consumeCv_;
    size_t capacity_;
};

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
            const std::string& codeFile = task->file;
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

void PopulateParseTaskVectorFiles(std::vector<ParseTask>& parseTask, std::vector<std::string>&& files, const FilterContext& filter)
{
    parseTask.reserve(parseTask.size() + files.size());
    for (auto& f : files) {
        if (filter.ShouldFilterOut(f)) {
            continue;
        }
        parseTask.push_back(ParseTask { .file = std::move(f) });
    }
    files.clear();
}
void PopulateParseTaskVectorDirs(std::vector<ParseTask>& parseTask, const std::vector<std::string>& dirs, const FilterContext& filter)
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
            parseTask.push_back(ParseTask { .file = std::move(pathString) });
        }
    };
}

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
    std::string compileDataBaseFile;
    std::vector<std::string> includeRegexs;
    std::vector<std::string> excludeRegexs;
    std::vector<std::string> concatenatedClangParamsList;
    std::vector<std::string> dirs;
    std::vector<std::string> files;
    uint32_t workThreadsCount = std::max(std::thread::hardware_concurrency() / 2, 1U);
    app.add_option("-s,--script", scriptFile, "The script used to process the parse result")
        ->required()
        ->expected(1);
    app.add_option("-d,--dir", dirs, "A list of directories in which the files will be parsed");
    app.add_option("-f,--file", files, "A list of files which will be parsed");
    app.add_option("--include", includeRegexs, "A list of regex to filter in files, so that only they will not be processed");
    app.add_option("--exclude", excludeRegexs, "A list of regex to filter out files, so that they will not be processed");
    app.add_option("--clang-params", concatenatedClangParamsList,
        "A list of command params to be passed to libclang. The first letter will be used as delimiter,"
        " which will then divide the rest into a list of arguments, which will be passed to libclang. e.g. ';-Idir1;-Idir2',"
        " then we will pass '-Idir1' and '-Idir2' to libclang.");
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

    std::vector<ParseTask> parseTasks;
    {
        FilterContext filterContext {
            includeRegexs,
            excludeRegexs,
        };
        PopulateParseTaskVectorFiles(parseTasks, std::move(files), filterContext);
        PopulateParseTaskVectorDirs(parseTasks, dirs, filterContext);
    }
    ParseTaskQueue taskQueue(workThreadsCount * 2);

    std::vector<std::unique_ptr<WorkThread>> workThreads;
    workThreads.resize(workThreadsCount);
    for (auto& t : workThreads) {
        t = std::make_unique<WorkThread>(taskQueue, clangParams);
        if (!t->Initialize(scriptFile)) {
            std::cerr << "Failed to initialize work thread" << std::endl;
            return 2;
        }
    }

    for (auto& item : parseTasks) {
        taskQueue.Push(&item);
    }
    for (uint32_t i = 0; i < workThreadsCount; ++i) {
        taskQueue.Push(nullptr); // to notify the work thread exit
    }

    return 0;
}