#ifndef CLANG_AST_TO_ASR_H
#define CLANG_AST_TO_ASR_H

#define WITH_LFORTRAN_ASSERT

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

#include "clang/CodeGen/ObjectFilePCHContainerOperations.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include "clang/Rewrite/Frontend/FrontendActions.h"
#include "clang/StaticAnalyzer/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Syntax/BuildTree.h"
#include "clang/Tooling/Syntax/TokenBufferTokenManager.h"
#include "clang/Tooling/Syntax/Tokens.h"
#include "clang/Tooling/Syntax/Tree.h"
#include "clang/Tooling/Tooling.h"

#include <libasr/pickle.h>

#include <iostream>

namespace LCompilers {

class ClangASTtoASRVisitor: public clang::RecursiveASTVisitor<ClangASTtoASRVisitor> {

public:

    std::string ast;
    SymbolTable *current_scope=nullptr;
    Allocator& al;
    ASR::asr_t*& tu;
    ASR::asr_t* tmp;
    Vec<ASR::stmt_t*>* current_body;
    bool is_stmt_created;

    explicit ClangASTtoASRVisitor(clang::ASTContext *Context_,
        Allocator& al_, ASR::asr_t*& tu_):
        Context(Context_), al{al_}, tu{tu_},
        tmp{nullptr}, current_body{nullptr},
        is_stmt_created{true} {}

    template <typename T>
    Location Lloc(T *x) {
        Location l;
        l.first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
        l.last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
        return l;
    }

    template <typename T>
    std::string loc(T *x) {
        uint64_t first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
        uint64_t last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
        return std::to_string(first) + ":" + std::to_string(last);
    }

    ASR::symbol_t* declare_dummy_variable(std::string var_name, SymbolTable* scope, Location& loc, ASR::ttype_t* var_type) {
        var_name = scope->get_unique_name(var_name, false);
        SetChar variable_dependencies_vec;
        variable_dependencies_vec.reserve(al, 1);
        ASRUtils::collect_variable_dependencies(al, variable_dependencies_vec, var_type);
        ASR::asr_t* variable_asr = ASR::make_Variable_t(al, loc, scope,
                                        s2c(al, var_name), variable_dependencies_vec.p,
                                        variable_dependencies_vec.size(), ASR::intentType::Local,
                                        nullptr, nullptr, ASR::storage_typeType::Default,
                                        var_type, nullptr, ASR::abiType::Source, ASR::accessType::Public,
                                        ASR::presenceType::Required, false);
        ASR::symbol_t* dummy_variable_sym = ASR::down_cast<ASR::symbol_t>(variable_asr);
        scope->add_symbol(var_name, dummy_variable_sym);
        return dummy_variable_sym;
    }

    void construct_program() {
        // Convert the main function into a program
        ASR::TranslationUnit_t* tu = (ASR::TranslationUnit_t*)this->tu;
        Location& loc = tu->base.base.loc;

        Vec<ASR::stmt_t*> prog_body;
        prog_body.reserve(al, 1);
        SetChar prog_dep;
        prog_dep.reserve(al, 1);
        SymbolTable *program_scope = al.make_new<SymbolTable>(tu->m_symtab);

        std::string main_func = "main";
        ASR::symbol_t *main_sym = tu->m_symtab->resolve_symbol(main_func);
        LCOMPILERS_ASSERT(main_sym);
        if (main_sym == nullptr) {
            return;
        }
        ASR::Function_t *main = ASR::down_cast<ASR::Function_t>(main_sym);
        LCOMPILERS_ASSERT(main);

        // Make a FunctionCall to main
        ASR::ttype_t *int32_type = ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4));
        ASR::asr_t* func_call_main = ASR::make_FunctionCall_t(al, loc, main_sym, main_sym,
             nullptr, 0, int32_type, nullptr, nullptr);

        ASR::symbol_t* exit_variable_sym = declare_dummy_variable("exit_code", program_scope, loc, int32_type);
        ASR::expr_t* variable_var = ASRUtils::EXPR(ASR::make_Var_t(al, loc, exit_variable_sym));
        ASR::asr_t* assign_stmt = ASR::make_Assignment_t(al, loc, variable_var, ASRUtils::EXPR(func_call_main), nullptr);

        prog_body.push_back(al, ASRUtils::STMT(assign_stmt));

        prog_dep.push_back(al, s2c(al, main_func));

        std::string prog_name = "main_program";
        ASR::asr_t *prog = ASR::make_Program_t(
            al, loc,
            /* a_symtab */ program_scope,
            /* a_name */ s2c(al, prog_name),
            prog_dep.p,
            prog_dep.n,
            /* a_body */ prog_body.p,
            /* n_body */ prog_body.n);
        tu->m_symtab->add_symbol(prog_name, ASR::down_cast<ASR::symbol_t>(prog));
    }

    bool TraverseTranslationUnitDecl(clang::TranslationUnitDecl *x) {
        SymbolTable *parent_scope = al.make_new<SymbolTable>(nullptr);
        current_scope = parent_scope;
        Location l = Lloc(x);
        tu = ASR::make_TranslationUnit_t(al, l, current_scope, nullptr, 0);

        for (auto D = x->decls_begin(), DEnd = x->decls_end(); D != DEnd; ++D) {
            TraverseDecl(*D);
        }

        construct_program();

        return true;
    }

    ASR::ttype_t* ClangTypeToASRType(const clang::QualType& qual_type) {
        const clang::SplitQualType& split_qual_type = qual_type.split();
        const clang::Type* clang_type = split_qual_type.asPair().first;
        const clang::Qualifiers qualifiers = split_qual_type.asPair().second;
        Location l; l.first = 1, l.last = 1;
        ASR::ttype_t* type = nullptr;
        if( clang_type->isCharType() ) {
            type = ASRUtils::TYPE(ASR::make_Character_t(al, l, 1, -1, nullptr));
        } else if( clang_type->isIntegerType() ) {
            type = ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4));
        } else if( clang_type->isPointerType() ) {
            type = ClangTypeToASRType(qual_type->getPointeeType());
            if( !ASRUtils::is_character(*type) ) {
                type = ASRUtils::TYPE(ASR::make_Pointer_t(al, l, type));
            }
        } else {
            throw std::runtime_error("clang::QualType not yet supported.");
        }

        if( qualifiers.hasConst() ) {
            type = ASRUtils::TYPE(ASR::make_Const_t(al, l, type));
        }
        return type;
    }

    bool TraverseParmVarDecl(clang::ParmVarDecl* x) {
        std::string name = x->getName().str();
        if( name == "" ) {
            name = current_scope->get_unique_name("param");
        }
        ASR::ttype_t* type = ClangTypeToASRType(x->getType());
        clang::Expr *init = x->getDefaultArg();
        ASR::expr_t* asr_init = nullptr;
        if (init) {
            TraverseStmt(init);
            asr_init = ASRUtils::EXPR(tmp);
        }

        tmp = ASR::make_Variable_t(al, Lloc(x), current_scope, s2c(al, name),
            nullptr, 0, ASR::intentType::InOut, asr_init, nullptr,
            ASR::storage_typeType::Default, type, nullptr, ASR::abiType::Source,
            ASR::accessType::Public, ASR::presenceType::Required, false);
        current_scope->add_symbol(name, ASR::down_cast<ASR::symbol_t>(tmp));
        is_stmt_created = false;
        return true;
    }

    void handle_printf(clang::CallExpr *x) {
        Vec<ASR::expr_t*> args;
        args.reserve(al, 1);
        for (auto *p : x->arguments()) {
            TraverseStmt(p);
            args.push_back(al, ASRUtils::EXPR(tmp));
        }
        tmp = ASR::make_Print_t(al, Lloc(x), args.p, args.size(), nullptr, nullptr);
        is_stmt_created = true;
    }

    bool check_printf(ASR::expr_t* callee) {
        ASR::Var_t* callee_Var = ASR::down_cast<ASR::Var_t>(callee);
        return std::string(ASRUtils::symbol_name(callee_Var->m_v)) == "printf";
    }

    bool TraverseCallExpr(clang::CallExpr *x) {
        TraverseStmt(x->getCallee());
        ASR::expr_t* callee = ASRUtils::EXPR(tmp);
        if( check_printf(callee) ) {
            handle_printf(x);
        } else {
            throw std::runtime_error("Calling user defined functions isn't supported yet.");
        }
        return true;
    }

    bool TraverseFunctionDecl(clang::FunctionDecl *x) {
        SymbolTable* parent_scope = current_scope;
        current_scope = al.make_new<SymbolTable>(parent_scope);

        std::string name = x->getName().str();
        Vec<ASR::expr_t*> args;
        args.reserve(al, 1);
        for (auto &p : x->parameters()) {
            TraverseDecl(p);
            ASR::symbol_t* arg_sym = ASR::down_cast<ASR::symbol_t>(tmp);
            args.push_back(al,
                ASRUtils::EXPR(ASR::make_Var_t(al, arg_sym->base.loc, arg_sym)));
        }

        ASR::ttype_t* return_type = ClangTypeToASRType(x->getReturnType());
        ASR::symbol_t* return_sym = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, Lloc(x),
            current_scope, s2c(al, "__return_var"), nullptr, 0, ASR::intentType::ReturnVar, nullptr, nullptr,
            ASR::storage_typeType::Default, return_type, nullptr, ASR::abiType::Source, ASR::accessType::Public,
            ASR::presenceType::Required, false));
        current_scope->add_symbol("__return_var", return_sym);

        Vec<ASR::stmt_t*>* current_body_copy = current_body;
        Vec<ASR::stmt_t*> body; body.reserve(al, 1);
        current_body = &body;
        if( x->hasBody() ) {
            TraverseStmt(x->getBody());
        }
        current_body = current_body_copy;

        ASR::expr_t* return_var = ASRUtils::EXPR(ASR::make_Var_t(al, return_sym->base.loc, return_sym));
        tmp = ASRUtils::make_Function_t_util(al, Lloc(x), current_scope, s2c(al, name), nullptr, 0,
            args.p, args.size(), body.p, body.size(), return_var, ASR::abiType::Source, ASR::accessType::Public,
            ASR::deftypeType::Implementation, nullptr, false, false, false, false, false, nullptr, 0,
            false, false, false);
        parent_scope->add_symbol(name, ASR::down_cast<ASR::symbol_t>(tmp));
        current_scope = parent_scope;
        is_stmt_created = false;
        return true;
    }

    bool TraverseVarDecl(clang::VarDecl *x) {
        std::string name = x->getName().str();
        ASR::ttype_t *asr_type = ClangTypeToASRType(x->getType());
        ASR::symbol_t *v = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, Lloc(x),
            current_scope, s2c(al, name), nullptr, 0, ASR::intentType::Local, nullptr, nullptr,
            ASR::storage_typeType::Default, asr_type, nullptr, ASR::abiType::Source,
            ASR::accessType::Public, ASR::presenceType::Required, false));
        current_scope->add_symbol(name, v);
        is_stmt_created = false;
        return true;
    }

    bool TraverseBinaryOperator(clang::BinaryOperator *x) {
        clang::BinaryOperatorKind op = x->getOpcode();
        TraverseStmt(x->getLHS());
        ASR::expr_t* x_lhs = ASRUtils::EXPR(tmp);
        TraverseStmt(x->getRHS());
        ASR::expr_t* x_rhs = ASRUtils::EXPR(tmp);
        if( op == clang::BO_Assign ) {
            tmp = ASR::make_Assignment_t(al, Lloc(x), x_lhs, x_rhs, nullptr);
            is_stmt_created = true;
        } else {
            bool is_binop = false, is_cmpop = false;
            ASR::binopType binop_type;
            ASR::cmpopType cmpop_type;
            switch (op) {
                case clang::BO_Add: {
                    binop_type = ASR::binopType::Add;
                    is_binop = true;
                    break;
                }
                case clang::BO_Sub: {
                    binop_type = ASR::binopType::Sub;
                    is_binop = true;
                    break;
                }
                case clang::BO_Mul: {
                    binop_type = ASR::binopType::Mul;
                    is_binop = true;
                    break;
                }
                case clang::BO_Div: {
                    binop_type = ASR::binopType::Div;
                    is_binop = true;
                    break;
                }
                case clang::BO_EQ: {
                    cmpop_type = ASR::cmpopType::Eq;
                    is_cmpop = true;
                    break;
                }
                default: {
                    throw std::runtime_error("BinaryOperator not supported " + std::to_string(op));
                    break;
                }
            }
            if( is_binop ) {
                if( ASRUtils::is_integer(*ASRUtils::expr_type(x_lhs)) &&
                    ASRUtils::is_integer(*ASRUtils::expr_type(x_rhs)) ) {
                    tmp = ASR::make_IntegerBinOp_t(al, Lloc(x), x_lhs,
                        binop_type, x_rhs, ASRUtils::expr_type(x_lhs), nullptr);
                } else {
                    throw std::runtime_error("Only integer type is supported so far");
                }
            } else {
                throw std::runtime_error("Only binary operators supported so far");
            }
            is_stmt_created = false;
        }
        return true;
    }

    bool TraverseDeclRefExpr(clang::DeclRefExpr* x) {
        std::string name = x->getNameInfo().getAsString();
        ASR::symbol_t* sym = current_scope->resolve_symbol(name);
        LCOMPILERS_ASSERT(sym != nullptr);
        tmp = ASR::make_Var_t(al, Lloc(x), sym);
        is_stmt_created = false;
        return true;
    }

    bool TraverseIntegerLiteral(clang::IntegerLiteral* x) {
        int64_t i = x->getValue().getLimitedValue();
        tmp = ASR::make_IntegerConstant_t(al, Lloc(x), i,
            ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4)));
        is_stmt_created = false;
        return true;
    }

    bool TraverseStringLiteral(clang::StringLiteral *x) {
        std::string s = x->getString().str();
        tmp = ASR::make_StringConstant_t(al, Lloc(x), s2c(al, s),
            ASRUtils::TYPE(ASR::make_Character_t(al, Lloc(x), 1, s.size(), nullptr)));
        is_stmt_created = false;
        return true;
    }

    bool TraverseCompoundStmt(clang::CompoundStmt *x) {
        for (auto &s : x->body()) {
            bool is_stmt_created_ = is_stmt_created;
            is_stmt_created = false;
            TraverseStmt(s);
            if( is_stmt_created ) {
                current_body->push_back(al, ASRUtils::STMT(tmp));
            }
            is_stmt_created_ = is_stmt_created;
        }
        return true;
    }

    bool TraverseReturnStmt(clang::ReturnStmt *x) {
        ASR::symbol_t* return_sym = current_scope->resolve_symbol("__return_var");
        ASR::expr_t* return_var = ASRUtils::EXPR(ASR::make_Var_t(al, Lloc(x), return_sym));
        TraverseStmt(x->getRetValue());
        tmp = ASR::make_Assignment_t(al, Lloc(x), return_var, ASRUtils::EXPR(tmp), nullptr);
        current_body->push_back(al, ASRUtils::STMT(tmp));
        tmp = ASR::make_Return_t(al, Lloc(x));
        is_stmt_created = true;
        return true;
    }


private:
    clang::ASTContext *Context;
};

class FindNamedClassConsumer: public clang::ASTConsumer {

public:

    explicit FindNamedClassConsumer(clang::ASTContext *Context,
        Allocator& al_, ASR::asr_t*& tu_): Visitor(Context, al_, tu_) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

private:
    LCompilers::ClangASTtoASRVisitor Visitor;
};

class FindNamedClassAction: public clang::ASTFrontendAction {

    public:

        Allocator& al;
        ASR::asr_t*& tu;

        FindNamedClassAction(Allocator& al_, ASR::asr_t*& tu_): al{al_}, tu{tu_} {

        }

        virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance &Compiler, llvm::StringRef /*InFile*/) {
            return std::make_unique<FindNamedClassConsumer>(
                &Compiler.getASTContext(), al, tu);
        }
};

}

#endif
