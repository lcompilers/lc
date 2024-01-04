#ifndef CLANG_AST_TO_ASR_H
#define CLANG_AST_TO_ASR_H

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
#include <libasr/pass/intrinsic_array_function_registry.h>

#include <iostream>

namespace LCompilers {

enum SpecialFunc {
    Printf,
    Exit,
    View,
    Shape,
    Empty,
    Fill,
    All,
    Any,
    NotEqual,
    Equal,
    Exp,
    Abs,
    AMax,
    Sum,
    Range,
    Size,
    Pow,
    Reshape,
    Iota,
};

std::map<std::string, SpecialFunc> special_function_map = {
    {"printf", SpecialFunc::Printf},
    {"exit", SpecialFunc::Exit},
    {"view", SpecialFunc::View},
    {"shape", SpecialFunc::Shape},
    {"empty", SpecialFunc::Empty},
    {"fill", SpecialFunc::Fill},
    {"all", SpecialFunc::All},
    {"any", SpecialFunc::Any},
    {"not_equal", SpecialFunc::NotEqual},
    {"equal", SpecialFunc::Equal},
    {"exp", SpecialFunc::Exp},
    {"abs", SpecialFunc::Abs},
    {"amax", SpecialFunc::AMax},
    {"sum", SpecialFunc::Sum},
    {"range", SpecialFunc::Range},
    {"size", SpecialFunc::Size},
    {"pow", SpecialFunc::Pow},
    {"reshape", SpecialFunc::Reshape},
    {"operator\"\"i", SpecialFunc::Iota},
};

class OneTimeUseString {

    private:

        std::string value;

    public:

        OneTimeUseString(): value{""} {}

        std::string get() {
            std::string value_ = value;
            value = "";
            return value_;
        }

        void set(std::string value_) {
            value = value_;
        }
};

template <typename T>
class OneTimeUseASRNode {

    private:

        T* node;

    public:

        OneTimeUseASRNode(): node{nullptr} {}

        T* get() {
            T* node_ = node;
            node = nullptr;
            return node_;
        }

        void operator=(T* other) {
            node = other;
        }

        bool operator!=(T* other) {
            return node != other;
        }

        bool operator==(T* other) {
            return node == other;
        }

};

class ClangASTtoASRVisitor: public clang::RecursiveASTVisitor<ClangASTtoASRVisitor> {

public:

    std::string ast;
    SymbolTable *current_scope=nullptr;
    Allocator& al;
    ASR::asr_t*& tu;
    OneTimeUseASRNode<ASR::asr_t> tmp;
    Vec<ASR::stmt_t*>* current_body;
    bool is_stmt_created;
    std::vector<std::map<std::string, std::string>> scopes;
    OneTimeUseString cxx_operator_name_obj, member_name_obj;
    ASR::expr_t* assignment_target;
    Vec<ASR::expr_t*>* print_args;
    bool is_all_called, is_range_called;
    OneTimeUseASRNode<ASR::expr_t> range_start, range_end, range_step;

    explicit ClangASTtoASRVisitor(clang::ASTContext *Context_,
        Allocator& al_, ASR::asr_t*& tu_):
        Context(Context_), al{al_}, tu{tu_},
        current_body{nullptr}, is_stmt_created{true},
        assignment_target{nullptr}, print_args{nullptr},
        is_all_called{false}, is_range_called{false} {}

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

    void remove_special_functions() {
        ASR::TranslationUnit_t* tu = (ASR::TranslationUnit_t*)this->tu;
        if (tu->m_symtab->resolve_symbol("printf")) {
            tu->m_symtab->erase_symbol("printf");
        }
        if (tu->m_symtab->resolve_symbol("exit")) {
            tu->m_symtab->erase_symbol("exit");
        }
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
        remove_special_functions();

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

    ASR::ttype_t* ClangTypeToASRType(const clang::QualType& qual_type,
        Vec<ASR::dimension_t>* xshape_result=nullptr) {
        const clang::SplitQualType& split_qual_type = qual_type.split();
        const clang::Type* clang_type = split_qual_type.asPair().first;
        const clang::Qualifiers qualifiers = split_qual_type.asPair().second;
        Location l; l.first = 1, l.last = 1;
        ASR::ttype_t* type = nullptr;
        if (clang_type->isVoidType() ) {
            // do nothing
        } else if( clang_type->isCharType() ) {
            type = ASRUtils::TYPE(ASR::make_Character_t(al, l, 1, -1, nullptr));
        } else if( clang_type->isBooleanType() ) {
            type = ASRUtils::TYPE(ASR::make_Logical_t(al, l, 4));
        } else if( clang_type->isIntegerType() ) {
            type = ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4));
        } else if( clang_type->isFloatingType() ) {
            type = ASRUtils::TYPE(ASR::make_Real_t(al, l, 8));
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
            dim.loc = l; dim.m_length = ASRUtils::EXPR(tmp.get());
            dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, 0,
                ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
            vec.push_back(al, dim);
            type = ASRUtils::make_Array_t_util(al, l, type, vec.p, vec.size());
            type = flatten_Array(type);
        } else if( clang_type->getTypeClass() == clang::Type::LValueReference ) {
            const clang::LValueReferenceType* lvalue_reference_type = clang_type->getAs<clang::LValueReferenceType>();
            clang::QualType pointee_type = lvalue_reference_type->getPointeeType();
            type = ClangTypeToASRType(pointee_type, xshape_result);
        } else if( clang_type->getTypeClass() == clang::Type::TypeClass::Elaborated ) {
            const clang::ElaboratedType* elaborated_type = clang_type->getAs<clang::ElaboratedType>();
            clang::QualType desugared_type = elaborated_type->desugar();
            type = ClangTypeToASRType(desugared_type, xshape_result);
        } else if( clang_type->getTypeClass() == clang::Type::TypeClass::TemplateSpecialization ) {
            const clang::TemplateSpecializationType* template_specialization = clang_type->getAs<clang::TemplateSpecializationType>();
            std::string template_name = template_specialization->getTemplateName().getAsTemplateDecl()->getNameAsString();
            if( template_name == "xtensor" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( template_arguments.size() != 2 ) {
                    throw std::runtime_error("xtensor type must be initialised with element type and rank.");
                }
                const clang::QualType& qual_type = template_arguments.at(0).getAsType();
                clang::Expr* clang_rank = template_arguments.at(1).getAsExpr();
                TraverseStmt(clang_rank);
                int rank = 0;
                if( !ASRUtils::extract_value(ASRUtils::EXPR(tmp.get()), rank) ) {
                    throw std::runtime_error("Rank provided in the xtensor initialisation must be a constant.");
                }
                tmp = nullptr;
                Vec<ASR::dimension_t> empty_dims; empty_dims.reserve(al, rank);
                for( int dim = 0; dim < rank; dim++ ) {
                    ASR::dimension_t empty_dim;
                    empty_dim.loc = l;
                    empty_dim.m_start = nullptr;
                    empty_dim.m_length = nullptr;
                    empty_dims.push_back(al, empty_dim);
                }
                type = ASRUtils::TYPE(ASR::make_Array_t(al, l,
                    ClangTypeToASRType(qual_type),
                    empty_dims.p, empty_dims.size(),
                    ASR::array_physical_typeType::DescriptorArray));
                type = ASRUtils::TYPE(ASR::make_Allocatable_t(al, l, type));
            } else if( template_name == "xtensor_fixed" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( template_arguments.size() != 2 ) {
                    throw std::runtime_error("xtensor_fixed type must be initialised with element type and shape.");
                }
                const clang::QualType& qual_type = template_arguments.at(0).getAsType();
                const clang::QualType& shape_type = template_arguments.at(1).getAsType();
                Vec<ASR::dimension_t> xtensor_fixed_dims; xtensor_fixed_dims.reserve(al, 1);
                ClangTypeToASRType(shape_type, &xtensor_fixed_dims);
                type = ASRUtils::TYPE(ASR::make_Array_t(al, l,
                    ClangTypeToASRType(qual_type),
                    xtensor_fixed_dims.p, xtensor_fixed_dims.size(),
                    ASR::array_physical_typeType::FixedSizeArray));
            } else if( template_name == "xshape" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( xshape_result == nullptr ) {
                    throw std::runtime_error("Result Vec<ASR::dimention_t>* not provided.");
                }

                ASR::expr_t* zero = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, 0,
                    ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
                for( int i = 0; i < template_arguments.size(); i++ ) {
                    clang::Expr* clang_rank = template_arguments.at(i).getAsExpr();
                    TraverseStmt(clang_rank);
                    int rank = 0;
                    if( !ASRUtils::extract_value(ASRUtils::EXPR(tmp.get()), rank) ) {
                        throw std::runtime_error("Rank provided in the xshape must be a constant.");
                    }
                    ASR::dimension_t dim; dim.loc = l;
                    dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, rank,
                        ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
                    dim.m_start = zero;
                    xshape_result->push_back(al, dim);
                }
                return nullptr;
            } else if( template_name == "complex" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( template_arguments.size() > 1 ) {
                    throw std::runtime_error("std::complex accepts only one argument.");
                }

                const clang::QualType& qual_type = template_arguments.at(0).getAsType();
                ASR::ttype_t* complex_subtype = ClangTypeToASRType(qual_type, xshape_result);
                if( !ASRUtils::is_real(*complex_subtype) ) {
                    throw std::runtime_error(std::string("std::complex accepts only real types, found: ") +
                        ASRUtils::type_to_str(complex_subtype));
                }
                type = ASRUtils::TYPE(ASR::make_Complex_t(al, l,
                    ASRUtils::extract_kind_from_ttype_t(complex_subtype)));
            } else {
                throw std::runtime_error(std::string("Unrecognized type ") + template_name);
            }
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
        ASR::intentType intent_type = ASR::intentType::InOut;
        if( ASR::is_a<ASR::Const_t>(*type) ) {
            intent_type = ASR::intentType::In;
            type = ASRUtils::type_get_past_const(type);
        }
        if( ASRUtils::is_allocatable(type) ) {
            type = ASRUtils::type_get_past_allocatable(type);
        }
        if( x->getType()->getTypeClass() != clang::Type::LValueReference &&
            ASRUtils::is_array(type) ) {
            throw std::runtime_error("Array objects should be passed by reference only.");
        }
        clang::Expr *init = x->getDefaultArg();
        ASR::expr_t* asr_init = nullptr;
        if (init) {
            TraverseStmt(init);
            asr_init = ASRUtils::EXPR(tmp.get());
        }

        tmp = ASR::make_Variable_t(al, Lloc(x), current_scope, s2c(al, name),
            nullptr, 0, ASR::intentType::InOut, asr_init, nullptr,
            ASR::storage_typeType::Default, type, nullptr, ASR::abiType::Source,
            ASR::accessType::Public, ASR::presenceType::Required, false);
        ASR::symbol_t* tmp_sym = ASR::down_cast<ASR::symbol_t>(tmp.get());
        current_scope->add_symbol(name, tmp_sym);
        tmp = ASR::make_Var_t(al, Lloc(x), tmp_sym);
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
        ASR::expr_t* arg = ASRUtils::EXPR(tmp.get());
        tmp = ASR::make_Cast_t(al, Lloc(x), arg, asr_cast_kind,
                ClangTypeToASRType(x->getType()), nullptr);
        is_stmt_created = false;
        return true;
    }

    bool TraverseMemberExpr(clang::MemberExpr* x) {
        member_name_obj.set(x->getMemberDecl()->getNameAsString());
        return clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseMemberExpr(x);
    }

    bool TraverseCXXMemberCallExpr(clang::CXXMemberCallExpr* x) {
        clang::Expr* callee = x->getCallee();
        TraverseStmt(callee);
        ASR::expr_t* asr_callee = ASRUtils::EXPR(tmp.get());
        if( !check_and_handle_special_function(x, asr_callee) ) {
             throw std::runtime_error("Only xt::xtensor::shape, "
                "xt::xtensor::fill, xt::xtensor::size is supported.");
        }

        return true;
    }

    bool TraverseCXXOperatorCallExpr(clang::CXXOperatorCallExpr* x) {
        #define generate_code_for_binop(op) if( x->getNumArgs() != 2 ) {    \
                throw std::runtime_error(cxx_operator_name + " accepts two arguments, found " + std::to_string(x->getNumArgs()));    \
            }    \
            clang::Expr** args = x->getArgs();    \
            TraverseStmt(args[0]);    \
            ASR::expr_t* obj = ASRUtils::EXPR(tmp.get());    \
            TraverseStmt(args[1]);    \
            ASR::expr_t* value = ASRUtils::EXPR(tmp.get());    \
            if( ASRUtils::is_array(ASRUtils::expr_type(obj)) ||    \
                ASRUtils::is_complex(*ASRUtils::expr_type(obj)) ||    \
                ASRUtils::is_array(ASRUtils::expr_type(value)) ||    \
                ASRUtils::is_complex(*ASRUtils::expr_type(value)) ) {    \
                CreateBinOp(obj, value, op, Lloc(x));    \
            } else {    \
                throw std::runtime_error(cxx_operator_name + " is supported only for arrays and complex types, found "    \
                    + ASRUtils::type_to_str(ASRUtils::expr_type(obj)));    \
            }    \

        #define generate_code_for_cmpop(op) if( x->getNumArgs() != 2 ) {    \
                throw std::runtime_error(cxx_operator_name + " accepts two arguments, found " + std::to_string(x->getNumArgs()));    \
            }    \
            clang::Expr** args = x->getArgs();    \
            TraverseStmt(args[0]);    \
            ASR::expr_t* obj = ASRUtils::EXPR(tmp.get());    \
            TraverseStmt(args[1]);    \
            ASR::expr_t* value = ASRUtils::EXPR(tmp.get());    \
            if( ASRUtils::is_array(ASRUtils::expr_type(obj)) ||    \
                ASRUtils::is_complex(*ASRUtils::expr_type(obj)) ||    \
                ASRUtils::is_array(ASRUtils::expr_type(value)) ||    \
                ASRUtils::is_complex(*ASRUtils::expr_type(value)) ) {    \
                CreateCompareOp(obj, value, op, Lloc(x));    \
            } else {    \
                throw std::runtime_error(cxx_operator_name + " is supported only for arrays and complex types, found "    \
                    + ASRUtils::type_to_str(ASRUtils::expr_type(obj)));    \
            }    \

        clang::Expr* callee = x->getCallee();
        TraverseStmt(callee);
        std::string cxx_operator_name = cxx_operator_name_obj.get();
        if( cxx_operator_name == "operator<<" ) {
            if( print_args == nullptr ) {
                print_args = al.make_new<Vec<ASR::expr_t*>>();
                print_args->reserve(al, 1);
            }
            clang::Expr** args = x->getArgs();
            TraverseStmt(args[1]);
            cxx_operator_name = cxx_operator_name_obj.get();
            if( cxx_operator_name.size() == 0 && print_args != nullptr && tmp != nullptr ) {
                ASR::expr_t* arg = ASRUtils::EXPR(tmp.get());
                print_args->push_back(al, arg);
            }
            TraverseStmt(args[0]);
            cxx_operator_name = cxx_operator_name_obj.get();
            if( cxx_operator_name == "cout" ) {
                Vec<ASR::expr_t*> print_args_vec;
                print_args_vec.reserve(al, print_args->size());
                for( int i = print_args->size() - 1; i >= 0; i-- ) {
                    print_args_vec.push_back(al, print_args->p[i]);
                }
                tmp = ASR::make_Print_t(al, Lloc(x), print_args_vec.p,
                    print_args_vec.size(), nullptr, nullptr);
                print_args = nullptr;
                is_stmt_created = true;
            }
        } else if( cxx_operator_name == "operator()" ) {
            clang::Expr** args = x->getArgs();
            if( x->getNumArgs() == 0 ) {
                throw std::runtime_error("operator() needs at least the callee to be present.");
            }

            TraverseStmt(args[0]);
            ASR::expr_t* obj = ASRUtils::EXPR(tmp.get());
            if( ASRUtils::is_array(ASRUtils::expr_type(obj)) ) {
                Vec<ASR::array_index_t> array_indices;
                array_indices.reserve(al, x->getNumArgs() - 1);
                for( size_t i = 1; i < x->getNumArgs(); i++ ) {
                    TraverseStmt(args[i]);
                    ASR::expr_t* index = ASRUtils::EXPR(tmp.get());
                    ASR::array_index_t array_index;
                    array_index.loc = index->base.loc;
                    array_index.m_left = nullptr;
                    array_index.m_right = index;
                    array_index.m_step = nullptr;
                    array_indices.push_back(al, array_index);
                }
                tmp = ASR::make_ArrayItem_t(al, Lloc(x), obj,
                    array_indices.p, array_indices.size(),
                    ASRUtils::extract_type(ASRUtils::expr_type(obj)),
                    ASR::arraystorageType::RowMajor, nullptr);
            } else {
                throw std::runtime_error("Only indexing arrays is supported for now with operator().");
            }
        } else if( cxx_operator_name == "operator=" ) {
            if( x->getNumArgs() != 2 ) {
                throw std::runtime_error("operator= accepts two arguments, found " + std::to_string(x->getNumArgs()));
            }

            clang::Expr** args = x->getArgs();
            TraverseStmt(args[0]);
            ASR::expr_t* obj = ASRUtils::EXPR(tmp.get());
            assignment_target = obj;
            if( ASRUtils::is_array(ASRUtils::expr_type(obj)) ) {
                TraverseStmt(args[1]);
                if( !is_stmt_created ) {
                    ASR::expr_t* value = ASRUtils::EXPR(tmp.get());
                    tmp = ASR::make_Assignment_t(al, Lloc(x), obj, value, nullptr);
                    is_stmt_created = true;
                }
                assignment_target = nullptr;
            } else {
                throw std::runtime_error("operator= is supported only for arrays.");
            }
        } else if( cxx_operator_name == "operator+" ) {
            generate_code_for_binop(ASR::binopType::Add);
        } else if( cxx_operator_name == "operator-" ) {
            if( x->getNumArgs() == 1 ) {
                clang::Expr** args = x->getArgs();    \
                TraverseStmt(args[0]);    \
                ASR::expr_t* obj = ASRUtils::EXPR(tmp.get());    \
                CreateUnaryMinus(obj, Lloc(x));
            } else {
                generate_code_for_binop(ASR::binopType::Sub);
            }
        } else if( cxx_operator_name == "operator/" ) {
            generate_code_for_binop(ASR::binopType::Div);
        } else if( cxx_operator_name == "operator*" ) {
            generate_code_for_binop(ASR::binopType::Mul);
        } else if( cxx_operator_name == "operator>" ) {
            generate_code_for_cmpop(ASR::cmpopType::Gt);
        } else if( cxx_operator_name == "operator<" ) {
            generate_code_for_cmpop(ASR::cmpopType::Lt);
        } else if( cxx_operator_name == "operator<=" ) {
            generate_code_for_cmpop(ASR::cmpopType::LtE);
        } else if( cxx_operator_name == "operator>=" ) {
            generate_code_for_cmpop(ASR::cmpopType::GtE);
        } else if( cxx_operator_name == "operator!=" ) {
            generate_code_for_cmpop(ASR::cmpopType::NotEq);
        } else {
            throw std::runtime_error("C++ operator is not supported yet, " + cxx_operator_name);
        }
        return true;
    }

    template <typename CallExprType>
    bool check_and_handle_special_function(
        CallExprType *x, ASR::expr_t* callee) {
        std::string func_name;
        std::string cxx_operator_name = cxx_operator_name_obj.get();
        std::string member_name = member_name_obj.get();
        if( cxx_operator_name.size() > 0 ) {
            func_name = cxx_operator_name;
        } else if( member_name.size() > 0 ) {
            if( callee == nullptr ) {
                throw std::runtime_error("Callee object not available.");
            }
            func_name = member_name;
        } else {
            ASR::Var_t* callee_Var = ASR::down_cast<ASR::Var_t>(callee);
            func_name = std::string(ASRUtils::symbol_name(callee_Var->m_v));
        }
        if (special_function_map.find(func_name) == special_function_map.end()) {
            return false;
        }
        SpecialFunc sf = special_function_map[func_name];
        Vec<ASR::expr_t*> args;
        args.reserve(al, 1);
        if( sf != SpecialFunc::View ) {
            bool skip_format_str = true;
            ASR::expr_t* assignment_target_copy = assignment_target;
            assignment_target = nullptr;
            for (auto *p : x->arguments()) {
                TraverseStmt(p);
                if (sf == SpecialFunc::Printf && skip_format_str) {
                    skip_format_str = false;
                    continue;
                }
                if( (tmp == nullptr || p->getStmtClass() ==
                     clang::Stmt::StmtClass::CXXDefaultArgExprClass ) ) {
                    args.push_back(al, nullptr);
                } else {
                    ASR::asr_t* tmp_ = tmp.get();
                    args.push_back(al, ASRUtils::EXPR(tmp_));
                }
            }
            assignment_target = assignment_target_copy;
        }
        if (sf == SpecialFunc::Printf) {
            tmp = ASR::make_Print_t(al, Lloc(x), args.p, args.size(), nullptr, nullptr);
            is_stmt_created = true;
        } else if (sf == SpecialFunc::View) {
            clang::Expr** view_args = x->getArgs();
            size_t view_nargs = x->getNumArgs();
            ASR::expr_t* assignment_target_copy = assignment_target;
            assignment_target = nullptr;
            TraverseStmt(view_args[0]);
            assignment_target = assignment_target_copy;
            ASR::expr_t* array = ASRUtils::EXPR(tmp.get());
            size_t rank = ASRUtils::extract_n_dims_from_ttype(ASRUtils::expr_type(array));
            Vec<ASR::array_index_t> array_section_indices;
            array_section_indices.reserve(al, rank);
            size_t i, j, result_dims = 0;
            for( i = 0, j = 1; j < view_nargs; j++, i++ ) {
                ASR::expr_t* assignment_target_copy = assignment_target;
                assignment_target = nullptr;
                TraverseStmt(view_args[j]);
                assignment_target = assignment_target_copy;
                ASR::array_index_t index;
                if( (tmp == nullptr && (is_all_called || is_range_called)) ||
                    (tmp == nullptr || view_args[j]->getStmtClass() ==
                        clang::Stmt::StmtClass::CXXDefaultArgExprClass ) ) {
                    index.loc = array->base.loc;
                    if( is_range_called ) {
                        index.m_left = range_start.get();
                        ASR::expr_t* range_end_ = range_end.get();
                        CreateBinOp(range_end_,
                            ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, index.loc,
                            1, ASRUtils::expr_type(range_end_))),
                            ASR::binopType::Sub, index.loc);
                        index.m_right = ASRUtils::EXPR(tmp.get());
                        index.m_step = range_step.get();
                        array_section_indices.push_back(al, index);
                    } else if( is_all_called ) {
                        index.m_left = ASRUtils::get_bound<SemanticError>(array, i + 1, "lbound", al);
                        index.m_right = ASRUtils::get_bound<SemanticError>(array, i + 1, "ubound", al);
                        index.m_step = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, index.loc, 1,
                            ASRUtils::TYPE(ASR::make_Integer_t(al, index.loc, 4))));
                        array_section_indices.push_back(al, index);
                    } else {
                        throw std::runtime_error("Neither xt::range nor xt::all is being called for slicing array.");
                    }
                    is_range_called = false;
                    is_all_called = false;
                    result_dims += 1;
                } else {
                    ASR::expr_t* arg_ = ASRUtils::EXPR(tmp.get());
                    index.loc = arg_->base.loc;
                    index.m_left = nullptr;
                    index.m_right = arg_;
                    index.m_step = nullptr;
                    array_section_indices.push_back(al, index);
                }
            }
            for( ; i < rank; i++ ) {
                ASR::array_index_t index;
                index.loc = array->base.loc;
                index.m_left = ASRUtils::get_bound<SemanticError>(array, i + 1, "lbound", al);
                index.m_right = ASRUtils::get_bound<SemanticError>(array, i + 1, "ubound", al);
                index.m_step = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, index.loc, 1,
                    ASRUtils::TYPE(ASR::make_Integer_t(al, index.loc, 4))));
                array_section_indices.push_back(al, index);
                result_dims += 1;
            }
            ASR::ttype_t* element_type = ASRUtils::extract_type(ASRUtils::expr_type(array));
            Vec<ASR::dimension_t> empty_dims; empty_dims.reserve(al, result_dims);
            for( size_t i = 0; i < result_dims; i++ ) {
                ASR::dimension_t dim;
                dim.loc = Lloc(x);
                dim.m_length = nullptr;
                dim.m_start = nullptr;
                empty_dims.push_back(al, dim);
            }
            ASR::ttype_t* array_section_type = ASRUtils::TYPE(ASR::make_Array_t(al, Lloc(x),
                element_type, empty_dims.p, empty_dims.size(), ASR::array_physical_typeType::DescriptorArray));
            tmp = ASR::make_ArraySection_t(al, Lloc(x), array, array_section_indices.p,
                array_section_indices.size(), array_section_type, nullptr);
        } else if (sf == SpecialFunc::Shape) {
            if( args.size() == 0 ) {
                throw std::runtime_error("Calling xt::xtensor::shape without dimension is not supported yet.");
            }

            ASR::expr_t* dim = args.p[0];
            int dim_value = -1;
            if( ASRUtils::extract_value(dim, dim_value) ) {
                dim = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, dim->base.loc,
                    dim_value + 1, ASRUtils::expr_type(dim)));
            } else {
                dim = ASRUtils::EXPR(ASR::make_IntegerBinOp_t(al, dim->base.loc,
                    dim, ASR::binopType::Add, ASRUtils::get_constant_one_with_given_type(
                        al, ASRUtils::expr_type(dim)), ASRUtils::expr_type(dim), nullptr));
            }

            tmp = ASR::make_ArraySize_t(al, Lloc(x), callee, dim,
                ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4)),
                nullptr);
        } else if (sf == SpecialFunc::Reshape) {
            size_t args_size = 0;
            for( size_t r = 0; r < args.size(); r++ ) {
                if( args.p[r] != nullptr ) {
                    args_size += 1;
                }
            }
            if( args_size != 1 ) {
                throw std::runtime_error("Calling xt::xtensor::reshape should be called only with target shape.");
            }

            ASR::expr_t* target_shape = args.p[0];
            size_t target_shape_rank = ASRUtils::extract_n_dims_from_ttype(ASRUtils::expr_type(target_shape));
            if( target_shape_rank != 1 ) {
                throw std::runtime_error("Target shape must be an array of one dimension only.");
            }
            size_t target_rank = ASRUtils::get_fixed_size_of_array(ASRUtils::expr_type(target_shape));
            size_t callee_rank = ASRUtils::extract_n_dims_from_ttype(ASRUtils::expr_type(callee));
            if( target_rank <= 0 ) {
                throw std::runtime_error("Target shape must be a constant sized array of one dimension.");
            }
            if( target_rank != callee_rank ) {
                throw std::runtime_error("Ranks must be same before and after reshaping.");
            }
            Vec<ASR::dimension_t> empty_dims;
            empty_dims.reserve(al, target_rank);
            for( size_t r = 0; r < target_rank; r++ ) {
                ASR::dimension_t dim;
                dim.loc = Lloc(x);
                dim.m_length = nullptr;
                dim.m_start = nullptr;
                empty_dims.push_back(al, dim);
            }

            ASR::array_physical_typeType target_physical_type = ASR::array_physical_typeType::DescriptorArray;
            bool override_physical_type = false;
            if( ASRUtils::extract_physical_type(ASRUtils::expr_type(callee)) ==
                ASR::array_physical_typeType::FixedSizeArray ) {
                target_physical_type = ASR::array_physical_typeType::FixedSizeArray;
                override_physical_type = true;
            }
            ASR::ttype_t* target_type = ASRUtils::make_Array_t_util(al, Lloc(x),
                ASRUtils::extract_type(ASRUtils::expr_type(callee)), empty_dims.p, empty_dims.size(),
                ASR::abiType::Source, false, target_physical_type, override_physical_type, false);
            ASR::expr_t* new_shape = ASRUtils::cast_to_descriptor(al, args.p[0]);
            tmp = ASR::make_ArrayReshape_t(al, Lloc(x), callee, new_shape, target_type, nullptr);
            tmp = ASR::make_Assignment_t(al, Lloc(x), callee, ASRUtils::EXPR(tmp.get()), nullptr);
            is_stmt_created = true;
        } else if (sf == SpecialFunc::Size) {
            if( args.size() != 0 ) {
                throw std::runtime_error("xt::xtensor::size should be called with only one argument.");
            }

            tmp = ASR::make_ArraySize_t(al, Lloc(x), callee, nullptr,
                ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4)),
                nullptr);
        } else if (sf == SpecialFunc::Fill) {
            if( args.size() != 1 ) {
                throw std::runtime_error("xt::xtensor::fill should be called with only one argument.");
            }
            if( ASRUtils::is_array(ASRUtils::expr_type(args.p[0])) ) {
                throw std::runtime_error("Argument of xt::xtensor::fill should be a scalar.");
            }

            ASR::expr_t* arg = args.p[0];
            ASRUtils::make_ArrayBroadcast_t_util(al, Lloc(x), callee, arg);
            tmp = ASR::make_Assignment_t(al, Lloc(x), callee, arg, nullptr);
            is_stmt_created = true;
        } else if( sf == SpecialFunc::All ) {
            // Handles xt::all() - no arguments
            // Handle with argument case later.
            is_all_called = true;
            tmp = nullptr;
        } else if( sf == SpecialFunc::Range ) {
            if( args.size() < 2 ) {
                throw std::runtime_error("xt::range accepts at least 2 arguments.");
            }
            range_start = args.p[0];
            range_end = args.p[1];
            if( args.size() > 2 ) {
                range_step = args.p[2];
            } else {
                range_step = ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                    al, args.p[0]->base.loc, 1, ASRUtils::expr_type(args.p[0])));
            }
            is_range_called = true;
            tmp = nullptr;
        } else if( sf == SpecialFunc::Any ) {
            tmp = ASRUtils::make_IntrinsicArrayFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicArrayFunctions::Any),
                args.p, args.size(), 0, ASRUtils::TYPE(ASR::make_Logical_t(
                    al, Lloc(x), 4)), nullptr);
        } else if( sf == SpecialFunc::Exp ) {
            tmp = ASRUtils::make_IntrinsicScalarFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicScalarFunctions::Exp),
                args.p, args.size(), 0, ASRUtils::expr_type(args.p[0]), nullptr);
        } else if( sf == SpecialFunc::Pow ) {
            if( args.size() != 2 ) {
                throw std::runtime_error("Pow accepts exactly two arguments.");
            }

            CreateBinOp(args.p[0], args.p[1], ASR::binopType::Pow, Lloc(x));
        } else if( sf == SpecialFunc::Abs ) {
            tmp = ASRUtils::make_IntrinsicScalarFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicScalarFunctions::Abs),
                args.p, args.size(), 0, ASRUtils::expr_type(args.p[0]), nullptr);
        } else if( sf == SpecialFunc::AMax ) {
            if( args.size() > 1 && args.p[1] != nullptr ) {
                throw std::runtime_error("dim argument not yet supported with " + func_name);
            }
            tmp = ASRUtils::make_IntrinsicArrayFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicArrayFunctions::MaxVal),
                args.p, 1, 0, ASRUtils::extract_type(ASRUtils::expr_type(args.p[0])),
                nullptr);
        }  else if( sf == SpecialFunc::Sum ) {
            if( args.size() > 1 && args.p[1] != nullptr ) {
                throw std::runtime_error("dim argument not yet supported with " + func_name);
            }
            tmp = ASRUtils::make_IntrinsicArrayFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicArrayFunctions::Sum),
                args.p, 1, 0, ASRUtils::extract_type(ASRUtils::expr_type(args.p[0])),
                nullptr);
        } else if( sf == SpecialFunc::NotEqual ) {
            ASR::expr_t* arg1 = args.p[0];
            ASR::expr_t* arg2 = args.p[1];
            CreateCompareOp(arg1, arg2, ASR::cmpopType::NotEq, Lloc(x));
        } else if( sf == SpecialFunc::Equal ) {
            ASR::expr_t* arg1 = args.p[0];
            ASR::expr_t* arg2 = args.p[1];
            CreateCompareOp(arg1, arg2, ASR::cmpopType::Eq, Lloc(x));
        } else if (sf == SpecialFunc::Exit) {
            LCOMPILERS_ASSERT(args.size() == 1);
            int code = 0;
            ASRUtils::extract_value(args[0], code);
            if( code != 0 ) {
                tmp = ASR::make_ErrorStop_t(al, Lloc(x), args[0]);
            } else {
                tmp = ASR::make_Stop_t(al, Lloc(x), args[0]);
            }
            is_stmt_created = true;
        } else if (sf == SpecialFunc::Empty) {
            if( args.size() != 1 ) {
                throw std::runtime_error("xt::empty must be provided with shape.");
            }
            if( assignment_target == nullptr ) {
                throw std::runtime_error("xt::empty should be used only in assignment statement.");
            }
            if( !ASRUtils::is_allocatable(assignment_target) ) {
                throw std::runtime_error("Assignment target must be an alloctable");
            }

            if( ASR::is_a<ASR::ArrayConstant_t>(*args.p[0]) ) {
                ASR::ArrayConstant_t* array_constant = ASR::down_cast<ASR::ArrayConstant_t>(args.p[0]);
                size_t target_rank = ASRUtils::extract_n_dims_from_ttype(
                    ASRUtils::expr_type(assignment_target));
                if( array_constant->n_args != target_rank ) {
                    throw std::runtime_error("Assignment target must be of same rank as the size of the shape array.");
                }

                Vec<ASR::alloc_arg_t> alloc_args; alloc_args.reserve(al, 1);
                ASR::alloc_arg_t alloc_arg; alloc_arg.loc = Lloc(x);
                alloc_arg.m_a = assignment_target;
                alloc_arg.m_len_expr = nullptr; alloc_arg.m_type = nullptr;
                Vec<ASR::dimension_t> alloc_dims; alloc_dims.reserve(al, target_rank);
                for( size_t i = 0; i < target_rank; i++ ) {
                    ASR::dimension_t alloc_dim;
                    alloc_dim.loc = Lloc(x);
                    alloc_dim.m_length = array_constant->m_args[i];
                    alloc_dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                        al, Lloc(x), 0, ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4))));
                    alloc_dims.push_back(al, alloc_dim);
                }
                alloc_arg.m_dims = alloc_dims.p; alloc_arg.n_dims = alloc_dims.size();
                alloc_args.push_back(al, alloc_arg);
                tmp = ASR::make_Allocate_t(al, Lloc(x), alloc_args.p, alloc_args.size(),
                    nullptr, nullptr, nullptr);
                is_stmt_created = true;
            } else {
                throw std::runtime_error("Only {...} is allowed for supplying shape to xt::empty.");
            }
        } else if (sf == SpecialFunc::Iota) {
            tmp = ASR::make_ComplexConstant_t(al, Lloc(x), 0.0, 1.0,
                ASRUtils::TYPE(ASR::make_Complex_t(al, Lloc(x), 8)));
        } else {
            throw std::runtime_error("Only printf and exit special functions supported");
        }
        return true;
    }

    bool TraverseCallExpr(clang::CallExpr *x) {
        TraverseStmt(x->getCallee());
        ASR::expr_t* callee = nullptr;
        if( tmp != nullptr ) {
            callee = ASRUtils::EXPR(tmp.get());
        }
        if( check_and_handle_special_function(x, callee) ) {
            return true;
        }

        clang::Expr** args = x->getArgs();
        size_t n_args = x->getNumArgs();
        Vec<ASR::call_arg_t> call_args;
        call_args.reserve(al, n_args);
        for( size_t i = 0; i < n_args; i++ ) {
            TraverseStmt(args[i]);
            ASR::expr_t* arg = ASRUtils::EXPR(tmp.get());
            ASR::call_arg_t call_arg;
            call_arg.loc = arg->base.loc;
            call_arg.m_value = arg;
            call_args.push_back(al, call_arg);
        }

        ASR::Var_t* callee_Var = ASR::down_cast<ASR::Var_t>(callee);
        ASR::symbol_t* callee_sym = callee_Var->m_v;
        const clang::QualType& qual_type = x->getCallReturnType(*Context);
        ASR::ttype_t* return_type = ClangTypeToASRType(qual_type);
        if( return_type == nullptr ) {
            tmp = ASRUtils::make_SubroutineCall_t_util(al, Lloc(x), callee_sym,
                callee_sym, call_args.p, call_args.size(), nullptr,
                nullptr, false);
            is_stmt_created = true;
        } else {
            tmp = ASRUtils::make_FunctionCall_t_util(al, Lloc(x), callee_sym,
                callee_sym, call_args.p, call_args.size(), return_type,
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
            args.push_back(al, ASRUtils::EXPR(tmp.get()));
        }

        ASR::ttype_t* return_type = ClangTypeToASRType(x->getReturnType());
        ASR::symbol_t* return_sym = nullptr;
        ASR::expr_t* return_var = nullptr;
        if (return_type != nullptr) {
            return_sym = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, Lloc(x),
                current_scope, s2c(al, "__return_var"), nullptr, 0, ASR::intentType::ReturnVar, nullptr, nullptr,
                ASR::storage_typeType::Default, return_type, nullptr, ASR::abiType::Source, ASR::accessType::Public,
                ASR::presenceType::Required, false));
            current_scope->add_symbol("__return_var", return_sym);
        }

        Vec<ASR::stmt_t*>* current_body_copy = current_body;
        Vec<ASR::stmt_t*> body; body.reserve(al, 1);
        current_body = &body;
        if( x->hasBody() ) {
            TraverseStmt(x->getBody());
        }
        current_body = current_body_copy;

        if (return_type != nullptr) {
            return_var = ASRUtils::EXPR(ASR::make_Var_t(al, return_sym->base.loc, return_sym));
        }
        tmp = ASRUtils::make_Function_t_util(al, Lloc(x), current_scope, s2c(al, name), nullptr, 0,
            args.p, args.size(), body.p, body.size(), return_var, ASR::abiType::Source, ASR::accessType::Public,
            ASR::deftypeType::Implementation, nullptr, false, false, false, false, false, nullptr, 0,
            false, false, false);
        parent_scope->add_symbol(name, ASR::down_cast<ASR::symbol_t>(tmp.get()));
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
                current_body->push_back(al, ASRUtils::STMT(tmp.get()));
            }
        }
        is_stmt_created = false;
        return true;
    }

    bool TraverseCXXConstructExpr(clang::CXXConstructExpr* x) {
        if( x->getNumArgs() >= 0 ) {
            return clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseCXXConstructExpr(x);
        }
        tmp = nullptr;
        return true;
    }

    void add_reshape_if_needed(ASR::expr_t*& expr, ASR::expr_t* target_expr) {
        ASR::ttype_t* expr_type = ASRUtils::expr_type(expr);
        ASR::ttype_t* target_expr_type = ASRUtils::expr_type(target_expr);
        ASR::dimension_t *expr_dims = nullptr, *target_expr_dims = nullptr;
        size_t expr_rank = ASRUtils::extract_dimensions_from_ttype(expr_type, expr_dims);
        size_t target_expr_rank = ASRUtils::extract_dimensions_from_ttype(target_expr_type, target_expr_dims);
        if( expr_rank == target_expr_rank ) {
            return ;
        }

        const Location& loc = expr->base.loc;
        Vec<ASR::expr_t*> new_shape_; new_shape_.reserve(al, target_expr_rank);
        for( size_t i = 0; i < target_expr_rank; i++ ) {
            new_shape_.push_back(al, ASRUtils::get_size(target_expr, i + 1, al));
        }

        Vec<ASR::dimension_t> new_shape_dims; new_shape_dims.reserve(al, 1);
        ASR::dimension_t new_shape_dim; new_shape_dim.loc = loc;
        new_shape_dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, loc,
            target_expr_rank, ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4))));
        new_shape_dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, loc,
            0, ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4))));
        new_shape_dims.push_back(al, new_shape_dim);
        ASR::ttype_t* new_shape_type = ASRUtils::TYPE(ASR::make_Array_t(al, loc,
            ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4)), new_shape_dims.p,
            new_shape_dims.size(), ASR::array_physical_typeType::FixedSizeArray));
        ASR::expr_t* new_shape = ASRUtils::EXPR(ASR::make_ArrayConstant_t(al, loc,
            new_shape_.p, new_shape_.size(), new_shape_type, ASR::arraystorageType::RowMajor));
        new_shape = ASRUtils::cast_to_descriptor(al, new_shape);

        ASR::ttype_t* reshaped_expr_type = target_expr_type;
        if( ASRUtils::is_fixed_size_array(expr_type) ) {
            reshaped_expr_type = ASRUtils::duplicate_type_with_empty_dims(al,
                ASRUtils::type_get_past_allocatable(
                    ASRUtils::type_get_past_pointer(target_expr_type)),
                ASR::array_physical_typeType::FixedSizeArray, true);
        }
        ASR::expr_t* reshaped_expr = ASRUtils::EXPR(ASR::make_ArrayReshape_t(al, loc, expr,
            new_shape, reshaped_expr_type, nullptr));
        expr = reshaped_expr;
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
            tmp = nullptr;
            ASR::expr_t* var = ASRUtils::EXPR(ASR::make_Var_t(al, Lloc(x), v));
            assignment_target = var;
            TraverseStmt(x->getInit());
            assignment_target = nullptr;
            if( tmp != nullptr && !is_stmt_created ) {
                ASR::expr_t* init_val = ASRUtils::EXPR(tmp.get());
                add_reshape_if_needed(init_val, var);
                tmp = ASR::make_Assignment_t(al, Lloc(x), var, init_val, nullptr);
                is_stmt_created = true;
            }
        }
        return true;
    }

    void flatten_ArrayConstant(ASR::expr_t* array_constant) {
        if( !ASRUtils::is_array(ASRUtils::expr_type(array_constant)) ) {
            return ;
        }

        LCOMPILERS_ASSERT(ASR::is_a<ASR::ArrayConstant_t>(*array_constant));
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

    bool extract_dimensions_from_array_type(ASR::ttype_t* array_type,
        Vec<ASR::dimension_t>& alloc_dims) {
        if( !ASRUtils::is_array(array_type) ) {
            return false;
        }

        ASR::Array_t* array_t = ASR::down_cast<ASR::Array_t>(
            ASRUtils::type_get_past_allocatable(
                ASRUtils::type_get_past_pointer(
                    ASRUtils::type_get_past_const(array_type))));
        for( int i = 0; i < array_t->n_dims; i++ ) {
            alloc_dims.push_back(al, array_t->m_dims[i]);
        }

        extract_dimensions_from_array_type(array_t->m_type, alloc_dims);
        return true;
    }

    void create_allocate_stmt(ASR::expr_t* var, ASR::ttype_t* array_type) {
        if( var == nullptr || !ASRUtils::is_allocatable(var) ) {
            return ;
        }

        const Location& loc = var->base.loc;
        Vec<ASR::dimension_t> alloc_dims; alloc_dims.reserve(al, 1);
        if( extract_dimensions_from_array_type(array_type, alloc_dims) ) {
            Vec<ASR::alloc_arg_t> alloc_args; alloc_args.reserve(al, 1);
            ASR::alloc_arg_t alloc_arg; alloc_arg.loc = loc;
            alloc_arg.m_a = var;
            alloc_arg.m_dims = alloc_dims.p; alloc_arg.n_dims = alloc_dims.size();
            alloc_arg.m_type = nullptr; alloc_arg.m_len_expr = nullptr;
            alloc_args.push_back(al, alloc_arg);
            ASR::stmt_t* allocate_stmt = ASRUtils::STMT(ASR::make_Allocate_t(al, loc,
                alloc_args.p, alloc_args.size(), nullptr, nullptr, nullptr));
            current_body->push_back(al, allocate_stmt);
        }
    }

    bool TraverseInitListExpr(clang::InitListExpr* x) {
        Vec<ASR::expr_t*> init_exprs;
        init_exprs.reserve(al, x->getNumInits());
        clang::Expr** clang_inits = x->getInits();
        ASR::expr_t* assignment_target_copy = assignment_target;
        assignment_target = nullptr;
        for( size_t i = 0; i < x->getNumInits(); i++ ) {
            TraverseStmt(clang_inits[i]);
            init_exprs.push_back(al, ASRUtils::EXPR(tmp.get()));
        }
        assignment_target = assignment_target_copy;
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
        create_allocate_stmt(assignment_target, type);
        flatten_ArrayConstant(array_constant);
        tmp = (ASR::asr_t*) array_constant;
        return true;
    }

    void CreateCompareOp(ASR::expr_t* lhs, ASR::expr_t* rhs,
        ASR::cmpopType cmpop_type, const Location& loc) {
        cast_helper(lhs, rhs, false);
        ASRUtils::make_ArrayBroadcast_t_util(al, loc, lhs, rhs);
        ASR::dimension_t* m_dims;
        size_t n_dims = ASRUtils::extract_dimensions_from_ttype(
            ASRUtils::expr_type(lhs), m_dims);
        ASR::ttype_t* result_type = ASRUtils::TYPE(ASR::make_Logical_t(al, loc, 4));
        if( n_dims > 0 ) {
            result_type = ASRUtils::make_Array_t_util(al, loc, result_type, m_dims, n_dims);
        }
        if( ASRUtils::is_integer(*ASRUtils::expr_type(lhs)) &&
            ASRUtils::is_integer(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_IntegerCompare_t(al, loc, lhs,
                cmpop_type, rhs, result_type, nullptr);
        } else if( ASRUtils::is_real(*ASRUtils::expr_type(lhs)) &&
                   ASRUtils::is_real(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_RealCompare_t(al, loc, lhs,
                cmpop_type, rhs, result_type, nullptr);
        } else if( ASRUtils::is_logical(*ASRUtils::expr_type(lhs)) &&
                   ASRUtils::is_logical(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_LogicalCompare_t(al, loc, lhs,
                cmpop_type, rhs, result_type, nullptr);
        } else if( ASRUtils::is_complex(*ASRUtils::expr_type(lhs)) &&
                   ASRUtils::is_complex(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_ComplexCompare_t(al, loc, lhs,
                cmpop_type, rhs, ASRUtils::expr_type(lhs), nullptr);
        } else {
            throw std::runtime_error("Only integer, real and complex types are supported so "
                "far for comparison operator, found: " + ASRUtils::type_to_str(ASRUtils::expr_type(lhs))
                + " and " + ASRUtils::type_to_str(ASRUtils::expr_type(rhs)));
        }
    }

    void CreateBinOp(ASR::expr_t* lhs, ASR::expr_t* rhs,
        ASR::binopType binop_type, const Location& loc) {
        cast_helper(lhs, rhs, false);
        ASRUtils::make_ArrayBroadcast_t_util(al, loc, lhs, rhs);
        if( ASRUtils::is_integer(*ASRUtils::expr_type(lhs)) &&
            ASRUtils::is_integer(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_IntegerBinOp_t(al, loc, lhs,
                binop_type, rhs, ASRUtils::expr_type(lhs), nullptr);
        } else if( ASRUtils::is_real(*ASRUtils::expr_type(lhs)) &&
                   ASRUtils::is_real(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_RealBinOp_t(al, loc, lhs,
                binop_type, rhs, ASRUtils::expr_type(lhs), nullptr);
        } else if( ASRUtils::is_complex(*ASRUtils::expr_type(lhs)) &&
                   ASRUtils::is_complex(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_ComplexBinOp_t(al, loc, lhs,
                binop_type, rhs, ASRUtils::expr_type(lhs), nullptr);
        } else {
            throw std::runtime_error("Only integer, real and complex types are supported so "
                "far for binary operator, found: " + ASRUtils::type_to_str(ASRUtils::expr_type(lhs))
                + " and " + ASRUtils::type_to_str(ASRUtils::expr_type(rhs)));
        }
    }

    void CreateUnaryMinus(ASR::expr_t* op, const Location& loc) {
        if( ASRUtils::is_integer(*ASRUtils::expr_type(op)) ) {
            tmp = ASR::make_IntegerUnaryMinus_t(al, loc, op,
                ASRUtils::expr_type(op), nullptr);
        } else if( ASRUtils::is_real(*ASRUtils::expr_type(op)) ) {
            tmp = ASR::make_RealUnaryMinus_t(al, loc, op,
                ASRUtils::expr_type(op), nullptr);
        } else {
            throw std::runtime_error("Only integer and real types are supported so "
                "far for unary operator, found: " + ASRUtils::type_to_str(ASRUtils::expr_type(op)));
        }
    }

    bool TraverseBinaryOperator(clang::BinaryOperator *x) {
        clang::BinaryOperatorKind op = x->getOpcode();
        TraverseStmt(x->getLHS());
        ASR::expr_t* x_lhs = ASRUtils::EXPR(tmp.get());
        TraverseStmt(x->getRHS());
        ASR::expr_t* x_rhs = ASRUtils::EXPR(tmp.get());
        if( op == clang::BO_Assign ) {
            cast_helper(x_lhs, x_rhs, true);
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
                case clang::BO_NE: {
                    cmpop_type = ASR::cmpopType::NotEq;
                    is_cmpop = true;
                    break;
                }
                case clang::BO_LT: {
                    cmpop_type = ASR::cmpopType::Lt;
                    is_cmpop = true;
                    break;
                }
                case clang::BO_LE: {
                    cmpop_type = ASR::cmpopType::LtE;
                    is_cmpop = true;
                    break;
                }
                case clang::BO_GT: {
                    cmpop_type = ASR::cmpopType::Gt;
                    is_cmpop = true;
                    break;
                }
                case clang::BO_GE: {
                    cmpop_type = ASR::cmpopType::GtE;
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
                CreateCompareOp(x_lhs, x_rhs, cmpop_type, Lloc(x));
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
        if( name == "operator<<" || name == "cout" || name == "endl" ||
            name == "operator()" || name == "operator+" || name == "operator=" ||
            name == "operator*" || name == "view" || name == "empty" ||
            name == "all" || name == "any" || name == "not_equal" ||
            name == "exit" || name == "printf" || name == "exp" ||
            name == "sum" || name == "amax" || name == "abs" ||
            name == "operator-" || name == "operator/" || name == "operator>" ||
            name == "range" || name == "pow" || name == "equal" ||
            name == "operator<" || name == "operator<=" || name == "operator>=" ||
            name == "operator!=" || name == "operator\"\"i" ) {
            if( sym != nullptr && ASR::is_a<ASR::Function_t>(
                    *ASRUtils::symbol_get_past_external(sym)) ) {
                throw std::runtime_error("Special function " + name + " cannot be overshadowed yet.");
            }
            if( sym == nullptr ) {
                cxx_operator_name_obj.set(name);
                tmp = nullptr;
                return true;
            }
        }
        if( sym == nullptr ) {
            throw std::runtime_error("Symbol " + name + " not found in current scope.");
        }
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

    void cast_helper(ASR::expr_t*& left, ASR::expr_t*& right, bool is_assign) {
        bool no_cast = ((ASR::is_a<ASR::Pointer_t>(*ASRUtils::expr_type(left)) &&
                        ASR::is_a<ASR::Var_t>(*left)) ||
                        (ASR::is_a<ASR::Pointer_t>(*ASRUtils::expr_type(right)) &&
                        ASR::is_a<ASR::Var_t>(*right)));
        ASR::ttype_t *right_type = ASRUtils::expr_type(right);
        ASR::ttype_t *left_type = ASRUtils::expr_type(left);
        left_type = ASRUtils::extract_type(left_type);
        right_type = ASRUtils::extract_type(right_type);
        if( no_cast ) {
            int lkind = ASRUtils::extract_kind_from_ttype_t(left_type);
            int rkind = ASRUtils::extract_kind_from_ttype_t(right_type);
            if( left_type->type != right_type->type || lkind != rkind ) {
                throw SemanticError("Casting for mismatching pointer types not supported yet.",
                                    right_type->base.loc);
            }
        }

        // Handle presence of logical types in binary operations
        // by converting them into 32-bit integers.
        // See integration_tests/test_bool_binop.py for its significance.
        if(!is_assign && ASRUtils::is_logical(*left_type) && ASRUtils::is_logical(*right_type) ) {
            ASR::ttype_t* dest_type = ASRUtils::TYPE(ASR::make_Integer_t(al, left_type->base.loc, 4));
            ASR::ttype_t* dest_type_left = dest_type;
            if( ASRUtils::is_array(left_type) ) {
                ASR::dimension_t* m_dims = nullptr;
                size_t n_dims = ASRUtils::extract_dimensions_from_ttype(left_type, m_dims);
                dest_type_left = ASRUtils::make_Array_t_util(al, dest_type->base.loc,
                    dest_type, m_dims, n_dims);
            }
            ASR::ttype_t* dest_type_right = dest_type;
            if( ASRUtils::is_array(right_type) ) {
                ASR::dimension_t* m_dims = nullptr;
                size_t n_dims = ASRUtils::extract_dimensions_from_ttype(right_type, m_dims);
                dest_type_right = ASRUtils::make_Array_t_util(al, dest_type->base.loc,
                    dest_type, m_dims, n_dims);
            }
            left = CastingUtil::perform_casting(left, left_type, dest_type_left, al, left->base.loc);
            right = CastingUtil::perform_casting(right, right_type, dest_type_right, al, right->base.loc);
            return ;
        }

        ASR::ttype_t *src_type = nullptr, *dest_type = nullptr;
        ASR::expr_t *src_expr = nullptr, *dest_expr = nullptr;
        int casted_expression_signal = CastingUtil::get_src_dest(
            left, right, src_expr, dest_expr, src_type, dest_type, is_assign);
        if( casted_expression_signal == 2 ) {
            return ;
        }
        src_expr = CastingUtil::perform_casting(
            src_expr, src_type, dest_type, al,
            src_expr->base.loc);
        if( casted_expression_signal == 0 ) {
            left = src_expr;
            right = dest_expr;
        } else if( casted_expression_signal == 1 ) {
            left = dest_expr;
            right = src_expr;
        }
    }

    bool TraverseUserDefinedLiteral(clang::UserDefinedLiteral* x) {
        clang::Expr* clang_cooked_literal = x->getCookedLiteral();
        TraverseStmt(clang_cooked_literal);
        ASR::expr_t* cooked_literal = ASRUtils::EXPR(tmp.get());

        clang::Expr* clang_literal = x->getCallee();
        TraverseStmt(clang_literal);
        if( tmp != nullptr ) {
            throw std::runtime_error("User defined literals only work for operator\"\"i.");
        }
        if( !check_and_handle_special_function(x, nullptr) ) {
            throw std::runtime_error("User defined literals only work for operator\"\"i.");
        }
        ASR::expr_t* literal = ASRUtils::EXPR(tmp.get());
        cast_helper(literal, cooked_literal, false);
        tmp = ASR::make_ComplexBinOp_t(al, Lloc(x), literal, ASR::binopType::Mul,
            cooked_literal, ASRUtils::expr_type(literal), nullptr);
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
                current_body->push_back(al, ASRUtils::STMT(tmp.get()));
            }
            is_stmt_created_ = is_stmt_created;
        }
        return true;
    }

    bool TraverseReturnStmt(clang::ReturnStmt *x) {
        ASR::symbol_t* return_sym = current_scope->resolve_symbol("__return_var");
        ASR::expr_t* return_var = ASRUtils::EXPR(ASR::make_Var_t(al, Lloc(x), return_sym));
        TraverseStmt(x->getRetValue());
        tmp = ASR::make_Assignment_t(al, Lloc(x), return_var, ASRUtils::EXPR(tmp.get()), nullptr);
        current_body->push_back(al, ASRUtils::STMT(tmp.get()));
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
        ASR::expr_t* array = flatten_ArrayItem(ASRUtils::EXPR(tmp.get()));
        clang::Expr* clang_index = x->getIdx();
        TraverseStmt(clang_index);
        ASR::expr_t* index = ASRUtils::EXPR(tmp.get());
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
        ASR::expr_t* x_lhs = ASRUtils::EXPR(tmp.get());
        TraverseStmt(x->getRHS());
        ASR::expr_t* x_rhs = ASRUtils::EXPR(tmp.get());
        CreateBinOp(x_lhs, x_rhs, ASR::binopType::Add, Lloc(x));
        ASR::expr_t* sum_expr = ASRUtils::EXPR(tmp.get());
        tmp = ASR::make_Assignment_t(al, Lloc(x), x_lhs, sum_expr, nullptr);
        is_stmt_created = true;
        return true;
    }

    bool TraverseUnaryOperator(clang::UnaryOperator* x) {
        clang::UnaryOperatorKind op = x->getOpcode();
        clang::Expr* expr = x->getSubExpr();
        TraverseStmt(expr);
        ASR::expr_t* var = ASRUtils::EXPR(tmp.get());
        switch( op ) {
            case clang::UnaryOperatorKind::UO_PostInc: {
                ASR::expr_t* incbyone = ASRUtils::EXPR(ASR::make_IntegerBinOp_t(
                    al, Lloc(x), var, ASR::binopType::Add,
                    ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                        al, Lloc(x), 1, ASRUtils::expr_type(var))),
                    ASRUtils::expr_type(var), nullptr));
                tmp = ASR::make_Assignment_t(al, Lloc(x), var, incbyone, nullptr);
                is_stmt_created = true;
                break;
            }
            case clang::UnaryOperatorKind::UO_Minus: {
                CreateUnaryMinus(var, Lloc(x));
                break;
            }
            default: {
                throw std::runtime_error("Only postfix increment and minus are supported so far.");
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
        current_body->push_back(al, ASRUtils::STMT(tmp.get()));

        clang::Expr* loop_cond = x->getCond();
        TraverseStmt(loop_cond);
        ASR::expr_t* test = ASRUtils::EXPR(tmp.get());

        Vec<ASR::stmt_t*> body; body.reserve(al, 1);
        Vec<ASR::stmt_t*>*current_body_copy = current_body;
        current_body = &body;
        clang::Stmt* loop_body = x->getBody();
        TraverseStmt(loop_body);
        clang::Stmt* inc_stmt = x->getInc();
        TraverseStmt(inc_stmt);
        LCOMPILERS_ASSERT(tmp != nullptr && is_stmt_created);
        body.push_back(al, ASRUtils::STMT(tmp.get()));
        current_body = current_body_copy;

        tmp = ASR::make_WhileLoop_t(al, Lloc(x), nullptr, test, body.p, body.size());
        is_stmt_created = true;
        scopes.pop_back();
        return true;
    }

    bool TraverseIfStmt(clang::IfStmt* x) {
        std::map<std::string, std::string> alias;
        scopes.push_back(alias);

        clang::Expr* if_cond = x->getCond();
        TraverseStmt(if_cond);
        ASR::expr_t* test = ASRUtils::EXPR(tmp.get());

        Vec<ASR::stmt_t*> then_body; then_body.reserve(al, 1);
        Vec<ASR::stmt_t*>*current_body_copy = current_body;
        current_body = &then_body;
        clang::Stmt* clang_then_body = x->getThen();
        TraverseStmt(clang_then_body);
        current_body = current_body_copy;

        Vec<ASR::stmt_t*> else_body; else_body.reserve(al, 1);
        if( x->hasElseStorage() ) {
            Vec<ASR::stmt_t*>*current_body_copy = current_body;
            current_body = &else_body;
            clang::Stmt* clang_else_body = x->getElse();
            TraverseStmt(clang_else_body);
            current_body = current_body_copy;
        }

        tmp = ASR::make_If_t(al, Lloc(x), test, then_body.p,
            then_body.size(), else_body.p, else_body.size());
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
            "lib/gcc",
            "lib/clang",
            "micromamba-root/envs"
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
