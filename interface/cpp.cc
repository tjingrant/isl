/*
 * Copyright 2016 Tobias Grosser. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY SVEN VERDOOLAEGE ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SVEN VERDOOLAEGE OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as
 * representing official policies, either expressed or implied, of
 * Sven Verdoolaege.
 */

#include "isl_config.h"

#include <cstdio>
#include <cstdarg>
#include <iostream>
#include <map>
#include <vector>
#include "cpp.h"

/* Print string formatted according to "fmt" to ostream "os".
 *
 * This fprintf method allows us to use printf style formatting constructs when
 * writing to an ostream.
 */
void fprintf(ostream &os, const char *format, ...)
{
	va_list arguments;
	FILE *string_stream;
	char *string_pointer;
	size_t size;

	va_start(arguments, format);

	string_stream = open_memstream(&string_pointer, &size);

	if (!string_stream) {
		fprintf(stderr, "open_memstream failed -- aborting!\n");
		exit(1);
	}

	vfprintf(string_stream, format, arguments);
	fclose(string_stream);
	os << string_pointer;
}

cpp_generator::cpp_generator( set<RecordDecl *> &exported_types,
	set<FunctionDecl *> exported_functions, set<FunctionDecl *> functions)
    : generator(exported_types, exported_functions, functions)
{
}

void cpp_generator::generate()
{
	ostream &os = cout;

	fprintf(os, "\n");
	fprintf(os, "#ifndef ISL_CPP_ALL\n");
	fprintf(os, "#define ISL_CPP_ALL\n\n");
	fprintf(os, "namespace isl {\n\n");

	print_forward_declarations(os);

	os <<
		"#define STRINGIZE_(X) #X\n"
		"#define STRINGIZE(X) STRINGIZE_(X)\n"
		"\n"
		"#define ISLPP_ASSERT(test, message)     \\\n"
		"	do {								\\\n"
		"		if (test)						\\\n"
		"			break;						\\\n"
		"		fputs(\"Assertion \\\"\" #test \"\\\" failed at \" __FILE__ \":\" STRINGIZE(__LINE__)\"\\n  \" message  \"\\n\", stderr); \\\n"
		"	} while (0)\n"
		"\n"
		"// nullptr_r is also returned by pointer-valued functions in case of error.\n"
		"static const nullptr_t error;\n"
		"\n"
		"// Three-value logic\n"
		"class Tribool {\n"
		"private:\n"
		"	// Users don't need access to the internal representation.\n"
		"	//\n"
		"	// However, there is one use case where it could be useful:\n"
		"	//\n"
		"	// switch (tribool.switch()) {\n"
		"	//   case Tribool::False:\n"
		"	//   case Tribool::True:\n"
		"	//   case Tribool::Error: // or default:\n"
		"	// }\n"
		"	enum Values {\n"
		"		False = isl_bool_false,\n"
		"		True = isl_bool_true,\n"
		"		Error = isl_bool_error\n"
		"	};\n"
		"	Values Val;\n"
		"\n"
		"	/* implicit */ Tribool(Values Val) : Val(Val) {}\n"
		"\n"
		"public:\n"
		"	// Use the error-state by default\n"
		"	Tribool() : Val(Error) { }\n"
		"\n"
		"	// Allow assigning 'false', 'true' and 'error' to variables of type Tribool.\n"
		"	/* implicit */ Tribool(bool Val): Val(Val ? True : False) {}\n"
		"	/* implicit */ Tribool(nullptr_t) : Val(Error) {}\n"
		"\n"
		"	// For converting results from isl functions.\n"
		"	explicit Tribool(isl_bool Val) : Val(static_cast<Values>(Val)) {}\n"
		"\n"
		"	// Prefer one of these instead the implicit bool-conversion to be aware what should happen in the error-case.\n"
		"	bool isError() const { return Val == Error; }\n"
		"	bool isFalseOrError() const { return Val != True; }\n"
		"	bool isTrueOrError() const { return Val != False; }\n"
		"	bool isNoError() const { return Val != Error; }\n"
		"	bool isFalseNoError() const { return Val == False; }\n"
		"	bool isTrueNoError() const { return Val == True; }\n"
		"\n"
		"	// I would have preferred assert(isNoError()) or an exception here.\n"
		"	// Maybe even remove this so users must use one of the explicit conversions above.\n"
		"	//\n"
		"	// In case we cannot use the ISLPP_ASSERT, I opted for error being false-like (instead true-like, as isl_bool does), to be able to implement this schema:\n"
		"	// if (tristate) {\n"
		"	// } else if (!tristate) {\n"
		"	// } else {\n"
		"	//   /* error-case */\n"
		"	// }\n"
		"	explicit operator bool() const { ISLPP_ASSERT(isNoError(), \"IMPLEMENTATION ERROR: Unhandled error state\"); return Val == True; }\n"
		"\n"
		"	// isl_bool_not is a function call, maybe we should implement it directly to avoid the overhead.\n"
		"	Tribool operator!() const { return Tribool(isl_bool_not(static_cast<isl_bool>(Val))); }\n"
		"};\n"
		"\n"
		"// Users might expect this to be equivalent to (lhs.Val==rhs.Val), but error means the lack of a value. That is eg. the state 'error' means we could not determine the correct answer; and when comparing to another error, we still don't know what the correct answer would have been.\n"
		"static Tribool operator==(Tribool lhs, Tribool rhs) {\n"
		"	if (lhs.isError() || rhs.isError())\n"
		"		return error;\n"
		"	return lhs.isTrueNoError() == rhs.isTrueNoError();\n"
		"}\n"
		"// Users might expect this to be equivalent to (lhs.Val!=rhs.Val), but in three-values logic, the equivalence (lhs!=rhs) <=> (lhs^rhs) should hold.\n"
		"static Tribool operator!=(Tribool lhs, Tribool rhs) {\n"
		"	if (lhs.isError() || rhs.isError())\n"
		"		return error;\n"
		"	return lhs.isTrueNoError() != rhs.isTrueNoError();\n"
		"}\n"
		"// By definition, error-states propagate\n"
		"static Tribool operator|(Tribool lhs, Tribool rhs) {\n"
		"	if (lhs.isError() || rhs.isError())\n"
		"		return error;\n"
		"	return lhs.isTrueNoError() || rhs.isTrueNoError();\n"
		"}\n"
		"// However, depending on the value of one argument, the value of the other argument is irrelevant, hence the shortcut-operators take this into account.\n"
		"static Tribool operator||(Tribool lhs, Tribool rhs) {\n"
		"	if (lhs.isTrueNoError() && rhs.isTrueNoError())\n"
		"		return true;\n"
		"	if (lhs.isError() || rhs.isError())\n"
		"		return error;\n"
		"	return lhs.isTrueNoError() || rhs.isTrueNoError();\n"
		"}\n"
		"static Tribool operator&(Tribool lhs, Tribool rhs) {\n"
		"	if (lhs.isError() || rhs.isError())\n"
		"		return error;\n"
		"	return lhs.isTrueNoError() && rhs.isTrueNoError();\n"
		"}\n"
		"static Tribool operator&&(Tribool lhs, Tribool rhs) {\n"
		"	if (lhs.isFalseNoError() && rhs.isFalseNoError())\n"
		"		return false;\n"
		"	if (lhs.isError() || rhs.isError())\n"
		"		return error;\n"
		"	return lhs.isTrueNoError() && rhs.isTrueNoError();\n"
		"}\n"
		"static Tribool operator^(Tribool lhs, Tribool rhs) {\n"
		"	if (lhs.isError() || rhs.isError())\n"
		"		return error;\n"
		"	return lhs.isTrueNoError() ^ rhs.isTrueNoError();\n"
		"}\n"
"\n"
"// Because of the bool conversion operator to bool, we need more overloads so the compiler knows which one to pick.\n"
"static Tribool operator==(bool lhs, Tribool rhs) { return operator==(Tribool(lhs), rhs); }\n"
"static Tribool operator==(Tribool lhs, bool rhs) { return operator==(lhs, Tribool(rhs)); }\n"
"static Tribool operator!=(bool lhs, Tribool rhs) { return operator==(Tribool(lhs), rhs); }\n"
"static Tribool operator!=(Tribool lhs, bool rhs) { return operator==(lhs, Tribool(rhs)); }\n"
"static Tribool operator|(bool lhs, Tribool rhs) { return operator|(Tribool(lhs), rhs); }\n"
"static Tribool operator|(Tribool lhs, bool rhs) { return operator|(lhs, Tribool(rhs)); }\n"
"static Tribool operator||(bool lhs, Tribool rhs) { return operator||(Tribool(lhs), rhs); }\n"
"static Tribool operator||(Tribool lhs, bool rhs) { return operator||(lhs, Tribool(rhs)); }\n"
"static Tribool operator&(bool lhs, Tribool rhs) { return operator&(Tribool(lhs), rhs); }\n"
"static Tribool operator&(Tribool lhs, bool rhs) { return operator&(lhs, Tribool(rhs)); }\n"
"static Tribool operator&&(bool lhs, Tribool rhs) { return operator&&(Tribool(lhs), rhs); }\n"
"static Tribool operator&&(Tribool lhs, bool rhs) { return operator&&(lhs, Tribool(rhs)); }\n"
"static Tribool operator^(bool lhs, Tribool rhs) { return operator^(Tribool(lhs), rhs); }\n"
"static Tribool operator^(Tribool lhs, bool rhs) { return operator^(lhs, Tribool(rhs)); }\n"
"\n";

	os <<
		"enum class DimType {\n"
		"	Cst =  isl_dim_cst,\n"
		"	Param = isl_dim_param,\n"
		"	In = isl_dim_in,\n"
		"	Out = isl_dim_out,\n"
		"	Set = isl_dim_set,\n"
		"	Div = isl_dim_div,\n"
		"	All = isl_dim_all\n"
		"};\n\n";

	print_declarations(os);
	print_implementations(os);

	fprintf(os, "};\n\n");
	fprintf(os, "#endif /* ISL_CPP_ALL */\n");
}

void cpp_generator::print_forward_declarations(ostream &os)
{
	map<string, isl_class>::iterator ci;

	fprintf(os, "// forward declarations\n");

	for (ci = classes.begin(); ci != classes.end(); ++ci)
		print_class_forward_decl(os, ci->second);
	fprintf(os, "\n");
}

void cpp_generator::print_declarations(ostream &os)
{
	map<string, isl_class>::iterator ci;

	for (ci = classes.begin(); ci != classes.end(); ++ci)
		print_class(os, ci->second);
}

void cpp_generator::print_implementations(ostream &os)
{
	map<string, isl_class>::iterator ci;

	for (ci = classes.begin(); ci != classes.end(); ++ci)
		print_class_impl(os, ci->second);
}

void cpp_generator::print_class(ostream &os, const isl_class &clazz)
{
	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();

	fprintf(os, "// declarations for isl::%s\n", cppname);

	print_class_global_constructor(os, clazz);
	fprintf(os, "class %s {\n", cppname);
	fprintf(os, "  friend ");
	print_class_global_constructor(os, clazz);
	fprintf(os, "  %s *Ptr = nullptr;\n", name);
	fprintf(os, "\n");
	print_private_constructors(os, clazz);
	fprintf(os, "\n");
	fprintf(os, "public:\n");
	print_public_constructors(os, clazz);
	print_conversion_constructors(os, clazz);
	print_copy_assignment(os, clazz);
	print_destructor(os, clazz);
	print_ptr(os, clazz);
	print_str(os, clazz);
	print_get_ctx(os, clazz);
	print_methods(os, clazz);

	fprintf(os, "};\n\n");
}

void cpp_generator::print_class_forward_decl(ostream &os,
	const isl_class &clazz)
{
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();

	fprintf(os, "class %s;\n", cppname);
}

void cpp_generator::print_class_global_constructor(ostream &os,
	const isl_class &clazz)
{
	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();

	fprintf(os, "inline %s manage(__isl_take %s *Ptr);\n\n", cppname,
		name);
}

void cpp_generator::print_private_constructors(ostream &os,
	const isl_class &clazz)
{
	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "  inline explicit %s(__isl_take %s *Ptr);\n", cppname,
		name);
}

void cpp_generator::print_public_constructors(ostream &os,
	const isl_class &clazz)
{
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "  inline %s();\n", cppname);
	fprintf(os, "  inline %s(const %s &Obj);\n", cppname, cppname);
}

void cpp_generator::print_conversion_constructors(ostream &os,
       const isl_class &clazz)
{
	set<FunctionDecl *>::const_iterator in;
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();

	for (in = clazz.constructors.begin(); in != clazz.constructors.end();
		++in) {
		auto cons = *in;
		if (!is_conversion_constructor(cons))
			continue;
		string fullname = cons->getName();
		ParmVarDecl *param = cons->getParamDecl(0);
		QualType type = param->getOriginalType();
		std::string typestr = type->getPointeeType().getAsString();
		std::string paramstring = type2cpp(typestr);
		const char *paramname = paramstring.c_str();
		fprintf(os, "  inline %s(%s Obj);\n",
		cppname, paramname);
	}
}

void cpp_generator::print_copy_assignment(ostream &os,
	const isl_class &clazz)
{
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "  inline %s& operator=(%s Obj);\n", cppname, cppname);
}

void cpp_generator::print_destructor(ostream &os, const isl_class &clazz)
{
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "  inline ~%s();\n", cppname);
}

void cpp_generator::print_ptr(ostream &os, const isl_class &clazz)
{
	const char *name = clazz.name.c_str();
	fprintf(os, "  inline __isl_give %s *copy() const &;\n", name);
	fprintf(os, "  inline __isl_give %s *copy() && = delete;\n", name);
	fprintf(os, "  inline __isl_keep %s *get() const;\n", name);
	fprintf(os, "  inline __isl_give %s *release();\n", name);
	fprintf(os, "  inline bool isNull() const;\n");
}

void cpp_generator::print_str(ostream &os, const isl_class &clazz)
{
	if (!clazz.fn_to_str)
		return;

	fprintf(os, "  inline std::string getStr() const;\n");
}

void cpp_generator::print_get_ctx(ostream &os, const isl_class &clazz)
{
	if (!clazz.fn_get_ctx)
		return;

	fprintf(os, "  inline isl_ctx *getCtx() const;\n");
}

void cpp_generator::print_methods(ostream &os, const isl_class &clazz)
{
	map<string, set<FunctionDecl *> >::const_iterator it;
	for (it = clazz.methods.begin(); it != clazz.methods.end(); ++it)
		print_method(os, clazz, it->first, it->second);
}

void cpp_generator::print_method(ostream &os, const isl_class &clazz,
	const string &fullname, const set<FunctionDecl *> &methods)
{
	FunctionDecl *any_method;

	any_method = *methods.begin();
	if (methods.size() == 1 && !is_overload(any_method)) {
		print_method(os, clazz, any_method);
		return;
	}

	return;
}

void cpp_generator::print_method(ostream &os, const isl_class &clazz,
	FunctionDecl *method)
{
	if (!is_supported_method(clazz, method))
		return;

	print_method_header(os, clazz, method, true /* is_declaration */);
}

void cpp_generator::print_class_impl(ostream &os, const isl_class &clazz)
{
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();

	fprintf(os, "// implementations for isl::%s\n", cppname);

	print_class_global_constructor_impl(os, clazz);
	print_public_constructors_impl(os, clazz);
	print_private_constructors_impl(os, clazz);
	print_conversion_constructors_impl(os, clazz);
	print_copy_assignment_impl(os, clazz);
	print_destructor_impl(os, clazz);
	print_ptr_impl(os, clazz);
	print_str_impl(os, clazz);
	print_raw_ostream_impl(os, clazz);
	print_get_ctx_impl(os, clazz);
	print_methods_impl(os, clazz);
}

void cpp_generator::print_class_global_constructor_impl(ostream &os,
	const isl_class &clazz)
{
	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();

	fprintf(os, "%s manage(__isl_take %s *Ptr) {\n", cppname, name);
	fprintf(os, "  return %s(Ptr);\n", cppname);
	fprintf(os, "}\n\n");
}

void cpp_generator::print_private_constructors_impl(ostream &os,
	const isl_class &clazz)
{
	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "%s::%s(__isl_take %s *Ptr) : Ptr(Ptr) {}\n\n", cppname,
		cppname, name);
}

void cpp_generator::print_public_constructors_impl(ostream &os,
	const isl_class &clazz)
{
	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "%s::%s() : Ptr(nullptr) {}\n\n", cppname, cppname);
	fprintf(os, "%s::%s(const %s &Obj) : Ptr(Obj.copy()) {}\n\n",
		cppname, cppname, cppname, name);
}

void cpp_generator::print_conversion_constructors_impl(ostream &os,
       const isl_class &clazz)
{
	set<FunctionDecl *>::const_iterator in;
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();

	for (in = clazz.constructors.begin(); in != clazz.constructors.end();
		++in) {
		auto cons = *in;
		if (!is_conversion_constructor(cons))
			continue;
		string fullname = cons->getName();
		ParmVarDecl *param = cons->getParamDecl(0);
		QualType type = param->getOriginalType();
		std::string typestr = type->getPointeeType().getAsString();
		std::string paramstring = type2cpp(typestr);
		const char *paramname = paramstring.c_str();
		fprintf(os, "%s::%s(%s Obj) {\n", cppname,
			cppname, paramname);
		fprintf(os, "  Ptr = %s(Obj.release());\n", fullname.c_str());
		fprintf(os, "}\n\n");
	}
}

void cpp_generator::print_copy_assignment_impl(ostream &os,
	const isl_class &clazz)
{
	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "%s& %s::operator=(%s Obj) {\n", cppname,
		cppname, cppname);
	fprintf(os, "  std::swap(this->Ptr, Obj.Ptr);\n", name);
	fprintf(os, "  return *this;\n");
	fprintf(os, "}\n\n");
}

void cpp_generator::print_destructor_impl(ostream &os,
	const isl_class &clazz)
{
	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "%s::~%s() {\n", cppname, cppname);
	fprintf(os, "  if (Ptr)\n");
	fprintf(os, "    %s_free(Ptr);\n", name);
	fprintf(os, "}\n\n");
}

void cpp_generator::print_ptr_impl(ostream &os, const isl_class &clazz)
{
	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "__isl_give %s *%s::copy() const & {\n", name, cppname);
	fprintf(os, "  return %s_copy(Ptr);\n", name);
	fprintf(os, "}\n\n");
	fprintf(os, "__isl_keep %s *%s::get() const {\n", name, cppname);
	fprintf(os, "  return Ptr;\n");
	fprintf(os, "}\n\n");
	fprintf(os, "__isl_give %s *%s::release() {\n", name, cppname);
	fprintf(os, "  %s *Tmp = Ptr;\n", name);
	fprintf(os, "  Ptr = nullptr;\n");
	fprintf(os, "  return Tmp;\n");
	fprintf(os, "}\n\n");
	fprintf(os, "bool %s::isNull() const {\n", cppname);
	fprintf(os, "  return Ptr == nullptr;\n");
	fprintf(os, "}\n\n");
}

void cpp_generator::print_str_impl(ostream &os, const isl_class &clazz)
{
	if (!clazz.fn_to_str)
		return;

	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "std::string %s::getStr() const {\n", cppname);
	fprintf(os, "  char *Tmp = %s_to_str(get());\n", name, name);
	fprintf(os, "  std::string S(Tmp);\n");
	fprintf(os, "  free(Tmp);\n");
	fprintf(os, "  return Tmp;\n");
	fprintf(os, "}\n\n");
}

void cpp_generator::print_raw_ostream_impl(ostream &os, const isl_class &clazz)
{
	if (!clazz.fn_to_str)
		return;

	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "inline ");
	fprintf(os, "llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,\n");
	fprintf(os, "  %s &Obj) {\n", cppname);
	fprintf(os, "  OS << Obj.getStr();\n");
	fprintf(os, "  return OS;\n");
	fprintf(os, "}\n\n");
}

void cpp_generator::print_get_ctx_impl(ostream &os, const isl_class &clazz)
{
	if (!clazz.fn_get_ctx)
		return;

	const char *name = clazz.name.c_str();
	std::string cppstring = type2cpp(clazz.name);
	const char *cppname = cppstring.c_str();
	fprintf(os, "isl_ctx *%s::getCtx() const {\n", cppname);
	fprintf(os, "  return %s_get_ctx(get());\n", name, name);
	fprintf(os, "}\n\n");
}

void cpp_generator::print_methods_impl(ostream &os, const isl_class &clazz)
{
	map<string, set<FunctionDecl *> >::const_iterator it;
	for (it = clazz.methods.begin(); it != clazz.methods.end(); ++it)
		print_method_impl(os, clazz, it->first, it->second);
}

void cpp_generator::print_method_impl(ostream &os, const isl_class &clazz,
	const string &fullname, const set<FunctionDecl *> &methods)
{
	FunctionDecl *any_method;

	any_method = *methods.begin();
	if (methods.size() == 1 && !is_overload(any_method)) {
		print_method_impl(os, clazz, any_method);
		return;
	}

	return;
}

void cpp_generator::print_method_impl(ostream &os, const isl_class &clazz,
	FunctionDecl *method)
{
	string fullname = method->getName();
	string cname = fullname.substr(clazz.name.length() + 1);
	int num_params = method->getNumParams();

	if (!is_supported_method(clazz, method))
		return;

	print_method_header(os, clazz, method, false /* is_declaration */);

	QualType return_type = method->getReturnType();
	string rettype_str =
		type2cpp(return_type->getPointeeType().getAsString());

	fprintf(os, "   return ");
	if (is_isl_type(return_type))
		fprintf(os, "manage(");
	fprintf(os, "%s(", fullname.c_str());

	for (int i = 0; i < num_params; ++i) {
		ParmVarDecl *param = method->getParamDecl(i);
		QualType type = param->getOriginalType();
		string type_name = type.getAsString();

		if (type_name == "isl_dim_type" || type_name == "enum isl_dim_type") {
			os << "static_cast<isl_dim_type>(" << param->getName().str() << ")";
		} else if (is_isl_type(type)) {
			if (i == 0)
				fprintf(os, "");
			else
				fprintf(os, "%s.",
					param->getName().str().c_str());

			if (takes(param))
				fprintf(os, "copy()");
			else
				fprintf(os, "get()");
		} else {
			fprintf(os, "%s", param->getName().str().c_str());
		}

		if (i != num_params - 1)
		  fprintf(os, ", ");
	}
	if (is_isl_type(return_type))
		fprintf(os, ")");
	fprintf(os, ");\n");

	fprintf(os, "}\n\n");

}

void cpp_generator::print_method_header(ostream &os, const isl_class &clazz,
	FunctionDecl *method, bool is_declaration)
{
	string fullname = method->getName();
	string cname = fullname.substr(clazz.name.length() + 1);
	cname = to_camel_case(cname, true /* start_lowercase */);
	int num_params = method->getNumParams();

	QualType return_type = method->getReturnType();
	string classname = type2cpp(clazz.name);

	if (is_declaration)
		fprintf(os, "  inline ");

	if (is_isl_type(return_type)) {
		string rettype_str =
			type2cpp(return_type->getPointeeType().getAsString());
		fprintf(os, "%s ", rettype_str.c_str());
	} else {
		string return_type_name = return_type.getAsString();
		if (return_type_name == "isl_bool" || return_type_name == "enum isl_bool")
			return_type_name = "Tribool";
		fprintf(os, "%s ", return_type_name.c_str());
	}

	if (is_declaration)
		fprintf(os, "%s(", cname.c_str());
	else
		fprintf(os, "%s::%s(", classname.c_str(), cname.c_str());


	for (int i = 1; i < num_params; ++i) {
		ParmVarDecl *param = method->getParamDecl(i);
		QualType type = param->getOriginalType();

		if (is_isl_type(type)) {
			string cpptype = type2cpp(type->getPointeeType().getAsString());
			fprintf(os, "const %s &%s", cpptype.c_str(),
				param->getName().str().c_str());
		} else {
			// TODO: Refactor out QualType to C++ type conversion, and it is NOT type2cpp!!
			string type_name = type.getAsString();
			if (type_name == "isl_dim_type" || type_name == "enum isl_dim_type")
				type_name = "DimType";

			fprintf(os, "%s %s",
				type_name.c_str(),
				param->getName().str().c_str());
		}

		if (i != num_params - 1)
		  fprintf(os, ", ");
	}
	if (is_declaration)
		fprintf(os, ") const;\n");
	else
		fprintf(os, ") const {\n");
}

/* An array of C++ keywords which prevent us from directly use certain isl
 * method names in C++.
 */
static const char *cpp_keywords[] = {
  "union",
};

bool cpp_generator::is_supported_method(const isl_class &clazz,
	FunctionDecl *method) {
	string fullname = method->getName();
	string cname = fullname.substr(clazz.name.length() + 1);
	int num_params = method->getNumParams();

	if (first_arg_is_isl_ctx(method))
		return false;

	for (size_t i = 0; i < sizeof(cpp_keywords)/sizeof(cpp_keywords[0]); i++)
		if (cname.compare(cpp_keywords[i]) == 0)
			return false;

	if (is_static(clazz, method))
		return false;

	for (int i = 1; i < num_params; ++i)
		if (!is_supported_method_param(method->getParamDecl(i)))
			return false;

	if (!is_supported_method_rettype(method->getReturnType()))
		return false;

	return true;
}

bool cpp_generator::is_supported_method_param(ParmVarDecl *param)
{
	QualType type = param->getOriginalType();
	if (is_isl_type(type))
		return true;
	if (type->isIntegerType())
		return true;

	return false;
}

bool cpp_generator::is_supported_method_rettype(QualType type)
{
	if (is_isl_type(type))
		return true;

	if (type->isIntegerType())
		return true;

	return false;
}

string cpp_generator::to_camel_case(const string &input, bool start_lowercase)
{
	string output;
	bool uppercase = !start_lowercase;

	for (const char &character : input) {
		if (character == '_') {
			uppercase = true;
			continue;
		}
		if (uppercase) {
			output.append(1, toupper(character));
			uppercase = false;
		} else {
			output.append(1, character);
		}
	}

	return output;
}

string cpp_generator::type2cpp(string name)
{
	return to_camel_case(name.substr(4));
}

bool cpp_generator::is_conversion_constructor(FunctionDecl *cons)
{
	int num_params = cons->getNumParams();
	if (num_params != 1)
		return false;

	ParmVarDecl *param = cons->getParamDecl(0);
	QualType type = param->getOriginalType();
	std::string type_name = type->getPointeeType().getAsString();

	if (type_name == "isl_ctx")
		return false;

	if (cons->getName().find("from_") == std::string::npos)
		return false;

	return is_isl_type(type);
}
