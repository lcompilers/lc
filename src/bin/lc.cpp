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
#include <libasr/codegen/c_evaluator.h>
#include <libasr/codegen/asr_to_c.h>
#include <libasr/codegen/asr_to_cpp.h>
#include <libasr/codegen/asr_to_fortran.h>
#include <libasr/codegen/asr_to_llvm.h>
#include <libasr/codegen/asr_to_wasm.h>
#include <libasr/codegen/wasm_to_wat.h>

#include <lc/clang_ast_to_asr.h>
#include <lc/utils.h>

#include <iostream>

using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory ClangCheckCategory("clang-check options");
static const opt::OptTable &Options = getDriverOptTable();

static cl::opt<std::string> ASTDumpFile("ast-dump-file",
    cl::desc("dump the AST output in the specified file"), cl::cat(ClangCheckCategory));
static cl::opt<bool> ASTList("ast-list",
    cl::desc(Options.getOptionHelpText(options::OPT_ast_list)), cl::cat(ClangCheckCategory));
static cl::opt<bool> ASTPrint("ast-print",
    cl::desc(Options.getOptionHelpText(options::OPT_ast_print)), cl::cat(ClangCheckCategory));
static cl::opt<std::string> ASTDumpFilter("ast-dump-filter",
    cl::desc(Options.getOptionHelpText(options::OPT_ast_dump_filter)), cl::cat(ClangCheckCategory));

// LC options
static cl::opt<bool>
    ShowAST("show-ast", cl::desc("dump the AST"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    ShowASR("show-asr", cl::desc("dump the ASR"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    NoIndent("no-indent", cl::desc("do not indent output"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    NoColor("no-color", cl::desc("do not use color for ASR"), cl::cat(ClangCheckCategory));
static cl::opt<std::string>
    ArgO("o",
    cl::desc(Options.getOptionHelpText(options::OPT_o)), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    ArgC("c",
    cl::desc(Options.getOptionHelpText(options::OPT_c)), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    ShowWAT("show-wat",
    cl::desc("Show WAT (WebAssembly Text Format) and exit"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    ShowC("show-c",
    cl::desc("Show C translation source for the given file and exit"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    ShowCPP("show-cpp",
    cl::desc("Show C++ translation source for the given file and exit"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    ShowFortran("show-fortran",
    cl::desc("Show Fortran translation source for the given file and exit"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    ShowLLVM("show-llvm",
    cl::desc("Show LLVM IR for the given file and exit"), cl::cat(ClangCheckCategory));
static cl::opt<std::string>
    ArgBackend("backend",
    cl::desc("Select a backend (llvm, wasm, c, cpp, fortran)"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    GetRtlHeaderDir("get-rtl-header-dir",
    cl::desc("Print the path to the runtime library header file"), cl::cat(ClangCheckCategory));
static cl::opt<bool>
    GetRtlDir("get-rtl-dir",
    cl::desc("Print the path to the runtime library file"), cl::cat(ClangCheckCategory));

enum class Backend {
    llvm, wasm, c, cpp, x86, fortran
};

std::map<std::string, Backend> string_to_backend = {
    {"", Backend::llvm}, // by default it is llvm
    {"llvm", Backend::llvm},
    {"wasm", Backend::wasm},
    {"c", Backend::c},
    {"cpp", Backend::cpp},
    {"fortran", Backend::fortran},
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

    std::string infile;

    ClangCheckActionFactory(std::string infile_): infile(infile_) {}

    std::unique_ptr<clang::ASTConsumer> newASTConsumer() {
        if (ASTList) {
            return clang::CreateASTDeclNodeLister();
        } else if ( ShowAST ) {
            llvm::raw_fd_ostream* llvm_fd_ostream = nullptr;
            if( ASTDumpFile.size() > 0 ) {
                std::error_code errorCode;
                llvm_fd_ostream = new llvm::raw_fd_ostream(ASTDumpFile, errorCode);
            }
            std::unique_ptr<llvm::raw_ostream> llvm_ostream(llvm_fd_ostream);
            return clang::CreateASTDumper(std::move(llvm_ostream), ASTDumpFilter,
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
    compiler_options.po.realloc_lhs = true; \
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

int emit_cpp(Allocator &al, std::string &infile, LCompilers::ASR::TranslationUnit_t* asr) {
    DeclareLCompilersUtilVars;

    // Apply ASR passes
    LCompilers::PassManager pass_manager;
    pass_manager.use_default_passes(true);
    pass_manager.apply_passes(al, asr, compiler_options.po, diagnostics);

    auto res = LCompilers::asr_to_cpp(al, *asr, diagnostics, compiler_options, 0);
    std::cerr << diagnostics.render(lm, compiler_options);
    if (!res.ok) {
        LCOMPILERS_ASSERT(diagnostics.has_error())
        return 1;
    }
    std::cout << res.result;
    return 0;
}

int emit_fortran(Allocator &al, std::string &infile, LCompilers::ASR::TranslationUnit_t* asr) {
    DeclareLCompilersUtilVars;

    auto res = LCompilers::asr_to_fortran(*asr, diagnostics, false, 4);
    std::cerr << diagnostics.render(lm, compiler_options);
    if (!res.ok) {
        LCOMPILERS_ASSERT(diagnostics.has_error())
        return 1;
    }
    std::cout << res.result;
    return 0;
}

int emit_llvm(Allocator &al, std::string &infile, LCompilers::ASR::TranslationUnit_t* asr) {
    DeclareLCompilersUtilVars;

    LCompilers::PassManager pass_manager;
    pass_manager.use_default_passes();
    LCompilers::LLVMEvaluator e(compiler_options.target);

    std::string run_fn = "__lfortran_evaluate_0";

    std::unique_ptr<LCompilers::LLVMModule> m;
    LCompilers::Result<std::unique_ptr<LCompilers::LLVMModule>> res
        = asr_to_llvm(*asr, diagnostics, e.get_context(), al,
            pass_manager, compiler_options, run_fn, infile);
    if (res.ok) {
        m = std::move(res.result);
    } else {
        LCOMPILERS_ASSERT(diagnostics.has_error())
        return 1;
    }
    std::cerr << diagnostics.render(lm, compiler_options);

    if (compiler_options.po.fast) {
        e.opt(*m->m_m);
    }

    std::cout << m->str();
    return 0;
}

int compile_to_binary_wasm(
    Allocator &al, const std::string &infile,
    const std::string &outfile, LCompilers::ASR::TranslationUnit_t* asr) {
    DeclareLCompilersUtilVars;
    LCompilers::Result<int> res = LCompilers::asr_to_wasm(*asr, al,  outfile, false, diagnostics, compiler_options);
    if (!res.ok) {
        LCOMPILERS_ASSERT(diagnostics.has_error())
        return 1;
    }
    return 0;
}

int compile_to_c(Allocator &al, const std::string &infile,
    const std::string &outfile, LCompilers::ASR::TranslationUnit_t* asr) {
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
    FILE *fp;
    fp = fopen(outfile.c_str(), "w");
    fputs(res.result.c_str(), fp);
    fclose(fp);
    return 0;
}

int compile_to_cpp(Allocator &al, const std::string &infile,
    const std::string &outfile, LCompilers::ASR::TranslationUnit_t* asr) {
    DeclareLCompilersUtilVars;

    // Apply ASR passes
    LCompilers::PassManager pass_manager;
    pass_manager.use_default_passes(true);
    pass_manager.apply_passes(al, asr, compiler_options.po, diagnostics);

    auto res = LCompilers::asr_to_cpp(al, *asr, diagnostics, compiler_options, 0);
    std::cerr << diagnostics.render(lm, compiler_options);
    if (!res.ok) {
        LCOMPILERS_ASSERT(diagnostics.has_error())
        return 1;
    }
    FILE *fp;
    fp = fopen(outfile.c_str(), "w");
    fputs(res.result.c_str(), fp);
    fclose(fp);
    return 0;
}

int compile_to_fortran(Allocator &al, const std::string &infile,
    const std::string &outfile, LCompilers::ASR::TranslationUnit_t* asr) {
    DeclareLCompilersUtilVars;

    auto res = LCompilers::asr_to_fortran(*asr, diagnostics, false, 4);
    std::cerr << diagnostics.render(lm, compiler_options);
    if (!res.ok) {
        LCOMPILERS_ASSERT(diagnostics.has_error())
        return 1;
    }
    FILE *fp;
    fp = fopen(outfile.c_str(), "w");
    fputs(res.result.c_str(), fp);
    fclose(fp);
    return 0;
}

int compile_to_binary_object(Allocator &al, const std::string &infile,
    const std::string &outfile, LCompilers::ASR::TranslationUnit_t* asr) {
    DeclareLCompilersUtilVars;

    LCompilers::PassManager pass_manager;
    pass_manager.use_default_passes();
    LCompilers::LLVMEvaluator e(compiler_options.target);

    std::string run_fn = "__lfortran_evaluate_0";

    std::unique_ptr<LCompilers::LLVMModule> m;
    LCompilers::Result<std::unique_ptr<LCompilers::LLVMModule>> res
        = asr_to_llvm(*asr, diagnostics, e.get_context(), al,
            pass_manager, compiler_options, run_fn, infile);
    if (res.ok) {
        m = std::move(res.result);
    } else {
        LCOMPILERS_ASSERT(diagnostics.has_error())
        return 1;
    }
    std::cerr << diagnostics.render(lm, compiler_options);

    if (compiler_options.po.fast) {
        e.opt(*m->m_m);
    }

    e.save_object_file(*(m->m_m), outfile);
    return 0;
}

int compile_c_to_object_file(const std::string &infile,
        const std::string &outfile) {

    std::string CC = "cc";
    char *env_CC = std::getenv("LFORTRAN_CC");
    if (env_CC) CC = env_CC;
    std::string options;
    options += "-I" + LCompilers::LC::get_runtime_library_c_header_dir() + " ";
    std::string cmd = CC + " " + options + " -o " + outfile + " -c " + infile;
    int err = system(cmd.c_str());
    if (err) {
        std::cout << "The command '" + cmd + "' failed." << std::endl;
        return 1;
    }
    return 0;
}

int compile_cpp_to_object_file(const std::string &infile,
        const std::string &outfile) {

    std::string CXX = "g++";
    std::string options;
    options += "-I" + LCompilers::LC::get_runtime_library_c_header_dir() + " ";
    std::string cmd = CXX + " " + options + " -o " + outfile + " -c " + infile;
    int err = system(cmd.c_str());
    if (err) {
        std::cout << "The command '" + cmd + "' failed." << std::endl;
        return 1;
    }
    return 0;
}

int compile_fortran_to_object_file(const std::string &infile,
        const std::string &outfile) {

    std::string cmd = "gfortran -fno-backtrace -o " + outfile + " -c " + infile;
    int err = system(cmd.c_str());
    if (err) {
        std::cout << "The command '" + cmd + "' failed." << std::endl;
        return 1;
    }
    return 0;
}

// infile is an object file
// outfile will become the executable
int link_executable(const std::vector<std::string> &infiles,
    const std::string &outfile,
    const std::string &runtime_library_dir, Backend backend,
    bool static_executable, bool link_with_gcc, bool kokkos,
    LCompilers::CompilerOptions &compiler_options)
{
    /*
    The `gcc` line for dynamic linking that is constructed below:

    gcc -o $outfile $infile \
        -Lsrc/runtime -Wl,-rpath=src/runtime -llc_runtime

    is equivalent to the following:

    ld -o $outfile $infile \
        -Lsrc/runtime -rpath=src/runtime -llc_runtime \
        -dynamic-linker /lib64/ld-linux-x86-64.so.2  \
        /usr/lib/x86_64-linux-gnu/Scrt1.o /usr/lib/x86_64-linux-gnu/libc.so

    and this for static linking:

    gcc -static -o $outfile $infile \
        -Lsrc/runtime -Wl,-rpath=src/runtime -llc_runtime_static

    is equivalent to:

    ld -o $outfile $infile \
        -Lsrc/runtime -rpath=src/runtime -llc_runtime_static \
        /usr/lib/x86_64-linux-gnu/crt1.o /usr/lib/x86_64-linux-gnu/crti.o \
        /usr/lib/x86_64-linux-gnu/libc.a \
        /usr/lib/gcc/x86_64-linux-gnu/7/libgcc_eh.a \
        /usr/lib/x86_64-linux-gnu/libc.a \
        /usr/lib/gcc/x86_64-linux-gnu/7/libgcc.a \
        /usr/lib/x86_64-linux-gnu/crtn.o

    This was tested on Ubuntu 18.04.

    The `gcc` and `ld` approaches are equivalent except:

    1. The `gcc` command knows how to find and link the `libc` library,
       while in `ld` we must do that manually
    2. For dynamic linking, we must also specify the dynamic linker for `ld`

    Notes:

    * We can use `lld` to do the linking via the `ld` approach, so `ld` is
      preferable if we can mitigate the issues 1. and 2.
    * If we ship our own libc (such as musl), then we know how to find it
      and link it, which mitigates the issue 1.
    * If we link `musl` statically, then issue 2. does not apply.
    * If we link `musl` dynamically, then we have to find the dynamic
      linker (doable), which mitigates the issue 2.

    One way to find the default dynamic linker is by:

        $ readelf -e /bin/bash | grep ld-linux
            [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]

    There are probably simpler ways.
    */

#ifdef HAVE_LFORTRAN_LLVM
    std::string t = (compiler_options.target == "") ? LCompilers::LLVMEvaluator::get_default_target_triple() : compiler_options.target;
#else
    std::string t = (compiler_options.platform == LCompilers::Platform::Windows) ? "x86_64-pc-windows-msvc" : compiler_options.target;
#endif
    size_t dot_index = outfile.find_last_of(".");
    std::string file_name = outfile.substr(0, dot_index);
    std::string extra_runtime_linker_path;
    if (!compiler_options.runtime_linker_paths.empty()) {
        for (auto &s: compiler_options.runtime_linker_paths) {
            extra_runtime_linker_path += " -Wl,-rpath," + s;
        }
    }
    if (backend == Backend::llvm) {
        std::string run_cmd = "", compile_cmd = "";
        if (t == "x86_64-pc-windows-msvc") {
            compile_cmd = "link /NOLOGO /OUT:" + outfile + " ";
            for (auto &s : infiles) {
                compile_cmd += s + " ";
            }
            compile_cmd += runtime_library_dir + "\\lc_runtime_static.lib";
            run_cmd = outfile;
        } else {
            std::string CC;
            std::string base_path = "\"" + runtime_library_dir + "\"";
            std::string options;
            std::string runtime_lib = "lc_runtime";

            bool is_macos = (compiler_options.platform == LCompilers::Platform::macOS_Intel ||
                compiler_options.platform == LCompilers::Platform::macOS_ARM);

            if (link_with_gcc || is_macos) {
                CC = "gcc";
            } else {
                CC = "clang";
            }

            char *env_CC = std::getenv("LFORTRAN_CC");
            if (env_CC) CC = env_CC;

            if (compiler_options.target != "" && !link_with_gcc) {
                options = " -target " + compiler_options.target;
            }

            if (static_executable) {
                if (compiler_options.platform != LCompilers::Platform::macOS_Intel
                && compiler_options.platform != LCompilers::Platform::macOS_ARM) {
                    options += " -static ";
                }
                runtime_lib = "lc_runtime_static";
            }
            compile_cmd = CC + options + " -o " + outfile + " ";
            for (auto &s : infiles) {
                compile_cmd += s + " ";
            }
            compile_cmd += + " -L"
                + base_path + " -Wl,-rpath," + base_path;
            if (!extra_runtime_linker_path.empty()) {
                compile_cmd += extra_runtime_linker_path;
            }
            compile_cmd += " -l" + runtime_lib + " -lm";
            run_cmd = "./" + outfile;
        }
        int err = system(compile_cmd.c_str());
        if (err) {
            std::cout << "The command '" + compile_cmd + "' failed." << std::endl;
            return 10;
        }

#ifdef HAVE_RUNTIME_STACKTRACE
        if (compiler_options.emit_debug_info) {
            // TODO: Replace the following hardcoded part
            std::string cmd = "";
#ifdef HAVE_LFORTRAN_MACHO
            cmd += "dsymutil " + file_name + ".out && llvm-dwarfdump --debug-line "
                + file_name + ".out.dSYM > ";
#else
            cmd += "llvm-dwarfdump --debug-line " + file_name + ".out > ";
#endif
            std::string libasr_path = LCompilers::LC::get_runtime_library_c_header_dir() + "/../";
            cmd += file_name + "_ldd.txt && (" + libasr_path + "dwarf_convert.py "
                + file_name + "_ldd.txt " + file_name + "_lines.txt "
                + file_name + "_lines.dat && " + libasr_path + "dat_convert.py "
                + file_name + "_lines.dat)";
            int status = system(cmd.c_str());
            if ( status != 0 ) {
                std::cerr << "Error in creating the files used to generate "
                    "the debug information. This might be caused because either"
                    " `llvm-dwarfdump` or `Python` are not available. "
                    "Please activate the CONDA environment and compile again.\n";
                return status;
            }
        }
#endif
    } else if (backend == Backend::c) {
        std::string CXX = "gcc";
        std::string cmd = CXX + " -o " + outfile + " ";
        std::string base_path = "\"" + runtime_library_dir + "\"";
        std::string runtime_lib = "lc_runtime";
        // TODO: Remove the following when object file is passed
        // Currently source file is passed
        cmd += "-I" + LCompilers::LC::get_runtime_library_c_header_dir() + " ";
        for (auto &s : infiles) {
            cmd += s + " ";
        }
        cmd += " -L" + base_path
            + " -Wl,-rpath," + base_path;
        if (!extra_runtime_linker_path.empty()) {
            cmd += extra_runtime_linker_path;
        }
        cmd += " -l" + runtime_lib + " -lm";
        int err = system(cmd.c_str());
        if (err) {
            std::cout << "The command '" + cmd + "' failed." << std::endl;
            return 10;
        }
    } else if (backend == Backend::cpp) {
        std::string CXX = "g++";
        std::string options, post_options;
        if (static_executable) {
            options += " -static ";
        }
        if (compiler_options.openmp) {
            options += " -fopenmp ";
        }
        if (kokkos) {
            std::string kokkos_dir = LCompilers::LC::get_kokkos_dir();
            post_options += kokkos_dir + "/lib/libkokkoscontainers.a "
                + kokkos_dir + "/lib/libkokkoscore.a -ldl";
        }
        std::string cmd = CXX + options + " -o " + outfile + " ";
        // TODO: Remove the following when object file is passed
        // Currently source file is passed
        cmd += "-I" + LCompilers::LC::get_runtime_library_c_header_dir() + " ";
        for (auto &s : infiles) {
            cmd += s + " ";
        }
        cmd += " " + post_options + " -lm";
        int err = system(cmd.c_str());
        if (err) {
            std::cout << "The command '" + cmd + "' failed." << std::endl;
            return 10;
        }
    } else if (backend == Backend::x86) {
        std::string cmd = "cp " + infiles[0] + " " + outfile;
        int err = system(cmd.c_str());
        if (err) {
            std::cout << "The command '" + cmd + "' failed." << std::endl;
            return 10;
        }
    } else if (backend == Backend::wasm) {
        // do nothing
    } else if (backend == Backend::fortran) {
        std::string cmd = "gfortran -o " + outfile + " ";
        std::string base_path = "\"" + runtime_library_dir + "\"";
        std::string runtime_lib = "lc_runtime";
        for (auto &s : infiles) {
            cmd += s + " ";
        }
        cmd += " -L" + base_path
            + " -Wl,-rpath," + base_path;
        cmd += " -l" + runtime_lib + " -lm";
        int err = system(cmd.c_str());
        if (err) {
            std::cout << "The command '" + cmd + "' failed." << std::endl;
            return 10;
        }
    } else {
        LCOMPILERS_ASSERT(false);
        return 1;
    }

    if ( compiler_options.arg_o != "" ) {
        return 0;
    }

    std::string run_cmd = "";
    if (backend == Backend::wasm) {
        // for node version less than 16, we need to also provide flag --experimental-wasm-bigint
        run_cmd = "node --experimental-wasi-unstable-preview1 " + outfile + ".js";
    } else if (t == "x86_64-pc-windows-msvc") {
        run_cmd = outfile;
    } else {
        run_cmd = "./" + outfile;
    }
    int err = system(run_cmd.c_str());
    if (err != 0) {
        if (0 < err && err < 256) {
            return err;
        } else {
            return LCompilers::LC::get_exit_status(err);
        }
    }
    return 0;
}

void do_print_rtl_header_dir() {
    std::string rtl_header_dir = LCompilers::LC::get_runtime_library_header_dir();
    std::cout << rtl_header_dir << std::endl;
}

void do_print_rtl_dir() {
    std::string rtl_dir = LCompilers::LC::get_runtime_library_dir();
    std::cout << rtl_dir << std::endl;
}

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ClangCheckCategory);
    if (!ExpectedParser) {
        if (GetRtlHeaderDir) {
            do_print_rtl_header_dir();
            return 0;
        }

        if (GetRtlDir) {
            do_print_rtl_dir();
            return 0;
        }
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }

    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    std::vector<std::string> sourcePaths = OptionsParser.getSourcePathList();
    ClangTool Tool(OptionsParser.getCompilations(), sourcePaths);

    std::string infile = sourcePaths[0];

    // Handle Clang related options in the following
    if (ShowAST || ASTList || ASTPrint) {
        ClangCheckActionFactory CheckFactory(infile);
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

    if (ShowASR) {
        bool indent = !NoIndent, color = !NoColor;
        std::cout<< LCompilers::pickle(*tu, color, indent, true) << std::endl;
        return 0;
    } else if (ShowWAT) {
        return emit_wat(al, infile, (LCompilers::ASR::TranslationUnit_t*)tu);
    } else if (ShowC) {
        return emit_c(al, infile, (LCompilers::ASR::TranslationUnit_t*)tu);
    } else if (ShowCPP) {
        return emit_cpp(al, infile, (LCompilers::ASR::TranslationUnit_t*)tu);
    } else if (ShowFortran) {
        return emit_fortran(al, infile, (LCompilers::ASR::TranslationUnit_t*)tu);
    } else if (ShowLLVM) {
        return emit_llvm(al, infile, (LCompilers::ASR::TranslationUnit_t*)tu);
    }

    // compile to binary
    std::string outfile = construct_outfile(infile, ArgO);

    Backend backend;
    if (string_to_backend.find(ArgBackend) != string_to_backend.end()) {
        backend = string_to_backend[ArgBackend];
    }

    std::string tmp_file;
    if (backend == Backend::wasm) {
        status = compile_to_binary_wasm(al, infile, outfile, (LCompilers::ASR::TranslationUnit_t*)tu);
    } else if (backend == Backend::c) {
        tmp_file = outfile + "__generated__.c";
        status = compile_to_c(al, infile, tmp_file, (LCompilers::ASR::TranslationUnit_t*)tu);
    } else if (backend == Backend::cpp) {
        tmp_file = outfile + "__generated__.cpp";
        status = compile_to_cpp(al, infile, tmp_file, (LCompilers::ASR::TranslationUnit_t*)tu);
    } else if (backend == Backend::fortran) {
        tmp_file = outfile + "__generated__.f90";
        status = compile_to_fortran(al, infile, tmp_file, (LCompilers::ASR::TranslationUnit_t*)tu);
    } else if (backend == Backend::llvm) {
        tmp_file = ArgC ? outfile : (outfile + "__generated__.o");
        status = compile_to_binary_object(al, infile, tmp_file, (LCompilers::ASR::TranslationUnit_t*)tu);
    }

    if (status != 0) {
        return status;
    }

    LCompilers::CompilerOptions co;
    co.arg_o = ArgO;

    if (ArgC) {
        // generate object file from source code
        if (backend == Backend::c) {
            status = compile_c_to_object_file(tmp_file, outfile);
        } else if (backend == Backend::cpp) {
            status = compile_cpp_to_object_file(tmp_file, outfile);
        } else if (backend == Backend::fortran) {
            status = compile_fortran_to_object_file(tmp_file, outfile);
        }
        return status;
    }

    std::vector<std::string> infiles = {tmp_file};
    status = link_executable(infiles, outfile, LCompilers::LC::get_runtime_library_dir(), backend, false, false, true, co);
    return status;
}
