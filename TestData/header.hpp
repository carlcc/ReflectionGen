#ifndef P_PROPERTY
#define P_PROPERTY(...)
#endif
#ifndef P_METHOD
#define P_METHOD(...)
#endif
#ifndef P_CLASS
#define P_CLASS(...)
#endif
#ifndef P_ENUM
#define P_ENUM(...)
#endif

#define P_DECLARE_FRIENDS(className) friend class ReflectionGenerated_Operator_##className;

//#include <string>

namespace ReflectionTest {

using PFN = int (*)(int);

enum class P_ENUM() StrongTypedEnum {
    kA = 1,
    kB = 2,
};
enum class P_ENUM() StrongTypedEnumWithType : short {
    kA = -1,
    kB = 2,
};

enum P_ENUM() CStyleEnum {
    kA = 1,
    kB,
};

enum CStyleEnumWithType : char {
    kC = 1,
    kD,
};

class OnlyADeclareClass;
class P_CLASS() OnlyADeclareClass{
public:
    P_METHOD(Internal)
    void OnlyADeclareClassMethod();
};
class P_CLASS() ReflectedClass {
    P_DECLARE_FRIENDS(ReflectedClass)
public:

    class P_CLASS(Internal Class) InternalClass {
    public:
        P_METHOD(Internal)
        void AMethod();
    };

    P_METHOD()
    ReflectedClass();
    P_METHOD()
    ReflectedClass(int a, std::string& b);

    P_PROPERTY(ff, fss, Category = "hello")
    static int publicStaticField;
    P_PROPERTY()
    int publicMemberField;

    P_METHOD()
    void publicMemberMethod();

    P_METHOD()
    void publicStaticMethod();

private:
    P_PROPERTY()
    static int privateStaticField;
    P_PROPERTY()
    int privateMemberField;

    P_METHOD()
    void privateMemberMethod(int a, char* bug);

    P_METHOD()
    static void privateStaticMethod();
};

}