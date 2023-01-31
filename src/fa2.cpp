#include <iostream>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

#include <libasr/alloc.h>
#include <libasr/asr_scopes.h>
#include <libasr/asr.h>
#include <libasr/string_utils.h>
#include <libasr/asr_utils.h>

namespace LFortran {

class ASRPickleVisitor :
    public ASR::PickleBaseVisitor<ASRPickleVisitor>
{
public:
    bool show_intrinsic_modules;

    std::string get_str() {
        return s;
    }
    void visit_symbol(const ASR::symbol_t &x) {
        s.append(ASRUtils::symbol_parent_symtab(&x)->get_counter());
        s.append(" ");
        if (use_colors) {
            s.append(color(fg::yellow));
        }
        s.append(ASRUtils::symbol_name(&x));
        if (use_colors) {
            s.append(color(fg::reset));
        }
    }
    void visit_IntegerConstant(const ASR::IntegerConstant_t &x) {
        s.append("(");
        if (use_colors) {
            s.append(color(style::bold));
            s.append(color(fg::magenta));
        }
        s.append("IntegerConstant");
        if (use_colors) {
            s.append(color(fg::reset));
            s.append(color(style::reset));
        }
        s.append(" ");
        if (use_colors) {
            s.append(color(fg::cyan));
        }
        s.append(std::to_string(x.m_n));
        if (use_colors) {
            s.append(color(fg::reset));
        }
        s.append(" ");
        this->visit_ttype(*x.m_type);
        s.append(")");
    }
};

std::string pickle(ASR::asr_t &asr, bool colors, bool indent,
        bool show_intrinsic_modules) {
    ASRPickleVisitor v;
    v.use_colors = colors;
    v.indent = indent;
    v.show_intrinsic_modules = show_intrinsic_modules;
    v.visit_asr(asr);
    return v.get_str();
}

std::string pickle(ASR::TranslationUnit_t &asr, bool colors, bool indent, bool show_intrinsic_modules) {
    return pickle((ASR::asr_t &)asr, colors, indent, show_intrinsic_modules);
}


}

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static llvm::cl::extrahelp
    CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static llvm::cl::extrahelp MoreHelp("\nMore help text...\n");

// The easiest source of documentation is to read these two source files:
//
// https://github.com/llvm/llvm-project/blob/1b9ba5856add7d557a5c1100f9e3033ba54e7efe/clang/include/clang/AST/TextNodeDumper.h
// https://github.com/llvm/llvm-project/blob/1b9ba5856add7d557a5c1100f9e3033ba54e7efe/clang/lib/AST/TextNodeDumper.cpp

namespace LFortran {

class FindNamedClassVisitor
    : public clang::RecursiveASTVisitor<FindNamedClassVisitor> {
public:
    std::string ast;
    SymbolTable *current_scope=nullptr;
    Allocator al;

    explicit FindNamedClassVisitor(clang::ASTContext *Context)
        : Context(Context), al{4*1024} {}

    template <typename T>
    std::string loc(T *x) {
        uint64_t first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
        uint64_t last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
        return std::to_string(first) + ":" + std::to_string(last);
    }

    bool TraverseTranslationUnitDecl(clang::TranslationUnitDecl *x) {
        SymbolTable *parent_scope = al.make_new<SymbolTable>(nullptr);
        current_scope = parent_scope;
        Location l;
        l.first = 1; l.last = 1;
        ASR::asr_t *tu = ASR::make_TranslationUnit_t(al, l,
            current_scope, nullptr, 0);

        x->dump();
        std::string tmp = "(TranslationUnitDecl " + loc(x) + " [";
        for (auto D = x->decls_begin(), DEnd = x->decls_end(); D != DEnd; ++D) {
            ast = "";
            TraverseDecl(*D);
            if (ast != "") tmp += ast;
        }
        tmp += "])";
        ast = tmp;

        std::cout << pickle(*tu, true, true, true) << std::endl;
        return true;
    }

    bool TraverseFunctionDecl(clang::FunctionDecl *x) {
        std::string name = x->getName().str();
        std::string type = clang::QualType::getAsString(
            x->getType().split(), Context->getPrintingPolicy());
        std::string tmp = "(FunctionDecl " + loc(x) + " ";
        tmp += name + " \"" + type + "\" [";
        for (auto &p : x->parameters()) {
            TraverseDecl(p);
            tmp += ast + " ";
        }
        tmp += "] ";
        TraverseStmt(x->getBody());
        tmp += ast;
        tmp += ")";
        ast = tmp;
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
        ASR::ttype_t *asr_type = ASR::down_cast<ASR::ttype_t>(ASR::make_Integer_t(al, l, 4, nullptr, 0));
        ASR::symbol_t *v = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, l,                                          
            current_scope, LFortran::s2c(al, name), nullptr,
            0, intent, nullptr, nullptr,
            LFortran::ASR::storage_typeType::Default, asr_type,
            current_procedure_abi_type, LFortran::ASR::Public,
            LFortran::ASR::presenceType::Required, false));
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

}

class FindNamedClassConsumer : public clang::ASTConsumer {
public:
    explicit FindNamedClassConsumer(clang::ASTContext *Context)
        : Visitor(Context) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
        std::cout << Visitor.ast << std::endl;
    }

private:
    LFortran::FindNamedClassVisitor Visitor;
};

class FindNamedClassAction : public clang::ASTFrontendAction {
public:
    virtual std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &Compiler,
                      llvm::StringRef InFile) {
        return std::make_unique<FindNamedClassConsumer>(
            &Compiler.getASTContext());
    }
};

int main(int argc, const char **argv) {
    auto ExpectedParser = clang::tooling::CommonOptionsParser::create(
        argc, argv, MyToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    clang::tooling::CommonOptionsParser &OptionsParser = ExpectedParser.get();
    clang::tooling::ClangTool Tool(OptionsParser.getCompilations(),
                                   OptionsParser.getSourcePathList());

    std::cout << "Start" << std::endl;
    std::cout << std::endl;
    return Tool.run(
        clang::tooling::newFrontendActionFactory<FindNamedClassAction>().get());
}
