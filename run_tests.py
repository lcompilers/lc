#!/usr/bin/env python

import sys
import os

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__)))
sys.path.append(os.path.join(ROOT_DIR, "src", "libasr"))

from compiler_tester.tester import color, fg, log, run_test, style, tester_main


def single_test(test, verbose, no_llvm, skip_run_with_dbg, skip_cpptranslate, update_reference,
                no_color, specific_backends=None, excluded_backends=None):
    filename = test["filename"]
    def is_included(backend):
         return test.get(backend, False) \
             and (specific_backends is None or backend in specific_backends) \
             and (excluded_backends is None or backend not in excluded_backends)
    show_verbose = "" if not verbose else "-v"
    ast = is_included("ast")
    asr = is_included("asr")
    asr_json = is_included("asr_json")
    llvm = is_included("llvm")
    llvm_dbg = is_included("llvm_dbg")
    cpp = is_included("cpp")
    c = is_included("c")
    is_cumulative = is_included("cumulative")
    wat = is_included("wat")
    clang_extra_arg = test.get("extra_arg", "")
    disable_main = is_included("disable_main")
    pass_ = test.get("pass", None)
    optimization_passes = ["flip_sign", "div_to_mul", "fma", "sign_from_value",
                           "inline_function_calls", "loop_unroll",
                           "dead_code_removal", "loop_vectorise", "print_list_tuple",
                           "class_constructor"]

    if pass_ and (pass_ not in ["do_loops", "global_stmts"] and
                  pass_ not in optimization_passes):
        raise Exception(f"Unknown pass: {pass_}")
    if no_color:
        log.debug(f" START TEST: {filename}")
    else:
        log.debug(f"{color(style.bold)} START TEST: {color(style.reset)} {filename}")

    if ast:
        run_test(
            filename,
            "ast",
            "lc --ast-dump {infile} -o {outfile}" + f' -extra-arg="{clang_extra_arg}"',
            filename,
            update_reference)

    if asr:
        run_test(
            filename,
            "asr",
            "lc --asr-dump --no-color {infile} -o {outfile}" + f' -extra-arg="{clang_extra_arg}"',
            filename,
            update_reference)

    if asr_json:
        run_test(
            filename,
            "asr_json",
            "lc --show-asr --json --no-color {infile} -o {outfile}",
            filename,
            update_reference)

    if pass_ is not None:
        cmd = "lc "
        if is_cumulative:
            cmd += "--cumulative "
        cmd += "--pass=" + pass_ + \
            " --show-asr --no-color {infile} -o {outfile}"
        run_test(filename, "pass_{}".format(pass_), cmd,
                 filename, update_reference)

    if no_llvm:
        log.info(f"{filename} * llvm   SKIPPED as requested")
    else:
        if llvm:
            run_test(
                filename,
                "llvm",
                "lc --no-color --show-llvm {infile} -o {outfile}",
                filename,
                update_reference)
        if llvm_dbg:
            run_test(
                filename,
                "llvm_dbg",
                "lc --no-color --show-llvm -g --debug-with-line-column "
                    "{infile} -o {outfile}",
                filename,
                update_reference)

    if cpp:
        run_test(filename, "cpp", "lc --no-color --show-cpp {infile}",
                 filename, update_reference)

    if c:
        if disable_main:
            run_test(filename, "c", "lc --no-color --disable-main --show-c {infile}" + f' -extra-arg="{clang_extra_arg}"',
                 filename, update_reference)
        else:
            run_test(filename, "c", "lc --no-color --show-c {infile}" + f' -extra-arg="{clang_extra_arg}"',
                 filename, update_reference)
    if wat:
        run_test(filename, "wat", "lc --no-color --show-wat {infile}" + f' -extra-arg="{clang_extra_arg}"',
                 filename, update_reference)

if __name__ == "__main__":
    tester_main("LC", single_test)
