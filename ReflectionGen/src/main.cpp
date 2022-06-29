#include "ReflectionParser.h"
#include <filesystem>
#include <iostream>
#include <ostream>
#include <sol/sol.hpp>
#include <string>
#include <vector>

static void BindScript(sol::state& lua)
{
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::coroutine,
        sol::lib::string, sol::lib::os, sol::lib::math, sol::lib::table,
        sol::lib::debug, sol::lib::bit32, sol::lib::io, sol::lib::ffi,
        sol::lib::jit, sol::lib::utf8);

    auto refGen = lua["ReflectionGen"].get_or_create<sol::table>();

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
}

static int DoScript(sol::state& lua, const std::string& scriptPath)
{
    auto pr = lua.do_file(scriptPath, sol::load_mode::any);
    if (pr.valid()) {
        return 0;
    } else {
        sol::error err = pr;
        std::cout << "Failed to do script " << scriptPath.c_str()
                  << ": " << err.what();
        return 1;
    }
}

static bool GetCompilerOptions(sol::state& lua, std::vector<const char*>& result)
{
    static std::vector<std::string> sResult;

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
    auto table = lua["ReflectionGen"]["Config"]["CompilerOptions"].get<sol::table>();

    //    auto table = compilerOptionsProxy.get<sol::table>();
    sResult.reserve(table.size());
    result.reserve(table.size());
    for (auto& kv : table) {
        auto opt = kv.second.as<sol::optional<std::string>>();
        if (!opt.has_value()) {
            std::cerr << "Failed to parse config: 'ReflectionGen.Config.CompilerOptions' should be an array of string" << std::endl;
            return false;
        }
        sResult.push_back(opt.value());
        result.push_back(sResult.back().c_str());
    }
    return true;
}

static void PrintUsage(int argc, char** argv)
{
    std::cout << "Usage:\n"
              << "\t" << argv[0] << " lua_script.lua code.h/cpp" << std::endl;
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        PrintUsage(argc, argv);
        return -1;
    }

    std::string scriptFile = argv[1];
    std::string code = argv[2];

    sol::state lua;
    BindScript(lua);

    if (0 != DoScript(lua, scriptFile)) {
        return 1;
    }

    std::vector<const char*> compilerArgs;
    if (!GetCompilerOptions(lua, compilerArgs)) {
        return -2;
    }

    ReflectionParser parser { code };
    if (!parser.Initialize(compilerArgs)) {
        return -1;
    }

    if (!parser.Parse()) {
        return -2;
    }

    return parser.TraverseClasses([&lua](const ParseState& result) {
        auto pr = lua["ReflectionGen"]["OnFileParsed"](result);
        if (pr.valid()) {
            return 0;
        } else {
            sol::error err = pr;
            std::cout << "Failed to callback 'OnFileParsed'"
                      << ": " << err.what();
            return 1;
        }
    });
}