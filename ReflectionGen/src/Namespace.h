#pragma once

#include <memory>
#include <string>
#include <unordered_map>

struct Namespace {
    Namespace(std::string name, Namespace* parent, bool isStruct = false)
        : name(std::move(name))
        , parent(parent)
        , isStruct { isStruct }
    {
    }

    const std::string& GetFullName() const
    {
        if (parent == nullptr) {
            return name;
        }
        if (fullName_.empty()) {
            fullName_ = parent->GetFullName() + "::" + name;
        }
        return fullName_;
    }

    std::string name {};
    Namespace* parent {};
    bool isStruct { false };
    std::unordered_map<std::string, std::shared_ptr<Namespace>> children;

private:
    mutable std::string fullName_ {};
};

class NamespaceState {
public:
    Namespace* EnterChild(const std::string& name)
    {
        auto& child = current_->children[name];
        if (child == nullptr) {
            child = std::make_unique<Namespace>(name, current_);
        }
        current_ = child.get();
        return current_;
    }
    Namespace* LeaveChild()
    {
        current_ = current_->parent;
        return current_;
    }
    Namespace* Current() const { return current_; };

private:
    Namespace root_ { "", nullptr };
    Namespace* current_ { &root_ };
};