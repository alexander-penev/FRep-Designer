#pragma once
// core/frep/custom_expr.hpp
//
// CustomExprNode — F-Rep node whose SDF is described by a text expression.
//
// The expression is parsed once (per node instance, on demand) into a
// shared AST (see expr_ast.hpp), and that AST is then consumed by
// three different back-ends:
//
//   1. LLVM IR codegen      — for the CPU JIT pipeline
//                              (`CustomExprNode::codegen`)
//   2. Direct interpretation — for `FRepNode::eval()` calls coming from
//                              the picker, marching cubes, and BVH
//   3. GLSL emission         — for the GPU compute path
//                              (`CustomExprNode::emit_glsl`)
//
// All three back-ends walk the same AST and so are guaranteed to agree
// on the syntax accepted, the arity of functions, and the meaning of
// each operator.

#include "core/frep/expr_ast.hpp"
#include "core/frep/node.hpp"

#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <ostream>
#include <string>

namespace frep {

// CustomExprCompiler — emits an LLVM function from a CustomExpr-AST.
// This is the LLVM back-end; the parse is done once by
// frep::expr::parse() (called inside CustomExprNode::codegen) and the
// resulting AST is passed in.
class CustomExprCompiler {
public:
    // Build an LLVM function `<fn_name>(float, float, float) -> float`
    // implementing the AST. Returns nullptr on failure (see last_error).
    llvm::Function* compile(llvm::Module&         mod,
                            llvm::LLVMContext&    ctx,
                            const std::string&    fn_name,
                            const expr::NodePtr&  ast);

    // Convenience overload that parses the source string first.
    llvm::Function* compile(llvm::Module&         mod,
                            llvm::LLVMContext&    ctx,
                            const std::string&    fn_name,
                            const std::string&    expr_src);

    const std::string& last_error() const { return error_; }

private:
    std::string        error_;
    llvm::LLVMContext* ctx_ = nullptr;
    llvm::Module*      mod_ = nullptr;
    llvm::IRBuilder<>* b_   = nullptr;
    llvm::Value*       vx_  = nullptr;
    llvm::Value*       vy_  = nullptr;
    llvm::Value*       vz_  = nullptr;

    llvm::Value* gen(const expr::Node& n);
    llvm::Value* gen_call(const expr::Node& n);

    llvm::Type*  f32() { return llvm::Type::getFloatTy(*ctx_); }
    llvm::Value* fc(float v);

    void fail(const std::string& msg) {
        if (error_.empty()) error_ = msg;
    }
};


// CustomExprNode — FRepNode wrapping a user-supplied analytic expression.
class CustomExprNode final : public FRepNode {
public:
    CustomExprNode(std::string expr_text, std::string nid = "expr")
        : expr_(std::move(expr_text))
    {
        kind = NodeKind::Plugin;
        id   = std::move(nid);
    }

    float eval(float x, float y, float z) const override {
        ensure_parsed();
        return eval_ast(*ast_, x, y, z);
    }

    llvm::Value* codegen(CgCtx& c, llvm::Value* x,
                         llvm::Value* y, llvm::Value* z) const override;

    std::size_t structural_hash() const noexcept override {
        return std::hash<std::string>{}(expr_) ^ 0xCAFE'BABEull;
    }
    std::size_t structure_hash() const noexcept override {
        return std::hash<std::string>{}(expr_) ^ 0xCAFE'BABEull;
    }

    const char* type_name() const noexcept override { return "CustomExpr"; }

    bool emit_glsl(std::ostream& out,
                   const std::vector<std::string>& /*child_exprs*/,
                   const std::string& /*var_prefix*/) const override {
        ensure_parsed();
        out << "(";
        emit_glsl_ast(out, *ast_);
        out << ")";
        return true;
    }

    const std::string& expression() const { return expr_; }

private:
    std::string expr_;

    // Cached AST — parsed lazily on first back-end call.
    mutable expr::NodePtr ast_;

    // Cached LLVM function name — deduplicates IR when scene_normal
    // calls the SDF 6 times in a row.
    mutable std::string cached_fn_name_;

    void ensure_parsed() const {
        if (!ast_) ast_ = expr::fold(expr::parse(expr_));
    }

    static float eval_ast(const expr::Node& n, float x, float y, float z);
    static void  emit_glsl_ast(std::ostream& out, const expr::Node& n);
};

} // namespace frep
