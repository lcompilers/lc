#include <iostream>
#include <fstream>

#include <llvm/IR/LLVMContext.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/InstSimplifyPass.h>
#include <llvm/Transforms/Vectorize.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Instrumentation/AddressSanitizer.h>
#include <llvm/Transforms/Instrumentation/ThreadSanitizer.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/ExecutionEngine/ObjectCache.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Target/TargetOptions.h>
#if LLVM_VERSION_MAJOR >= 14
#    include <llvm/MC/TargetRegistry.h>
#else
#    include <llvm/Support/TargetRegistry.h>
#endif
#include <llvm/Support/Host.h>

#include <libasr/codegen/c_evaluator.h>
#include <libasr/codegen/asr_to_llvm.h>
#include <libasr/codegen/asr_to_cpp.h>
#include <libasr/exception.h>
#include <libasr/asr.h>
#include <libasr/string_utils.h>


namespace LCompilers {

// Extracts the integer from APInt.
// APInt does not seem to have this functionality, so we implement it here.
uint64_t APInt_getint(const llvm::APInt &i) {
    // The APInt::isSingleWord() is private, but we can emulate it:
    bool isSingleWord = !i.needsCleanup();
    if (isSingleWord) {
        return *i.getRawData();
    } else {
        throw std::runtime_error("APInt too large to fit uint64_t");
    }
}

LLVMModule::LLVMModule(std::unique_ptr<llvm::Module> m)
{
    m_m = std::move(m);
}

LLVMModule::~LLVMModule() = default;

std::string LLVMModule::str()
{
    return LLVMEvaluator::module_to_string(*m_m);
}

std::string LLVMModule::get_return_type(const std::string &fn_name)
{
    llvm::Module *m = m_m.get();
    llvm::Function *fn = m->getFunction(fn_name);
    if (!fn) {
        return "none";
    }
    llvm::Type *type = fn->getReturnType();
    if (type->isFloatTy()) {
        return "real4";
    } else if (type->isDoubleTy()) {
        return "real8";
    } else if (type->isIntegerTy(32)) {
        return "integer4";
    } else if (type->isIntegerTy(64)) {
        return "integer8";
    } else if (type->isStructTy()) {
        llvm::StructType *st = llvm::cast<llvm::StructType>(type);
        if (st->hasName()) {
            if (startswith(std::string(st->getName()), "complex_4")) {
                return "complex4";
            } else if (startswith(std::string(st->getName()), "complex_8")) {
                return "complex8";
            } else {
                throw LCompilersException("LLVMModule::get_return_type(): Struct return type `" + std::string(st->getName()) + "` not supported");
            }
        } else {
            throw LCompilersException("LLVMModule::get_return_type(): Noname struct return type not supported");
        }
    } else if (type->isVectorTy()) {
        // Used for passing complex_4 on some platforms
        return "complex4";
    } else if (type->isVoidTy()) {
        return "void";
    } else {
        throw LCompilersException("LLVMModule::get_return_type(): Return type not supported");
    }
}

extern "C" {

float _lfortran_stan(float x);

}

#if LLVM_VERSION_MAJOR >= 16
#    define RM_OPTIONAL_TYPE std::optional
#else
#    define RM_OPTIONAL_TYPE llvm::Optional
#endif

LLVMEvaluator::LLVMEvaluator(const std::string &t)
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

#ifdef HAVE_TARGET_AARCH64
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeAArch64AsmParser();
#endif
#ifdef HAVE_TARGET_X86
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86AsmParser();
#endif
#ifdef HAVE_TARGET_WASM
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyTargetMC();
    LLVMInitializeWebAssemblyAsmPrinter();
    LLVMInitializeWebAssemblyAsmParser();
#endif

    context = std::make_unique<llvm::LLVMContext>();

    if (t != "")
        target_triple = t;
    else
        target_triple = LLVMGetDefaultTargetTriple();

    std::string Error;
    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(target_triple, Error);
    if (!target) {
        throw LCompilersException(Error);
    }
    std::string CPU = "generic";
    std::string features = "";
    llvm::TargetOptions opt;
    RM_OPTIONAL_TYPE<llvm::Reloc::Model> RM = llvm::Reloc::Model::PIC_;
    TM = target->createTargetMachine(target_triple, CPU, features, opt, RM);

    _lfortran_stan(0.5);
}

LLVMEvaluator::~LLVMEvaluator()
{
    context.reset();
}

void LLVMEvaluator::save_object_file(llvm::Module &m, const std::string &filename) {
    m.setTargetTriple(target_triple);
    m.setDataLayout(TM->createDataLayout());

    llvm::legacy::PassManager pass;
    llvm::CodeGenFileType ft = llvm::CGFT_ObjectFile;
    std::error_code EC;
    llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);
    if (EC) {
        throw std::runtime_error("raw_fd_ostream failed");
    }
    if (TM->addPassesToEmitFile(pass, dest, nullptr, ft)) {
        throw std::runtime_error("TargetMachine can't emit a file of this type");
    }
    pass.run(m);
    dest.flush();
}

void LLVMEvaluator::opt(llvm::Module &m) {
    m.setTargetTriple(target_triple);
    m.setDataLayout(TM->createDataLayout());

    llvm::legacy::PassManager mpm;
    mpm.add(new llvm::TargetLibraryInfoWrapperPass(TM->getTargetTriple()));
    mpm.add(llvm::createTargetTransformInfoWrapperPass(TM->getTargetIRAnalysis()));
    llvm::legacy::FunctionPassManager fpm(&m);
    fpm.add(llvm::createTargetTransformInfoWrapperPass(TM->getTargetIRAnalysis()));

    int optLevel = 3;
    int sizeLevel = 0;
    llvm::PassManagerBuilder builder;
    builder.OptLevel = optLevel;
    builder.SizeLevel = sizeLevel;
    builder.Inliner = llvm::createFunctionInliningPass(optLevel, sizeLevel,
        false);
    builder.DisableUnrollLoops = false;
    builder.LoopVectorize = true;
    builder.SLPVectorize = true;
    builder.populateFunctionPassManager(fpm);
    builder.populateModulePassManager(mpm);

    fpm.doInitialization();
    for (llvm::Function &func : m) {
        fpm.run(func);
    }
    fpm.doFinalization();

    mpm.add(llvm::createVerifierPass());
    mpm.run(m);
}

std::string LLVMEvaluator::module_to_string(llvm::Module &m) {
    std::string buf;
    llvm::raw_string_ostream os(buf);
    m.print(os, nullptr);
    os.flush();
    return buf;
}

llvm::LLVMContext &LLVMEvaluator::get_context()
{
    return *context;
}

std::string LLVMEvaluator::get_default_target_triple()
{
    return llvm::sys::getDefaultTargetTriple();
}

} // namespace LCompilers
