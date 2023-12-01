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

    bool TraverseTranslationUnitDecl(clang::TranslationUnitDecl *x) {
        SymbolTable *parent_scope = al.make_new<SymbolTable>(nullptr);
        current_scope = parent_scope;
        Location l = Lloc(x);
        tu = ASR::make_TranslationUnit_t(al, l, current_scope, nullptr, 0);

        for (auto D = x->decls_begin(), DEnd = x->decls_end(); D != DEnd; ++D) {
            TraverseDecl(*D);
        }

        // std::cout << LCompilers::pickle(*tu, true, true, true) << std::endl;

        return true;
    }

    ASR::ttype_t* ClangTypeToASRType(const clang::QualType& qual_type) {
        const clang::SplitQualType& split_qual_type = qual_type.split();
        const clang::Type* clang_type = split_qual_type.asPair().first;
        Location l; l.first = 1, l.last = 1;
        if( clang_type->isIntegerType() ) {
            return ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4));
        } else {
            throw std::runtime_error("clang::QualType not yet supported.");
        }
        return nullptr;
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
