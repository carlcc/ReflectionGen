#pragma once
#include "Namespace.h"
#include <memory>
#include <string>
#include <vector>

struct NamedObject {
    std::string name;
    std::string type;
};
struct EnumValue {
    std::string name;
    std::string value;
};
struct BaseMeta : public NamedObject {
    std::vector<std::string> annotations;
    Namespace* namespace_;

    std::string GetFullName() const
    {
        return namespace_->GetFullName() + "::" + name;
    }
};

struct MethodMeta : public BaseMeta {
    bool isStatic;

    std::string returnType;
    std::vector<NamedObject> arguments;
};

struct ConstructorMeta : public BaseMeta {
    std::vector<NamedObject> arguments;
};

struct FieldMeta : public BaseMeta {
    bool isStatic;
};

struct EnumMeta : public BaseMeta {
    bool isClass;
    std::string underlyingType;
    std::vector<EnumValue> values;
};

struct ClassMeta : public BaseMeta {
    std::vector<std::shared_ptr<ConstructorMeta>> constructors;
    std::vector<std::shared_ptr<MethodMeta>> methods;
    std::vector<std::shared_ptr<FieldMeta>> fields;
};