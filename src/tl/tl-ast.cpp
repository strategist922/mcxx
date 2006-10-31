#include "tl-builtin.hpp"
#include "tl-ast.hpp"

namespace TL
{
	std::map<AST, AST_t*> AST_t::ast_cache;

	AST_t* AST_t::wrap_ast(AST ast)
	{
		if (ast_cache.find(ast) == ast_cache.end())
		{
			ast_cache[ast] = new TL::AST_t(ast);
		}

		return ast_cache[ast];
	}

	tl_type_t* AST_t::get_extended_attribute(const std::string& name) const
	{
		//  First get the extended attribute
		void* p = extensible_struct_get_field_pointer(&ast_extensible_schema,
				&(this->_ast->extended_data),
				name.c_str());

		return (tl_type_t*) p;
	}

	std::string AST_t::prettyprint() const
	{
		char* c = prettyprint_in_buffer(this->_ast);
		std::string result(c);
		return result;
	}

	void AST_t::replace_with(AST_t* ast)
	{
		AST orig_tree = this->_ast;
		*(this->_ast) = *(ast->_ast);
		relink_parent(orig_tree, this->_ast);
	}

	AST_t* AST_t::duplicate() const
	{
		AST_t* result = wrap_ast(duplicate_ast(this->_ast));
		return result;
	}

	AST_list_t AST_t::get_all_subtrees_predicate(const Predicate& p) const
	{
		AST_list_t result;
		tree_iterator(*this, p, result);

		return result;
	}

	void AST_t::tree_iterator(const AST_t& a, const Predicate& p, AST_list_t& result)
	{
		if (p(a))
		{
			result.push_back(wrap_ast(a._ast));
		}

		AST tree = a._ast;

		for (int i = 0; i < ASTNumChildren(tree); i++)
		{
			if (ASTChild(tree, i) != NULL)
			{
				tree_iterator(AST_t(ASTChild(tree, i)), p, result);
			}
		}
	}

	std::string AST_t::internal_ast_type() const
	{
		char* inner_name = ast_node_names[ASTType(this->_ast)];
		std::string result(inner_name);
		return result;
	}

	void AST_t::append_to_translation_unit(AST_t* tree)
	{
		AST this_translation_unit = get_translation_unit(this->_ast);
		AST tree_translation_unit = get_translation_unit(tree->_ast);

		AST this_declaration_seq = ASTSon0(this_translation_unit);
		AST tree_declaration_seq = ASTSon0(tree_translation_unit);

		if (this_declaration_seq == NULL)
		{
			ASTSon0(this_translation_unit) = tree_declaration_seq;
		}
		else
		{
			append_list(this_declaration_seq, tree_declaration_seq);
		}
	}

	void AST_t::prepend_to_translation_unit(AST_t* tree)
	{
		AST this_translation_unit = get_translation_unit(this->_ast);
		AST tree_translation_unit = get_translation_unit(tree->_ast);

		AST this_declaration_seq = ASTSon0(this_translation_unit);
		AST tree_declaration_seq = ASTSon0(tree_translation_unit);

		if (this_declaration_seq == NULL)
		{
			ASTSon0(this_translation_unit) = tree_declaration_seq;
		}
		else
		{
			prepend_list(this_declaration_seq, tree_declaration_seq);
		}
	}

	void AST_t::prepend_list(AST orig_list, AST prepended_list)
	{
		if (ASTType(orig_list) != AST_NODE_LIST
				|| ASTType(prepended_list) != AST_NODE_LIST)
		{
			std::cerr << "You tried to prepend two lists that are not" << std::endl;
			return;
		}

		// The deeper node of orig_list must point to the top node of prepended_list
		AST iter = orig_list;
		while (ASTSon0(iter) != NULL)
		{
			iter = ASTSon0(iter);
		}

		// Relink the parent, first removep pointer to the prepended_list this son
		if (ASTParent(prepended_list) != NULL)
		{
			AST parent = ASTParent(prepended_list);
			for (int i = 0; i < ASTNumChildren(parent); i++)
			{
				if (ASTChild(parent, i) == prepended_list)
				{
					ASTChild(parent, i) = NULL;
					break;
				}
			}
		}

		// Now make the prepended_list as the son
		ASTSon0(iter) = prepended_list;
		ASTParent(prepended_list) = iter;
	}

	void AST_t::append_list(AST orig_list, AST appended_list)
	{
		if (ASTType(orig_list) != AST_NODE_LIST
				|| ASTType(appended_list) != AST_NODE_LIST)
		{
			std::cerr << "You tried to append two lists that are not" << std::endl;
			return;
		}

		// Appending one list to another is as easy as making the deeper node
		// "appended_list" to point to the top node of "orig_list"

		// First replace appended_list as the new child of the parent of orig_list
		relink_parent(orig_list, appended_list);

		// Now link the deeper node of the list to the top node of "orig_list"
		AST iter = appended_list;
		while (ASTSon0(iter) != NULL)
		{
			iter = ASTSon0(iter);
		}

		ASTSon0(iter) = orig_list;
		ASTParent(orig_list) = iter;
	}

	void AST_t::relink_parent(AST previous_child, AST new_child)
	{
		AST parent = ASTParent(previous_child);
		int i;
		for (i = 0; i < ASTNumChildren(parent); i++)
		{
			if (ASTChild(parent, i) == previous_child)
				break;
		}
		if (i == ASTNumChildren(parent))
		{
			std::cerr << "Claimed parent does not have the node as a child" << std::endl;
			return;
		}

		AST parent_new_child = ASTParent(new_child);
		if (parent_new_child != NULL)
		{
			int j;
			for (j = 0; j < ASTNumChildren(parent_new_child); j++)
			{
				if (ASTChild(parent_new_child, j) == new_child)
					break;
			}
			if (j == ASTNumChildren(parent))
			{
				std::cerr << "Claimed parent of new child does not have the node as a child" << std::endl;
				return;
			}

			// Disable for sanity this son now
			ASTChild(parent, j) = NULL;
		}

		// Relink
		ASTChild(parent, i) = new_child;
		ASTParent(new_child) = parent;
	}

	AST AST_t::get_translation_unit(AST node)
	{
		if  (node == NULL)
			return NULL;

		if (ASTType(node) == AST_TRANSLATION_UNIT)
		{
			return node;
		}

		return ASTParent(node);
	}

	AST_t* AST_t::get_enclosing_block()
	{
		AST node = _ast;
		while (!is_extensible_block(node))
		{
			node = ASTParent(node);
		}
		return wrap_ast(node);
	}

	bool AST_t::is_extensible_block(AST node)
	{
		return (ASTType(node) == AST_COMPOUND_STATEMENT
				|| ASTType(node) == AST_CLASS_SPECIFIER
				|| ASTType(node) == AST_TRANSLATION_UNIT
				|| ASTType(node) == AST_NAMESPACE_DEFINITION);
	}

	AST AST_t::get_list_of_extensible_block(AST node)
	{
		switch ((int)ASTType(node))
		{
			case AST_COMPOUND_STATEMENT :
				{
					// This can be null
					return ASTSon0(node);
					break;
				}
			case AST_CLASS_SPECIFIER :
				{
					// This one has to be handled specially because of its
					// special nature
					return ASTSon1(node);
					break;
				}
			case AST_TRANSLATION_UNIT :
				{
					// This can be null
					return ASTSon0(node);
					break;
				}
			case AST_NAMESPACE_DEFINITION :
				{
					// This can be null
					return ASTSon1(node);
					break;
				}
			default:
				return NULL;
		}
	}

	void AST_t::prepend(AST_t* t)
	{
		if (!is_extensible_block(_ast))
		{
			std::cerr << "This tree cannot be prepended anything (" 
				<< ast_print_node_type(ASTType(_ast)) << ")" << std::endl;
			return;
		}

		AST list = get_list_of_extensible_block(_ast);

		AST prepended_list = t->_ast;
		if (ASTType(t->_ast) != AST_NODE_LIST)
		{
			prepended_list = ASTListLeaf(prepended_list);
		}

		prepend_list(list, prepended_list);
	}

	void AST_t::append(AST_t* t)
	{
		if (!is_extensible_block(_ast))
		{
			std::cerr << "This tree cannot be appended anything (" 
				<< ast_print_node_type(ASTType(_ast)) << ")" << std::endl;
			return;
		}
		
		AST list = get_list_of_extensible_block(_ast);

		if (ASTType(list) == AST_MEMBER_SPEC)
		{
			// Handle this one appart
		}

		AST appended_list = t->_ast;
		if (ASTType(t->_ast) != AST_NODE_LIST)
		{
			appended_list = ASTListLeaf(appended_list);
		}

		append_list(list, appended_list);
	}

	AST_t* AST_t::get_enclosing_function_definition()
	{
		AST node = _ast;

		while (ASTType(node) != AST_FUNCTION_DEFINITION)
		{
			node = ASTParent(node);
		}

		return wrap_ast(node);
	}
}
