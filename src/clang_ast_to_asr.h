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

    explicit ClangASTtoASRVisitor(clang::ASTContext *Context_,
        Allocator& al_, ASR::asr_t*& tu_):
        Context(Context_), al{al_}, tu{tu_}, tmp{nullptr} {}

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
            LCOMPILERS_ASSERT(false);
        }
        return nullptr;
    }

    bool TraverseFunctionDecl(clang::FunctionDecl *x) {
        SymbolTable* parent_scope = current_scope;
        current_scope = al.make_new<SymbolTable>(parent_scope);
        std::string name = x->getName().str();
        // const clang::QualType& qual_type = x->getType();
        std::string type = clang::QualType::getAsString(
            x->getType().split(), Context->getPrintingPolicy());
        Vec<ASR::expr_t*> args;
        args.reserve(al, 1);
        for (auto &p : x->parameters()) {
            TraverseDecl(p);
            ASR::symbol_t* arg_sym = ASR::down_cast<ASR::symbol_t>(tmp);
            args.push_back(al,
                ASRUtils::EXPR(ASR::make_Var_t(al, arg_sym->base.loc, arg_sym)));
        }
        if( x->hasBody() ) {
            TraverseStmt(x->getBody());
        }
        ASR::ttype_t* return_type = ClangTypeToASRType(x->getReturnType());
        ASR::symbol_t* return_sym = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, Lloc(x),
            current_scope, s2c(al, "__return_var"), nullptr, 0, ASR::intentType::Local, nullptr, nullptr,
            ASR::storage_typeType::Default, return_type, nullptr, ASR::abiType::Source, ASR::accessType::Public,
            ASR::presenceType::Required, false));
        current_scope->add_symbol("__return_var", return_sym);
        ASR::expr_t* return_var = ASRUtils::EXPR(ASR::make_Var_t(al, return_sym->base.loc, return_sym));
        tmp = ASRUtils::make_Function_t_util(al, Lloc(x), current_scope, s2c(al, name), nullptr, 0,
            args.p, args.size(), nullptr, 0, return_var, ASR::abiType::Source, ASR::accessType::Public,
            ASR::deftypeType::Implementation, nullptr, false, false, false, false, false, nullptr, 0,
            false, false, false);
        parent_scope->add_symbol(name, ASR::down_cast<ASR::symbol_t>(tmp));
        current_scope = parent_scope;
        return true;
    }

    bool TraverseCompoundStmt(clang::CompoundStmt *x) {
        std::string tmp = "(CompoundStmt " + loc(x) + " [";
        for (auto &s : x->body()) {
            TraverseStmt(s);
            tmp += ast + " ";
        }
        tmp += "])";
        ast = tmp;
        return true;
    }

    bool TraverseVarDecl(clang::VarDecl *x) {
        std::string name = x->getName().str();
        std::string type = clang::QualType::getAsString(
            x->getType().split(), Context->getPrintingPolicy());

        Location l;
        l.first = 1; l.last = 1;

        ASR::intentType intent = ASR::intentType::Local;
        ASR::abiType current_procedure_abi_type = ASR::abiType::Source;
        ASR::ttype_t *asr_type = ASR::down_cast<ASR::ttype_t>(ASR::make_Integer_t(al, l, 4));
        ASR::symbol_t *v = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, l,
            current_scope, s2c(al, name), nullptr,
            0, intent, nullptr, nullptr,
            ASR::storage_typeType::Default, asr_type, nullptr,
            current_procedure_abi_type, ASR::accessType::Public,
            ASR::presenceType::Required, false));
        current_scope->add_symbol(name, v);

        std::string tmp = "(VarDecl " + loc(x) + " ";
        tmp += name + " " + type + " ";
        clang::Expr *init = x->getInit();
        if (init) {
            TraverseStmt(init);
            tmp += ast;
        } else {
            tmp += "()";
        }
        tmp += ")";
        ast = tmp;
        return true;
    }

    bool TraverseParmVarDecl(clang::ParmVarDecl *x) {
        std::string name = x->getName().str();
        std::string type = clang::QualType::getAsString(
            x->getType().split(), Context->getPrintingPolicy());
        std::string tmp = "(ParmVarDecl " + loc(x) + " ";
        tmp += name + " " + type + " ";
        clang::Expr *init = x->getDefaultArg();
        if (init) {
            TraverseStmt(init);
            tmp += ast;
        } else {
            tmp += "()";
        }
        tmp += ")";
        ast = tmp;
        return true;
    }

    bool TraverseImplicitCastExpr(clang::ImplicitCastExpr *x) {
        std::string name = x->getCastKindName();
        std::string type = clang::QualType::getAsString(
            x->getType().split(), Context->getPrintingPolicy());
        std::string tmp = "(ImplicitCastExpr " + loc(x) + " ";
        tmp += name + " " + type + " ";
        TraverseStmt(x->getSubExpr());
        tmp += ast;
        tmp += ")";
        ast = tmp;
        return true;
    }

    bool TraverseParenExpr(clang::ParenExpr *x) {
        std::string tmp = "(ParenExpr " + loc(x) + " ";
        TraverseStmt(x->getSubExpr());
        tmp += ast;
        tmp += ")";
        ast = tmp;
        return true;
    }

    bool TraverseBinaryOperator(clang::BinaryOperator *x) {
        std::string tmp = "(BinaryOperator " + loc(x) + " ";
        clang::BinaryOperatorKind op = x->getOpcode();
        switch (op) {
            case clang::BO_Add: {
                tmp += "+";
                break;
            }
            case clang::BO_Sub: {
                tmp += "-";
                break;
            }
            case clang::BO_Mul: {
                tmp += "*";
                break;
            }
            case clang::BO_Div: {
                tmp += "/";
                break;
            }
            case clang::BO_EQ: {
                tmp += "==";
                break;
            }
            case clang::BO_Assign: {
                tmp += "=";
                break;
            }
            default: {
                throw std::runtime_error("BinaryOperator kind not supported");
                break;
            }
        }
        tmp += " ";
        TraverseStmt(x->getLHS());
        tmp += ast + " ";
        TraverseStmt(x->getRHS());
        tmp += ast + ")";
        ast = tmp;
        return true;
    }

    bool TraverseCallExpr(clang::CallExpr *x) {
        std::string tmp = "(CallExpr " + loc(x) + " ";
        tmp += " ";
        TraverseStmt(x->getCallee());
        tmp += ast + " [";
        for (auto *p : x->arguments()) {
            TraverseStmt(p);
            tmp += ast + " ";
        }
        tmp += "])";
        ast = tmp;
        return true;
    }

    bool TraverseDeclStmt(clang::DeclStmt *x) {
        std::string tmp = "(DeclStmt " + loc(x) + " ";
        if (x->isSingleDecl()) {
            TraverseDecl(x->getSingleDecl());
            tmp += ast;
        } else {
            throw std::runtime_error("DeclGroup not supported");
        }
        tmp += ")";
        ast = tmp;
        return true;
    }

    bool TraverseDeclRefExpr(clang::DeclRefExpr *x) {
        std::string kind = x->getDecl()->getDeclKindName();
        std::string name
            = clang::dyn_cast<clang::NamedDecl>(x->getDecl())->getName().str();
        std::string type = clang::QualType::getAsString(
            x->getType().split(), Context->getPrintingPolicy());
        std::string tmp = "(DeclRefExpr " + loc(x) + " ";
        tmp += kind + " " + name + " " + type + ")";
        ast = tmp;
        return true;
    }

    bool TraverseIntegerLiteral(clang::IntegerLiteral *x) {
        uint64_t i = x->getValue().getLimitedValue();
        std::string tmp = "(IntegerLiteral " + loc(x) + " ";
        tmp += std::to_string(i) + ")";
        ast = tmp;
        return true;
    }

    bool TraverseStringLiteral(clang::StringLiteral *x) {
        std::string s = x->getString().str();
        std::string tmp = "(StringLiteral " + loc(x) + " ";
        tmp += "'" + s + "')";
        ast = tmp;
        return true;
    }

    bool TraverseReturnStmt(clang::ReturnStmt *x) {
        std::string tmp = "(ReturnStmt " + loc(x) + " ";
        TraverseStmt(x->getRetValue());
        tmp += ast + ")";
        ast = tmp;
        return true;
    }

    bool TraverseTypedefDecl(clang::TypedefDecl *x) {
        std::string name = x->getName().str();
        std::string type = x->getUnderlyingType().getAsString();
        std::string tmp = "(TypedefDecl " + loc(x) + " ";
        tmp += name + " " + type + ")";
        ast = tmp;
        return true;
    }

    bool TraverseBuiltinType(clang::BuiltinType *x) {
        std::string type = x->getName(Context->getPrintingPolicy()).str();
        std::string tmp = "(BuiltinType " + type + ")";
        ast = tmp;
        return true;
    }

    bool TraversePointerType(clang::PointerType *x) {
        /*
        std::string type = clang::QualType::getAsString(x->getPointeeType(), Context->getPrintingPolicy());
        */
        std::string tmp = "(PointerType )" /*+ type + ")"*/;
        ast = tmp;
        return true;
    }

    bool TraverseRecordType(clang::RecordType *x) {
        clang::RecordDecl *decl = x->getDecl();
        TraverseRecordDecl(decl);
        std::string tmp = "(RecordType " + ast + ")";
        ast = tmp;
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
