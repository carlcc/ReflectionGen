#pragma
#include "StringUtils.h"
#include <algorithm>
#include <clang-c/Index.h>
#include <ostream>
#include <string>

inline std::ostream& operator<<(std::ostream& os, const CXString& str)
{
    os << clang_getCString(str);
    clang_disposeString(str);
    return os;
}
inline std::string toStdString(const CXString& str)
{
    std::string s = clang_getCString(str);
    clang_disposeString(str);
    return s;
}

inline std::string toStdString(const CXType type)
{
    return toStdString(clang_getTypeSpelling(type));
}

inline std::vector<std::string> AnnotationsToVector(const std::string& annotations)
{
    std::vector<std::string> vec = StringUtils::Split(annotations, ',');
    for (auto& s : vec) {
        StringUtils::Trim(s);
    }
    vec.erase(std::remove(vec.begin(), vec.end(), ""), vec.end());

    return vec;
}

inline std::string GetClangCursorSpelling(CXCursor c)
{
    return toStdString(clang_getCursorSpelling(c));
}
inline std::string GetClangCursorKindSpelling(CXCursor c)
{
    return toStdString(clang_getCursorKindSpelling(clang_getCursorKind(c)));
}
inline std::string GetClangCursorTypeSpelling(CXCursor c)
{
    return toStdString(clang_getTypeSpelling(clang_getCursorType(c)));
}