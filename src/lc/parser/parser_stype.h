#ifndef LC_PARSER_STYPE_H
#define LC_PARSER_STYPE_H

#include <cstring>
#include <lc/ast.h>
#include <libasr/location.h>
#include <libasr/containers.h>
#include <libasr/bigint.h>

namespace LCompilers::LC {

struct Key_Val {
    LC::AST::expr_t* key;
    LC::AST::expr_t* value;
};

struct Args {
    LC::AST::arguments_t arguments;
};

struct Arg {
    bool default_value;
    LC::AST::arg_t _arg;
    LC::AST::expr_t *defaults;
};

struct Var_Kw {
    Vec<Arg*> vararg;
    Vec<Arg*> kwonlyargs;
    Vec<Arg*> kwarg;
};

struct Args_ {
    Vec<Arg*> args;
    bool var_kw_val;
    Var_Kw *var_kw;
};

struct Fn_Arg {
    Vec<Arg*> posonlyargs;
    bool args_val;
    Args_ *args;
};

struct Kw_or_Star_Arg {
    LC::AST::expr_t *star_arg;
    LC::AST::keyword_t *kw_arg;
};

struct Call_Arg {
    Vec<LC::AST::expr_t*> expr;
    Vec<LC::AST::keyword_t> kw;
};

struct Key_Val_Pattern {
    LC::AST::expr_t* key;
    LC::AST::pattern_t* val;
};

union YYSTYPE {
    int64_t n;
    double f;
    Str string;

    LC::AST::ast_t* ast;
    Vec<LC::AST::ast_t*> vec_ast;

    LC::AST::alias_t* alias;
    Vec<LC::AST::alias_t> vec_alias;

    Arg *arg;
    Vec<Arg*> vec_arg;

    Args *args;
    Vec<Args> vec_args;

    Fn_Arg *fn_arg;
    Args_ *args_;
    Var_Kw *var_kw;

    Key_Val *key_val;
    Vec<Key_Val*> vec_key_val;

    LC::AST::withitem_t* withitem;
    Vec<LC::AST::withitem_t> vec_withitem;

    LC::AST::keyword_t* keyword;
    Vec<LC::AST::keyword_t> vec_keyword;

    Kw_or_Star_Arg* kw_or_star;
    Vec<Kw_or_Star_Arg> vec_kw_or_star;

    Call_Arg *call_arg;

    LC::AST::comprehension_t* comp;
    Vec<LC::AST::comprehension_t> vec_comp;

    LC::AST::operatorType operator_type;

    LC::AST::match_case_t* match_case;
    Vec<LC::AST::match_case_t> vec_match_case;

    LC::AST::pattern_t* pattern;
    Vec<LC::AST::pattern_t*> vec_pattern;

    Key_Val_Pattern *kw_val_pattern;
    Vec<Key_Val_Pattern> vec_kw_val_pattern;
};

static_assert(std::is_standard_layout<YYSTYPE>::value);
static_assert(std::is_trivial<YYSTYPE>::value);
// Ensure the YYSTYPE size is equal to Vec<AST::ast_t*>, which is a required member, so
// YYSTYPE must be at least as big, but it should not be bigger, otherwise it
// would reduce performance.
// A temporary fix for PowerPC 32-bit, where the following assert fails with (16 == 12).
#ifndef __ppc__
static_assert(sizeof(YYSTYPE) == sizeof(Vec<LC::AST::ast_t*>));
#endif

static_assert(std::is_standard_layout<Location>::value);
static_assert(std::is_trivial<Location>::value);

} // namespace LCompilers::LC


typedef struct LCompilers::Location YYLTYPE;
#define YYLTYPE_IS_DECLARED 1
#define YYLTYPE_IS_TRIVIAL 0
#define YYINITDEPTH 2000


#endif // LC_PARSER_STYPE_H
