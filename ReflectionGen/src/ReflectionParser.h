#pragma once

#include "Meta.h"
#include "ParseState.h"
#include <clang-c/Index.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ReflectionParser {
public:
    explicit ReflectionParser(std::string file)
        : file_ { std::move(file) }
    {
    }

    ~ReflectionParser()
    {
        if (nullptr != translationUnit_) {
            clang_disposeTranslationUnit(translationUnit_);
            translationUnit_ = nullptr;
        }
        if (nullptr != index_) {
            clang_disposeIndex(index_);
            index_ = nullptr;
        }
    }

    bool Initialize(const std::vector<const char*>& compilerArgs);

    bool Parse();

    int TraverseClasses(std::function<int(const ParseState&)> callback) const
    {
        return callback(parseState_);
    }

private:
    CXChildVisitResult VisitNamespace(CXCursor c, CXCursor parent);
    CXChildVisitResult VisitClass(CXCursor c, CXCursor parent);
    CXChildVisitResult VisitConstructor(CXCursor cursor, CXCursor parent, ClassMeta* owner);
    CXChildVisitResult VisitField(CXCursor c, CXCursor parent, ClassMeta* owner, bool isStatic);
    CXChildVisitResult VisitMethod(CXCursor cursor, CXCursor parent, ClassMeta* owner, bool isStatic);
    CXChildVisitResult VisitEnum(CXCursor cursor, CXCursor parent);

private:
    std::string file_ {};

    // libclang
    CXIndex index_ { nullptr };
    CXTranslationUnit translationUnit_ { nullptr };
    CXCursor rootCursor_ {};

    // parse state
    ParseState parseState_ {};
};