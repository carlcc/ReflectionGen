#pragma once

#include "Namespace.h"
#include <unordered_map>

class ClassMeta;

struct ParseState {
    using ClassMap = std::unordered_map<std::string, std::shared_ptr<ClassMeta>>;
    using EnumMap = std::unordered_map<std::string, std::shared_ptr<EnumMeta>>;

    NamespaceState namespaceState {};
    ClassMap classes_;
    EnumMap enums_;

    std::shared_ptr<ClassMeta> GetOrCreateClassMetaInCurrentNamespace(const std::string& className)
    {
        auto fullName = namespaceState.Current()->GetFullName() + "::" + className;
        auto& ptr = classes_[fullName];
        if (ptr == nullptr) {
            ptr = std::make_shared<ClassMeta>();
            ptr->name = className;
            ptr->namespace_ = namespaceState.Current();
        }
        return ptr;
    }

    std::shared_ptr<EnumMeta> GetOrCreateEnumMetaInCurrentNamespace(const std::string& enumName)
    {
        auto fullName = namespaceState.Current()->GetFullName() + "::" + enumName;
        auto& ptr = enums_[fullName];
        if (ptr == nullptr) {
            ptr = std::make_shared<EnumMeta>();
            ptr->name = enumName;
            ptr->namespace_ = namespaceState.Current();
        }
        return ptr;
    }
};