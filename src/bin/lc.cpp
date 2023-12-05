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
#include <libasr/pass/pass_manager.h>
#include <libasr/pickle.h>
#include <libasr/codegen/asr_to_c.h>
#include <libasr/codegen/asr_to_wasm.h>
#include <libasr/codegen/wasm_to_wat.h>

#include "clang_ast_to_asr.h"

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
static cl::opt<bool>
    NoColor("no-color", cl::desc("do not use color for ASR"), cl::cat(ClangCheckCategory));
static cl::opt<std::string>
    ArgO("o",
    cl::desc(Options.getOptionHelpText(options::OPT_o)), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    ShowWAT("show-wat",
    cl::desc("Show WAT (WebAssembly Text Format) and exit"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    ShowC("show-c",
    cl::desc("Show C translation source for the given file and exit"), cl::cat(ClangCheckCategory));
static cl::opt<std::string>
    ArgBackend("backend",
    cl::desc("Select a backend (wasm, c)"), cl::cat(ClangCheckCategory));

enum class Backend {
    llvm, wasm, c
};

std::map<std::string, Backend> string_to_backend = {
    {"", Backend::llvm}, // by default it is llvm
    {"llvm", Backend::llvm},
    {"wasm", Backend::wasm},
    {"c", Backend::c},
};

std::string remove_extension(const std::string& filename) {
    size_t lastdot = filename.find_last_of(".");
    if (lastdot == std::string::npos) return filename;
    return filename.substr(0, lastdot);
}

std::string remove_path(const std::string& filename) {
    size_t lastslash = filename.find_last_of("/");
    if (lastslash == std::string::npos) return filename;
    return filename.substr(lastslash+1);
}

std::string construct_outfile(std::string &arg_file, std::string &ArgO) {
    std::string outfile;
    std::string basename;
    basename = remove_extension(arg_file);
    basename = remove_path(basename);
    if (ArgO.size() > 0) {
        outfile = ArgO;
    } else {
        outfile = basename + ".out";
    }
    return outfile;
}

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

#define DeclareLCompilersUtilVars \
    LCompilers::CompilerOptions compiler_options; \
    LCompilers::diag::Diagnostics diagnostics; \
    LCompilers::LocationManager lm; \
    { \
        LCompilers::LocationManager::FileLocations fl; \
        fl.in_filename = infile; \
        lm.files.push_back(fl); \
        std::string input = LCompilers::read_file(infile); \
        lm.init_simple(input); \
        lm.file_ends.push_back(input.size()); \
    } \
    compiler_options.po.always_run = true; \
    compiler_options.po.run_fun = "f"; \
    diagnostics.diagnostics.clear(); \

int emit_wat(Allocator &al, std::string &infile, LCompilers::ASR::TranslationUnit_t *asr) {
    DeclareLCompilersUtilVars;

    LCompilers::Result<LCompilers::Vec<uint8_t>> r2 = LCompilers::asr_to_wasm_bytes_stream(*asr, al, diagnostics, compiler_options);
    std::cerr << diagnostics.render(lm, compiler_options);
    if (!r2.ok) {
        LCOMPILERS_ASSERT(diagnostics.has_error())
        return 1;
    }

    diagnostics.diagnostics.clear();
    LCompilers::Result<std::string> res = LCompilers::wasm_to_wat(r2.result, al, diagnostics);
    std::cerr << diagnostics.render(lm, compiler_options);
    if (!res.ok) {
        LCOMPILERS_ASSERT(diagnostics.has_error())
        return 2;
    }
    std::cout << res.result;
    return 0;
}

int emit_c(Allocator &al, std::string &infile, LCompilers::ASR::TranslationUnit_t* asr) {
    DeclareLCompilersUtilVars;

    // Apply ASR passes
    LCompilers::PassManager pass_manager;
    pass_manager.use_default_passes(true);
    pass_manager.apply_passes(al, asr, compiler_options.po, diagnostics);

    auto res = LCompilers::asr_to_c(al, *asr, diagnostics, compiler_options, 0);
    std::cerr << diagnostics.render(lm, compiler_options);
    if (!res.ok) {
        LCOMPILERS_ASSERT(diagnostics.has_error())
        return 1;
    }
    std::cout << res.result;
    return 0;
}

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ClangCheckCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }

    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    std::vector<std::string> sourcePaths = OptionsParser.getSourcePathList();
    ClangTool Tool(OptionsParser.getCompilations(), sourcePaths);

    // Handle Clang related options in the following
    if (ASTDump || ASTList || ASTPrint) {
        ClangCheckActionFactory CheckFactory;
        int status = Tool.run(newFrontendActionFactory(&CheckFactory).get());
        return status;
    }

    // Handle LC related options
    Allocator al(4*1024);
    LCompilers::ASR::asr_t* tu = nullptr;
    FrontendActionFactory* FrontendFactory;
    FrontendFactory = LCompilers::newFrontendActionLCompilersFactory<LCompilers::FindNamedClassAction>(al, tu);
    int status = Tool.run(FrontendFactory);
    if (status != 0) {
        return status;
    }

    std::string infile = sourcePaths[0];
    if (ASRDump) {
        bool indent = !NoIndent, color = !NoColor;
        std::cout<< LCompilers::pickle(*tu, color, indent, true) << std::endl;
        return 0;
    } else if (ShowWAT) {
        return emit_wat(al, infile, (LCompilers::ASR::TranslationUnit_t*)tu);
    } else if (ShowC) {
        return emit_c(al, infile, (LCompilers::ASR::TranslationUnit_t*)tu);
    }

    // compile to binary
    std::string outfile = construct_outfile(infile, ArgO);

    return status;
}
