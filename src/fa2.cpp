#include <iostream>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static llvm::cl::extrahelp CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static llvm::cl::extrahelp MoreHelp("\nMore help text...\n");

// The easiest source of documentation is to read these two source files:
//
// https://github.com/llvm/llvm-project/blob/1b9ba5856add7d557a5c1100f9e3033ba54e7efe/clang/include/clang/AST/TextNodeDumper.h
// https://github.com/llvm/llvm-project/blob/1b9ba5856add7d557a5c1100f9e3033ba54e7efe/clang/lib/AST/TextNodeDumper.cpp

class FindNamedClassVisitor
  : public clang::RecursiveASTVisitor<FindNamedClassVisitor> {
public:
  explicit FindNamedClassVisitor(clang::ASTContext *Context)
    : Context(Context) {}

  bool TraverseTranslationUnitDecl(clang::TranslationUnitDecl *x) {
    uint64_t first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
    uint64_t last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
    x->dump();
    std::cout << "(TranslationUnitDecl " << first << ":" << last << " ";
    for (auto D = x->decls_begin(), DEnd = x->decls_end(); D != DEnd; ++D) {
        TraverseDecl(*D);
    }
    std::cout << ")";
    return true;
  }

  bool TraverseVarDecl(clang::VarDecl *x) {
    uint64_t first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
    uint64_t last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
    std::cout << "(VarDecl ";
    clang::Expr *init = x->getInit();
    if (init) {
        TraverseStmt(init);
    } else {
        std::cout << "()";
    }
    std::cout << " " << first << ":" << last;
    std::cout << ")";
    return true;
  }

  bool TraverseBinaryOperator(clang::BinaryOperator *x) {
    uint64_t first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
    uint64_t last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
    std::cout << "(BinaryOperator ";
    clang::BinaryOperatorKind op = x->getOpcode();
    switch (op) {
        case clang::BO_Div: {
            std::cout << "/";
            break;
        }
        default : {
            throw std::runtime_error("BinaryOperator kind not supported");
            break;
        }
    }
    std::cout << " ";
    TraverseStmt(x->getLHS());
    std::cout << " ";
    TraverseStmt(x->getRHS());
    std::cout << " " << first << ":" << last << ")";
    return true;
  }

  bool TraverseDeclRefExpr(clang::DeclRefExpr *x) {
    uint64_t first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
    uint64_t last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
    clang::Decl *d = x->getDecl();
    std::string kind = d->getDeclKindName();
    std::string name; 
    std::string type; 
    if (clang::NamedDecl *nd = clang::dyn_cast<clang::NamedDecl>(d)) {
        name = nd->getName();
    }
    if (clang::ValueDecl *vd = clang::dyn_cast<clang::ValueDecl>(d)) {
        clang::SplitQualType T_split = vd->getType().split();
        clang::PrintingPolicy PrintPolicy = Context->getPrintingPolicy();
        type = clang::QualType::getAsString(T_split, PrintPolicy);
    }
    std::cout << "(DeclRefExpr " << kind << " " << name << " " << type;
    std::cout << " " << first << ":" << last;
    std::cout << ")";
    return true;
  }

  bool TraverseIntegerLiteral(clang::IntegerLiteral *x) {
    uint64_t first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
    uint64_t last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
    llvm::APInt v = x->getValue();
    uint64_t i = v.getLimitedValue();
    std::cout << "(IntegerLiteral " << i;
    std::cout << " " << first << ":" << last;
    std::cout << ")";
    return true;
  }

private:
  clang::ASTContext *Context;
};

class FindNamedClassConsumer : public clang::ASTConsumer {
public:
  explicit FindNamedClassConsumer(clang::ASTContext *Context)
    : Visitor(Context) {}

  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());
  }
private:
  FindNamedClassVisitor Visitor;
};

class FindNamedClassAction : public clang::ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return std::make_unique<FindNamedClassConsumer>(&Compiler.getASTContext());
  }
};

int main(int argc, const char **argv) {
    auto ExpectedParser =
        clang::tooling::CommonOptionsParser::create(argc, argv, MyToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    clang::tooling::CommonOptionsParser &OptionsParser = ExpectedParser.get();
    clang::tooling::ClangTool Tool(OptionsParser.getCompilations(),
                    OptionsParser.getSourcePathList());

    std::cout << "Start" << std::endl;
    std::cout << std::endl;
    return Tool.run(clang::tooling::newFrontendActionFactory<FindNamedClassAction>().get());
}
