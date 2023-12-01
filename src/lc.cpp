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
#include <libasr/assert.h>

#include "clang_ast_to_asr.h"

#include <libasr/pickle.h>

#include <iostream>

using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory ClangCheckCategory("clang-check options");
static const opt::OptTable &Options = getDriverOptTable();

static cl::opt<bool> ASTDump("ast-dump",
    cl::desc(Options.getOptionHelpText(options::OPT_ast_dump)), cl::cat(ClangCheckCategory));
static cl::opt<bool> ASTList("ast-list",
    cl::desc(Options.getOptionHelpText(options::OPT_ast_list)), cl::cat(ClangCheckCategory));
static cl::opt<bool> ASTPrint("ast-print",
    cl::desc(Options.getOptionHelpText(options::OPT_ast_print)), cl::cat(ClangCheckCategory));
static cl::opt<std::string> ASTDumpFilter("ast-dump-filter",
    cl::desc(Options.getOptionHelpText(options::OPT_ast_dump_filter)), cl::cat(ClangCheckCategory));

// LC options
static cl::opt<bool>
    ASRDump("asr-dump", cl::desc("dump the ASR"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    NoIndent("no-indent", cl::desc("do not indent output"), cl::cat(ClangCheckCategory));

class ClangCheckActionFactory {

public:
    std::unique_ptr<clang::ASTConsumer> newASTConsumer() {
        if (ASTList) {
            return clang::CreateASTDeclNodeLister();
        } else if (ASTDump) {
            return clang::CreateASTDumper(nullptr /*Dump to stdout.*/, ASTDumpFilter,
                                          /*DumpDecls=*/true,
                                          /*Deserialize=*/false,
                                          /*DumpLookups=*/false,
                                          /*DumpDeclTypes=*/false,
                                          clang::ADOF_Default);
        } else if (ASTPrint) {
            return clang::CreateASTPrinter(nullptr, ASTDumpFilter);
        }
        return std::make_unique<clang::ASTConsumer>();
    }
};

namespace LCompilers {

    template <typename T>
    class LCompilersFrontendActionFactory : public FrontendActionFactory {

    public:

        Allocator& al;
        ASR::asr_t*& tu;

        LCompilersFrontendActionFactory(Allocator& al_, ASR::asr_t*& tu_):
            al(al_), tu(tu_) {}

        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<T>(al, tu);
        }
    };

    template <typename T>
    FrontendActionFactory* newFrontendActionLCompilersFactory(Allocator& al_, ASR::asr_t*& tu_) {
        return new LCompilersFrontendActionFactory<T>(al_, tu_);
    }

}

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ClangCheckCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }

    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    ClangCheckActionFactory CheckFactory;

    Allocator al(4*1024);
    LCompilers::ASR::asr_t* tu = nullptr;

    // Choose the correct factory based on the selected mode.
    int status;
    if (ASRDump) {
        FrontendActionFactory* FrontendFactory;
        FrontendFactory = LCompilers::newFrontendActionLCompilersFactory<LCompilers::FindNamedClassAction>(al, tu);
        status = Tool.run(FrontendFactory);
        bool indent = true;
        if( NoIndent ) {
            indent = false;
        }
        std::cout<< LCompilers::pickle(*tu, true, indent, true) << std::endl;
    } else {
        status = Tool.run(newFrontendActionFactory(&CheckFactory).get());
    }

    return status;
}
