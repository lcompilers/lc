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
#include <libasr/asr_utils.h>

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
    std::vector<std::map<std::string, std::string>> scopes;

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
    std::string get_file_name(T* x) {
        clang::SourceLocation loc = x->getLocation();
        if( loc.isInvalid() ) {
            return "";
        }

        clang::FullSourceLoc full_source_loc = Context->getFullLoc(loc);
        return std::string(full_source_loc.getPresumedLoc().getFilename());
    }

    template <typename T>
    std::string loc(T *x) {
        uint64_t first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
        uint64_t last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
        return std::to_string(first) + ":" + std::to_string(last);
    }

    ASR::symbol_t* declare_dummy_variable(std::string var_name, SymbolTable* scope, Location& loc, ASR::ttype_t* var_type) {
        var_name = scope->get_unique_name(var_name, false);
        SetChar variable_dependencies_vec;
        variable_dependencies_vec.reserve(al, 1);
        ASRUtils::collect_variable_dependencies(al, variable_dependencies_vec, var_type);
        ASR::asr_t* variable_asr = ASR::make_Variable_t(al, loc, scope,
                                        s2c(al, var_name), variable_dependencies_vec.p,
                                        variable_dependencies_vec.size(), ASR::intentType::Local,
                                        nullptr, nullptr, ASR::storage_typeType::Default,
                                        var_type, nullptr, ASR::abiType::Source, ASR::accessType::Public,
                                        ASR::presenceType::Required, false);
        ASR::symbol_t* dummy_variable_sym = ASR::down_cast<ASR::symbol_t>(variable_asr);
        scope->add_symbol(var_name, dummy_variable_sym);
        return dummy_variable_sym;
    }

    void construct_program() {
        // Convert the main function into a program
        ASR::TranslationUnit_t* tu = (ASR::TranslationUnit_t*)this->tu;
        Location& loc = tu->base.base.loc;

        Vec<ASR::stmt_t*> prog_body;
        prog_body.reserve(al, 1);
        SetChar prog_dep;
        prog_dep.reserve(al, 1);
        SymbolTable *program_scope = al.make_new<SymbolTable>(tu->m_symtab);

        std::string main_func = "main";
        ASR::symbol_t *main_sym = tu->m_symtab->resolve_symbol(main_func);
        LCOMPILERS_ASSERT(main_sym);
        if (main_sym == nullptr) {
            return;
        }
        ASR::Function_t *main = ASR::down_cast<ASR::Function_t>(main_sym);
        LCOMPILERS_ASSERT(main);

        // Make a FunctionCall to main
        ASR::ttype_t *int32_type = ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4));
        ASR::asr_t* func_call_main = ASR::make_FunctionCall_t(al, loc, main_sym, main_sym,
             nullptr, 0, int32_type, nullptr, nullptr);

        ASR::symbol_t* exit_variable_sym = declare_dummy_variable("exit_code", program_scope, loc, int32_type);
        ASR::expr_t* variable_var = ASRUtils::EXPR(ASR::make_Var_t(al, loc, exit_variable_sym));
        ASR::asr_t* assign_stmt = ASR::make_Assignment_t(al, loc, variable_var, ASRUtils::EXPR(func_call_main), nullptr);

        prog_body.push_back(al, ASRUtils::STMT(assign_stmt));

        prog_dep.push_back(al, s2c(al, main_func));

        std::string prog_name = "main_program";
        ASR::asr_t *prog = ASR::make_Program_t(
            al, loc,
            /* a_symtab */ program_scope,
            /* a_name */ s2c(al, prog_name),
            prog_dep.p,
            prog_dep.n,
            /* a_body */ prog_body.p,
            /* n_body */ prog_body.n);
        tu->m_symtab->add_symbol(prog_name, ASR::down_cast<ASR::symbol_t>(prog));
    }

    bool TraverseTranslationUnitDecl(clang::TranslationUnitDecl *x) {
        SymbolTable *parent_scope = al.make_new<SymbolTable>(nullptr);
        current_scope = parent_scope;
        Location l = Lloc(x);
        tu = ASR::make_TranslationUnit_t(al, l, current_scope, nullptr, 0);

        for (auto D = x->decls_begin(), DEnd = x->decls_end(); D != DEnd; ++D) {
            TraverseDecl(*D);
        }

        construct_program();
        // remove printf definition
        ((ASR::TranslationUnit_t*)tu)->m_symtab->erase_symbol("printf");

        return true;
    }

    ASR::ttype_t* flatten_Array(ASR::ttype_t* array) {
        if( !ASRUtils::is_array(array) ) {
            return array;
        }
        ASR::Array_t* array_t = ASR::down_cast<ASR::Array_t>(array);
        ASR::ttype_t* m_type_flattened = flatten_Array(array_t->m_type);
        if( !ASR::is_a<ASR::Array_t>(*m_type_flattened) ) {
            return array;
        }

        ASR::Array_t* array_t_flattened = ASR::down_cast<ASR::Array_t>(m_type_flattened);
        ASR::dimension_t row = array_t->m_dims[0];
        Vec<ASR::dimension_t> new_dims; new_dims.reserve(al, array_t->n_dims + array_t_flattened->n_dims);
        new_dims.push_back(al, row);
        for( size_t i = 0; i < array_t_flattened->n_dims; i++ ) {
            new_dims.push_back(al, array_t_flattened->m_dims[i]);
        }
        array_t->m_type = array_t_flattened->m_type;
        array_t->m_dims = new_dims.p;
        array_t->n_dims = new_dims.size();
        std::map<ASR::array_physical_typeType, int> physicaltype2priority = {
            {ASR::array_physical_typeType::DescriptorArray, 2},
            {ASR::array_physical_typeType::PointerToDataArray, 1},
            {ASR::array_physical_typeType::FixedSizeArray, 0}
        };
        ASR::array_physical_typeType physical_type;
        if( physicaltype2priority[array_t->m_physical_type] >
            physicaltype2priority[array_t_flattened->m_physical_type] ) {
            physical_type = array_t->m_physical_type;
        } else {
            physical_type = array_t_flattened->m_physical_type;
        }
        array_t->m_physical_type = physical_type;
        return array;
    }

    ASR::ttype_t* ClangTypeToASRType(const clang::QualType& qual_type) {
        const clang::SplitQualType& split_qual_type = qual_type.split();
        const clang::Type* clang_type = split_qual_type.asPair().first;
        const clang::Qualifiers qualifiers = split_qual_type.asPair().second;
        Location l; l.first = 1, l.last = 1;
        ASR::ttype_t* type = nullptr;
        if( clang_type->isCharType() ) {
            type = ASRUtils::TYPE(ASR::make_Character_t(al, l, 1, -1, nullptr));
        } else if( clang_type->isIntegerType() ) {
            type = ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4));
        } else if( clang_type->isFloatingType() ) {
            type = ASRUtils::TYPE(ASR::make_Real_t(al, l, 4));
        } else if( clang_type->isPointerType() ) {
            type = ClangTypeToASRType(qual_type->getPointeeType());
            if( !ASRUtils::is_character(*type) ) {
                type = ASRUtils::TYPE(ASR::make_Pointer_t(al, l, type));
            }
        } else if( clang_type->isConstantArrayType() ) {
            const clang::ArrayType* array_type = clang_type->getAsArrayTypeUnsafe();
            const clang::ConstantArrayType* fixed_size_array_type =
                reinterpret_cast<const clang::ConstantArrayType*>(array_type);
            type = ClangTypeToASRType(array_type->getElementType());
            llvm::APInt ap_int = fixed_size_array_type->getSize();
            uint64_t size = ap_int.getZExtValue();
            Vec<ASR::dimension_t> vec; vec.reserve(al, 1);
            ASR::dimension_t dim;
            dim.loc = l; dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                al, l, size, ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
            dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, 0,
                ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
            vec.push_back(al, dim);
            type = ASRUtils::TYPE(ASR::make_Array_t(al, l, type, vec.p, vec.size(),
                ASR::array_physical_typeType::FixedSizeArray));
            type = flatten_Array(type);
        } else if( clang_type->isVariableArrayType() ) {
            const clang::ArrayType* array_type = clang_type->getAsArrayTypeUnsafe();
            const clang::VariableArrayType* variable_array_type =
                reinterpret_cast<const clang::VariableArrayType*>(array_type);
            type = ClangTypeToASRType(array_type->getElementType());
            clang::Expr* expr = variable_array_type->getSizeExpr();
            TraverseStmt(expr);
            Vec<ASR::dimension_t> vec; vec.reserve(al, 1);
            ASR::dimension_t dim;
            dim.loc = l; dim.m_length = ASRUtils::EXPR(tmp);
            dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, 0,
                ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
            vec.push_back(al, dim);
            type = ASRUtils::make_Array_t_util(al, l, type, vec.p, vec.size());
            type = flatten_Array(type);
        } else {
            throw std::runtime_error("clang::QualType not yet supported " +
                std::string(clang_type->getTypeClassName()));
        }

        if( qualifiers.hasConst() ) {
            type = ASRUtils::TYPE(ASR::make_Const_t(al, l, type));
        }
        return type;
    }

    bool TraverseParmVarDecl(clang::ParmVarDecl* x) {
        std::string name = x->getName().str();
        if( name == "" ) {
            name = current_scope->get_unique_name("param");
        }
        ASR::ttype_t* type = ClangTypeToASRType(x->getType());
        clang::Expr *init = x->getDefaultArg();
        ASR::expr_t* asr_init = nullptr;
        if (init) {
            TraverseStmt(init);
            asr_init = ASRUtils::EXPR(tmp);
        }

        tmp = ASR::make_Variable_t(al, Lloc(x), current_scope, s2c(al, name),
            nullptr, 0, ASR::intentType::InOut, asr_init, nullptr,
            ASR::storage_typeType::Default, type, nullptr, ASR::abiType::Source,
            ASR::accessType::Public, ASR::presenceType::Required, false);
        current_scope->add_symbol(name, ASR::down_cast<ASR::symbol_t>(tmp));
        is_stmt_created = false;
        return true;
    }

    bool TraverseImplicitCastExpr(clang::ImplicitCastExpr* x) {
        clang::CastKind cast_kind = x->getCastKind();
        ASR::cast_kindType asr_cast_kind;
        switch( cast_kind ) {
            case clang::CastKind::CK_IntegralToFloating: {
                asr_cast_kind = ASR::cast_kindType::IntegerToReal;
                break;
            }
            case clang::CastKind::CK_FloatingCast: {
                asr_cast_kind = ASR::cast_kindType::RealToReal;
                break;
            }
            default: {
                clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseImplicitCastExpr(x);
                return true;
            }
        }
        clang::Expr* sub_expr = x->getSubExpr();
        TraverseStmt(sub_expr);
        ASR::expr_t* arg = ASRUtils::EXPR(tmp);
        tmp = ASR::make_Cast_t(al, Lloc(x), arg, asr_cast_kind,
                ClangTypeToASRType(x->getType()), nullptr);
        is_stmt_created = false;
        return true;
    }

    void handle_printf(clang::CallExpr *x) {
        Vec<ASR::expr_t*> args;
        args.reserve(al, 1);
        bool skip_format_str = true;
        for (auto *p : x->arguments()) {
            TraverseStmt(p);
            if (skip_format_str) {
                skip_format_str = false;
                continue;
            }
            args.push_back(al, ASRUtils::EXPR(tmp));
        }
        tmp = ASR::make_Print_t(al, Lloc(x), args.p, args.size(), nullptr, nullptr);
        is_stmt_created = true;
    }

    bool check_printf(ASR::expr_t* callee) {
        ASR::Var_t* callee_Var = ASR::down_cast<ASR::Var_t>(callee);
        return std::string(ASRUtils::symbol_name(callee_Var->m_v)) == "printf";
    }

    bool TraverseCallExpr(clang::CallExpr *x) {
        TraverseStmt(x->getCallee());
        ASR::expr_t* callee = ASRUtils::EXPR(tmp);
        if( check_printf(callee) ) {
            handle_printf(x);
            return true;
        }

        clang::Expr** args = x->getArgs();
        size_t n_args = x->getNumArgs();
        Vec<ASR::call_arg_t> call_args;
        call_args.reserve(al, n_args);
        for( size_t i = 0; i < n_args; i++ ) {
            TraverseStmt(args[i]);
            ASR::expr_t* arg = ASRUtils::EXPR(tmp);
            ASR::call_arg_t call_arg;
            call_arg.loc = arg->base.loc;
            call_arg.m_value = arg;
            call_args.push_back(al, call_arg);
        }

        ASR::Var_t* callee_Var = ASR::down_cast<ASR::Var_t>(callee);
        ASR::symbol_t* callee_sym = callee_Var->m_v;
        if( x->getCallReturnType(*Context).split().asPair().first->isVoidType() ) {
            throw std::runtime_error("Void return type not yet supported.");
        } else {
            const clang::QualType& qual_type = x->getCallReturnType(*Context);
            tmp = ASRUtils::make_FunctionCall_t_util(al, Lloc(x), callee_sym,
                callee_sym, call_args.p, call_args.size(), ClangTypeToASRType(qual_type),
                nullptr, nullptr);
        }
        return true;
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

    bool TraverseDeclStmt(clang::DeclStmt* x) {
        if( x->isSingleDecl() ) {
            return clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseDeclStmt(x);
        }

        clang::DeclGroup& decl_group = x->getDeclGroup().getDeclGroup();
        for( size_t i = 0; i < decl_group.size(); i++ ) {
            TraverseDecl(decl_group[i]);
            if( is_stmt_created ) {
                current_body->push_back(al, ASRUtils::STMT(tmp));
            }
        }
        is_stmt_created = false;
        return true;
    }

    bool TraverseVarDecl(clang::VarDecl *x) {
        std::string name = x->getName().str();
        if( scopes.size() > 0 ) {
            if( scopes.back().find(name) != scopes.back().end() ) {
                throw std::runtime_error(name + std::string(" is already defined."));
            } else {
                std::string aliased_name = current_scope->get_unique_name(name);
                scopes.back()[name] = aliased_name;
                name = aliased_name;
            }
        } else {
            if( current_scope->resolve_symbol(name) ) {
                throw std::runtime_error(name + std::string(" is already defined."));
            }
        }
        ASR::ttype_t *asr_type = ClangTypeToASRType(x->getType());
        ASR::symbol_t *v = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, Lloc(x),
            current_scope, s2c(al, name), nullptr, 0, ASR::intentType::Local, nullptr, nullptr,
            ASR::storage_typeType::Default, asr_type, nullptr, ASR::abiType::Source,
            ASR::accessType::Public, ASR::presenceType::Required, false));
        current_scope->add_symbol(name, v);
        is_stmt_created = false;
        if (x->hasInit()) {
            TraverseStmt(x->getInit());
            ASR::expr_t* init_val = ASRUtils::EXPR(tmp);
            ASR::expr_t* var = ASRUtils::EXPR(ASR::make_Var_t(al, Lloc(x), v));
            tmp = ASR::make_Assignment_t(al, Lloc(x), var, init_val, nullptr);
            is_stmt_created = true;
        }
        return true;
    }

    void flatten_ArrayConstant(ASR::expr_t* array_constant) {
        if( !ASRUtils::is_array(ASRUtils::expr_type(array_constant)) ) {
            return ;
        }

        LCOMPILERS_ASSERT(ASR::is_a<ASR::ArrayConstant_t>(array_constant));
        ASR::ArrayConstant_t* array_constant_t = ASR::down_cast<ASR::ArrayConstant_t>(array_constant);
        Vec<ASR::expr_t*> new_elements; new_elements.reserve(al, array_constant_t->n_args);
        for( size_t i = 0; i < array_constant_t->n_args; i++ ) {
            flatten_ArrayConstant(array_constant_t->m_args[i]);
            if( ASR::is_a<ASR::ArrayConstant_t>(*array_constant_t->m_args[i]) ) {
                ASR::ArrayConstant_t* aci = ASR::down_cast<ASR::ArrayConstant_t>(array_constant_t->m_args[i]);
                for( size_t j = 0; j < aci->n_args; j++ ) {
                    new_elements.push_back(al, aci->m_args[j]);
                }
            } else {
                new_elements.push_back(al, array_constant_t->m_args[i]);
            }
        }
        array_constant_t->m_args = new_elements.p;
        array_constant_t->n_args = new_elements.size();
        Vec<ASR::dimension_t> new_dims; new_dims.reserve(al, 1);
        const Location& loc = array_constant->base.loc;
        ASR::dimension_t dim; dim.loc = loc;
        dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, loc,
            new_elements.size(), ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4))));
        dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, loc,
            0, ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4))));
        new_dims.push_back(al, dim);
        ASR::ttype_t* new_type = ASRUtils::TYPE(ASR::make_Array_t(al, loc,
            ASRUtils::type_get_past_array(flatten_Array(array_constant_t->m_type)),
            new_dims.p, new_dims.size(), ASR::array_physical_typeType::FixedSizeArray));
        array_constant_t->m_type = new_type;
    }

    bool TraverseInitListExpr(clang::InitListExpr* x) {
        Vec<ASR::expr_t*> init_exprs;
        init_exprs.reserve(al, x->getNumInits());
        clang::Expr** clang_inits = x->getInits();
        for( size_t i = 0; i < x->getNumInits(); i++ ) {
            TraverseStmt(clang_inits[i]);
            init_exprs.push_back(al, ASRUtils::EXPR(tmp));
        }
        ASR::ttype_t* type = ASRUtils::expr_type(init_exprs[init_exprs.size() - 1]);
        Vec<ASR::dimension_t> dims; dims.reserve(al, 1);
        ASR::dimension_t dim; dim.loc = Lloc(x);
        dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, Lloc(x),
            x->getNumInits(), ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4))));
        dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, Lloc(x),
            0, ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4))));
        dims.push_back(al, dim);
        type = ASRUtils::TYPE(ASR::make_Array_t(al, Lloc(x),
            type, dims.p, dims.size(), ASR::array_physical_typeType::FixedSizeArray));
        ASR::expr_t* array_constant = ASRUtils::EXPR(ASR::make_ArrayConstant_t(al, Lloc(x),
            init_exprs.p, init_exprs.size(), type, ASR::arraystorageType::RowMajor));
        flatten_ArrayConstant(array_constant);
        tmp = (ASR::asr_t*) array_constant;
        return true;
    }

    void CreateBinOp(ASR::expr_t* lhs, ASR::expr_t* rhs,
        ASR::binopType binop_type, const Location& loc) {
        if( ASRUtils::is_integer(*ASRUtils::expr_type(lhs)) &&
            ASRUtils::is_integer(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_IntegerBinOp_t(al, loc, lhs,
                binop_type, rhs, ASRUtils::expr_type(lhs), nullptr);
        } else if( ASRUtils::is_real(*ASRUtils::expr_type(lhs)) &&
                   ASRUtils::is_real(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_RealBinOp_t(al, loc, lhs,
                binop_type, rhs, ASRUtils::expr_type(lhs), nullptr);
        }  else {
            throw SemanticError("Only integer and real types are supported so "
                "far for binary operator", loc);
        }
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
                case clang::BO_LT: {
                    cmpop_type = ASR::cmpopType::Lt;
                    is_cmpop = true;
                    break;
                }
                default: {
                    throw std::runtime_error("BinaryOperator not supported " + std::to_string(op));
                    break;
                }
            }
            if( is_binop ) {
                CreateBinOp(x_lhs, x_rhs, binop_type, Lloc(x));
            } else if( is_cmpop ) {
                if( ASRUtils::is_integer(*ASRUtils::expr_type(x_lhs)) &&
                    ASRUtils::is_integer(*ASRUtils::expr_type(x_rhs)) ) {
                    tmp = ASR::make_IntegerCompare_t(al, Lloc(x), x_lhs,
                        cmpop_type, x_rhs, ASRUtils::expr_type(x_lhs), nullptr);
                } else {
                    throw std::runtime_error("Only integer type is supported so "
                                             "far for comparison operator");
                }
            } else {
                throw std::runtime_error("Only binary operators supported so far");
            }
            is_stmt_created = false;
        }
        return true;
    }

    ASR::symbol_t* check_aliases(std::string name) {
        for( auto itr = scopes.rbegin(); itr != scopes.rend(); itr++ ) {
            std::map<std::string, std::string>& alias = *itr;
            if( alias.find(name) != alias.end() ) {
                return current_scope->resolve_symbol(alias[name]);
            }
        }

        return nullptr;
    }

    ASR::symbol_t* resolve_symbol(std::string name) {
        ASR::symbol_t* sym = check_aliases(name);
        if( sym ) {
            return sym;
        }

        return current_scope->resolve_symbol(name);
    }

    bool TraverseDeclRefExpr(clang::DeclRefExpr* x) {
        std::string name = x->getNameInfo().getAsString();
        ASR::symbol_t* sym = resolve_symbol(name);
        LCOMPILERS_ASSERT(sym != nullptr);
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

    bool TraverseFloatingLiteral(clang::FloatingLiteral* x) {
        double d = x->getValue().convertToDouble();
        tmp = ASR::make_RealConstant_t(al, Lloc(x), d,
            ASRUtils::TYPE(ASR::make_Real_t(al, Lloc(x), 8)));
        is_stmt_created = false;
        return true;
    }

    bool TraverseStringLiteral(clang::StringLiteral *x) {
        std::string s = x->getString().str();
        tmp = ASR::make_StringConstant_t(al, Lloc(x), s2c(al, s),
            ASRUtils::TYPE(ASR::make_Character_t(al, Lloc(x), 1, s.size(), nullptr)));
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

    ASR::expr_t* flatten_ArrayItem(ASR::expr_t* expr) {
        if( !ASR::is_a<ASR::ArrayItem_t>(*expr) ) {
            return expr;
        }

        ASR::ArrayItem_t* array_item_t = ASR::down_cast<ASR::ArrayItem_t>(expr);
        if( !ASR::is_a<ASR::ArrayItem_t>(*array_item_t->m_v) ) {
            return expr;
        }

        ASR::expr_t* flattened_array_item_expr = flatten_ArrayItem(array_item_t->m_v);
        ASR::ArrayItem_t* flattened_array_item = ASR::down_cast<ASR::ArrayItem_t>(flattened_array_item_expr);
        array_item_t->m_v = flattened_array_item->m_v;
        Vec<ASR::array_index_t> indices; indices.from_pointer_n_copy(
            al, flattened_array_item->m_args, flattened_array_item->n_args);
        indices.push_back(al, array_item_t->m_args[0]);
        array_item_t->m_args = indices.p;
        array_item_t->n_args = indices.size();
        return expr;
    }

    bool TraverseArraySubscriptExpr(clang::ArraySubscriptExpr* x) {
        clang::Expr* clang_array = x->getBase();
        TraverseStmt(clang_array);
        ASR::expr_t* array = flatten_ArrayItem(ASRUtils::EXPR(tmp));
        clang::Expr* clang_index = x->getIdx();
        TraverseStmt(clang_index);
        ASR::expr_t* index = ASRUtils::EXPR(tmp);
        Vec<ASR::array_index_t> indices; indices.reserve(al, 1);
        ASR::array_index_t array_index; array_index.loc = index->base.loc;
        array_index.m_left = nullptr; array_index.m_right = index; array_index.m_step = nullptr;
        indices.push_back(al, array_index);
        ASR::expr_t* array_item = ASRUtils::EXPR(ASR::make_ArrayItem_t(al, Lloc(x), array, indices.p, indices.size(),
            ASRUtils::extract_type(ASRUtils::expr_type(array)), ASR::arraystorageType::RowMajor, nullptr));
        array_item = flatten_ArrayItem(array_item);
        tmp = (ASR::asr_t*) array_item;
        return true;
    }

    bool TraverseCompoundAssignOperator(clang::CompoundAssignOperator* x) {
        TraverseStmt(x->getLHS());
        ASR::expr_t* x_lhs = ASRUtils::EXPR(tmp);
        TraverseStmt(x->getRHS());
        ASR::expr_t* x_rhs = ASRUtils::EXPR(tmp);
        CreateBinOp(x_lhs, x_rhs, ASR::binopType::Add, Lloc(x));
        ASR::expr_t* sum_expr = ASRUtils::EXPR(tmp);
        tmp = ASR::make_Assignment_t(al, Lloc(x), x_lhs, sum_expr, nullptr);
        is_stmt_created = true;
        return true;
    }

    bool TraverseUnaryOperator(clang::UnaryOperator* x) {
        clang::UnaryOperatorKind op = x->getOpcode();
        switch( op ) {
            case clang::UnaryOperatorKind::UO_PostInc: {
                clang::Expr* expr = x->getSubExpr();
                TraverseStmt(expr);
                ASR::expr_t* var = ASRUtils::EXPR(tmp);
                ASR::expr_t* incbyone = ASRUtils::EXPR(ASR::make_IntegerBinOp_t(
                    al, Lloc(x), var, ASR::binopType::Add,
                    ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                        al, Lloc(x), 1, ASRUtils::expr_type(var))),
                    ASRUtils::expr_type(var), nullptr));
                tmp = ASR::make_Assignment_t(al, Lloc(x), var, incbyone, nullptr);
                is_stmt_created = true;
                break;
            }
            default: {
                throw SemanticError("Only postfix increment is supported so far", Lloc(x));
            }
        }
        return true;
    }

    bool TraverseForStmt(clang::ForStmt* x) {
        std::map<std::string, std::string> alias;
        scopes.push_back(alias);
        clang::Stmt* init_stmt = x->getInit();
        TraverseStmt(init_stmt);
        LCOMPILERS_ASSERT(tmp != nullptr && is_stmt_created);
        current_body->push_back(al, ASRUtils::STMT(tmp));

        clang::Expr* loop_cond = x->getCond();
        TraverseStmt(loop_cond);
        ASR::expr_t* test = ASRUtils::EXPR(tmp);

        Vec<ASR::stmt_t*> body; body.reserve(al, 1);
        Vec<ASR::stmt_t*>*current_body_copy = current_body;
        current_body = &body;
        clang::Stmt* loop_body = x->getBody();
        TraverseStmt(loop_body);
        clang::Stmt* inc_stmt = x->getInc();
        TraverseStmt(inc_stmt);
        LCOMPILERS_ASSERT(tmp != nullptr && is_stmt_created);
        body.push_back(al, ASRUtils::STMT(tmp));
        current_body = current_body_copy;

        tmp = ASR::make_WhileLoop_t(al, Lloc(x), nullptr, test, body.p, body.size());
        is_stmt_created = true;
        scopes.pop_back();
        return true;
    }

    template <typename T>
    bool process_AST_node(T* x) {
        std::string file_path = get_file_name(x);
        if( file_path.size() == 0 ) {
            return false;
        }

        std::vector<std::string> include_paths = {
            "include/c++/v1",
            "include/stddef.h",
            "include/__stddef_max_align_t.h",
            "usr/include",
            "mambaforge/envs",
        };
        for( std::string& path: include_paths ) {
            if( file_path.find(path) != std::string::npos ) {
                return false;
            }
        }

        return true;
    }

    bool TraverseDecl(clang::Decl* x) {
        if( x->isImplicit() ) {
            return true;
        }

        if( process_AST_node(x) || x->getKind() == clang::Decl::Kind::TranslationUnit ) {
            return clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseDecl(x);
        } else {
            return true;
        }

        return false;
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
