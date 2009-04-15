/* nih-dbus-tool
 *
 * marshal.c - type marshalling
 *
 * Copyright © 2009 Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <dbus/dbus.h>

#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/string.h>
#include <nih/logging.h>

#include "indent.h"
#include "type.h"
#include "marshal.h"


/* Prototypes for static functions */
static char *marshal_basic  (const void *parent,
			     DBusSignatureIter *iter,
			     const char *iter_name, const char *name,
			     const char *oom_error_code,
			     NihList *inputs, NihList *locals)
	__attribute__ ((malloc, warn_unused_result));
static char *marshal_array  (const void *parent,
			     DBusSignatureIter *iter,
			     const char *iter_name, const char *name,
			     const char *oom_error_code,
			     NihList *inputs, NihList *locals)
	__attribute__ ((malloc, warn_unused_result));
static char *marshal_struct (const void *parent,
			     DBusSignatureIter *iter,
			     const char *iter_name, const char *name,
			     const char *oom_error_code,
			     NihList *inputs, NihList *locals)
	__attribute__ ((malloc, warn_unused_result));


/**
 * marshal:
 * @parent: parent object for new string,
 * @signature: signature of type,
 * @iter_name: name of iterator variable,
 * @name: name of variable,
 * @oom_error_code: code to execute on OOM Error,
 * @inputs: list to append input variables to,
 * @locals: list to append local variables to.
 *
 * Generates C code to marshal any D-Bus type from an appropriately typed
 * variable named @name into the D-Bus iterator variable named @iter_name.
 *
 * The type should be the current element of the signature iterator @iter.
 * This then simply calls marshal_fixed(), marshal_string(),
 * marshal_fixed_array(), marshal_flexible_array() or marshal_struct()
 * as appropriate.
 *
 * The generated code detects out-of-memory conditions but does not know
 * how to handle them, therefore you need to pass the appropriate handling
 * code in @oom_error_code.  This code will be inserted wherever an OOM
 * condition is detected.
 *
 * The expected input variable types and names are given as TypeVar objects
 * appended to the @inputs list, each name is guaranteed to begin with @name
 * and the first member will always be @name itself.  Should the C code
 * require local variables, similar TypeVar objects will be appended to
 * the @locals list.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned string.  When all parents
 * of the returned string are entry, the returned string will also be
 * freed.
 *
 * Returns: newly allocated string or NULL if insufficient memory.
 **/
char *
marshal (const void *       parent,
	 DBusSignatureIter *iter,
	 const char *       iter_name,
	 const char *       name,
	 const char *       oom_error_code,
	 NihList *          inputs,
	 NihList *          locals)
{
	int dbus_type;

	nih_assert (iter != NULL);
	nih_assert (iter_name != NULL);
	nih_assert (name != NULL);
	nih_assert (oom_error_code != NULL);
	nih_assert (inputs != NULL);
	nih_assert (locals != NULL);

	dbus_type = dbus_signature_iter_get_current_type (iter);

	if (dbus_type_is_basic (dbus_type)) {
		return marshal_basic (parent, iter,
				      iter_name, name,
				      oom_error_code,
				      inputs, locals);
	} else if (dbus_type == DBUS_TYPE_ARRAY) {
		return marshal_array (parent, iter,
				      iter_name, name,
				      oom_error_code,
				      inputs, locals);
	} else if ((dbus_type == DBUS_TYPE_STRUCT)
		   || (dbus_type == DBUS_TYPE_DICT_ENTRY)) {
		return marshal_struct (parent, iter,
				       iter_name, name,
				       oom_error_code,
				       inputs, locals);
	} else {
		nih_assert_not_reached ();
	}
}


/**
 * marshal_basic:
 * @parent: parent object for new string,
 * @iter: D-Bus signature iterator,
 * @iter_name: name of iterator variable,
 * @name: name of variable,
 * @oom_error_code: code to execute on OOM Error,
 * @inputs: list to append input variables to,
 * @locals: list to append local variables to.
 *
 * Generates C code to marshal a D-Bus basic type (ie. numerics and strings)
 * from an appropriately typed variable named @name into the D-Bus iterator
 * variable named @iter_name.
 *
 * The type should be the current element of the signature iterator @iter.
 *
 * The generated code detects out-of-memory conditions but does not know
 * how to handle them, therefore you need to pass the appropriate handling
 * code in @oom_error_code.  This code will be inserted wherever an OOM
 * condition is detected.
 *
 * The expected input variable types and names are given as TypeVar objects
 * appended to the @inputs list, each name is guaranteed to begin with @name
 * and the first member will always be @name itself.  Should the C code
 * require local variables, similar TypeVar objects will be appended to
 * the @locals list.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned string.  When all parents
 * of the returned string are entry, the returned string will also be
 * freed.
 *
 * Returns: newly allocated string or NULL if insufficient memory.
 **/
static char *
marshal_basic (const void *       parent,
	       DBusSignatureIter *iter,
	       const char *       iter_name,
	       const char *       name,
	       const char *       oom_error_code,
	       NihList *          inputs,
	       NihList *          locals)
{
	int             dbus_type;
	const char *    dbus_const;
	nih_local char *oom_error_block = NULL;
	nih_local char *c_type = NULL;
	char *          code = NULL;
	TypeVar *       var;

	nih_assert (iter != NULL);
	nih_assert (iter_name != NULL);
	nih_assert (name != NULL);
	nih_assert (oom_error_code != NULL);
	nih_assert (inputs != NULL);
	nih_assert (locals != NULL);

	dbus_type = dbus_signature_iter_get_current_type (iter);
	dbus_const = type_const (dbus_type);

	oom_error_block = nih_strdup (NULL, oom_error_code);
	if (! oom_error_block)
		return NULL;
	if (! indent (&oom_error_block, NULL, 1))
		return NULL;

	/* We make our C type const as a promise that we never modify the
	 * value, if it's a pointer.
	 */
	c_type = type_of (NULL, iter);
	if (! c_type)
		return NULL;
	if (! type_to_const (&c_type, NULL))
		return NULL;

	if (! nih_strcat_sprintf (&code, parent,
				  "/* Marshal a %s onto the message */\n"
				  "if (! dbus_message_iter_append_basic (&%s, %s, &%s)) {\n"
				  "%s"
				  "}\n",
				  c_type,
				  iter_name, dbus_const, name,
				  oom_error_block))
		return NULL;

	/* Append our required input variable */
	var = type_var_new (code, c_type, name);
	if (! var) {
		nih_free (code);
		return NULL;
	}

	nih_list_add (inputs, &var->entry);

	return code;
}


/**
 * marshal_array:
 * @parent: parent object for new string,
 * @iter: D-Bus signature iterator,
 * @iter_name: name of iterator variable,
 * @name: name of variable,
 * @oom_error_code: code to execute on OOM Error,
 * @inputs: list to append input variables to,
 * @locals: list to append local variables to.
 *
 * Generates C code to marshal a D-Bus array type from an appropriately
 * typed, NULL-terminated, array variable named @name into the D-Bus
 * iterator variable named @iter_name.  In the case of arrays (of any
 * number of levels) ultimately to a fixed type, an additional input
 * named "@name"_len is required of size_t type or an appropriate number
 * of pointers to it.
 *
 * The type should be the current element of the signature iterator @iter.
 *
 * The generated code detects out-of-memory conditions but does not know
 * how to handle them, therefore you need to pass the appropriate handling
 * code in @oom_error_code.  This code will be inserted wherever an OOM
 * condition is detected.
 *
 * The expected input variable types and names are given as TypeVar objects
 * appended to the @inputs list, each name is guaranteed to begin with @name
 * and the first member will always be @name itself.  Should the C code
 * require local variables, similar TypeVar objects will be appended to
 * the @locals list.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned string.  When all parents
 * of the returned string are entry, the returned string will also be
 * freed.
 *
 * Returns: newly allocated string or NULL if insufficient memory.
 **/
static char *
marshal_array (const void *       parent,
	       DBusSignatureIter *iter,
	       const char *       iter_name,
	       const char *       name,
	       const char *       oom_error_code,
	       NihList *          inputs,
	       NihList *          locals)
{
	nih_local char *   array_iter_name = NULL;
	nih_local char *   loop_name = NULL;
	nih_local char *   element_name = NULL;
	nih_local char *   len_name = NULL;
	nih_local char *   oom_error_block = NULL;
	DBusSignatureIter  subiter;
	int                element_type;
	char *             signature;
	char *             code = NULL;
	TypeVar *          array_iter_var;
	NihList            element_inputs;
	NihList            element_locals;
	nih_local char *   element_block = NULL;
	nih_local char *   element_c_type = NULL;
	nih_local TypeVar *element_var = NULL;
	nih_local TypeVar *element_len_var = NULL;
	nih_local char *   block = NULL;
	nih_local char *   vars_block = NULL;

	nih_assert (iter != NULL);
	nih_assert (iter_name != NULL);
	nih_assert (name != NULL);
	nih_assert (oom_error_code != NULL);
	nih_assert (inputs != NULL);
	nih_assert (locals != NULL);

	array_iter_name = nih_sprintf (NULL, "%s_iter", name);
	if (! array_iter_name)
		return NULL;

	loop_name = nih_sprintf (NULL, "%s_i", name);
	if (! loop_name)
		return NULL;

	element_name = nih_sprintf (NULL, "%s_element", name);
	if (! element_name)
		return NULL;

	len_name = nih_sprintf (NULL, "%s_len", name);
	if (! len_name)
		return NULL;

	oom_error_block = nih_strdup (NULL, oom_error_code);
	if (! oom_error_block)
		return NULL;
	if (! indent (&oom_error_block, NULL, 1))
		return NULL;

	/* Open the array container, we need to give D-Bus the container
	 * signature to do this and we need a local variable for the
	 * recursed iterator.
	 */
	dbus_signature_iter_recurse (iter, &subiter);
	element_type = dbus_signature_iter_get_current_type (&subiter);

	signature = dbus_signature_iter_get_signature (&subiter);

	if (! nih_strcat_sprintf (&code, parent,
				  "/* Marshal an array onto the message */\n"
				  "if (! dbus_message_iter_open_container (&%s, DBUS_TYPE_ARRAY, \"%s\", &%s)) {\n"
				  "%s"
				  "}\n"
				  "\n",
				  iter_name, signature, array_iter_name,
				  oom_error_block)) {
		dbus_free (signature);
		return NULL;
	}

	dbus_free (signature);

	array_iter_var = type_var_new (code, "DBusMessageIter",
				       array_iter_name);
	if (! array_iter_var) {
		nih_free (code);
		return NULL;
	}

	nih_list_add (locals, &array_iter_var->entry);

	if (dbus_type_is_fixed (element_type)) {
		if (! nih_strcat_sprintf (&code, parent,
					  "for (size_t %s = 0; %s < %s; %s++) {\n",
					  loop_name, loop_name, len_name, loop_name)) {
			nih_free (code);
			return NULL;
		}
	} else {
		if (! nih_strcat_sprintf (&code, parent,
					  "for (size_t %s = 0; %s[%s]; %s++) {\n",
					  loop_name, name, loop_name, loop_name)) {
			nih_free (code);
			return NULL;
		}
	}

	/* Get the code that will marshal the individual elements, the
	 * inputs that we need to give and any local variables we have
	 * to declare.
	 */
	nih_list_init (&element_inputs);
	nih_list_init (&element_locals);
	element_block = marshal (NULL, &subiter,
				 array_iter_name, element_name,
				 oom_error_code,
				 &element_inputs,
				 &element_locals);
	if (! element_block) {
		nih_free (code);
		return NULL;
	}

	/* Each of the inputs of the marshalling code equates to one of our
	 * own inputs, except that we add another level of pointers for the
	 * array and make them const to promise we won't modify the values;
	 * at the same time, we keep the suffix and append it to our own name.
	 * Instead of mucking around with pointers and structure members,
	 * we also append the inputs onto the local lists and array for the
	 * value to be marshalled into this variable.
	 */
	NIH_LIST_FOREACH_SAFE (&element_inputs, iter) {
		TypeVar *       input_var = (TypeVar *)iter;
		nih_local char *var_type = NULL;
		char *          suffix;
		nih_local char *var_name = NULL;
		TypeVar *       var;

		var_type = nih_strdup (NULL, input_var->type);
		if (! var_type) {
			nih_free (code);
			return NULL;
		}

		if (! type_to_pointer (&var_type, NULL)) {
			nih_free (code);
			return NULL;
		}

		if (! type_to_const (&var_type, NULL)) {
			nih_free (code);
			return NULL;
		}

		nih_assert (! strncmp (input_var->name, element_name,
				       strlen (element_name)));
		suffix = input_var->name + strlen (element_name);

		var_name = nih_sprintf (NULL, "%s%s", name, suffix);
		if (! var_name) {
			nih_free (code);
			return NULL;
		}

		var = type_var_new (code, var_type, var_name);
		if (! var) {
			nih_free (code);
			return NULL;
		}

		nih_list_add (inputs, &var->entry);

		if (! nih_strcat_sprintf (&block, NULL,
					  "%s = %s[%s];\n",
					  input_var->name, var_name,
					  loop_name)) {
			nih_free (code);
			return NULL;
		}

		nih_list_add (&element_locals, &input_var->entry);
	}

	/* FIXME have a function to do this! */
	NIH_LIST_FOREACH (&element_locals, iter) {
		TypeVar *local_var = (TypeVar *)iter;

		if (! nih_strcat_sprintf (&vars_block, NULL, "%s %s;\n",
					  local_var->type,
					  local_var->name)) {
			nih_free (code);
			return NULL;
		}
	}

	/* Lay all that out in an indented block inside the for loop.
	 * Make sure that we initialise the individual elements from the
	 * pointer.
	 */
	if (! indent (&vars_block, NULL, 1)) {
		nih_free (code);
		return NULL;
	}

	if (! indent (&block, NULL, 1)) {
		nih_free (code);
		return NULL;
	}

	if (! indent (&element_block, NULL, 1)) {
		nih_free (code);
		return NULL;
	}


	if (! nih_strcat_sprintf (&code, parent,
			   "%s"
			   "\n"
			   "%s"
			   "\n"
			   "%s",
			   vars_block,
			   block,
			   element_block)) {
		nih_free (code);
		return NULL;
	}

	/* Close the container again */
	if (! nih_strcat_sprintf (&code, parent,
				  "}\n"
				  "\n"
				  "if (! dbus_message_iter_close_container (&%s, &%s)) {\n"
				  "%s"
				  "}\n",
				  iter_name, array_iter_name,
				  oom_error_block)) {
		nih_free (code);
		return NULL;
	}

	/* When iterating a fixed type, we get an extra length input */
	if (dbus_type_is_fixed (element_type)) {
		TypeVar *var;

		var = type_var_new (code, "size_t", len_name);
		if (! var) {
			nih_free (code);
			return NULL;
		}

		nih_list_add (inputs, &var->entry);
	}

	return code;
}


/**
 * marshal_struct:
 * @parent: parent object for new string,
 * @iter: D-Bus signature iterator,
 * @iter_name: name of iterator variable,
 * @name: name of variable,
 * @oom_error_code: code to execute on OOM Error,
 * @inputs: list to append input variables to,
 * @locals: list to append local variables to.
 *
 * Generates C code to marshal a D-Bus structure type, and its members,
 * from an appropriately typed variable named @name into the D-Bus iterator
 * variable named @iter_name.
 *
 * The type should be the current element of the signature iterator @iter.
 *
 * The generated code detects out-of-memory conditions but does not know
 * how to handle them, therefore you need to pass the appropriate handling
 * code in @oom_error_code.  This code will be inserted wherever an OOM
 * condition is detected.
 *
 * The expected input variable types and names are given as TypeVar objects
 * appended to the @inputs list, each name is guaranteed to begin with @name
 * and the first member will always be @name itself.  Should the C code
 * require local variables, similar TypeVar objects will be appended to
 * the @locals list.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned string.  When all parents
 * of the returned string are entry, the returned string will also be
 * freed.
 *
 * Returns: newly allocated string or NULL if insufficient memory.
 **/
static char *
marshal_struct (const void *       parent,
		DBusSignatureIter *iter,
		const char *       iter_name,
		const char *       name,
		const char *       oom_error_code,
		NihList *          inputs,
		NihList *          locals)
{
	int               dbus_type;
	const char *      dbus_const;
	nih_local char *  struct_iter_name = NULL;
	nih_local char *  oom_error_block = NULL;
	nih_local char *  c_type = NULL;
	DBusSignatureIter subiter;
	char *            code = NULL;
	TypeVar *         struct_iter_var;
	size_t            count = 0;
	TypeVar *         var;

	nih_assert (iter != NULL);
	nih_assert (iter_name != NULL);
	nih_assert (name != NULL);
	nih_assert (oom_error_code != NULL);
	nih_assert (inputs != NULL);
	nih_assert (locals != NULL);

	dbus_type = dbus_signature_iter_get_current_type (iter);
	dbus_const = type_const (dbus_type);

	struct_iter_name = nih_sprintf (NULL, "%s_iter", name);
	if (! struct_iter_name)
		return NULL;

	oom_error_block = nih_strdup (NULL, oom_error_code);
	if (! oom_error_block)
		return NULL;
	if (! indent (&oom_error_block, NULL, 1))
		return NULL;

	/* We make our C type const as a promise that we never modify the
	 * value, if it's a pointer.
	 */
	c_type = type_of (NULL, iter);
	if (! c_type)
		return NULL;
	if (! type_to_const (&c_type, NULL))
		return NULL;

	/* Open the struct container, for that we need to know whether this
	 * is a struct or a dictionary entry even though we handle the two
	 * identically.  We'll obviously need a local variable for the
	 * recursed iterator.
	 */
	dbus_signature_iter_recurse (iter, &subiter);

	if (! nih_strcat_sprintf (&code, parent,
				  "/* Marshal a structure onto the message */\n"
				  "if (! dbus_message_iter_open_container (&%s, %s, NULL, &%s)) {\n"
				  "%s"
				  "}\n"
				  "\n",
				  iter_name, dbus_const, struct_iter_name,
				  oom_error_block))
		return NULL;

	struct_iter_var = type_var_new (code, "DBusMessageIter",
					struct_iter_name);
	if (! struct_iter_var) {
		nih_free (code);
		return NULL;
	}

	nih_list_add (locals, &struct_iter_var->entry);

	/* Deal with each structure element individually, however we have
	 * to end up with just one set of locals and one block so we
	 * append directly onto our locals.
	 */
	do {
		nih_local char *item_name = NULL;
		NihList         item_inputs;
		NihList         item_locals;
		nih_local char *item_code = NULL;

		/* FIXME there should be a way to override the item names
		 * via an annotation, which would also show up in the
		 * structure definition itself.
		 */
		item_name = nih_sprintf (NULL, "%s_item%zu", name, count);
		if (! item_name) {
			nih_free (code);
			return NULL;
		}

		/* Get the code to do the marshalling of this item */
		nih_list_init (&item_inputs);
		nih_list_init (&item_locals);
		item_code = marshal (NULL, &subiter,
				     struct_iter_name, item_name,
				     oom_error_code,
				     &item_inputs,
				     &item_locals);
		if (! item_code) {
			nih_free (code);
			return NULL;
		}

		/* Append the item locals onto our locals list, we have
		 * to reference these as we go.
		 */
		NIH_LIST_FOREACH_SAFE (&item_locals, iter) {
			TypeVar *local_var = (TypeVar *)iter;

			nih_list_add (locals, &local_var->entry);
			nih_ref (local_var, code);
		}

		/* Instead of mucking around with pointers and structure
		 * members, each of the marshalling code inputs is appended
		 * onto the local list and we copy the value from the
		 * array into this variable.
		 */
		NIH_LIST_FOREACH_SAFE (&item_inputs, iter) {
			TypeVar *input_var = (TypeVar *)iter;
			char *   suffix;

			nih_list_add (locals, &input_var->entry);
			nih_ref (input_var, code);

			nih_assert (! strncmp (input_var->name, item_name,
					       strlen (item_name)));
			suffix = input_var->name + strlen (item_name);

			if (! nih_strcat_sprintf (&code, parent,
						  "%s = %s->item%zu%s;\n",
						  input_var->name, name,
						  count, suffix)) {
				nih_free (code);
				return NULL;
			}
		}

		/* Append item marshalling code block */
		if (! nih_strcat_sprintf (&code, parent,
					  "\n"
					  "%s"
					  "\n",
					  item_code)) {
			nih_free (code);
			return NULL;
		}

		nih_assert (++count > 0);
	} while (dbus_signature_iter_next (&subiter));

	/* Close the container again */
	if (! nih_strcat_sprintf (&code, parent,
				  "if (! dbus_message_iter_close_container (&%s, &%s)) {\n"
				  "%s"
				  "}\n",
				  iter_name, struct_iter_name,
				  oom_error_block)) {
		nih_free (code);
		return NULL;
	}

	/* Append our required input variable */
	var = type_var_new (code, c_type, name);
	if (! var) {
		nih_free (code);
		return NULL;
	}

	nih_list_add (inputs, &var->entry);

	return code;
}