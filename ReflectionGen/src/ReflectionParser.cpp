#include "ReflectionParser.h"
#include "StringConvert.h"
#include <cassert>
#include <iostream>

// libclang
bool ReflectionParser::Initialize(const std::vector<const char*>& compilerArgs)
{
    index_ = clang_createIndex(0, 0);
    uint options = 0
        | CXTranslationUnit_DetailedPreprocessingRecord
        //        | CXTranslationUnit_PrecompiledPreamble
        | CXTranslationUnit_KeepGoing
        | CXTranslationUnit_SkipFunctionBodies
        | CXTranslationUnit_Incomplete;

    auto errorCode = clang_parseTranslationUnit2(index_, file_.c_str(),
        compilerArgs.data(), (int)compilerArgs.size(),
        nullptr, 0, options, &translationUnit_);
    if (translationUnit_ == nullptr) {
        std::cerr << "Unable to parse file: " << file_ << ", error code: " << errorCode << std::endl;
        return false;
    }
    rootCursor_ = clang_getTranslationUnitCursor(translationUnit_);
    return true;
}

bool ReflectionParser::Parse()
{
    return 0 == clang_visitChildren(
               rootCursor_, [](CXCursor c, CXCursor parent, CXClientData clientData) {
                   auto* self = reinterpret_cast<ReflectionParser*>(clientData);
                   return self->VisitNamespace(c, parent);
               },
               this);
}

#define ClangVisitChildren(cursor, callback)                         \
    [this](CXCursor c) {                                             \
        return clang_visitChildren(                                  \
            c, [](CXCursor c1, CXCursor p1, CXClientData d) {        \
                auto* self = reinterpret_cast<ReflectionParser*>(d); \
                return self->callback(c1, p1);                       \
            },                                                       \
            this);                                                   \
    }(cursor)
CXChildVisitResult ReflectionParser::VisitNamespace(CXCursor c, CXCursor parent)
{
    auto kind = clang_getCursorKind(c);
    switch (kind) {
    case CXCursor_Namespace: {
        parseState_.namespaceState.EnterChild(toStdString(clang_getCursorSpelling(c)));
        if (0 != ClangVisitChildren(c, VisitNamespace)) {
            parseState_.namespaceState.LeaveChild();
            return CXChildVisit_Break;
        }
        parseState_.namespaceState.LeaveChild();
        return CXChildVisit_Continue;
    }
    case CXCursor_StructDecl:
    case CXCursor_ClassDecl:
        return VisitClass(c, parent);

    case CXCursor_EnumDecl:
        return VisitEnum(c, parent);

    default:
        break;
    }
    return CXChildVisit_Recurse;
}

CXChildVisitResult ReflectionParser::VisitClass(CXCursor c, CXCursor parent)
{
    auto name = GetClangCursorSpelling(c);
    auto classMeta = parseState_.GetOrCreateClassMetaInCurrentNamespace(name);

    struct Context {
        ClassMeta* classMeta;
        ParseState* state;
        ReflectionParser* self;
    };
    Context context {
        classMeta.get(),
        &parseState_,
        this,
    };
    parseState_.namespaceState.EnterChild(name);
    auto ret = clang_visitChildren(
        c, [](CXCursor c1, CXCursor p1, CXClientData d) {
            auto* ctx = reinterpret_cast<Context*>(d);
            auto* self = ctx->self;
            auto kind = clang_getCursorKind(c1);
            switch (kind) {
            case CXCursor_ClassDecl:
                return self->VisitClass(c1, p1);

            case CXCursor_EnumDecl:
                return self->VisitEnum(c1, p1);

            case CXCursor_FieldDecl: // member
            case CXCursor_VarDecl:   // non-member
                return self->VisitField(c1, p1, ctx->classMeta, kind == CXCursor_VarDecl);

            case CXCursor_FunctionDecl:
            case CXCursor_CXXMethod:
                return self->VisitMethod(c1, p1, ctx->classMeta, kind == CXCursor_FunctionDecl);

            case CXCursor_Constructor:
                return self->VisitConstructor(c1, p1, ctx->classMeta);

            case CXCursor_AnnotateAttr: {
                assert(ctx->classMeta->annotations.empty());
                ctx->classMeta->annotations = AnnotationsToVector(toStdString(clang_getCursorSpelling(c1)));
                return CXChildVisit_Continue;
            }
            default:
                break;
            }
            return CXChildVisit_Continue;
        },
        &context);

    parseState_.namespaceState.LeaveChild();
    return ret == 0 ? CXChildVisit_Continue : CXChildVisit_Break;
}

CXChildVisitResult ReflectionParser::VisitConstructor(CXCursor cursor, CXCursor parent, ClassMeta* owner)
{
    auto type = clang_getCursorType(cursor);

    auto constructorMeta = std::make_shared<ConstructorMeta>();
    owner->constructors.push_back(constructorMeta);

    {
        constructorMeta->name = toStdString(clang_getCursorSpelling(cursor));
        constructorMeta->type = toStdString(clang_getTypeSpelling(type));
        constructorMeta->namespace_ = parseState_.namespaceState.Current();

        int numArgs = clang_Cursor_getNumArguments(cursor);
        for (int i = 0; i < numArgs; ++i) {
            auto argCursor = clang_Cursor_getArgument(cursor, i);
            NamedObject arg;
            arg.name = toStdString(clang_getCursorSpelling(argCursor));
            if (arg.name.empty()) {
                arg.name = "[unnamed]";
            }
            auto arg_type = clang_getArgType(type, i);
            arg.type = toStdString(arg_type);
            constructorMeta->arguments.push_back(arg);
        }
    }

    struct Context {
        ConstructorMeta* constructorMeta;
        ParseState* state;
    };
    Context context {
        constructorMeta.get(),
        &parseState_,
    };
    auto ret = clang_visitChildren(
        cursor, [](CXCursor c1, CXCursor p1, CXClientData d) {
            auto* ctx = reinterpret_cast<Context*>(d);

            auto kind = clang_getCursorKind(c1);

            switch (kind) {
            case CXCursor_AnnotateAttr: {
                assert(ctx->constructorMeta->annotations.empty());
                ctx->constructorMeta->annotations = AnnotationsToVector(toStdString(clang_getCursorSpelling(c1)));
                break;
            }
            default:
                break;
            }
            return CXChildVisit_Recurse;
        },
        &context);
    return ret == 0 ? CXChildVisit_Continue : CXChildVisit_Break;
}

CXChildVisitResult ReflectionParser::VisitField(CXCursor c, CXCursor parent, ClassMeta* owner, bool isStatic)
{
    auto fieldMeta = std::make_shared<FieldMeta>();
    fieldMeta->name = GetClangCursorSpelling(c);
    fieldMeta->type = GetClangCursorTypeSpelling(c);
    fieldMeta->namespace_ = parseState_.namespaceState.Current();
    fieldMeta->isStatic = isStatic;

    owner->fields.push_back(fieldMeta);

    struct Context {
        FieldMeta* fieldMeta;
        ParseState* state;
    };
    Context context {
        fieldMeta.get(),
        &parseState_,
    };
    auto ret = clang_visitChildren(
        c, [](CXCursor c1, CXCursor p1, CXClientData d) {
            auto* ctx = reinterpret_cast<Context*>(d);

            auto kind = clang_getCursorKind(c1);
            switch (kind) {
            case CXCursor_AnnotateAttr: {
                assert(ctx->fieldMeta->annotations.empty());
                ctx->fieldMeta->annotations = AnnotationsToVector(GetClangCursorSpelling(c1));
                break;
            }
            default:
                break;
            }
            return CXChildVisit_Continue;
        },
        &context);
    return ret == 0 ? CXChildVisit_Continue : CXChildVisit_Break;
}

CXChildVisitResult ReflectionParser::VisitMethod(CXCursor cursor, CXCursor parent, ClassMeta* owner, bool isStatic)
{
    auto type = clang_getCursorType(cursor);

    auto methodMeta = std::make_shared<MethodMeta>();
    owner->methods.push_back(methodMeta);

    {
        methodMeta->name = toStdString(clang_getCursorSpelling(cursor));
        methodMeta->type = toStdString(clang_getTypeSpelling(type));
        methodMeta->namespace_ = parseState_.namespaceState.Current();
        methodMeta->isStatic = isStatic;

        int numArgs = clang_Cursor_getNumArguments(cursor);
        for (int i = 0; i < numArgs; ++i) {
            auto argCursor = clang_Cursor_getArgument(cursor, i);
            NamedObject arg;
            arg.name = toStdString(clang_getCursorSpelling(argCursor));
            if (arg.name.empty()) {
                arg.name = "[unnamed]";
            }
            auto arg_type = clang_getArgType(type, i);
            arg.type = toStdString(arg_type);
            methodMeta->arguments.push_back(arg);
        }
        methodMeta->returnType = toStdString(clang_getResultType(type));
    }

    struct Context {
        MethodMeta* methodMeta;
        ParseState* state;
    };
    Context context {
        methodMeta.get(),
        &parseState_,
    };
    auto ret = clang_visitChildren(
        cursor, [](CXCursor c1, CXCursor p1, CXClientData d) {
            auto* ctx = reinterpret_cast<Context*>(d);

            auto kind = clang_getCursorKind(c1);

            switch (kind) {
            case CXCursor_AnnotateAttr: {
                assert(ctx->methodMeta->annotations.empty());
                ctx->methodMeta->annotations = AnnotationsToVector(toStdString(clang_getCursorSpelling(c1)));
                break;
            }
            default:
                break;
            }
            return CXChildVisit_Recurse;
        },
        &context);
    return ret == 0 ? CXChildVisit_Continue : CXChildVisit_Break;
}

static bool IsUnsignedType(CXTypeKind kind)
{
    return CXType_Unexposed == kind
        || CXType_Char_U == kind
        || CXType_UChar == kind
        || CXType_UShort == kind
        || CXType_UInt == kind
        || CXType_ULong == kind
        || CXType_ULongLong == kind
        || CXType_UInt128 == kind
        || CXType_UShortAccum == kind
        || CXType_UAccum == kind
        || CXType_ULongAccum == kind;
}

CXChildVisitResult ReflectionParser::VisitEnum(CXCursor cursor, CXCursor parent)
{
    auto name = GetClangCursorSpelling(cursor);
    auto enumMeta = parseState_.GetOrCreateEnumMetaInCurrentNamespace(name);
    {
        enumMeta->isClass = clang_EnumDecl_isScoped(cursor);
        enumMeta->underlyingType = toStdString(clang_getEnumDeclIntegerType(cursor));
    }

    struct Context {
        EnumMeta* enumMeta;
        bool isUnsigned;
    };

    Context ctx {
        enumMeta.get(),
        IsUnsignedType(clang_getEnumDeclIntegerType(cursor).kind),
    };
    clang_visitChildren(
        cursor, [](CXCursor c, CXCursor parent, CXClientData d) {
            auto* ctx = reinterpret_cast<Context*>(d);

            auto kind = clang_getCursorKind(c);

            switch (kind) {
            case CXCursor_AnnotateAttr: {
                assert(ctx->enumMeta->annotations.empty());
                ctx->enumMeta->annotations = AnnotationsToVector(toStdString(clang_getCursorSpelling(c)));
                break;
            }
            case CXCursor_EnumConstantDecl: {
                ctx->enumMeta->values.push_back({
                    GetClangCursorSpelling(c),
                    ctx->isUnsigned ? std::to_string(clang_getEnumConstantDeclUnsignedValue(c))
                                    : std::to_string(clang_getEnumConstantDeclValue(c)),
                });
            }
            default:
                break;
            }
            return CXChildVisit_Continue;
        },
        &ctx);
    return CXChildVisit_Continue;
}
