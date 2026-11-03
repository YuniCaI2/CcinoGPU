#include <gtest/gtest.h>
#include <algorithm>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "CodeGenerator/ASTGenerator.h"
#include "CodeGenerator/GLSLAST.h"
#include "CodeGenerator/SignatureFlattener.h"

namespace
{
    using namespace clang;
    using namespace clang::ast_matchers;

    // 在不修改 CodeGenerator/ASTGenerator.h 的前提下，通过标准 C++ 友元成员指针显式实例化获取私有成员 m_declGraph
    template <typename Tag, typename Tag::type M>
    struct AccessPrivate {
        friend typename Tag::type GetPrivate(Tag) { return M; }
    };

    struct DeclGraphTag {
        using type = Ccino::GLSL::DeclGraph Ccino::ASTGenerator::*;
        friend type GetPrivate(DeclGraphTag);
    };
    template struct AccessPrivate<DeclGraphTag, &Ccino::ASTGenerator::m_declGraph>;

    const Ccino::GLSL::DeclGraph& GetDeclGraph(const Ccino::ASTGenerator& generator) {
        return generator.*GetPrivate(DeclGraphTag{});
    }

    const std::string kDeclTestPreamble = R"(
        using uint32_t = unsigned int;

        template <typename BitType>
        struct Flags {
            uint32_t mask = 0;
            constexpr Flags() = default;
            constexpr Flags(BitType bit) : mask(static_cast<uint32_t>(bit)) {}
            constexpr explicit Flags(uint32_t flags) : mask(flags) {}
        };

        enum class BufferAccessFlagBits : uint32_t {
            Read = 1 << 0,
            Write = 1 << 1,
            ReadWrite = Read | Write,
            None = 0
        };
        using BufferAccessFlags = Flags<BufferAccessFlagBits>;

        namespace std {
            template<typename T, unsigned long long N>
            struct array {
                T elems[N];
                T& operator[](unsigned long long i) { return elems[i]; }
                const T& operator[](unsigned long long i) const { return elems[i]; }
            };
        }

        namespace glm {
            enum qualifier { defaultp };

            template<int L, typename T, qualifier Q = defaultp>
            struct vec {
                union {
                    struct { T x, y, z, w; };
                    struct { T r, g, b, a; };
                };
                vec() = default;
                vec(T s) : x(s), y(s), z(s), w(s) {}
                vec(T x_, T y_) : x(x_), y(y_), z{}, w{} {}
                vec(T x_, T y_, T z_) : x(x_), y(y_), z(z_), w{} {}
                vec(T x_, T y_, T z_, T w_) : x(x_), y(y_), z(z_), w(w_) {}

                T& operator[](int i) { return x; }
                const T& operator[](int i) const { return x; }

                vec operator+(const vec& rhs) const { return *this; }
                vec operator-(const vec& rhs) const { return *this; }
                vec operator*(const vec& rhs) const { return *this; }
                vec operator/(const vec& rhs) const { return *this; }
                vec operator*(T scalar) const { return *this; }
            };

            template<int C, int R, typename T, qualifier Q = defaultp>
            struct mat {
                mat() = default;
                mat(T s) {}
                mat operator*(const mat& rhs) const { return *this; }
            };

            template<typename T>
            T clamp(T v, T lo, T hi) { return v; }

            template<int L, typename T, qualifier Q>
            T dot(const vec<L, T, Q>& a, const vec<L, T, Q>& b) { return T{}; }
        }

        namespace Ccino {
            using Vec2 = glm::vec<2, float>;
            using Vec3 = glm::vec<3, float>;
            using Vec4 = glm::vec<4, float>;
            using IVec3 = glm::vec<3, int>;
            using UVec3 = glm::vec<3, uint32_t>;
            using Mat4 = glm::mat<4, 4, float>;

            template<typename T, unsigned long long N>
            using Array = std::array<T, N>;

            template<typename BufferResourceType, BufferAccessFlags accessFlags>
            class Accessor {
            public:
                BufferResourceType& operator[](uint32_t index) const {
                    static BufferResourceType dummy{};
                    return dummy;
                }
            };

            template<typename T>
            T Clamp(T v, T lo, T hi) { return glm::clamp(v, lo, hi); }

            template<int L, typename T, glm::qualifier Q>
            T Dot(const glm::vec<L, T, Q>& a, const glm::vec<L, T, Q>& b) { return glm::dot(a, b); }
        }
    )";

    // 统一收集测试源码中的结构体声明与函数声明，并调用 ASTGenerator 接口写入 DeclGraph
    class DeclCollectorCallback : public MatchFinder::MatchCallback
    {
    public:
        explicit DeclCollectorCallback(Ccino::ASTGenerator& gen,
                                       std::vector<std::string> targetStructs = {},
                                       std::vector<std::string> targetFuncs = {})
            : generator(gen),
              structFilter(std::move(targetStructs)),
              funcFilter(std::move(targetFuncs)) {}

        void run(const MatchFinder::MatchResult& result) override {
            if (const auto* record = result.Nodes.getNodeAs<CXXRecordDecl>("record")) {
                if (ShouldAcceptStruct(record)) {
                    generator.InsertCXXRecordDeclToGraph(const_cast<CXXRecordDecl*>(record));
                }
            }
            if (const auto* func = result.Nodes.getNodeAs<FunctionDecl>("func")) {
                if (ShouldAcceptFunc(func)) {
                    generator.InsertFunctionDeclToGraph(const_cast<FunctionDecl*>(func));
                }
            }
        }

    private:
        bool ShouldAcceptStruct(const CXXRecordDecl* record) const {
            if (!record) return false;
            std::string name = record->getNameAsString();
            std::string flatName = Ccino::SignatureFlattener::FlattenRecord(record);
            for (const auto& target : structFilter) {
                if (name == target || flatName == target) return true;
            }
            return false;
        }

        bool ShouldAcceptFunc(const FunctionDecl* func) const {
            if (!func) return false;
            std::string name = func->getNameAsString();
            std::string flatName = Ccino::SignatureFlattener::FlattenFunction(func, true);
            std::string flatNoParams = Ccino::SignatureFlattener::FlattenFunction(func, false);
            for (const auto& target : funcFilter) {
                if (name == target || flatName == target || flatNoParams == target) return true;
            }
            return false;
        }

        Ccino::ASTGenerator& generator;
        std::vector<std::string> structFilter;
        std::vector<std::string> funcFilter;
    };

    // 统一测试套件夹具：提供 ASTGenerator 实例、Clang AST 匹配运行器以及 DeclGraph 断言辅助函数
    class ProcessClangDeclTest : public ::testing::Test
    {
    protected:
        Ccino::ASTGenerator generator;

        const Ccino::GLSL::DeclGraph& Graph() const {
            return GetDeclGraph(generator);
        }

        void RunDeclCollection(const std::string& snippet,
                               const std::vector<std::string>& targetStructs,
                               const std::vector<std::string>& targetFuncs,
                               bool definitionOnly = true) {
            DeclCollectorCallback callback(generator, targetStructs, targetFuncs);
            MatchFinder finder;

            if (!targetStructs.empty()) {
                if (definitionOnly) {
                    finder.addMatcher(cxxRecordDecl(isDefinition()).bind("record"), &callback);
                } else {
                    finder.addMatcher(cxxRecordDecl().bind("record"), &callback);
                }
            }
            if (!targetFuncs.empty()) {
                if (definitionOnly) {
                    finder.addMatcher(functionDecl(isDefinition()).bind("func"), &callback);
                } else {
                    finder.addMatcher(functionDecl().bind("func"), &callback);
                }
            }

            ASSERT_TRUE(tooling::runToolOnCodeWithArgs(
                tooling::newFrontendActionFactory(&finder)->create(),
                kDeclTestPreamble + snippet,
                {"-std=c++20"}));
        }

        const Ccino::GLSL::StructDecl* FindStructNode(const std::string& nodeName) const {
            const auto& nodes = Graph().nodes;
            auto it = nodes.find(nodeName);
            if (it == nodes.end() || !it->second) return nullptr;
            if (it->second->GetKind() != Ccino::GLSL::Kind::StructDecl) return nullptr;
            return static_cast<const Ccino::GLSL::StructDecl*>(it->second.get());
        }

        const Ccino::GLSL::FunctionDecl* FindFuncNode(const std::string& nodeName) const {
            const auto& nodes = Graph().nodes;
            auto it = nodes.find(nodeName);
            if (it == nodes.end() || !it->second) return nullptr;
            if (it->second->GetKind() != Ccino::GLSL::Kind::FunctionDecl) return nullptr;
            return static_cast<const Ccino::GLSL::FunctionDecl*>(it->second.get());
        }

        bool HasEdge(const std::string& fromNode, const std::string& toDep) const {
            const auto& edges = Graph().edges;
            auto it = edges.find(fromNode);
            if (it == edges.end()) return false;
            return std::find(it->second.begin(), it->second.end(), toDep) != it->second.end();
        }
    };
} // namespace

// ============================================================================
// 1. 单测：InsertCXXRecordDeclToGraph 空指针与前向声明边界测试
// ============================================================================

TEST_F(ProcessClangDeclTest, InsertCXXRecordDecl_NullAndForwardDeclIgnored) {
    generator.InsertCXXRecordDeclToGraph(nullptr);
    EXPECT_TRUE(Graph().nodes.empty());
    EXPECT_TRUE(Graph().edges.empty());

    RunDeclCollection(
        R"(
            namespace Sim {
                struct ForwardOnly;
            }
        )",
        {"ForwardOnly"},
        {},
        /*definitionOnly=*/false);

    EXPECT_EQ(FindStructNode("Sim_ForwardOnly"), nullptr);
    EXPECT_TRUE(Graph().nodes.empty());
}

// ============================================================================
// 2. 单测：InsertCXXRecordDeclToGraph 标量、GLSL 向量、C 数组、std::array、嵌套结构体与模板特化
// ============================================================================

TEST_F(ProcessClangDeclTest, InsertCXXRecordDecl_FieldsArraysNestedAndTemplateStruct) {
    RunDeclCollection(
        R"(
            namespace Sim {
                struct SubItem {
                    int id;
                    Ccino::Vec3 velocity;
                };

                struct Particle {
                    Sim::SubItem sub;
                    float weights[4];
                    int grid[2][3];
                    Ccino::Array<float, 8> history;
                };

                template <typename T, int N>
                struct Packet {
                    T payload[N];
                    int tag;
                };
            }

            void UseTemplate(Sim::Packet<float, 4> pkt) {}
        )",
        {"SubItem", "Particle", "Sim_Packet_float_4"},
        {});

    // 1. 验证 Sim_SubItem 节点与字段
    const auto* subItem = FindStructNode("Sim_SubItem");
    ASSERT_NE(subItem, nullptr);
    EXPECT_EQ(subItem->name, "Sim_SubItem");
    ASSERT_EQ(subItem->fields.size(), 2u);
    EXPECT_EQ(subItem->fields[0]->type, "int");
    EXPECT_EQ(subItem->fields[0]->name, "id");
    EXPECT_EQ(subItem->fields[0]->dimensions, "");
    EXPECT_EQ(subItem->fields[1]->type, "vec3");
    EXPECT_EQ(subItem->fields[1]->name, "velocity");
    EXPECT_EQ(subItem->fields[1]->dimensions, "");

    // 2. 验证 Sim_Particle 节点与多维/模板数组字段拆分
    const auto* particle = FindStructNode("Sim_Particle");
    ASSERT_NE(particle, nullptr);
    EXPECT_EQ(particle->name, "Sim_Particle");
    ASSERT_EQ(particle->fields.size(), 4u);

    EXPECT_EQ(particle->fields[0]->type, "Sim_SubItem");
    EXPECT_EQ(particle->fields[0]->name, "sub");
    EXPECT_EQ(particle->fields[0]->dimensions, "");

    EXPECT_EQ(particle->fields[1]->type, "float");
    EXPECT_EQ(particle->fields[1]->name, "weights");
    EXPECT_EQ(particle->fields[1]->dimensions, "[4]");

    EXPECT_EQ(particle->fields[2]->type, "int");
    EXPECT_EQ(particle->fields[2]->name, "grid");
    EXPECT_EQ(particle->fields[2]->dimensions, "[2][3]");

    EXPECT_EQ(particle->fields[3]->type, "float");
    EXPECT_EQ(particle->fields[3]->name, "history");
    EXPECT_EQ(particle->fields[3]->dimensions, "[8]");

    // 3. 验证模板类实例化 Sim_Packet_float_4
    const auto* packet = FindStructNode("Sim_Packet_float_4");
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->name, "Sim_Packet_float_4");
    ASSERT_EQ(packet->fields.size(), 2u);
    EXPECT_EQ(packet->fields[0]->type, "float");
    EXPECT_EQ(packet->fields[0]->name, "payload");
    EXPECT_EQ(packet->fields[0]->dimensions, "[4]");
    EXPECT_EQ(packet->fields[1]->type, "int");
    EXPECT_EQ(packet->fields[1]->name, "tag");
    EXPECT_EQ(packet->fields[1]->dimensions, "");

    // 4. 验证结构体依赖边：Sim_Particle 依赖自定义结构体类型 Sim_SubItem（而非字段名 "sub"）
    EXPECT_TRUE(HasEdge("Sim_Particle", "Sim_SubItem"));
    EXPECT_FALSE(HasEdge("Sim_Particle", "sub"));
}

// ============================================================================
// 3. 单测：InsertFunctionDeclToGraph 空指针与无函数体声明边界测试
// ============================================================================

TEST_F(ProcessClangDeclTest, InsertFunctionDecl_NullAndPrototypeOnlyIgnored) {
    generator.InsertFunctionDeclToGraph(nullptr);
    EXPECT_TRUE(Graph().nodes.empty());

    RunDeclCollection(
        R"(
            namespace Math {
                float DeclaredButUndefined(float x, int y);
            }
        )",
        {},
        {"DeclaredButUndefined"},
        /*definitionOnly=*/false);

    EXPECT_EQ(FindFuncNode("Math_DeclaredButUndefined_float_int"), nullptr);
    EXPECT_TRUE(Graph().nodes.empty());
}

// ============================================================================
// 4. 单测：InsertFunctionDeclToGraph 普通全局/命名空间函数、inout 引用参数、数组参数与重载
// ============================================================================

TEST_F(ProcessClangDeclTest, InsertFunctionDecl_FreeFunctionsParamsBodyAndDeps) {
    RunDeclCollection(
        R"(
            namespace Sim {
                struct Config {
                    float scale;
                    int maxSteps;
                };

                struct State {
                    Ccino::Vec3 pos;
                    float energy;
                };
            }

            namespace Math {
                float StepEnergy(Sim::State& state, float dt, Ccino::Array<float, 4> weights) {
                    Sim::Config localCfg = {2.0f, 8};
                    state.energy = state.energy + dt * localCfg.scale * weights[0];
                    return state.energy;
                }

                float StepEnergy(float baseEnergy) {
                    return baseEnergy * 0.5f;
                }
            }
        )",
        {},
        {"StepEnergy"});

    // 1. 验证三参数重载 Math_StepEnergy_Sim_State_float_float_4
    const auto* stepFull = FindFuncNode("Math_StepEnergy_Sim_State_float_float_4");
    ASSERT_NE(stepFull, nullptr);
    EXPECT_EQ(stepFull->name, "Math_StepEnergy_Sim_State_float_float_4");
    EXPECT_EQ(stepFull->returnType, "float");

    ASSERT_EQ(stepFull->fields.size(), 3u);
    EXPECT_EQ(stepFull->fields[0]->type, "inout Sim_State");
    EXPECT_EQ(stepFull->fields[0]->name, "state");
    EXPECT_EQ(stepFull->fields[0]->dimensions, "");

    EXPECT_EQ(stepFull->fields[1]->type, "float");
    EXPECT_EQ(stepFull->fields[1]->name, "dt");
    EXPECT_EQ(stepFull->fields[1]->dimensions, "");

    EXPECT_EQ(stepFull->fields[2]->type, "float");
    EXPECT_EQ(stepFull->fields[2]->name, "weights");
    EXPECT_EQ(stepFull->fields[2]->dimensions, "[4]");

    // 验证函数体内声明的局部自定义结构体 Sim_Config 被收集到 depDeclNames
    ASSERT_NE(std::find(stepFull->depDeclNames.begin(), stepFull->depDeclNames.end(), "Sim_Config"),
              stepFull->depDeclNames.end());

    ASSERT_NE(stepFull->body, nullptr);
    EXPECT_EQ(
        stepFull->body->EmitGLSL(),
        "Sim_Config localCfg = Sim_Config(2.0,8);\n"
        "(state.energy = (state.energy + ((dt * localCfg.scale) * weights[0])));\n"
        "return state.energy;\n");

    // 2. 验证单参数同名重载函数节点独立存在
    const auto* stepSimple = FindFuncNode("Math_StepEnergy_float");
    ASSERT_NE(stepSimple, nullptr);
    EXPECT_EQ(stepSimple->returnType, "float");
    ASSERT_EQ(stepSimple->fields.size(), 1u);
    EXPECT_EQ(stepSimple->fields[0]->type, "float");
    EXPECT_EQ(stepSimple->fields[0]->name, "baseEnergy");
    ASSERT_NE(stepSimple->body, nullptr);
    EXPECT_EQ(stepSimple->body->EmitGLSL(), "return (baseEnergy * 0.5);\n");
}

// ============================================================================
// 5. 单测：InsertFunctionDeclToGraph 结构体成员函数、构造函数与 const 重载
// ============================================================================

TEST_F(ProcessClangDeclTest, InsertFunctionDecl_MemberMethodsConstructorsAndConstOverload) {
    RunDeclCollection(
        R"(
            namespace Physics {
                struct ImpulseCache {
                    Ccino::Vec3 delta;
                };

                struct RigidBody {
                    Ccino::Vec3 pos;
                    Ccino::Vec3 vel;
                    float mass;

                    RigidBody(Ccino::Vec3 initPos, float m) {
                        pos = initPos;
                        vel = Ccino::Vec3(0, 0, 0);
                        mass = m;
                    }

                    void ApplyImpulse(const Ccino::Vec3& j) {
                        vel = vel + j;
                    }

                    void Integrate(float dt) {
                        ImpulseCache cache = {Ccino::Vec3(0, -9, 0)};
                        this->ApplyImpulse(cache.delta);
                        pos = pos + this->vel * dt;
                    }

                    float Eval(int mode) {
                        return mass;
                    }

                    float Eval(int mode) const {
                        return mass;
                    }
                };
            }
        )",
        {},
        {"RigidBody", "ApplyImpulse", "Integrate", "Eval"});

    // 处理完毕后 m_currentStructDecl 必须已恢复为 nullptr
    EXPECT_EQ(generator.GetCurrentStructDecl(), nullptr);

    // 1. 验证构造函数：Physics_RigidBody_ctor_vec3_float
    const auto* ctorDecl = FindFuncNode("Physics_RigidBody_ctor_vec3_float");
    ASSERT_NE(ctorDecl, nullptr);
    EXPECT_EQ(ctorDecl->returnType, "void");
    ASSERT_EQ(ctorDecl->fields.size(), 3u);
    EXPECT_EQ(ctorDecl->fields[0]->type, "inout Physics_RigidBody");
    EXPECT_EQ(ctorDecl->fields[0]->name, "self");
    EXPECT_EQ(ctorDecl->fields[0]->dimensions, "");
    EXPECT_EQ(ctorDecl->fields[1]->type, "vec3");
    EXPECT_EQ(ctorDecl->fields[1]->name, "initPos");
    EXPECT_EQ(ctorDecl->fields[2]->type, "float");
    EXPECT_EQ(ctorDecl->fields[2]->name, "m");
    ASSERT_NE(ctorDecl->body, nullptr);
    EXPECT_EQ(
        ctorDecl->body->EmitGLSL(),
        "(self.pos = initPos);\n"
        "(self.vel = vec3(0, 0, 0));\n"
        "(self.mass = m);\n");

    // 2. 验证成员函数 Integrate：隐式/显式 this 访问转为 self，局部结构体依赖收集正确
    const auto* integrateDecl = FindFuncNode("Physics_RigidBody_Integrate_float");
    ASSERT_NE(integrateDecl, nullptr);
    EXPECT_EQ(integrateDecl->returnType, "void");
    ASSERT_EQ(integrateDecl->fields.size(), 2u);
    EXPECT_EQ(integrateDecl->fields[0]->type, "inout Physics_RigidBody");
    EXPECT_EQ(integrateDecl->fields[0]->name, "self");
    EXPECT_EQ(integrateDecl->fields[1]->type, "float");
    EXPECT_EQ(integrateDecl->fields[1]->name, "dt");
    ASSERT_NE(std::find(integrateDecl->depDeclNames.begin(),
                        integrateDecl->depDeclNames.end(),
                        "Physics_ImpulseCache"),
              integrateDecl->depDeclNames.end());
    ASSERT_NE(integrateDecl->body, nullptr);
    EXPECT_EQ(
        integrateDecl->body->EmitGLSL(),
        "Physics_ImpulseCache cache = Physics_ImpulseCache(vec3(0, (-9), 0));\n"
        "Physics_RigidBody_ApplyImpulse_vec3(self, cache.delta);\n"
        "(self.pos = (self.pos + (self.vel * dt)));\n");

    // 3. 验证 const 与非 const 成员函数重载独立入图
    const auto* evalMutable = FindFuncNode("Physics_RigidBody_Eval_int");
    const auto* evalConst = FindFuncNode("Physics_RigidBody_Eval_int_const");
    ASSERT_NE(evalMutable, nullptr);
    ASSERT_NE(evalConst, nullptr);
    EXPECT_EQ(evalMutable->body->EmitGLSL(), "return self.mass;\n");
    EXPECT_EQ(evalConst->body->EmitGLSL(), "return self.mass;\n");

    // 4. 验证成员函数在 DeclGraph.edges 中的依赖边（应为结构体类型名而非形参变量名 self / dt）
    EXPECT_TRUE(HasEdge("Physics_RigidBody_Integrate_float", "Physics_ImpulseCache"));
    EXPECT_FALSE(HasEdge("Physics_RigidBody_Integrate_float", "self"));
    EXPECT_FALSE(HasEdge("Physics_RigidBody_Integrate_float", "dt"));
}

// ============================================================================
// 6. 集成测试：完整多结构体 + 成员函数 + 全局函数入图与 DeclGraph DAG 依赖图验证
// ============================================================================

TEST_F(ProcessClangDeclTest, Integration_FullDeclGraphNodesAndDAGEdges) {
    RunDeclCollection(
        R"(
            namespace Ray {
                struct Material {
                    Ccino::Vec3 albedo;
                    float roughness;
                };

                struct Sphere {
                    Ccino::Vec3 center;
                    float radius;
                    Material mat;

                    float SurfaceWeight(float factor) const {
                        Material localMat = mat;
                        return radius * factor * localMat.roughness;
                    }
                };

                struct SceneSlice {
                    Sphere spheres[2];
                    int count;
                };

                float AccumulateScene(SceneSlice& slice, float gain) {
                    float total = 0.0f;
                    for (int i = 0; i < slice.count; ++i) {
                        Sphere s = slice.spheres[i];
                        total = total + s.SurfaceWeight(gain);
                    }
                    return total;
                }
            }
        )",
        {"Material", "Sphere", "SceneSlice"},
        {"SurfaceWeight", "AccumulateScene"});

    // 1. 验证所有结构体与函数节点均已写入 DeclGraph.nodes
    const auto* matNode = FindStructNode("Ray_Material");
    const auto* sphereNode = FindStructNode("Ray_Sphere");
    const auto* sliceNode = FindStructNode("Ray_SceneSlice");
    const auto* surfaceWeightNode = FindFuncNode("Ray_Sphere_SurfaceWeight_float_const");
    const auto* accumulateNode = FindFuncNode("Ray_AccumulateScene_Ray_SceneSlice_float");

    EXPECT_NE(matNode, nullptr);
    EXPECT_NE(sphereNode, nullptr);
    EXPECT_NE(sliceNode, nullptr);
    EXPECT_NE(surfaceWeightNode, nullptr);
    EXPECT_NE(accumulateNode, nullptr);

    // 2. 验证结构体字段及嵌套数组维度
    if (sliceNode) {
        ASSERT_EQ(sliceNode->fields.size(), 2u);
        EXPECT_EQ(sliceNode->fields[0]->type, "Ray_Sphere");
        EXPECT_EQ(sliceNode->fields[0]->name, "spheres");
        EXPECT_EQ(sliceNode->fields[0]->dimensions, "[2]");
        EXPECT_EQ(sliceNode->fields[1]->type, "int");
        EXPECT_EQ(sliceNode->fields[1]->name, "count");
        EXPECT_EQ(sliceNode->fields[1]->dimensions, "");
    }

    // 3. 验证全局函数与成员函数体生成结果
    if (surfaceWeightNode && surfaceWeightNode->body) {
        EXPECT_EQ(
            surfaceWeightNode->body->EmitGLSL(),
            "Ray_Material localMat = self.mat;\n"
            "return ((self.radius * factor) * localMat.roughness);\n");
    }

    if (accumulateNode && accumulateNode->body) {
        EXPECT_EQ(
            accumulateNode->body->EmitGLSL(),
            "float total = 0.0;\n"
            "for (int i = 0;(i < slice.count);(++i)) {\n"
            "Ray_Sphere s = slice.spheres[i];\n"
            "(total = (total + Ray_Sphere_SurfaceWeight_float_const(s, gain)));\n"
            "}\n"
            "return total;\n");
    }

    // 4. 验证 DeclGraph 的 DAG 边关系（节点依赖的是被引用声明的类型名，而不是字段/形参变量名）
    // Ray_Sphere 含字段 Material mat -> 依赖 Ray_Material
    EXPECT_TRUE(HasEdge("Ray_Sphere", "Ray_Material"));
    EXPECT_FALSE(HasEdge("Ray_Sphere", "mat"));

    // Ray_SceneSlice 含字段 Sphere spheres[2] -> 依赖 Ray_Sphere
    EXPECT_TRUE(HasEdge("Ray_SceneSlice", "Ray_Sphere"));
    EXPECT_FALSE(HasEdge("Ray_SceneSlice", "spheres"));

    // Ray_Sphere_SurfaceWeight_float_const 函数体内声明了 Material localMat -> 依赖 Ray_Material
    EXPECT_TRUE(HasEdge("Ray_Sphere_SurfaceWeight_float_const", "Ray_Material"));
    EXPECT_FALSE(HasEdge("Ray_Sphere_SurfaceWeight_float_const", "self"));
    EXPECT_FALSE(HasEdge("Ray_Sphere_SurfaceWeight_float_const", "factor"));

    // Ray_AccumulateScene_Ray_SceneSlice_float 函数体内 for 循环声明了 Sphere s -> 依赖 Ray_Sphere
    EXPECT_TRUE(HasEdge("Ray_AccumulateScene_Ray_SceneSlice_float", "Ray_Sphere"));
    EXPECT_FALSE(HasEdge("Ray_AccumulateScene_Ray_SceneSlice_float", "slice"));
    EXPECT_FALSE(HasEdge("Ray_AccumulateScene_Ray_SceneSlice_float", "gain"));
}

// ============================================================================
// 7. 单测：递归结构体声明收集与 BufferReference 生成
// ============================================================================

TEST_F(ProcessClangDeclTest, InsertCXXRecordDecl_RecursiveNestedStructsAndBufferReference) {
    // 仅显式传入顶层结构体 SceneBuffer，验证内部嵌套结构体 Material、Sphere（定长数组）与 Light（Ccino::Array）自动递归入图
    RunDeclCollection(
        R"(
            namespace Ray {
                struct Material {
                    Ccino::Vec3 albedo;
                    float roughness;
                };

                struct Sphere {
                    Material mat;
                    float radius;
                };

                struct Light {
                    Ccino::Vec3 radiance;
                };

                struct SceneBuffer {
                    Sphere spheres[2];
                    Ccino::Array<Light, 4> lights;
                    int activeCount;
                };
            }
        )",
        {"SceneBuffer"},
        {});

    // 1. 验证未显式指定的嵌套子结构体均已自动递归加入 DeclGraph，且 isBufferReference 为 false
    const auto* matNode    = FindStructNode("Ray_Material");
    const auto* sphereNode = FindStructNode("Ray_Sphere");
    const auto* lightNode  = FindStructNode("Ray_Light");
    const auto* sceneNode  = FindStructNode("Ray_SceneBuffer");

    ASSERT_NE(matNode, nullptr);
    ASSERT_NE(sphereNode, nullptr);
    ASSERT_NE(lightNode, nullptr);
    ASSERT_NE(sceneNode, nullptr);

    EXPECT_FALSE(matNode->isBufferReference);
    EXPECT_FALSE(sphereNode->isBufferReference);
    EXPECT_FALSE(lightNode->isBufferReference);
    EXPECT_FALSE(sceneNode->isBufferReference);

    EXPECT_TRUE(HasEdge("Ray_Sphere", "Ray_Material"));
    EXPECT_TRUE(HasEdge("Ray_SceneBuffer", "Ray_Sphere"));
    EXPECT_TRUE(HasEdge("Ray_SceneBuffer", "Ray_Light"));

    // 2. 验证 EmitDeclGraph 严格按照被依赖子结构体在前、顶层结构体在后的拓扑顺序输出
    const std::string emitted = generator.EmitDeclGraph();
    const size_t matPos    = emitted.find("struct Ray_Material{\n");
    const size_t spherePos = emitted.find("struct Ray_Sphere{\n");
    const size_t lightPos  = emitted.find("struct Ray_Light{\n");
    const size_t scenePos  = emitted.find("struct Ray_SceneBuffer{\n");

    ASSERT_NE(matPos, std::string::npos) << emitted;
    ASSERT_NE(spherePos, std::string::npos) << emitted;
    ASSERT_NE(lightPos, std::string::npos) << emitted;
    ASSERT_NE(scenePos, std::string::npos) << emitted;

    EXPECT_LT(matPos, spherePos);
    EXPECT_LT(spherePos, scenePos);
    EXPECT_LT(lightPos, scenePos);
}

TEST_F(ProcessClangDeclTest, GenerateUniformDataReference_BufferRefAndRecursiveStructInsertion) {
    class LambdaBindingCollector : public MatchFinder::MatchCallback {
    public:
        explicit LambdaBindingCollector(Ccino::ASTGenerator& gen) : generator(gen) {}

        void run(const MatchFinder::MatchResult& result) override {
            const auto* lambda = result.Nodes.getNodeAs<LambdaExpr>("lambda");
            if (!lambda || !result.Context) return;
            uniformBinding = generator.GenerateUniformDataReference(lambda, *result.Context);
        }

        Ccino::ASTGenerator& generator;
        std::unique_ptr<Ccino::GLSL::UniformBinding> uniformBinding;
    };

    LambdaBindingCollector callback(generator);
    MatchFinder finder;
    finder.addMatcher(lambdaExpr(hasAncestor(functionDecl(hasName("TestEntry")))).bind("lambda"), &callback);

    ASSERT_TRUE(tooling::runToolOnCodeWithArgs(
        tooling::newFrontendActionFactory(&finder)->create(),
        kDeclTestPreamble + R"(
            struct SubMeta {
                int version;
            };

            struct BufferPayload {
                SubMeta meta;
                Ccino::Vec3 value;
            };

            struct CapturedConfig {
                float exposure;
                int mode;
            };

            void TestEntry() {
                Ccino::Accessor<BufferPayload, BufferAccessFlagBits::ReadWrite> payloadBuf;
                CapturedConfig cfg{1.5f, 2};
                float dt = 0.016f;

                auto cs = [payloadBuf, cfg, dt]() {};
            }
        )",
        {"-std=c++20"}));

    ASSERT_NE(callback.uniformBinding, nullptr);
    ASSERT_EQ(callback.uniformBinding->fields.size(), 3u);

    // 1. Accessor<BufferPayload> 顶层结构体的 isBufferReference 应为 true，其内部嵌套的 SubMeta 应递归入图且 isBufferReference 为 false
    const auto* payloadNode = FindStructNode("BufferPayload");
    const auto* subMetaNode = FindStructNode("SubMeta");
    const auto* cfgNode     = FindStructNode("CapturedConfig");

    ASSERT_NE(payloadNode, nullptr);
    ASSERT_NE(subMetaNode, nullptr);
    ASSERT_NE(cfgNode, nullptr);

    EXPECT_TRUE(payloadNode->isBufferReference);
    EXPECT_FALSE(subMetaNode->isBufferReference);
    EXPECT_FALSE(cfgNode->isBufferReference);

    // 2. 验证 EmitDeclGraph 中 SubMeta 排在 buffer BufferPayload 之前，且 BufferPayload 生成为 buffer_reference
    const std::string emittedDecls = generator.EmitDeclGraph();
    const size_t subMetaPos = emittedDecls.find("struct SubMeta{\n");
    const size_t payloadPos = emittedDecls.find("layout(buffer_reference, scalar) buffer BufferPayload{ \n");
    const size_t cfgPos     = emittedDecls.find("struct CapturedConfig{\n");

    ASSERT_NE(subMetaPos, std::string::npos) << emittedDecls;
    ASSERT_NE(payloadPos, std::string::npos) << emittedDecls;
    ASSERT_NE(cfgPos, std::string::npos) << emittedDecls;
    EXPECT_LT(subMetaPos, payloadPos);
}

