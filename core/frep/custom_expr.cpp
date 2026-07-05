// core/frep/custom_expr.cpp
//
// CustomExprNode back-ends. All three (LLVM IR, CPU eval, GLSL emit)
// walk the shared frep::expr::Node AST produced by frep::expr::parse().

#include "custom_expr.hpp"
#include "core/compiler/llvm_compat.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <cmath>
#include <cstdio>
#include <numbers>
#include <stdexcept>

namespace frep {

// ═════════════════════════════════════════════════════════════════════════════
// Back-end #1: LLVM IR codegen
// ═════════════════════════════════════════════════════════════════════════════

llvm::Value* CustomExprCompiler::fc(float v) {
    return llvm::ConstantFP::get(f32(), v);
}

llvm::Value* CustomExprCompiler::gen(const expr::Node& n) {
    using Kind = expr::Node::Kind;
    switch (n.kind) {
        case Kind::Number: return fc(n.num);

        case Kind::Var:
            if (n.ident == "x") return vx_;
            if (n.ident == "y") return vy_;
            if (n.ident == "z") return vz_;
            fail("unknown variable '" + n.ident + "'");
            return nullptr;

        case Kind::Const:
            if (n.ident == "pi") return fc(std::numbers::pi_v<float>);
            if (n.ident == "e")  return fc(std::numbers::e_v<float>);
            fail("unknown constant '" + n.ident + "'");
            return nullptr;

        case Kind::UnaryNeg: {
            auto v = gen(*n.children[0]);
            if (!v) return nullptr;
            return b_->CreateFNeg(v);
        }

        case Kind::BinOp: {
            auto l = gen(*n.children[0]);
            auto r = gen(*n.children[1]);
            if (!l || !r) return nullptr;
            switch (n.bop) {
                case expr::Op::Add: return b_->CreateFAdd(l, r);
                case expr::Op::Sub: return b_->CreateFSub(l, r);
                case expr::Op::Mul: return b_->CreateFMul(l, r);
                case expr::Op::Div: return b_->CreateFDiv(l, r);
            }
            fail("unhandled BinOp");
            return nullptr;
        }

        case Kind::Call: return gen_call(n);
    }
    fail("unhandled AST kind");
    return nullptr;
}

llvm::Value* CustomExprCompiler::gen_call(const expr::Node& n) {
    using llvm::Intrinsic::ID;
    auto& b = *b_;

    // Pre-emit arg LLVM values.
    std::vector<llvm::Value*> args;
    args.reserve(n.children.size());
    for (const auto& a : n.children) {
        auto v = gen(*a);
        if (!v) return nullptr;
        args.push_back(v);
    }

    auto unary = [&](ID id) {
        return frep::llvm_compat::unary_intrinsic(b, id, args[0]);
    };
    auto binary = [&](ID id) {
        return frep::llvm_compat::binary_intrinsic(b, id, args[0], args[1]);
    };

    const auto& name = n.ident;
    if (name == "sqrt")  return unary(llvm::Intrinsic::sqrt);
    if (name == "abs")   return unary(llvm::Intrinsic::fabs);
    if (name == "sin")   return unary(llvm::Intrinsic::sin);
    if (name == "cos")   return unary(llvm::Intrinsic::cos);
    if (name == "exp")   return unary(llvm::Intrinsic::exp);
    if (name == "log")   return unary(llvm::Intrinsic::log);
    if (name == "floor") return unary(llvm::Intrinsic::floor);
    if (name == "ceil")  return unary(llvm::Intrinsic::ceil);
    if (name == "min")   return binary(llvm::Intrinsic::minnum);
    if (name == "max")   return binary(llvm::Intrinsic::maxnum);
    if (name == "pow")   return binary(llvm::Intrinsic::pow);
    // Inverse-trig / atan2 / mod: no LLVM intrinsics -> libm calls.
    // CPU-JIT resolves them from the process; GPU_IR (NVPTX) needs
    // self-contained transcendentals, so these are CPU-only for now.
    auto libm1 = [&](const char* fn) {
        auto* fty = llvm::FunctionType::get(b.getFloatTy(), {b.getFloatTy()}, false);
        return b.CreateCall(mod_->getOrInsertFunction(fn, fty), {args[0]});
    };
    auto libm2 = [&](const char* fn) {
        auto* fty = llvm::FunctionType::get(b.getFloatTy(), {b.getFloatTy(), b.getFloatTy()}, false);
        return b.CreateCall(mod_->getOrInsertFunction(fn, fty), {args[0], args[1]});
    };
    if (name == "asin")  return libm1("asinf");
    if (name == "acos")  return libm1("acosf");
    if (name == "atan")  return libm1("atanf");
    if (name == "atan2") return libm2("atan2f");
    if (name == "mod")   return libm2("fmodf");
    if (name == "tan") {
        // No tan intrinsic — build sin/cos.
        auto s = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::sin, args[0]);
        auto c = frep::llvm_compat::unary_intrinsic(b, llvm::Intrinsic::cos, args[0]);
        return b.CreateFDiv(s, c, "tan");
    }
    fail("unknown function '" + name + "'");
    return nullptr;
}

llvm::Function* CustomExprCompiler::compile(llvm::Module&        mod,
                                            llvm::LLVMContext&   ctx,
                                            const std::string&   fn_name,
                                            const expr::NodePtr& ast) {
    error_.clear();
    if (!ast) { fail("null AST passed to compile()"); return nullptr; }

    ctx_ = &ctx;
    mod_ = &mod;

    auto* fty = llvm::FunctionType::get(
        llvm::Type::getFloatTy(ctx),
        {llvm::Type::getFloatTy(ctx),
         llvm::Type::getFloatTy(ctx),
         llvm::Type::getFloatTy(ctx)},
        false);

    if (mod.getFunction(fn_name)) {
        fail("function " + fn_name + " already exists in module");
        return nullptr;
    }

    auto* fn = llvm::Function::Create(
        fty, llvm::Function::PrivateLinkage, fn_name, mod);
    fn->addFnAttr(llvm::Attribute::AlwaysInline);
    fn->addFnAttr(llvm::Attribute::NoUnwind);

    auto it = fn->arg_begin();
    vx_ = &*it++; vx_->setName("x");
    vy_ = &*it++; vy_->setName("y");
    vz_ = &*it++; vz_->setName("z");

    auto* bb = llvm::BasicBlock::Create(ctx, "entry", fn);
    llvm::IRBuilder<> b(bb);
    b_ = &b;

    auto* result = gen(*ast);
    if (!result || !error_.empty()) {
        fn->eraseFromParent();
        return nullptr;
    }
    b.CreateRet(result);

    std::string vfy;
    llvm::raw_string_ostream es(vfy);
    if (llvm::verifyFunction(*fn, &es)) {
        fail("verify error: " + vfy);
        fn->eraseFromParent();
        return nullptr;
    }
    return fn;
}

// Convenience overload — parse then compile.
llvm::Function* CustomExprCompiler::compile(llvm::Module&       mod,
                                            llvm::LLVMContext&  ctx,
                                            const std::string&  fn_name,
                                            const std::string&  expr_src) {
    expr::NodePtr ast;
    try {
        ast = expr::parse(expr_src);
    } catch (const expr::ParseError& e) {
        fail(std::string("parse error: ") + e.what());
        return nullptr;
    }
    return compile(mod, ctx, fn_name, ast);
}


// ═════════════════════════════════════════════════════════════════════════════
// Back-end #2: CPU evaluation (direct AST interpretation)
// ═════════════════════════════════════════════════════════════════════════════

float CustomExprNode::eval_ast(const expr::Node& n, float x, float y, float z) {
    using Kind = expr::Node::Kind;
    switch (n.kind) {
        case Kind::Number: return n.num;
        case Kind::Var:
            if (n.ident == "x") return x;
            if (n.ident == "y") return y;
            if (n.ident == "z") return z;
            throw std::runtime_error("unknown variable '" + n.ident + "'");
        case Kind::Const:
            if (n.ident == "pi") return std::numbers::pi_v<float>;
            if (n.ident == "e")  return std::numbers::e_v<float>;
            throw std::runtime_error("unknown constant '" + n.ident + "'");
        case Kind::UnaryNeg:
            return -eval_ast(*n.children[0], x, y, z);
        case Kind::BinOp: {
            float l = eval_ast(*n.children[0], x, y, z);
            float r = eval_ast(*n.children[1], x, y, z);
            switch (n.bop) {
                case expr::Op::Add: return l + r;
                case expr::Op::Sub: return l - r;
                case expr::Op::Mul: return l * r;
                case expr::Op::Div: return l / r;
            }
            throw std::runtime_error("unhandled BinOp");
        }
        case Kind::Call: {
            const auto& name = n.ident;
            // Eval args first.
            float a0 = eval_ast(*n.children[0], x, y, z);
            if (name == "sin")   return std::sin(a0);
            if (name == "cos")   return std::cos(a0);
            if (name == "tan")   return std::tan(a0);
            if (name == "sqrt")  return std::sqrt(a0);
            if (name == "abs")   return std::abs(a0);
            if (name == "exp")   return std::exp(a0);
            if (name == "log")   return std::log(a0);
            if (name == "floor") return std::floor(a0);
            if (name == "asin")  return std::asin(a0);
            if (name == "acos")  return std::acos(a0);
            if (name == "atan")  return std::atan(a0);
            if (name == "ceil")  return std::ceil(a0);
            float a1 = eval_ast(*n.children[1], x, y, z);
            if (name == "pow")   return std::pow(a0, a1);
            if (name == "min")   return std::fmin(a0, a1);
            if (name == "max")   return std::fmax(a0, a1);
            if (name == "atan2") return std::atan2(a0, a1);
            if (name == "mod")   return std::fmod(a0, a1);
            throw std::runtime_error("unknown function '" + name + "'");
        }
    }
    throw std::runtime_error("unhandled AST kind in eval");
}


// ═════════════════════════════════════════════════════════════════════════════
// Back-end #3: GLSL emission
// ═════════════════════════════════════════════════════════════════════════════

void CustomExprNode::emit_glsl_ast(std::ostream& out, const expr::Node& n) {
    using Kind = expr::Node::Kind;
    switch (n.kind) {
        case Kind::Number: {
            // GLSL prefers explicit float literal syntax — `1` would be
            // an int, which in float context auto-promotes but reads
            // strangely. Emit with one decimal place at minimum.
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.7g", n.num);
            std::string s(buf);
            if (s.find('.') == std::string::npos &&
                s.find('e') == std::string::npos)
                s += ".0";
            out << s;
            return;
        }
        case Kind::Var:
            out << n.ident;  // x, y, z are in scope at the call site
            return;
        case Kind::Const:
            // GLSL has no built-in pi/e; emit numeric value.
            if (n.ident == "pi") out << "3.14159265358979";
            else if (n.ident == "e") out << "2.71828182845905";
            else out << "0.0";
            return;
        case Kind::UnaryNeg:
            out << "(-";
            emit_glsl_ast(out, *n.children[0]);
            out << ")";
            return;
        case Kind::BinOp: {
            const char* op = "+";
            switch (n.bop) {
                case expr::Op::Add: op = "+"; break;
                case expr::Op::Sub: op = "-"; break;
                case expr::Op::Mul: op = "*"; break;
                case expr::Op::Div: op = "/"; break;
            }
            // Parenthesize liberally — GLSL parser still folds these
            // out, and we don't need to track precedence carefully.
            out << "(";
            emit_glsl_ast(out, *n.children[0]);
            out << " " << op << " ";
            emit_glsl_ast(out, *n.children[1]);
            out << ")";
            return;
        }
        case Kind::Call: {
            // All supported functions have matching names in GLSL.
            // `abs` works for floats; `pow/min/max` accept two scalars.
            // GLSL dialect: atan2(y,x) -> atan(y,x); mod() is GLSL mod.
            out << (n.ident == std::string("atan2") ? "atan" : n.ident.c_str()) << "(";
            for (std::size_t i = 0; i < n.children.size(); ++i) {
                if (i) out << ", ";
                emit_glsl_ast(out, *n.children[i]);
            }
            out << ")";
            return;
        }
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// CustomExprNode::codegen — wires up the cached fn lookup + LLVM compile.
// ═════════════════════════════════════════════════════════════════════════════
llvm::Value* CustomExprNode::codegen(CgCtx& c, llvm::Value* x,
                                     llvm::Value* y, llvm::Value* z) const {
    ensure_parsed();

    if (cached_fn_name_.empty())
        cached_fn_name_ = "frep_expr_" + std::to_string(structural_hash());

    auto* fn = c.mod.getFunction(cached_fn_name_);
    if (!fn) {
        CustomExprCompiler comp;
        auto saved = c.b.saveIP();
        fn = comp.compile(c.mod, c.lc, cached_fn_name_, ast_);
        c.b.restoreIP(saved);
        if (!fn) {
            llvm::errs() << "CustomExprNode: " << comp.last_error()
                         << " (expression: " << expr_ << ")\n";
            return c.fc(0.0f);
        }
    }
    return c.b.CreateCall(fn, {x, y, z}, "expr_v");
}

} // namespace frep
