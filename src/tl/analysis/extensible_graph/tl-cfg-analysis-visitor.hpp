
#ifndef TL_CFG_ANALYSIS_VISITOR_HPP
#define TL_CFG_ANALYSIS_VISITOR_HPP

#include "tl-node.hpp"
#include "tl-nodecl-visitor.hpp"

namespace TL
{
    // FIXME Here we don't want anything to be returned for the visit
    // We may build a basic visit for not returning anything
    class LIBTL_CLASS CfgAnalysisVisitor : public Nodecl::NodeclVisitor<void>
    {
    protected:
        //! Pointer to the Node in a CFG of type ExtensibleGraph where the Nodecl is contained
        //! The results of the analysis performed during the visit will be attached to the node
        Node* _node;
        
        //! State of the traversal
        //! This value will be true when the actual expression is a defined value
        //! Otherwise, when the value is just used, the value will be false
        bool _define;

    private:
        //! This method implements the visitor for any Literal
        //! It do nothing
        template <typename T>
        Ret literal_visit(const T& n);
        
        //! This method implements the visitor for any Binary operation
        /*!
         * \param n Nodecl containing the Binary operation
         */
        template <typename T>
        Ret binary_visit(const T& n);

        //! This method implements the visitor for any Binary Assignment operation
        /*!
         * \param n Nodecl containing the Binary Assignment operation
         */        
        template <typename T>
        Ret binary_assignment(const T& n);
        
        template <typename T>
        Ret unary_visit(const T& n);
        
        //! This method implements the visitor for any expression having nested expression
        template<typename T>
        Ret nested_visit(const T& n);
        
    public:
        // Constructors
        CfgAnalysisVisitor(Node* n);
        CfgAnalysisVisitor(const CfgAnalysisVisitor& v);
        
        // Visitors
        Ret unhandled_node(const Nodecl::NodeclBase& n);
        Ret visit(const Nodecl::Throw& n);
        Ret visit(const Nodecl::Symbol& n);
        Ret visit(const Nodecl::ExpressionStatement& n);        
        Ret visit(const Nodecl::ParenthesizedExpression& n);
        Ret visit(const Nodecl::ObjectInit& n);
        Ret visit(const Nodecl::ArraySubscript& n);
        Ret visit(const Nodecl::ClassMemberAccess& n);    
        Ret visit(const Nodecl::Concat& n);
        Ret visit(const Nodecl::New& n);
        Ret visit(const Nodecl::Delete& n);
        Ret visit(const Nodecl::DeleteArray& n);
        Ret visit(const Nodecl::Sizeof& n);
        Ret visit(const Nodecl::Type& n);
        Ret visit(const Nodecl::Typeid& n);
        Ret visit(const Nodecl::Cast& n);
        Ret visit(const Nodecl::Offset& n);
        Ret visit(const Nodecl::VirtualFunctionCall& n);
        Ret visit(const Nodecl::FunctionCall& n);
        Ret visit(const Nodecl::StringLiteral& n);
        Ret visit(const Nodecl::BooleanLiteral& n);
        Ret visit(const Nodecl::IntegerLiteral& n);
        Ret visit(const Nodecl::ComplexLiteral& n);
        Ret visit(const Nodecl::FloatingLiteral& n);
        Ret visit(const Nodecl::StructuredValue& n);
        Ret visit(const Nodecl::EmptyStatement& n);
        Ret visit(const Nodecl::ReturnStatement& n);  
        Ret visit(const Nodecl::GotoStatement& n);
        Ret visit(const Nodecl::LabeledStatement& n);
        Ret visit(const Nodecl::Assignment& n);
        Ret visit(const Nodecl::AddAssignment& n);
        Ret visit(const Nodecl::SubAssignment& n);
        Ret visit(const Nodecl::DivAssignment& n);
        Ret visit(const Nodecl::MulAssignment& n);
        Ret visit(const Nodecl::ModAssignment& n);
        Ret visit(const Nodecl::BitwiseAndAssignment& n);
        Ret visit(const Nodecl::BitwiseOrAssignment& n);
        Ret visit(const Nodecl::BitwiseXorAssignment& n);
        Ret visit(const Nodecl::ShrAssignment& n);
        Ret visit(const Nodecl::ShlAssignment& n);
        Ret visit(const Nodecl::Add& n);
        Ret visit(const Nodecl::Minus& n);
        Ret visit(const Nodecl::Mul& n);
        Ret visit(const Nodecl::Div& n);
        Ret visit(const Nodecl::Mod& n);
        Ret visit(const Nodecl::Power& n);
        Ret visit(const Nodecl::LogicalAnd& n);
        Ret visit(const Nodecl::LogicalOr& n);
        Ret visit(const Nodecl::BitwiseAnd& n);
        Ret visit(const Nodecl::BitwiseOr& n);
        Ret visit(const Nodecl::BitwiseXor& n);
        Ret visit(const Nodecl::Equal& n);
        Ret visit(const Nodecl::Different& n);
        Ret visit(const Nodecl::LowerThan& n);
        Ret visit(const Nodecl::GreaterThan& n);
        Ret visit(const Nodecl::LowerOrEqualThan& n);
        Ret visit(const Nodecl::GreaterOrEqualThan& n);
        Ret visit(const Nodecl::Shr& n);
        Ret visit(const Nodecl::Shl& n);
        Ret visit(const Nodecl::Predecrement& n);
        Ret visit(const Nodecl::Postdecrement& n);
        Ret visit(const Nodecl::Preincrement& n);
        Ret visit(const Nodecl::Postincrement& n);
        Ret visit(const Nodecl::Plus& n);
        Ret visit(const Nodecl::Neg& n);     
        Ret visit(const Nodecl::BitwiseNot& n);
        Ret visit(const Nodecl::LogicalNot& n);
        Ret visit(const Nodecl::Derreference& n);
        Ret visit(const Nodecl::Reference& n);
        Ret visit(const Nodecl::Text& n);
        Ret visit(const Nodecl::Comma& n);
    };
}
    
#endif      // TL_CFG_ANALYSIS_VISITOR_HPP