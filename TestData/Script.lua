
local Config = {
    CompilerOptions = {
        "-std=c++17",
        "-DP_PROPERTY(...)=__attribute__((annotate(\"reflected,\" #__VA_ARGS__)))",
        "-DP_METHOD(...)=__attribute__((annotate(\"reflected,\" #__VA_ARGS__)))",
        "-DP_CLASS(...)=__attribute__((annotate(\"reflected,\" #__VA_ARGS__)))",
        "-DP_ENUM(...)=__attribute__((annotate(\"reflected,\" #__VA_ARGS__)))",
    }
}

local function GetFunctionParameterDeclare(argList)
    local s = ''
    local isFirst = true
    for argIndex, arg in pairs(argList) do
        if isFirst then
            isFirst = false
        else
            s = s .. ', '
        end
        s = s .. arg.type .. ' ' .. arg.name
    end
    return s
end

local function GetFunctionArgumentsList(argList)
    local s = ''
    local isFirst = true
    for argIndex, arg in pairs(argList) do
        if isFirst then
            isFirst = false
        else
            s = s .. ', '
        end
        s = s .. arg.name
    end
    return s
end

local function GetFunctionSignature(retType, name, argList)
    local s = name .. '('
    if type(retType) == 'string' and retType ~= '' then
        s = retType .. ' ' .. s
    end
    s = s .. GetFunctionParameterDeclare(argList)
    s = s .. ')'
    return s
end

local function PrintClass(clazz)
    print("===========class==============")
    print("Name: " .. clazz.name)
    -- print("FullName: " .. clazz.namespace:GetFullName() .. "::" .. clazz.name)
    print("FullName: " .. clazz:GetFullName())
    print("Annotations: " .. table.concat(clazz.annotations, ", "))

    for index, ctor in ipairs(clazz.constructors) do
        print("--- constructor -----")
        print("\t" .. GetFunctionSignature(nil, ctor.name, ctor.arguments))
    end

    for index, field in ipairs(clazz.fields) do
        print("--- field -----")
        print("\tName: " .. field.name)
        print("\tFullName: " .. field:GetFullName())
        print("\tType: " .. field.type)
        print("\tIsStatic: " .. tostring(field.isStatic))
        print("\tAnnotations: " .. table.concat(field.annotations, ", "))
    end
    for index, method in ipairs(clazz.methods) do
        print("--- method -----")
        print("\tName: " .. method.name)
        print("\tFullName: " .. method:GetFullName())
        print("\tType: " .. method.type)
        print("\tSignature: " .. GetFunctionSignature(method.returnType, method.name, method.arguments))
        print("\tIsStatic: " .. tostring(method.isStatic))
        print("\tAnnotations: " .. table.concat(method.annotations, ", "))
    end
end

local function PrintEnum(enumMeta)
    print("===========enum==============")
    print("Name: " .. enumMeta.name)
    -- print("FullName: " .. clazz.namespace:GetFullName() .. "::" .. clazz.name)
    print("FullName: " .. enumMeta:GetFullName())
    print("IsClass: " .. tostring(enumMeta.isClass))
    print("UnderlyingType: " .. enumMeta.underlyingType)
    print("Annotations: " .. table.concat(enumMeta.annotations, ", "))

    for index, kvp in pairs(enumMeta.values) do
        local key = kvp.name
        local value = kvp.value
        print("\t" .. key .. ' = ' .. value)
    end

end

local Generator = {
    GenerateCreator = function (ctor)
        local fullName = ctor:GetFullName()
        local parameterDeclare = GetFunctionParameterDeclare(ctor.arguments)
        local arguments = GetFunctionArgumentsList(ctor.arguments)
        return string.format("static ScopedPtr<%s> Create(%s) { return new %s(%s); }",
            fullName, parameterDeclare, fullName, arguments);
    end
}

local function GenerateCodeForClass(clazz)
    local fullName = clazz:GetFullName():gsub('::', '_')
    local code = string.format([[
class %s_Operator {
public:
]], fullName)

    for index, ctor in ipairs(clazz.constructors) do
        code = code .. '\t' .. Generator.GenerateCreator(ctor) .. '\n'
    end

    code = code .. "\n};\n"

    print(code)
--     for index, field in ipairs(clazz.fields) do
--         print("--- field -----")
--         print("\tName: " .. field.name)
--         print("\tFullName: " .. field:GetFullName())
--         print("\tType: " .. field.type)
--         print("\tIsStatic: " .. tostring(field.isStatic))
--         print("\tAnnotations: " .. table.concat(field.annotations, ", "))
--     end
--     for index, method in ipairs(clazz.methods) do
--         print("--- method -----")
--         print("\tName: " .. method.name)
--         print("\tFullName: " .. method:GetFullName())
--         print("\tType: " .. method.type)
--         print("\tSignature: " .. GetFunctionSignature(method.returnType, method.name, method.arguments))
--         print("\tIsStatic: " .. tostring(method.isStatic))
--         print("\tAnnotations: " .. table.concat(method.annotations, ", "))
--     end

end

local function GenerateCode(parseResult)
    local classes = parseResult.classes
    local enums = parseResult.enums
    for fullName, clazz in pairs(classes) do
        if not clazz.annotations:empty() then
            PrintClass(clazz)
        end
    end
    for fullName, e in pairs(enums) do
        if not e.annotations:empty() then
            PrintEnum(e)
        end
    end
    for fullName, clazz in pairs(classes) do
        if not clazz.annotations:empty() then
            GenerateCodeForClass(clazz)
        end
    end
end

ReflectionGen = {
    Config = Config,
    OnFileParsed = GenerateCode,
}