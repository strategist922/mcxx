/*--------------------------------------------------------------------
(C) Copyright 2006-2013 Barcelona Supercomputing Center             *
Centro Nacional de Supercomputacion

This file is part of Mercurium C/C++ source-to-source compiler.

See AUTHORS file in the top level directory for information
regarding developers and contributors.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

Mercurium C/C++ source-to-source compiler is distributed in the hope
that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the GNU Lesser General Public License for more
details.

You should have received a copy of the GNU Lesser General Public
License along with Mercurium C/C++ source-to-source compiler; if
not, write to the Free Software Foundation, Inc., 675 Mass Ave,
Cambridge, MA 02139, USA.
--------------------------------------------------------------------*/

#include "cxx-cexpr.h"
#include "cxx-process.h"
#include "tl-nodecl-calc.hpp"
#include "tl-range-analysis.hpp"

#include <algorithm>
#include <fstream>
#include <list>
#include <queue>
#include <set>
#include <unistd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace TL {
namespace Analysis {
    
    // **************************************************************************************************** //
    // **************************** Visitor implementing constraint building ****************************** //
    
namespace {
        
#define RANGES_DEBUG
    
    static unsigned int cgnode_id = 0;
    
    unsigned int non_sym_constraint_id = 0;
    
    //! This maps stores the relationship between each variable in a given node and 
    //! the last identifier used to create a constraint for that variable
    std::map<NBase, unsigned int, Nodecl::Utils::Nodecl_structural_less> var_to_last_constraint_id;
    
    unsigned int get_next_id(const NBase& n)
    {
        unsigned int next_id = 0;
        if(!n.is_null())
        {
            if(var_to_last_constraint_id.find(n) != var_to_last_constraint_id.end())
                next_id = var_to_last_constraint_id[n] + 1;
            var_to_last_constraint_id[n] = next_id;
        }
        else
        {
            next_id = ++non_sym_constraint_id;
        }
        return next_id;
    }
    
    const_value_t* zero = const_value_get_zero(/*num_bytes*/ 4, /*sign*/1);
    const_value_t* one = const_value_get_one(/*num_bytes*/ 4, /*sign*/1);
    
    void print_constraint(std::string stmt_name, const Symbol& s, const NBase& val, const Type& t)
    {
#ifdef RANGES_DEBUG
        std::cerr << "    " << stmt_name << " Constraint " << s.get_name() << " = " << val.prettyprint() 
                  << " (" << t.print_declarator() << ")" << std::endl;
#endif
    }
    
    std::map<NBase, NBase, Nodecl::Utils::Nodecl_structural_less> CONSTRAINTS;
    ObjectList<NBase> ORDERED_CONSTRAINTS;
    
    Utils::Constraint build_constraint(const Symbol& s, const NBase& val, 
                                       const Type& t, std::string c_name)
    {
        // Create the constraint
        Utils::Constraint c = Utils::Constraint(s, val);
        
        // Insert the constraint in the global structures that will allow us building the Constraint Graph
        Nodecl::Symbol s_n = s.make_nodecl(/*set_ref_type*/false);
        if(CONSTRAINTS.find(s_n) == CONSTRAINTS.end())
            ORDERED_CONSTRAINTS.push_back(s_n);
        CONSTRAINTS[s_n] = val.no_conv();
        
        // Print the constraint in the standard error
        print_constraint(c_name, s, val, t);
        
        return c;
    }
}
    
    ConstraintReplacement::ConstraintReplacement(Utils::ConstraintMap constraints_map)
        : _constraints_map(constraints_map)
    {}
    
    void ConstraintReplacement::visit(const Nodecl::ArraySubscript& n)
    {
        if(_constraints_map.find(n) != _constraints_map.end())
            n.replace(_constraints_map[n].get_symbol().make_nodecl(/*set_ref_type*/false));
        else
        {
            walk(n.get_subscripted());
            walk(n.get_subscripts());
        }
    }
    
    void ConstraintReplacement::visit(const Nodecl::ClassMemberAccess& n)
    {
        if(_constraints_map.find(n) != _constraints_map.end())
            n.replace(_constraints_map[n].get_symbol().make_nodecl(/*set_ref_type*/false));
        else
        {
            walk(n.get_lhs());
            walk(n.get_member());
        }
    }
    
    void ConstraintReplacement::visit(const Nodecl::Symbol& n)
    {
        ERROR_CONDITION(_constraints_map.find(n) == _constraints_map.end(),
                        "No constraints found for symbol %s in locus %s. "
                        "We should replace the variable with the corresponding constraint.", 
                        n.prettyprint().c_str(), n.get_locus_str().c_str());
        
        n.replace(_constraints_map[n].get_symbol().make_nodecl(/*set_ref_type*/false));
    }
    
    ConstraintBuilderVisitor::ConstraintBuilderVisitor(Utils::ConstraintMap input_constraints_map, 
                                                       Utils::ConstraintMap current_constraints)
        : _input_constraints_map(input_constraints_map), _output_constraints_map(current_constraints), 
          _output_true_constraints_map(), _output_false_constraints_map()
    {}
    
    void ConstraintBuilderVisitor::compute_constraints(const NBase& n)
    {
        walk(n);
    }
    
    Utils::ConstraintMap ConstraintBuilderVisitor::get_output_constraints_map()
    {
        return _output_constraints_map;
    }

    Utils::ConstraintMap ConstraintBuilderVisitor::get_output_true_constraints_map()
    {
        return _output_true_constraints_map;
    }
    
    Utils::ConstraintMap ConstraintBuilderVisitor::get_output_false_constraints_map()
    {
        return _output_false_constraints_map;
    }
    
    void ConstraintBuilderVisitor::join_list(TL::ObjectList<Utils::Constraint>& list)
    {
        WARNING_MESSAGE("join_list of a list of constraint is not yet supported. Doing nothing.", 0);
    }
    
    Symbol ConstraintBuilderVisitor::get_condition_node_constraints(
            const NBase& lhs, const Type& t,
            std::string s_str, std::string nodecl_str)
    {
        Utils::Constraint c;
        NBase val;
        
        // 1. Compute the first constraint that corresponds to the current node: x COMP_OP c
        // -->    X1 = [0, 1]
        // 1.1 Get a new symbol for the constraint
//         std::stringstream ss; ss << get_next_id(NBase::null());
//         Symbol s_x(lhs.retrieve_context().new_symbol("_x_" + ss.str()));
//         s_x.set_type(t);
//         // 1.2 Build the value of the constraints
//         val = Nodecl::Range::make(const_value_to_nodecl(zero), const_value_to_nodecl(one), const_value_to_nodecl(one), t);
//         // 1.3 Build the actual constraint and insert it in the corresponding map
//         c = build_constraint(s_x, val, t, nodecl_str);
//         Symbol var_s(lhs.retrieve_context().new_symbol("_x"));
//         var_s.set_type(t);
//         Nodecl::Symbol var = var_s.make_nodecl(/*set_ref_type*/false);
//         _output_constraints_map[var] = c;
        
        // 2. Compute the second constraint that corresponds to the current node: x COMP_OP c
        // -->    X1 = X0
        // 2.1 Get a new symbol for the constraint
        std::stringstream ss_tmp; ss_tmp << get_next_id(lhs);
        Symbol s(lhs.retrieve_context().new_symbol(s_str + "_" + ss_tmp.str()));
        s.set_type(t);
        // 2.2 Build the value of the constraints
        val = _input_constraints_map[lhs].get_symbol().make_nodecl(/*set_ref_type*/false);
        
        // 2.3 Build the actual constraint and insert it in the corresponding map
        c = build_constraint(s, val, t, nodecl_str);
        _input_constraints_map[lhs] = c;
        _output_constraints_map[lhs] = c;
        
        return s;
    }

    void ConstraintBuilderVisitor::visit_assignment(const NBase& lhs, const NBase& rhs)
    {
        // Build a symbol for the new constraint based on the name of the original variable
        std::stringstream ss; ss << get_next_id(lhs);
        Symbol orig_s(Utils::get_nodecl_base(lhs).get_symbol());
        std::string constr_name = orig_s.get_name() + "_" + ss.str();
        Symbol s(lhs.retrieve_context().new_symbol(constr_name));
        Type t = orig_s.get_type();
        s.set_type(t);
        // Build the value of the constraint
        NBase val;
        if(rhs.is_constant())       // x = c;    -->    X1 = c
            val = Nodecl::Range::make(rhs.shallow_copy(), rhs.shallow_copy(), const_value_to_nodecl(zero), t);
        else 
        {   // Replace all the memory accesses by the symbols of the constraints arriving to the current node
            ConstraintReplacement cr(_input_constraints_map);
            val = rhs.shallow_copy();
            cr.walk(val);
        }
        
        // Build the constraint and insert it in the corresponding maps
        Utils::Constraint c = build_constraint(s, val, t, "Assignment");
        _input_constraints_map[lhs] = c;
        _output_constraints_map[lhs] = c;
    }
    
    
    void ConstraintBuilderVisitor::visit_increment(const NBase& rhs, bool positive)
    {
        ERROR_CONDITION(_input_constraints_map.find(rhs) == _input_constraints_map.end(), 
                        "Some input constraint required for the increment's RHS %s.\n", 
                        ast_print_node_type(rhs.get_kind()));
        
        // Build a symbol for the new constraint based on the name of the original variable
        std::stringstream ss; ss << get_next_id(rhs);
        Symbol orig_s(Utils::get_nodecl_base(rhs).get_symbol());
        Type t(orig_s.get_type());
        std::string constr_name = orig_s.get_name() + "_" + ss.str();
        Symbol s(rhs.retrieve_context().new_symbol(constr_name));
        s.set_type(t);
        
        NBase val;
        if(positive)
        {
            val = Nodecl::Add::make(_input_constraints_map[rhs].get_symbol().make_nodecl(/*set_ref_type*/false),
                                    const_value_to_nodecl(one), t);
        }
        else
        {
            val = Nodecl::Minus::make(_input_constraints_map[rhs].get_symbol().make_nodecl(/*set_ref_type*/false),
                                      const_value_to_nodecl(one), t);
        }
        Utils::Constraint c = build_constraint(s, val, t, "Pre/Post-in/decrement");
        _input_constraints_map[rhs] = c;
        _output_constraints_map[rhs] = c;
    }
    
    void ConstraintBuilderVisitor::visit(const Nodecl::AddAssignment& n)
    {
        NBase lhs = n.get_lhs();
        NBase rhs = n.get_rhs();
        NBase new_rhs = Nodecl::Assignment::make(lhs.shallow_copy(), 
                                                 Nodecl::Add::make(lhs.shallow_copy(), rhs.shallow_copy(), rhs.get_type()), 
                                                 lhs.get_type());
        n.replace(new_rhs);
        visit_assignment(n.get_lhs(), n.get_rhs());
    }
    
    void ConstraintBuilderVisitor::visit(const Nodecl::Assignment& n)
    {
        visit_assignment(n.get_lhs(), n.get_rhs());
    }
    
    // x == c;   ---TRUE-->    X1 = X0 ∩ [c, c]
    //           --FALSE-->    X1 = X0 ∩ ([-∞, c-1] U [c+1, -∞])
    void ConstraintBuilderVisitor::visit(const Nodecl::Equal& n)
    {
        NBase lhs = n.get_lhs();
        NBase rhs = n.get_rhs();
        
        // Check the input is something we expect: LHS has a constraint or is a parameter
        ERROR_CONDITION(_input_constraints_map.find(lhs) == _input_constraints_map.end(),
                        "Some input constraint required for the LHS when parsing a %s nodecl", 
                        ast_print_node_type(n.get_kind()));
        
        Symbol orig_s(Utils::get_nodecl_base(lhs).get_symbol());
        Type t = orig_s.get_type();
        std::string orig_s_str = orig_s.get_name();
        
        // 1.- Compute the conditions associated with the current node
        Symbol s = get_condition_node_constraints(lhs, t, orig_s_str, "Equal");
        
        // 2.- Compute the constraints generated from the condition to the possible TRUE and FALSE exit edges
        NBase val = rhs.shallow_copy();
        if(!rhs.is_constant())
        {   // Replace all the memory accesses by the symbols of the constraints arriving to the current node
            ConstraintReplacement cr(_input_constraints_map);
            cr.walk(val);
        }
        // 2.1.- Compute the constraint that corresponds to the true branch taken from this node
        // x < x;       --TRUE-->       X1 = X0 ∩ [c, c]
        // 2.1.1.- Build the TRUE constraint symbol
        std::stringstream ss_true; ss_true << get_next_id(lhs);
        Symbol s_true(n.retrieve_context().new_symbol(orig_s_str + "_" + ss_true.str()));
        s_true.set_type(t);
        // 2.1.2.- Build the TRUE constraint value
        NBase val_true = 
            Nodecl::Analysis::RangeIntersection::make(
                s.make_nodecl(/*set_ref_type*/false), 
                Nodecl::Range::make(val.shallow_copy(),
                                    val.shallow_copy(), 
                                    const_value_to_nodecl(one), t),
                t);
        // 2.1.3.- Build the TRUE constraint and store it
        Utils::Constraint c_true = build_constraint(s_true, val_true, t, "Equal TRUE");
        _output_true_constraints_map[lhs] = c_true;
        // 2.2.- Compute the constraint that corresponds to the false branch taken from this node
        // x < c;       --FALSE-->      X1 = X0 ∩ ([-∞, c-1] U [c+1, -∞])
        // 2.2.1.- Build the FALSE constraint symbol
        std::stringstream ss_false; ss_false << get_next_id(lhs);
        Symbol s_false(n.retrieve_context().new_symbol(orig_s_str + "_" + ss_false.str()));
        s_false.set_type(t);
        // 2.2.2.- Build the FALSE constraint value
        NBase lb, ub;
        if(rhs.is_constant())
        {
            lb = const_value_to_nodecl(const_value_add(rhs.get_constant(), one));
            ub = const_value_to_nodecl(const_value_sub(rhs.get_constant(), one));
        }
        else
        {
            lb = Nodecl::Add::make(val.shallow_copy(), const_value_to_nodecl(one), t);
            ub = Nodecl::Minus::make(val.shallow_copy(), const_value_to_nodecl(one), t);
        }
        NBase val_false = 
            Nodecl::Analysis::RangeIntersection::make(
                s.make_nodecl(/*set_ref_type*/false),
                Nodecl::Analysis::RangeUnion::make(Nodecl::Range::make(Nodecl::Analysis::MinusInfinity::make(t), ub, 
                                                                       const_value_to_nodecl(one), t), 
                                                   Nodecl::Range::make(lb, Nodecl::Analysis::PlusInfinity::make(t), 
                                                                       const_value_to_nodecl(one), t), 
                                                   t),
                t);
        // 2.2.3.- Build the FALSE constraint and store it
        Utils::Constraint c_false = build_constraint(s_false, val_false, t, "Equal FALSE");
        _output_false_constraints_map[lhs] = c_false;
    }
    
    // x >= c;   ---TRUE-->    X1 = X0 ∩ [ c, +∞ ]
    //           --FALSE-->    X1 = X0 ∩ [-∞, c-1]
    void ConstraintBuilderVisitor::visit(const Nodecl::GreaterOrEqualThan& n)
    {
        NBase lhs = n.get_lhs();
        NBase rhs = n.get_rhs();
        
        // Check the input is something we expect: LHS has a constraint or is a parameter
        ERROR_CONDITION(_input_constraints_map.find(lhs) == _input_constraints_map.end(),
                        "Some input constraint required for the LHS when parsing a %s nodecl", 
                        ast_print_node_type(n.get_kind()));
        
        Symbol orig_s(Utils::get_nodecl_base(lhs).get_symbol());
        Type t = orig_s.get_type();
        std::string orig_s_str = orig_s.get_name();
        
        // 1.- Compute the conditions associated with the current node
        Symbol s = get_condition_node_constraints(lhs, t, orig_s_str, "GreaterOrEqualThan");
        
        // 2.- Compute the constraints generated from the condition to the possible TRUE and FALSE exit edges
        NBase val = rhs.shallow_copy();
        if(!rhs.is_constant())
        {   // Replace all the memory accesses by the symbols of the constraints arriving to the current node
            ConstraintReplacement cr(_input_constraints_map);
            cr.walk(val);
        }
        // 2.1.- Compute the constraint that corresponds to the true branch taken from this node
        // x < x;       --TRUE-->       X1 = X0 ∩ [ c, +∞ ]
        // 2.1.1.- Build the TRUE constraint symbol
        std::stringstream ss_true; ss_true << get_next_id(lhs);
        Symbol s_true(n.retrieve_context().new_symbol(orig_s_str + "_" + ss_true.str()));
        s_true.set_type(t);
        // 2.1.2.- Build the TRUE constraint value
        NBase val_true = 
            Nodecl::Analysis::RangeIntersection::make(
                s.make_nodecl(/*set_ref_type*/false), 
                Nodecl::Range::make(val.shallow_copy(),
                                    Nodecl::Analysis::PlusInfinity::make(t), 
                                    const_value_to_nodecl(one), t),
                t);
        // 2.1.3.- Build the TRUE constraint and store it
        Utils::Constraint c_true = build_constraint(s_true, val_true, t, "GreaterOrEqualThan TRUE");
        _output_true_constraints_map[lhs] = c_true;
        // 2.2.- Compute the constraint that corresponds to the false branch taken from this node
        // x < c;       --FALSE-->      X1 = X0 ∩ [-∞, c-1]
        // 2.2.1.- Build the FALSE constraint symbol
        std::stringstream ss_false; ss_false << get_next_id(lhs);
        Symbol s_false(n.retrieve_context().new_symbol(orig_s_str + "_" + ss_false.str()));
        s_false.set_type(t);
        // 2.2.2.- Build the FALSE constraint value
        NBase ub = (rhs.is_constant() ? const_value_to_nodecl(const_value_sub(rhs.get_constant(), one)) 
                                      : Nodecl::Minus::make(val.shallow_copy(), const_value_to_nodecl(one), t));
        NBase val_false = 
            Nodecl::Analysis::RangeIntersection::make(
                s.make_nodecl(/*set_ref_type*/false),
                Nodecl::Range::make(Nodecl::Analysis::MinusInfinity::make(t), ub, const_value_to_nodecl(one), t),
                t);
        // 2.2.3.- Build the FALSE constraint and store it
        Utils::Constraint c_false = build_constraint(s_false, val_false, t, "GreaterOrEqualThan FALSE");
        _output_false_constraints_map[lhs] = c_false;
    }
    
    void ConstraintBuilderVisitor::visit(const Nodecl::LogicalAnd& n)
    {
        walk(n.get_lhs());
        walk(n.get_rhs());
    }
    
    // x < c;    ---TRUE-->    X1 = X0 ∩ [-∞, c-1]
    //           --FALSE-->    X1 = X0 ∩ [ c,  +∞]
    void ConstraintBuilderVisitor::visit(const Nodecl::LowerThan& n)
    {
        NBase lhs = n.get_lhs();
        NBase rhs = n.get_rhs();
        
        // Check the input is something we expect: LHS has a constraint or is a parameter
        ERROR_CONDITION(_input_constraints_map.find(lhs) == _input_constraints_map.end(),
                        "Some input constraint required for the LHS when parsing a %s nodecl", 
                        ast_print_node_type(n.get_kind()));
        
        Symbol orig_s(Utils::get_nodecl_base(lhs).get_symbol());
        Type t = orig_s.get_type();
        std::string orig_s_str = orig_s.get_name();
        
        // 1.- Compute the conditions associated with the current node
        Symbol s = get_condition_node_constraints(lhs, t, orig_s_str, "LowerThan");
        
        // 2.- Compute the constraints generated from the condition to the possible TRUE and FALSE exit edges
        NBase val = rhs.shallow_copy();
        if(!rhs.is_constant())
        {   // Replace all the memory accesses by the symbols of the constraints arriving to the current node
            ConstraintReplacement cr(_input_constraints_map);
            cr.walk(val);
        }
        // 2.1.- Compute the constraint that corresponds to the true branch taken from this node
        // x < x;       --TRUE-->       X1 = X0 ∩ [-∞, x-1]
        // 2.1.1.- Build the TRUE constraint symbol
        std::stringstream ss_true; ss_true << get_next_id(lhs);
        Symbol s_true(n.retrieve_context().new_symbol(orig_s.get_name() + "_" + ss_true.str()));
        s_true.set_type(t);
        // 2.1.2.- Build the TRUE constraint value
        NBase ub = (rhs.is_constant() ? const_value_to_nodecl(const_value_sub(rhs.get_constant(), one)) 
                                                   : Nodecl::Minus::make(val.shallow_copy(), const_value_to_nodecl(one), t));
        NBase val_true = 
            Nodecl::Analysis::RangeIntersection::make(
                s.make_nodecl(/*set_ref_type*/false), 
                Nodecl::Range::make(Nodecl::Analysis::MinusInfinity::make(t), ub, const_value_to_nodecl(zero), t),
                t);
        // 2.1.3.- Build the TRUE constraint and store it
        Utils::Constraint c_true = build_constraint(s_true, val_true, t, "LowerThan TRUE");
        _output_true_constraints_map[lhs] = c_true;
        
        // 2.2.- Compute the constraint that corresponds to the false branch taken from this node
        // x < c;       --FALSE-->      X1 = X0 ∩ [ c, +∞]
        // 2.2.1.- Build the FALSE constraint symbol
        std::stringstream ss_false; ss_false << get_next_id(lhs);
        Symbol s_false(n.retrieve_context().new_symbol(orig_s.get_name() + "_" + ss_false.str()));
        s_false.set_type(t);
        // 2.2.2.- Build the FALSE constraint value
        NBase val_false = 
            Nodecl::Analysis::RangeIntersection::make(
                s.make_nodecl(/*set_ref_type*/false), 
                Nodecl::Range::make(val.shallow_copy(),
                                    Nodecl::Analysis::PlusInfinity::make(t), 
                                    const_value_to_nodecl(zero), t), 
                t);
        // 2.2.3.- Build the FALSE constraint and store it
        Utils::Constraint c_false = build_constraint(s_false, val_false, t, "LowerThan FALSE");
        _output_false_constraints_map[lhs] = c_false;
    }
    
    void ConstraintBuilderVisitor::visit(const Nodecl::Mod& n)
    {
        // Build the constraint symbol
        std::stringstream ss; ss << get_next_id(NBase::null());
        std::string sym_name = "_x_" + ss.str();
        Symbol s(n.retrieve_context().new_symbol(sym_name));
        Type t = n.get_lhs().get_type();
        s.set_type(t);
        
        // Build the constraint value
        NBase rhs = n.get_rhs();
        NBase ub = 
                (rhs.is_constant() ? const_value_to_nodecl(const_value_sub(rhs.get_constant(), one)) 
                                   : Nodecl::Minus::make(rhs.shallow_copy(), const_value_to_nodecl(one), rhs.get_type()));
        NBase val = Nodecl::Range::make(const_value_to_nodecl(zero), ub, const_value_to_nodecl(zero), t);
        
        // Build the constraint
        Utils::Constraint c = build_constraint(s, val, t, "Mod");
        Nodecl::Symbol var = Nodecl::Symbol::make(TL::Symbol(n.retrieve_context().new_symbol("_x")));
        var.set_type(t);
        _output_constraints_map[var] = c;
    }
    
    void ConstraintBuilderVisitor::visit(const Nodecl::ObjectInit& n)
    {
        Symbol s(n.get_symbol());
        Nodecl::Symbol lhs = s.make_nodecl(/*set_ref_type*/false);
        NBase rhs = s.get_value();
        visit_assignment(lhs, rhs);
    }
    
    // FIXME Check the order of creation of constraints depending on whether the op. is pre-in/decrement or post-in/decrement
    //       Example: x = y++;              Wrong: 
    //                X0 = Y0;                     Y1 = Y0 + 1;
    //                Y1 = Y0 + 1;                 X0 = Y1;
    
    
    // x--;    -->    X1 = X0 + 1
    void ConstraintBuilderVisitor::visit(const Nodecl::Postdecrement& n)
    {
        visit_increment(n.get_rhs(), /*positive*/ false);
    }
    
    // x++;    -->    X1 = X0 + 1
    void ConstraintBuilderVisitor::visit(const Nodecl::Postincrement& n)
    {
        visit_increment(n.get_rhs(), /*positive*/ true);
    }
    
    // --x;    -->    X1 = X0 - 1
    void ConstraintBuilderVisitor::visit(const Nodecl::Predecrement& n)
    {
        visit_increment(n.get_rhs(), /*positive*/ false);
    }
    
    // ++x;    -->    X1 = X0 + 1
    void ConstraintBuilderVisitor::visit(const Nodecl::Preincrement& n)
    {
        visit_increment(n.get_rhs(), /*positive*/ true);
    }
    
    // ************************** END Visitor implementing constraint building **************************** //
    // **************************************************************************************************** //
    
    
    
    // **************************************************************************************************** //
    // ******************************* Class implementing constraint graph ******************************** //
    
namespace {
    std::set<CGNode*> convert_cgnodes_map_in_set(CGNode_map nodes_map)
    {
        std::set<CGNode*> result;
        for(CGNode_map::iterator it = nodes_map.begin(); it != nodes_map.end(); ++it)
            result.insert(it->second);
        return result;
    }
    
    std::map<CGNode*, SCC*> node_to_scc_map;
    Optimizations::Calculator calc;
    
    void print_constraints()
    {
        std::cerr << "CONSTRAINT MAP: " << std::endl;
        for(ObjectList<NBase>::iterator it = ORDERED_CONSTRAINTS.begin(); it != ORDERED_CONSTRAINTS.end(); ++it)
        {
            std::pair<NBase, NBase> c = *CONSTRAINTS.find(*it);
            std::cerr << "    " << c.first.prettyprint() << "  ->  " << c.second.prettyprint() << std::endl;
        }
    }
}
    
    CGNode::CGNode(CGNode_type type, const NBase& constraint)
        : _id(++cgnode_id), _type(type), _constraint(constraint), _valuation(), _entries(), _exits(), 
          _scc_index(-1), _scc_lowlink_index(-1)
    {}
    
    unsigned int CGNode::get_id() const
    { 
        return _id;
    }
    
    CGNode_type CGNode::get_type() const
    {
        return _type;
    }
    
    std::string CGNode::get_type_as_str() const
    {
        switch(_type)
        {
            #undef CGNODE_TYPE
            #define CGNODE_TYPE(X) case __##X : return #X;
            CGNODE_TYPE_LIST
            #undef CGNODE_TYPE
            default: WARNING_MESSAGE("Unexpected type of node '%d'", _type);
        }
        return "";
    }
    
    NBase CGNode::get_constraint() const
    { 
        return _constraint; 
    }
    
    NBase CGNode::get_valuation() const
    { 
        return _valuation;
    }
    
    void CGNode::set_valuation(const NBase& valuation)
    {
        _valuation = valuation;
    }
    
    ObjectList<CGEdge*> CGNode::get_entries() const
    {
        return _entries;
    }
    
    ObjectList<CGNode*> CGNode::get_parents()
    {
        ObjectList<CGNode*> parents;
        for(ObjectList<CGEdge*>::iterator it = _entries.begin(); it != _entries.end(); ++it)
            parents.append((*it)->get_source());
        return parents;
    }
    
    void CGNode::add_entry(CGEdge* e)
    {
        _entries.insert(e);
    }
    
    ObjectList<CGEdge*> CGNode::get_exits() const
    {
        return _exits;
    }
    
    ObjectList<CGNode*> CGNode::get_children()
    {
        ObjectList<CGNode*> children;
        for(ObjectList<CGEdge*>::iterator it = _exits.begin(); it != _exits.end(); ++it)
            children.append((*it)->get_target());
        return children;
    }
    
    CGEdge* CGNode::add_child(CGNode* child, bool is_back_edge, NBase predicate)
    {
        CGEdge* e;
        ObjectList<CGNode*> children = get_children();
        if(!children.contains(child))
        {
            e = new CGEdge(this, child, is_back_edge, predicate);
            _exits.insert(e); 
        }
        else
        {
            for(ObjectList<CGEdge*>::iterator it = _exits.begin(); it != _exits.end(); ++it)
            {
                if((*it)->get_target() == child)
                {
                    e = *it;
                    break;
                }
            }
        }
        return e;
    }
    
    int CGNode::get_scc_index() const
    {
        return _scc_index;
    }
    
    void CGNode::set_scc_index(int scc_index)
    {
        _scc_index = scc_index;
    }
    
    int CGNode::get_scc_lowlink_index() const
    {
        return _scc_lowlink_index;
    }
    
    void CGNode::set_scc_lowlink_index(int scc_lowlink_index)
    {
        _scc_lowlink_index = scc_lowlink_index;
    }
    
    CGEdge::CGEdge(CGNode* source, CGNode* target, bool is_back, const NBase& predicate)
        : _source(source), _target(target), _is_back_edge(is_back), _predicate(predicate), _is_saturated(false)
    {
        if(!predicate.is_null() && predicate.is<Nodecl::Range>())
            _is_saturated = true;
    }
    
    CGNode* CGEdge::get_source() const
    {
        return _source;
    }
    
    CGNode* CGEdge::get_target() const
    {
        return _target;
    }
    
    bool CGEdge::is_back_edge() const
    {
        return _is_back_edge;
    }
    
    NBase CGEdge::get_predicate() const
    {
        return _predicate;        
    }
    
    bool CGEdge::is_saturated() const
    {
        return _is_saturated;
    }
    
    void CGEdge::set_saturated(bool s)
    {
        _is_saturated = s;
    }
    
    
    static unsigned int SccId = 0;
    
    SCC::SCC()
        : _nodes(), _root(NULL), _id(++SccId)
    {}
    
    bool SCC::empty() const
    {
        return _nodes.empty();
    }
    
    std::vector<CGNode*> SCC::get_nodes() const
    {
        return _nodes;
    }
    
    void SCC::add_node(CGNode* n)
    {
        _nodes.push_back(n);
    }
    
    CGNode* SCC::get_root() const
    {
        return _root;
    }
    
    void SCC::set_root(CGNode* root)
    {
        _root = root;
    }
    
    unsigned int SCC::get_id() const
    {
        return _id;
    }
    
    bool SCC::is_trivial() const
    {
        return (_nodes.size() == 1);
    }
    
    // FIXME A SCC is positive if it contains a cycle that increments the involved variable
    bool SCC::is_positive() const
    {
        return (!is_trivial() && true);
    }
    
    ObjectList<SCC*> SCC::get_scc_exits()
    {
        ObjectList<SCC*> res;
        ObjectList<CGNode*> children;
        for(std::vector<CGNode*>::iterator it = _nodes.begin(); it != _nodes.end(); ++it)
        {
            children = (*it)->get_children();
            for(ObjectList<CGNode*>::iterator itt = children.begin(); itt != children.end(); ++itt)
            {
                SCC* scc = node_to_scc_map[*itt];
                if(scc != this)
                    res.append(scc);
            }
        }
        return res;
    }
    
    ConstraintGraph::ConstraintGraph(std::string name)
        : _name(name), _nodes()
    {}
    
    CGNode* ConstraintGraph::insert_node(const NBase& value)
    {
        CGNode* node = ((_nodes.find(value) == _nodes.end()) ? NULL 
                                                             : _nodes[value]);
        if(node==NULL)
        {
            node = new CGNode(__CG_Sym, value);
            _nodes[value] = node;
        }
        return node;
    }
    
    CGNode* ConstraintGraph::insert_node(CGNode_type type)
    {
        CGNode* node = new CGNode(type, NBase::null());
        NBase value = Nodecl::IntegerLiteral::make(Type::get_int_type(), 
                                                   const_value_get_integer(node->get_id(), /*num_bytes*/4, /*sign*/1));
        _nodes[value] = node;
        return node;
    }
    
    void ConstraintGraph::connect_nodes(CGNode* source, CGNode* target, NBase predicate)
    {
        ObjectList<CGNode*> children = source->get_children();
        if(!children.contains(target))
        {
            CGEdge* e = source->add_child(target, /*is_back_edge*/source->get_id()>target->get_id(), predicate);
            target->add_entry(e);
        }
    }
    
    void ConstraintGraph::collapse_consecutive_phi_nodes()
    {
        for(ObjectList<NBase>::iterator oit = ORDERED_CONSTRAINTS.begin(); oit != ORDERED_CONSTRAINTS.end(); ++oit)
        {
            std::map<NBase, NBase, Nodecl::Utils::Nodecl_structural_less>::iterator it = CONSTRAINTS.find(*oit);
            ERROR_CONDITION(it == CONSTRAINTS.end(), 
                            "Constraint %s not found in the constraints map.\n", 
                            oit->prettyprint().c_str());
            if(it->second.is<Nodecl::Analysis::Phi>())
            {
                // Collect all the expressions from nested Phi nodes
                Nodecl::Analysis::Phi phi = it->second.shallow_copy().as<Nodecl::Analysis::Phi>();
                Nodecl::List phi_exprs = phi.get_expressions().shallow_copy().as<Nodecl::List>();
                Nodecl::List new_phi_exprs;
                Nodecl::List phi_to_remove;
                bool has_phi = false;
                while(!phi_exprs.empty())
                {
                    NBase tmp = phi_exprs.back();
                    phi_exprs.pop_back();
                    
                    std::map<NBase, NBase, Nodecl::Utils::Nodecl_structural_less>::iterator tmp_it = CONSTRAINTS.find(tmp);
                    ERROR_CONDITION(tmp_it == CONSTRAINTS.end(), 
                                    "Constraint %s not found in the global constraints map.\n", 
                                    tmp.prettyprint().c_str());
                    
                    if(tmp_it->second.is<Nodecl::Analysis::Phi>())
                    {
                        phi_to_remove.append(tmp_it->first);
                        phi_exprs.append(tmp_it->second.as<Nodecl::Analysis::Phi>().get_expressions());
                        has_phi = true;
                    }
                    else
                    {
                        new_phi_exprs.append(tmp_it->first);
                    }
                }
                
                if(has_phi)
                {
                    // Create the new constraint
                    NBase new_phi = Nodecl::Analysis::Phi::make(new_phi_exprs, phi.get_type());
                    CONSTRAINTS[it->first] = new_phi;
                    
                    // Delete the nested constraints
                    for(Nodecl::List::iterator itt = phi_to_remove.begin(); itt != phi_to_remove.end(); ++itt)
                    {
                        CONSTRAINTS.erase(*itt);
                        for(ObjectList<NBase>::iterator ittt = ORDERED_CONSTRAINTS.begin(); 
                            ittt != ORDERED_CONSTRAINTS.end(); ++ittt)
                        {
                            if(Nodecl::Utils::structurally_equal_nodecls(*itt, *ittt, /*skip_conversion_nodes*/true))
                            {
                                ORDERED_CONSTRAINTS.erase(ittt);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    
    /*! Generate a constraint graph from the PCFG and the precomputed Constraints for each node
     *  A Constraint Graph is created as follows:
     *  - The nodes of the graphs are:
     *    - A. A node for each SSA variable from Constraints: c0 = ...              Node    c0
     *    - B. A node for each Constraint Value which is a Range: c1 = [lb, ub]     Node [lb, ub]
     *  - The edges are built following the rules below:
     *    - D. Constraint Phi: c2 = Phi(c0, c1)                                     Edge    c0 ---------------> c2
     *                                                                              Edge    c1 ---------------> c2
     *    - E. Constraint Intersection: c1 = c0 ∩ [lb, ub]                          Edge    c0 ----[lb, ub]---> c1
     *    - F. Constraint Range: c0 = [lb, ub]                                      Edge [lb, ub]-------------> c1
     *    - G. Constraint Arithmetic op: a. c1 = c0 + 1                             Edge    c1 ------ 1 ------> c0
     *                                   b. c1 = d + e                              Edge    d  ---------------> c1
     *                                                                              Edge    e  ---------------> c1
     *    - H. Constraint : c1 = c0                                                 Edge    c0 ---------------> c1
     */
    void ConstraintGraph::create_constraint_graph()
    {
        std::map<NBase, NBase> back_edges;
        for(ObjectList<NBase>::iterator oit = ORDERED_CONSTRAINTS.begin(); oit != ORDERED_CONSTRAINTS.end(); ++oit)
        {
            std::map<NBase, NBase, Nodecl::Utils::Nodecl_structural_less>::iterator it = CONSTRAINTS.find(*oit);
            ERROR_CONDITION(it == CONSTRAINTS.end(), 
                            "Constraint %s not found in the constraints map.\n", 
                            oit->prettyprint().c_str());
            NBase s = it->first;
            NBase val = it->second;
            
            // Insert in the CG the edges corresponding to the current Constraint
            if(val.is<Nodecl::Symbol>())
            {   // H. 
                CGNode* source = insert_node(val);
                CGNode* target = insert_node(s);  // A.
                connect_nodes(source, target);
            }
            else if(val.is<Nodecl::Analysis::Phi>())
            {   // D.
                // Create the target
                CGNode* target_phi = insert_node(__CG_Phi);
                // Create the sources
                std::queue<CGNode*> sources;
                Nodecl::List expressions = val.as<Nodecl::Analysis::Phi>().get_expressions().as<Nodecl::List>();
                for(Nodecl::List::iterator ite = expressions.begin(); ite != expressions.end(); ++ite)
                {
                    NBase e = *ite;
                    if(_nodes.find(e) == _nodes.end())
                    {   // The expression inside the Phi node comes from a back edge
                        // We will print it later so the order of node creation respects the order in the sequential code
                        back_edges.insert(std::pair<NBase, NBase>(e, s));
                    }
                    else
                    {
                        CGNode* source = insert_node(e);
                        sources.push(source);
                    }
                }
                CGNode* target = insert_node(s);  // A.
                // Connect them
                while(!sources.empty())
                {
                    CGNode* source = sources.front();
                    sources.pop();
                    connect_nodes(source, target_phi);
                }
                connect_nodes(target_phi, target);
            }
            else if(val.is<Nodecl::Analysis::RangeIntersection>())
            {   // E.
                NBase lhs = val.as<Nodecl::Analysis::RangeIntersection>().get_lhs().no_conv();
                const NBase rhs = val.as<Nodecl::Analysis::RangeIntersection>().get_rhs().no_conv();
                
                CGNode* source = insert_node(lhs);
                CGNode* target = insert_node(s);  // A.
                connect_nodes(source, target, rhs.shallow_copy());
            }
            else if(val.is<Nodecl::Range>())
            {
                // B. Create a new node if the Constraint Value is a Range
                CGNode* source = insert_node(val);
                CGNode* target = insert_node(s);  // A.
                // F. Create edge between the Range node and the Constraint node
                connect_nodes(source, target);
            }
            else if(val.is<Nodecl::Add>())
            {   // G.
                NBase lhs = val.as<Nodecl::Add>().get_lhs().no_conv();
                const NBase rhs = val.as<Nodecl::Add>().get_rhs().no_conv();
                if(lhs.is<Nodecl::Symbol>())
                {
                    if(rhs.is<Nodecl::Symbol>())
                    {   // G.b.
                        CGNode* source_1 = insert_node(lhs);
                        CGNode* source_2 = insert_node(rhs);
                        
                        CGNode* target_add = insert_node(__CG_Add);
                        CGNode* target = insert_node(s);  // A.
                        
                        connect_nodes(source_1, target);
                        connect_nodes(source_2, target);
                        connect_nodes(target_add, target);
                    }
                    else if(rhs.is_constant())
                    {   // G.a.
                        CGNode* source = insert_node(lhs);
                        CGNode* target = insert_node(s);  // A.
                        connect_nodes(source, target, rhs.shallow_copy());
                    }
                    else
                    {
                        internal_error("Unexpected Add constraint value %s. Expected 's+s', 'c+s' or 's+c'.\n", val.prettyprint().c_str());
                    }
                }
                else if(lhs.is_constant())
                {
                    if(rhs.is<Nodecl::Symbol>())
                    {   // G.a.
                        CGNode* source = insert_node(rhs);
                        CGNode* target = insert_node(s);  // A.
                        connect_nodes(source, target);
                    }
                    else
                    {
                        internal_error("Unexpected Add constraint value %s. Expected 's+s', 'c+s' or 's+c'.\n", val.prettyprint().c_str());
                    }
                }
                else
                {
                    internal_error("Unexpected Add constraint value %s. Expected 's+s', 'c+s' or 's+c'.\n", val.prettyprint().c_str());
                }
            }
            else if(val.is<Nodecl::Minus>())
            {   // G.
                NBase lhs = val.as<Nodecl::Add>().get_lhs().no_conv();
                const NBase rhs = val.as<Nodecl::Add>().get_rhs().no_conv();
                if(lhs.is<Nodecl::Symbol>())
                {
                    if(rhs.is<Nodecl::Symbol>())
                    {   // G.b.
                        CGNode* source_1 = insert_node(lhs);
                        CGNode* source_2 = insert_node(rhs);
                        
                        CGNode* target_sub = insert_node(__CG_Sub);
                        CGNode* target = insert_node(s);  // A.
                        
                        connect_nodes(source_1, target_sub);
                        connect_nodes(source_2, target_sub);
                        connect_nodes(target_sub, target);
                    }
                    else if(rhs.is_constant())
                    {   // G.a.
                        CGNode* source = insert_node(lhs);
                        CGNode* target = insert_node(s);  // A.
                        
                        NBase neg_rhs = Nodecl::Neg::make(rhs.shallow_copy(), rhs.get_type());
                        const_value_t* c_val = calc.compute_const_value(neg_rhs);
                        NBase predicate = Nodecl::IntegerLiteral::make(Type::get_int_type(), c_val);
                        connect_nodes(source, target, predicate);
                    }
                    else
                    {
                        internal_error("Unexpected Minus constraint value %s. Expected 's+s', 'c+s' or 's+c'.\n", val.prettyprint().c_str());
                    }
                }
                else if(lhs.is_constant())
                {
                    if(rhs.is<Nodecl::Symbol>())
                    {   // G.a.
                        CGNode* source = insert_node(rhs);
                        CGNode* target = insert_node(s);  // A.
                        NBase neg_lhs = Nodecl::Neg::make(lhs.shallow_copy(), lhs.get_type());
                        const_value_t* c_val = calc.compute_const_value(neg_lhs);
                        NBase predicate = Nodecl::IntegerLiteral::make(Type::get_int_type(), c_val);
                        connect_nodes(source, target, predicate);
                    }
                    else
                    {
                        internal_error("Unexpected Minus constraint value %s. Expected 's+s', 'c+s' or 's+c'.\n", val.prettyprint().c_str());
                    }
                }
                else
                {
                    internal_error("Unexpected Minus constraint value %s. Expected 's+s', 'c+s' or 's+c'.\n", val.prettyprint().c_str());
                }
            }
            else
            {
                internal_error("Unexpected type of Constraint value '%s' for constraint '%s'.\n", 
                               ast_print_node_type(val.get_kind()), val.prettyprint().c_str());
            }
        }
        
        // Connect now the back edges
        for(std::map<NBase, NBase>::iterator it = back_edges.begin(); it != back_edges.end(); ++it)
        {
            // Both source and target must exist already
            CGNode* source = insert_node(it->first);
            CGNode* target = insert_node(it->second);
            connect_nodes(source, target);
        }
    }
    
    void ConstraintGraph::print_graph_(std::ofstream& dot_file)
    {
        for(CGNode_map::iterator it = _nodes.begin(); it != _nodes.end(); ++it)
        {
            CGNode* n = it->second;
            unsigned int source = n->get_id();
            
            // 1.- Print the Constraint Node
            CGNode_type node_t = n->get_type();
            // 1.1.- The node has a constraint associated
            if(node_t == __CG_Sym)
            {
                NBase constraint = n->get_constraint();
                dot_file << "\t" << source << " [label=\"[" << source << "] " << constraint.prettyprint() << "\"];\n";
                NBase val = n->get_valuation();
                if(!constraint.is<Nodecl::Range>() && !val.is_null())
                {
                    // Print a node containing the valuation
                    dot_file << "\t0" << source << " [label=\"" << val.prettyprint() << "\", "
                            << "style=\"dashed\", color=\"gray55\", fontcolor=\"gray27\", shape=\"polygon\"];\n";
                    // Connect it to its constraint node
                    dot_file << "\t" << source << "->" << "0" << source << " [label=\"\", style=\"dashed\", color=\"gray55\"];\n";
                    // set the same rank to the constraint node an its valuation node
                    dot_file << "\t{rank=same; " << source << "; 0" << source << ";}";
                }
            }
            else
            {
                dot_file << "\t" << source << " [label=\"[" << source << "] " << n->get_type_as_str() << "\"];\n";
            }
            
            // Print the node relations
            ObjectList<CGEdge*> exits = n->get_exits();
            for(ObjectList<CGEdge*>::iterator ite = exits.begin(); ite != exits.end(); ++ite)
            {
                unsigned int target = (*ite)->get_target()->get_id();
                const NBase predicate = (*ite)->get_predicate();
                bool back_edge = (*ite)->is_back_edge();
                std::string attrs = " [label=\"" + (predicate.is_null() ? "" : predicate.prettyprint()) + "\","
                                  + " style=\"" + (back_edge ? "dotted" : "solid") + "\"]";
                dot_file << "\t" << source << "->" << target << attrs << ";\n";
            }
        }
    }
    
    void ConstraintGraph::print_graph()
    {
        // Get a file to print a DOT with the Constraint Graph
        // Create the directory of dot files if it has not been previously created
        char buffer[1024];
        char* err = getcwd(buffer, 1024);
        if(err == NULL)
            internal_error ("An error occurred while getting the path of the current directory", 0);
        struct stat st;
        std::string directory_name = std::string(buffer) + "/dot/";
        if(stat(directory_name.c_str(), &st) != 0)
        {
            int dot_directory = mkdir(directory_name.c_str(), S_IRWXU);
            if(dot_directory != 0)
                internal_error ("An error occurred while creating the dot directory in '%s'", 
                                directory_name.c_str());
        }
        
        // Create the file where we will store the DOT CG
        std::string dot_file_name = directory_name + _name + "_cg.dot";
        std::ofstream dot_cg;
        dot_cg.open(dot_file_name.c_str());
        if(!dot_cg.good())
            internal_error ("Unable to open the file '%s' to store the CG.", dot_file_name.c_str());
        if(VERBOSE)
            std::cerr << "- CG DOT file '" << dot_file_name << "'" << std::endl;
        dot_cg << "digraph CG {\n";
            dot_cg << "\tcompound=true;\n";
            print_graph_(dot_cg);
        dot_cg << "}\n";
        dot_cg.close();
        if(!dot_cg.good())
            internal_error ("Unable to close the file '%s' where CG has been stored.", dot_file_name.c_str());
    }
    
namespace {
    bool stack_contains_cgnode(const std::stack<CGNode*>& s, CGNode* n)
    {
        bool result = false;
        std::stack<CGNode*> tmp = s;
        while(!tmp.empty() && !result)
        {
            if(tmp.top()==n)
                result = true;
            tmp.pop();
        }
        return result;
    }
    
    void strong_connect(CGNode* n, unsigned int& scc_index, std::stack<CGNode*>& s, std::vector<SCC*>& scc_list)
    {
        // Set the depth index for 'n' to the smallest unused index
        n->set_scc_index(scc_index);
        n->set_scc_lowlink_index(scc_index);
        ++scc_index;
        s.push(n);
        
        // Consider the successors of 'n'
        ObjectList<CGNode*> succ = n->get_children();
        for(ObjectList<CGNode*>::iterator it = succ.begin(); it != succ.end(); ++it)
        {
            CGNode* m = *it;
            if(m->get_scc_index() == -1)
            {   // Successor 'm' has not yet been visited: recurse on it
                strong_connect(m, scc_index, s, scc_list);
                n->set_scc_lowlink_index(std::min(n->get_scc_lowlink_index(), m->get_scc_lowlink_index()));
            }
            else if(stack_contains_cgnode(s, m))
            {   // Successor 'm' is in the current SCC
                n->set_scc_lowlink_index(std::min(n->get_scc_lowlink_index(), m->get_scc_index()));
            }
        }   
        
        // If 'n' is a root node, pop the set and generate an SCC
        if((n->get_scc_lowlink_index() == n->get_scc_index()) && !s.empty())
        {
            SCC* scc = new SCC();
            while(!s.empty() && s.top()!=n)
            {
                scc->add_node(s.top());
                s.pop();
            }
            if(!s.empty() && s.top()==n)
            {
                scc->add_node(s.top());
                s.pop();
            }
            scc_list.push_back(scc);
        }
    }
}
    
    // Implementation of the Tarjan's strongly connected components algorithm
    std::vector<SCC*> ConstraintGraph::topologically_compose_strongly_connected_components()
    {
        std::vector<SCC*> scc_list;
        std::stack<CGNode*> s;
        unsigned int scc_index = 0;
        
        // Compute the sets
        std::set<CGNode*> all_nodes = convert_cgnodes_map_in_set(_nodes);
        for(std::set<CGNode*>::iterator it = all_nodes.begin(); it != all_nodes.end(); ++it)
        {
            if((*it)->get_scc_index() == -1)
                strong_connect(*it, scc_index, s, scc_list);
            
        }
        
        // Create a map between SCC nodes and their SCC
        for(std::vector<SCC*>::iterator it = scc_list.begin(); it != scc_list.end(); ++it)
        {
            std::vector<CGNode*> scc_nodes = (*it)->get_nodes();
            for(std::vector<CGNode*>::iterator itt = scc_nodes.begin(); itt != scc_nodes.end(); ++itt)
            {
                node_to_scc_map.insert(std::pair<CGNode*, SCC*>(*itt, *it));
            }
        }
        
        // Compute the root of each SCC
        for(std::vector<SCC*>::iterator it = scc_list.begin(); it != scc_list.end(); ++it)
        {
            SCC* scc = *it;
            std::vector<CGNode*> nodes = scc->get_nodes();
            if(scc->is_trivial())
            {
                scc->set_root(nodes[0]);
            }
            else
            {
                for(std::vector<CGNode*>::iterator itt = nodes.begin(); itt != nodes.end(); ++itt)
                {
                    ObjectList<CGNode*> parents = (*itt)->get_parents();
                    for(ObjectList<CGNode*>::iterator ittt = parents.begin(); ittt != parents.end(); ++ittt)
                    {
                        if(node_to_scc_map[*ittt] != scc)
                        {
                            scc->set_root(*itt);
                            goto root_done;
                        }
                    }
                }
            }
root_done:  ;
        }
        

#ifdef RANGES_DEBUG
        std::cerr << "STRONGLY CONNECTED COMPONENTS" << std::endl;
        for(std::vector<SCC*>::iterator it = scc_list.begin(); it != scc_list.end(); ++it)
        {
            SCC* scc = *it;
            std::vector<CGNode*> nodes = scc->get_nodes();
            std::cerr << "    SCC " << (*it)->get_id() << ": ";
            for(std::vector<CGNode*>::iterator itt = nodes.begin(); itt != nodes.end(); )
            {
                std::cerr << (*itt)->get_id();
//                 if((*itt)->get_type() == __CG_Sym)
//                     std::cerr << (*itt)->get_constraint().prettyprint();
//                 else
//                     std::cerr << (*itt)->get_id() << "_" << (*itt)->get_type_as_str();
                ++itt;
                if(itt != nodes.end())
                    std::cerr << ", ";
            }
            std::cerr << " (root: " << scc->get_root()->get_id() << ")" << std::endl;
        }
#endif
        
        // Collect the roots of each SCC tree
        std::vector<SCC*> roots;
        for(std::map<CGNode*, SCC*>::iterator it = node_to_scc_map.begin(); it != node_to_scc_map.end(); ++it)
        {
            if(it->first->get_entries().empty())
                roots.push_back(it->second);
        }
        
        return roots;
    }
    
namespace {
    
    NBase join_previous_valuations(NBase (*)(const NBase&, const NBase&), 
                                   const ObjectList<NBase>& previous_valuations)
    {
        NBase next_valuation;
        ObjectList<NBase>::const_iterator it = previous_valuations.begin();
        next_valuation = *it;
        if(previous_valuations.size() > 1)
        {
            ++it;
            while(it != previous_valuations.end())
            {
                next_valuation = Utils::range_union(next_valuation, *it);
                ++it;
            }
        }
        return next_valuation;
    }
    
    void evaluate_cgnode(CGNode* node)
    {
        NBase valuation;
        CGNode_type type = node->get_type();
        NBase constraint = node->get_constraint();
        if(!constraint.is_null() && constraint.is<Nodecl::Range>())
        {
            valuation = constraint;
        }
        else
        {
            ObjectList<NBase> entry_valuations;
            ObjectList<CGEdge*> entries = node->get_entries();
            ERROR_CONDITION(entries.empty(), 
                            "CG node %d representing symbol or operation has no entries. Expected at least one entry.\n", 
                            node->get_id());
            for(ObjectList<CGEdge*>::iterator it = entries.begin(); it != entries.end(); ++it)
            {
                if((*it)->is_back_edge())
                    continue;
                Nodecl::Range last_valuation = (*it)->get_source()->get_valuation().as<Nodecl::Range>();
                NBase predicate = (*it)->get_predicate();
                if(predicate.is_null())
                    valuation = last_valuation;
                else if(predicate.is<Nodecl::IntegerLiteral>())
                    valuation = Utils::range_value_add(last_valuation, predicate.as<Nodecl::IntegerLiteral>());
                else if(predicate.is<Nodecl::Range>() || predicate.is<Nodecl::Analysis::RangeUnion>())
                    valuation = Utils::range_intersection(last_valuation, predicate, /*is_positive*/true);
                else
                    internal_error("Unexpected type %s of CG predicate %s. Expected IntegerLiteral or Range.\n", 
                                   ast_print_node_type(predicate.get_kind()), predicate.prettyprint().c_str());
                    entry_valuations.append(valuation);
            }
            
            ObjectList<NBase>::iterator it = entry_valuations.begin();
            if(type == __CG_Sym)
            {
                ERROR_CONDITION(entry_valuations.size()>1, 
                                "Only one entry valuation expected for a Sym CGNode but %d found.\n", 
                                entry_valuations.size());
                valuation = *it;
            }
            else
            {
                if(type == __CG_Phi)
                    valuation = join_previous_valuations(Utils::range_union, entry_valuations);  // RANGE_UNION
                else if(type == __CG_Add)
                    valuation = join_previous_valuations(Utils::range_add, entry_valuations);    // RANGE_ADD
                else if(type == __CG_Sub)
                    valuation = join_previous_valuations(Utils::range_sub, entry_valuations);    // RANGE_SUB
            }
        }
#ifdef RANGES_DEBUG
        std::cerr << "    EVALUATE " << node->get_id() << "  ::  " << valuation.prettyprint() << std::endl;
#endif
        node->set_valuation(valuation);
    }
    
    void resolve_cycle(SCC* scc)
    {
#ifdef RANGES_DEBUG
        std::cerr << "_____________________________________" << std::endl;
        std::cerr << "SCC " << scc->get_id() << "  ->  RESOLVE CYCLE" << std::endl;
#endif
        // Evaluate the root of the SCC
        CGNode* root = scc->get_root();
        evaluate_cgnode(root);
        
        // Saturate all non-saturated edges in the SCC
        // Propagate the rest of edges
        std::queue<CGNode*> next_nodes;
        ObjectList<CGNode*> children = root->get_children();
        for(ObjectList<CGNode*>::iterator it = children.begin(); it != children.end(); ++it)
            next_nodes.push(*it);
        std::set<CGNode*> visited;
        visited.insert(root);
        ObjectList<CGEdge*> back_edges;
        while(!next_nodes.empty())
        {
            CGNode* n = next_nodes.front();
            next_nodes.pop();
            
            // If the node is not in the same SCC, then we will treat it later
            if(node_to_scc_map[n] != scc)
                continue;
            
            // If the node was already treated, there is nothing to be done
            if(visited.find(n) != visited.end())
                continue;
            
            // Check whether the node can be treated or not: all parent have already been treated
            bool is_ready = true;
            const ObjectList<CGEdge*> entries = n->get_entries();
            for(ObjectList<CGEdge*>::const_iterator it = entries.begin(); it != entries.end(); ++it)
            {
                if((*it)->is_back_edge())
                    back_edges.append(*it);
                else if((*it)->get_source()->get_valuation().is_null())
                {
                    is_ready = false;
                    break;
                }
            }
            if(!is_ready)
            {   // Insert the node again in the queue and keep iterating
                next_nodes.push(n);
                continue;
            }
            
            // Treat the node and insert it in the list of treated nodes    
            evaluate_cgnode(n);
            visited.insert(n);
            
            // Add the children of the node to the queue
            ObjectList<CGNode*> n_children = n->get_children();
            for(ObjectList<CGNode*>::iterator it = n_children.begin(); it != n_children.end(); ++it)
                next_nodes.push(*it);
        }
        
        NBase valuation;
        for(ObjectList<CGEdge*>::iterator it = back_edges.begin(); it != back_edges.end(); ++it)
        {
            NBase predicate = (*it)->get_predicate();
            ERROR_CONDITION(!predicate.is_null(), 
                            "Propagation in back edges with a predicate is not yet implemented.\n", 0);
            NBase source_valuation = (*it)->get_source()->get_valuation();
            if(!source_valuation.is_null())
            {
                CGNode* target = (*it)->get_target();
                NBase target_valuation = target->get_valuation();
                if(target_valuation.is_null())
                    valuation = source_valuation;
                else
                    valuation = Utils::range_union(source_valuation, target_valuation);
                target->set_valuation(valuation);
#ifdef RANGES_DEBUG
                std::cerr << "        PROPAGATE back edge to " << target->get_id() << "  ::  " << valuation.prettyprint() << std::endl;
#endif
            }
        }
        
#ifdef RANGES_DEBUG
        std::cerr << "_____________________________________" << std::endl;
#endif
    }
}
    void ConstraintGraph::solve_constraints(const std::vector<SCC*>& roots)
    {
#ifdef RANGES_DEBUG
        std::cerr << "SOLVE CONSTRAINTS" << std::endl;
#endif
        std::queue<SCC*> next_scc;
        for(std::vector<SCC*>::const_iterator it = roots.begin(); it != roots.end(); ++it)
            next_scc.push(*it);
        std::set<SCC*> visited;
        while(!next_scc.empty())
        {
            SCC* scc = next_scc.front();
            next_scc.pop();
            std::cerr << "  SCC " << scc->get_id() << std::endl;
            // Check whether this scc is ready to be solved
            CGNode* root = scc->get_root();
            ObjectList<CGEdge*> entries = root->get_entries();
            bool is_ready = true;
            for(ObjectList<CGEdge*>::iterator it = entries.begin(); it != entries.end(); ++it)
            {
                if((*it)->is_back_edge())
                    continue;
                if((*it)->get_source()->get_valuation().is_null())
                {
                    is_ready = false;
                    break;
                }
            }
            if(!is_ready)
            {
                next_scc.push(scc);
                continue;
            }
            
            // Treat the current SCC
            if(scc->is_trivial())
            {
                // Evaluate the only node within the SCC
                evaluate_cgnode(scc->get_nodes()[0]);
            }
            else
            {
                // Cycle resolution
                resolve_cycle(scc);
            }
            visited.insert(scc);
            
            // Prepare next iterations, if there are
            // We will add more than one child here when a single node generates more than one constraint
            ObjectList<SCC*> scc_exits = scc->get_scc_exits();
            for(ObjectList<SCC*>::iterator it = scc_exits.begin(); it != scc_exits.end(); ++it)
            {
                if(visited.find(*it) == visited.end())
                {
                    std::cerr << "  scc_exit: " << (*it)->get_id() << std::endl;
                    next_scc.push(*it);
                }
            }
        }
    }
    
    // ***************************** END class implementing constraint graph ****************************** //
    // **************************************************************************************************** //
    
    
    
    // **************************************************************************************************** //
    // ******************************** Class implementing range analysis ********************************* //
    
    RangeAnalysis::RangeAnalysis(ExtensibleGraph* pcfg)
        : _pcfg(pcfg), _cg(new ConstraintGraph(pcfg->get_name()))
    {}
    
    void RangeAnalysis::compute_range_analysis()
    {   
        // 1.- Compute the constraints of the current PCFG
        set_parameters_constraints();
        
        Node* entry = _pcfg->get_graph()->get_graph_entry_node();
        create_constraints(entry);
        ExtensibleGraph::clear_visits(entry);
        
        propagate_constraints_from_backwards_edges(entry);
        ExtensibleGraph::clear_visits(entry);
        
        // 2.- Create the Constraint Graph
//         _cg->collapse_consecutive_phi_nodes();
        if(VERBOSE)
            print_constraints();
        _cg->create_constraint_graph();
        if(VERBOSE)
            _cg->print_graph();
        
        // 3.- Extract the Strongly Connected Components (SCC) of the graph
        //     And get the root of each topologically ordered subgraph
        std::vector<SCC*> roots = _cg->topologically_compose_strongly_connected_components();
        
        // 4.- Constraints evaluation
        _cg->solve_constraints(roots);
        if(VERBOSE)
            _cg->print_graph();
    }
    
    // Set an constraint to the graph entry node for each parameter of the function
    void RangeAnalysis::set_parameters_constraints()
    {
        Symbol func_sym = _pcfg->get_function_symbol();
        if(!func_sym.is_valid())    // The PCFG have been built for something other than a FunctionCode
            return;
        
        // Set the constraints for each parameter to both nodes
        Utils::ConstraintMap constraints;
        Node* entry = _pcfg->get_graph()->get_graph_entry_node();
        const ObjectList<Symbol> params = func_sym.get_function_parameters();
        for(ObjectList<Symbol>::const_iterator it = params.begin(); it != params.end(); ++it)
        {
            Nodecl::Symbol param_s = it->make_nodecl(/*set_ref_type*/false);
            Type t = it->get_type();
            
            // Build a symbol for the new constraint based on the name of the original variable
            std::stringstream ss; ss << get_next_id(param_s);
            std::string constr_name = it->get_name() + "_" + ss.str();
            Symbol s(it->get_scope().new_symbol(constr_name));
            s.set_type(t);
            
            // Get the value for the constraint
            NBase val = Nodecl::Range::make(Nodecl::Analysis::MinusInfinity::make(t), 
                                            Nodecl::Analysis::PlusInfinity::make(t), 
                                            const_value_to_nodecl(zero), t);
            
            // Build the constraint and insert it in the constraints map
            Utils::Constraint c = build_constraint(s, val, t, "Parameter");
            constraints[param_s] = c;
        }
        // Attach the constraints to the entry node of the graph
        entry->set_constraints_map(constraints);
    }
    
    // This is a breadth-first search because for a given node we need all its parents 
    // (except from those that come from back edges, for they will be recalculated later 'propagate_constraints_from_backwards_edges')
    // to be computed before propagating their information to the node
    void RangeAnalysis::create_constraints(Node* entry)
    {
        ERROR_CONDITION(!entry->is_entry_node(), 
                        "Expected ENTRY node but found %s node.", entry->get_type_as_string().c_str());
        
        ObjectList<Node*> currents(1, entry);
        while(!currents.empty())
        {
            ObjectList<Node*>::iterator it = currents.begin();
            while(it != currents.end())
            {
                Node* n = *it;
                if((*it)->is_visited())
                {
                    currents.erase(it);
                    continue;
                }
                else
                {
                    (*it)->set_visited(true);
                    ++it;
                }
                
                if(n->is_graph_node())
                {
                    // recursively compute the constraints for the inner nodes
                    create_constraints(n->get_graph_entry_node());
                    // propagate constraint from the inner nodes (summarized in the exit node) to the graph node
                    n->set_propagated_constraints_map(n->get_graph_exit_node()->get_propagated_constraints_map());
                    n->add_propagated_constraints_map(n->get_graph_exit_node()->get_constraints_map());
                }
                else
                {
                    // Collect the constraints computed from all the parents
                    Utils::ConstraintMap input_constraints_map, new_input_constraints;
                    ObjectList<Node*> parents = (n->is_entry_node() ? n->get_outer_node()->get_parents() : n->get_parents());
#ifdef RANGES_DEBUG
                    std::cerr << "PCFG NODE " << n->get_id() << std::endl;
#endif
                    for(ObjectList<Node*>::iterator itt = parents.begin(); itt != parents.end(); ++itt)
                    {
                        Utils::ConstraintMap it_constraints_map = (*itt)->get_all_constraints_map();
                        for(Utils::ConstraintMap::iterator ittt = it_constraints_map.begin(); 
                            ittt != it_constraints_map.end(); ++ittt)
                        {
                            if(input_constraints_map.find(ittt->first)==input_constraints_map.end() && 
                               new_input_constraints.find(ittt->first)==new_input_constraints.end())
                            {   // No constraints already found for variable ittt->first
                                input_constraints_map[ittt->first] = ittt->second;
                            }
                            else
                            {   // Merge the constraint that already existed with the new one
                                // Get the existing constraint
                                Utils::Constraint old_constraint;
                                if(input_constraints_map.find(ittt->first)!=input_constraints_map.end())
                                    old_constraint = input_constraints_map[ittt->first];
                                else
                                    old_constraint = new_input_constraints[ittt->first];
                                NBase current_constraint = ittt->second.get_constraint();
                                TL::Symbol current_symbol = ittt->second.get_symbol();
                                // If the new constraint is different from the old one, compute the combination of both
                                if(!Nodecl::Utils::structurally_equal_nodecls(old_constraint.get_constraint(), current_constraint, 
                                                                              /*skip_conversion_nodes*/true))
                                {
                                    // Get a new symbol for the new constraint
                                    NBase lhs = ittt->first;
                                    std::stringstream ss; ss << get_next_id(lhs);
                                    Symbol orig_s(lhs.get_symbol());
                                    std::string constr_name = orig_s.get_name() + "_" + ss.str();
                                    Symbol s(lhs.retrieve_context().new_symbol(constr_name));
                                    Type t(orig_s.get_type());
                                    s.set_type(t);
                                    
                                    // Build a the value of the new constraint
                                    NBase new_constraint_val;
                                    if(old_constraint.get_constraint().is<Nodecl::Analysis::Phi>())
                                    {   // Attach a new element to the list inside the node Phi
                                        Nodecl::List expressions = old_constraint.get_constraint().as<Nodecl::Analysis::Phi>().get_expressions().as<Nodecl::List>();
                                        expressions.append(ittt->first);
                                        new_constraint_val = Nodecl::Analysis::Phi::make(expressions, ittt->first.get_type());
                                    }
                                    else
                                    {   // Create a new node Phi with the combination of the old constraint and the new one
                                        Nodecl::Symbol tmp1 = old_constraint.get_symbol().make_nodecl(/*set_ref_type*/false);
                                        Nodecl::Symbol tmp2 = ittt->second.get_symbol().make_nodecl(/*set_ref_type*/false);
                                        Nodecl::List expressions = Nodecl::List::make(tmp1, tmp2);
                                        new_constraint_val = Nodecl::Analysis::Phi::make(expressions, current_constraint.get_type());
                                    }
                                    
                                    // Remove the old constraint (if it was in the new_input_constraints map, it will be deleted with the insertion)
                                    if(input_constraints_map.find(ittt->first)!=input_constraints_map.end())
                                        input_constraints_map.erase(input_constraints_map.find(ittt->first));
                                    // Build the actual constraint and insert it in the proper list
                                    Utils::Constraint c = build_constraint(s, new_constraint_val, t, "Propagated");
                                    new_input_constraints[ittt->first] = c;
                                }
                            }
                        }
                    }
                    
                    // Propagate constraints from parent nodes to the current node
                    n->set_propagated_constraints_map(input_constraints_map);
                    n->add_constraints_map(new_input_constraints);
                    if(n->has_statements())
                    {
                        // Compute the constraints of the current node
                        // Note: take into account the constraints the node may already have (if it is the TRUE or FALSE child of a conditional)
                        Utils::ConstraintMap current_constraints_map = n->get_constraints_map();
                        ConstraintBuilderVisitor cbv(input_constraints_map, current_constraints_map);
                        NodeclList stmts = n->get_statements();
                        for(NodeclList::iterator itt = stmts.begin(); itt != stmts.end(); ++itt)
                            cbv.compute_constraints(*itt);
                        n->add_constraints_map(cbv.get_output_constraints_map());
                        
                        // Set true/false output constraints to current children, if applies
                        ObjectList<Edge*> exits = n->get_exit_edges();
                        if(exits.size()==2 &&
                            ((exits[0]->is_true_edge() && exits[1]->is_false_edge()) || (exits[1]->is_true_edge() && exits[0]->is_false_edge())))
                        {
                            Utils::ConstraintMap out_true_constraints_map = cbv.get_output_true_constraints_map();
                            Utils::ConstraintMap out_false_constraints_map = cbv.get_output_false_constraints_map();
                            
                            // We always propagate to the TRUE edge
                            Node* true_node = (exits[0]->is_true_edge() ? exits[0]->get_target() : exits[1]->get_target());
                            Node* real_true_node = true_node;
                            while(true_node->is_exit_node())
                                true_node = true_node->get_outer_node()->get_children()[0];
                            if(true_node->is_graph_node())
                                true_node = true_node->get_graph_entry_node();
                            true_node->add_constraints_map(out_true_constraints_map);
                            
                            // For the if_else cases, we only propagate to the FALSE edge when it contains statements ('else' statements)
                            Node* false_node = (exits[0]->is_true_edge() ? exits[1]->get_target() : exits[0]->get_target());
                            ObjectList<Node*> real_true_node_children = real_true_node->get_children();
                            if((false_node->get_entry_edges().size() == 1) || !real_true_node_children.contains(false_node) )
                            {   // If the true_node is a parent of the false_node, then there are no statements
                                // Avoid cases where the FALSE edge leads to the end of the graph
                                ObjectList<Node*> children;
                                while(false_node->is_exit_node())
                                {
                                    children = false_node->get_outer_node()->get_children();
                                    if(!children.empty())
                                        false_node = children[0];
                                    else 
                                    {
                                        false_node = NULL;
                                        break;
                                    }
                                }
                                if(false_node!=NULL)
                                {
                                    if(false_node->is_graph_node())
                                        false_node = false_node->get_graph_entry_node();
                                    false_node->add_constraints_map(out_false_constraints_map);
                                }
                            }
                        }
                    }
                }
            }
            
            ObjectList<Node*> next_currents;
            for(ObjectList<Node*>::iterator itt = currents.begin(); itt != currents.end(); ++itt)
                next_currents.append((*itt)->get_children());
            currents = next_currents;
        }
    }
    
    static void recompute_node_constraints(Node* n, Utils::ConstraintMap new_constraint_map)
    {
        Utils::ConstraintMap current_constraint_map = n->get_constraints_map();
        for(Utils::ConstraintMap::iterator it = new_constraint_map.begin(); it != new_constraint_map.end(); ++it)
        {
            if((current_constraint_map.find(it->first) != current_constraint_map.end()) && 
                (current_constraint_map[it->first] != it->second))
            {
                NBase c1 = current_constraint_map[it->first].get_constraint().shallow_copy();
                NBase c2 = it->second.get_symbol().make_nodecl(/*set_ref_type*/false);
                Nodecl::List expressions = Nodecl::List::make(c1, c2);
                NBase c_val = Nodecl::Analysis::Phi::make(expressions, Utils::get_nodecl_base(it->first).get_symbol().get_type());
                // Set the new constraint
                Utils::Constraint c = build_constraint(current_constraint_map[it->first].get_symbol(), c_val, c_val.get_type(), "Recomputed");
                current_constraint_map[it->first] = c;
            }
        }
        n->set_constraints_map(current_constraint_map);
    }
    
    void RangeAnalysis::propagate_constraints_from_backwards_edges(Node* n)
    {
        if(n->is_visited())
            return;
        n->set_visited(true);
        
        if(n->is_graph_node())
            propagate_constraints_from_backwards_edges(n->get_graph_entry_node());
        
        // Check the exit edges looking for back edges
        ObjectList<Edge*> exit_edges = n->get_exit_edges();
        for(ObjectList<Edge*>::iterator it = exit_edges.begin(); it != exit_edges.end(); ++it)
        {
            if((*it)->is_back_edge())
                recompute_node_constraints((*it)->get_target(), n->get_all_constraints_map());
        }
        
        // Recursively treat the children
        ObjectList<Node*> children = n->get_children();
        for(ObjectList<Node*>::iterator it = children.begin(); it != children.end(); ++it)
            propagate_constraints_from_backwards_edges(*it);
    }
    
    // ****************************** End class implementing range analysis ******************************* //
    // **************************************************************************************************** //
}
}
