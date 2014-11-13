
#include "rubyuno.hxx"

#include <cppuhelper/bootstrap.hxx>
#include <osl/file.hxx>
#include <osl/thread.h>
#include <rtl/textenc.h>
#include <rtl/ustrbuf.hxx>
#include <rtl/uuid.h>
#include <typelib/typedescription.hxx>

#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/XMaterialHolder.hpp>
#include <com/sun/star/lang/WrappedTargetException.hpp>
#include <com/sun/star/reflection/XConstantsTypeDescription.hpp>
#include <com/sun/star/reflection/XConstantTypeDescription.hpp>
#include <com/sun/star/reflection/XEnumTypeDescription.hpp>
#include <com/sun/star/reflection/XIdlClass.hpp>
#include <com/sun/star/script/InvocationInfo.hpp>
#include <com/sun/star/script/MemberType.hpp>
#include <com/sun/star/uno/Type.hxx>

using com::sun::star::beans::XMaterialHolder;
using com::sun::star::lang::WrappedTargetException;
using com::sun::star::reflection::XConstantsTypeDescription;
using com::sun::star::reflection::XConstantTypeDescription;
using com::sun::star::reflection::XEnumTypeDescription;
using com::sun::star::reflection::XIdlClass;
using com::sun::star::reflection::XTypeDescription;
using com::sun::star::script::InvocationInfo;
using com::sun::star::script::MemberType;
using com::sun::star::script::XInvocation2;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Type;
using com::sun::star::uno::TypeDescription;
using com::sun::star::uno::XComponentContext;
using com::sun::star::uno::UNO_QUERY;

using namespace cppu;
using namespace rtl;

using namespace com::sun::star::uno;
using namespace com::sun::star::beans;
using namespace com::sun::star::script;

using rtl::OUString;
using rtl::OUStringBuffer;
using rtl::OUStringToOString;

using namespace rubyuno;

namespace
{

/*
 * Make URL absolute from URL and relative path.
 * p Rubyuno.absolutize("file:///home/ruby/Desktop", "../")
 * #=> "file:///home/ruby/"
 */
static VALUE
rubyuno_absolutize(VALUE self, VALUE base_url, VALUE rel_path)
{
	rb_secure(2);
	OUString baseUrl = rbString2OUString(base_url);
	OUString relativePath = rbString2OUString(rel_path);
	OUString absUrl;

	osl::FileBase::RC e = osl::FileBase::getAbsoluteFileURL(baseUrl, relativePath, absUrl);
	if (e != osl::FileBase::E_None)
	{
		rb_raise(rb_eArgError, "invalid url (%s) or invalid relative path (%s)", RSTRING_PTR(base_url), RSTRING_PTR(rel_path));
	}
	return ustring2RString(absUrl);
}

/*
 * Convert system path to file URL. Multi-byte characters are escaped.
 * p Rubyuno.system_path_to_file_url("/home/ruby/Desktop")
 * #=> "file:///home/ruby/Desktop"
 */
static VALUE
rubyuno_getSystemPathFromFileURL(VALUE self, VALUE file_url)
{
	rb_secure(2);
	OUString url = rbString2OUString(file_url);
	OUString sysPath;
	osl::FileBase::RC e = osl::FileBase::getSystemPathFromFileURL(url, sysPath);
	if (e != osl::FileBase::E_None)
	{
		rb_raise(rb_eArgError, "invalid url (%s)", RSTRING_PTR(file_url));
	}
	return ustring2RString(sysPath);
}

/*
 * Convert file URL to system path.
 * p Rubyuno.file_url_to_system_path("file:///home/ruby/Desktop)
 * #=> "/home/ruby/Desktop"
 */
static VALUE
rubyuno_getFileURLFromSystemPath(VALUE self, VALUE path)
{
	rb_secure(2);
	OUString sysPath = rbString2OUString(path);
	OUString url;
	osl::FileBase::RC e = osl::FileBase::getFileURLFromSystemPath(sysPath, url);
	if (e != osl::FileBase::E_None)
	{
		rb_raise(rb_eArgError, "invalid path (%s)", RSTRING_PTR(path));
	}
	return ustring2RString(url);
}

/*
 * Generate UUID version 4
 */
static VALUE
rubyuno_create_uuid(VALUE self)
{
	Sequence< sal_Int8 > seq(16);
	rtl_createUuid((sal_uInt8*)seq.getArray(), 0, sal_False);
	VALUE ret;
	try
	{
		Runtime runtime;
		ret = runtime.any_to_VALUE(makeAny(seq));
	}
	catch (RuntimeException &e)
	{
		rb_raise(rb_eRuntimeError, "failed to create UUID.");
	}
	return ret;
}

/*
 * Returns component contex and initialize bridge.
 * ctx = Rubyuno.get_component_context
 */
static VALUE
rubyuno_getComponentContext(VALUE self)
{
	rb_secure(2);
	Reference< XComponentContext > ctx;

	if (Runtime::isInitialized())
	{
		Runtime r;
		ctx = r.getImpl()->xComponentContext;
	}
	else
	{
		ctx = defaultBootstrap_InitialComponentContext();
		if (ctx.is())
		{
			try
			{
				Runtime::initialize(ctx);
			}
			catch (RuntimeException &e)
			{
				rb_raise(rb_eTypeError, "wrong value in @rubyuno_runtime variable.");
			}
		}
		else
		{
			rb_raise(rb_eRuntimeError, "failed to bootstrap.");
		}
	}
	if (ctx.is()) {
		Runtime runtime;
		return runtime.any_to_VALUE(makeAny(ctx));
	}
	return Qnil;
}


/*
 * Load UNO value.
 */
static VALUE
rubyuno_uno_require(VALUE self, VALUE name)
{
	VALUE ret = Qnil;
	try
	{
		OUString typeName = rbString2OUString(name);
		Runtime runtime;
		Any a = runtime.getImpl()->xTypeDescription->getByHierarchicalName(typeName);
		if (a.getValueTypeClass() == TypeClass_INTERFACE)
		{
			Reference< XTypeDescription > xTd(a, UNO_QUERY);
			if (!xTd.is())
				rb_raise(rb_eArgError, "unknown type (%s)", RSTRING_PTR(name));

			// typeName is checked by the TypeDescriptionManager
			VALUE module, klass;
			ID id;
			sal_Int32 ends;
			OUString tmpName, moduleName;
			char *className;
			OUStringBuffer buf;

			ends = typeName.lastIndexOfAsciiL(".", 1);
			tmpName = typeName.copy(ends + 1);

			module = create_module(typeName);

			buf.append(tmpName.copy(0, 1).toAsciiUpperCase()).append(tmpName.copy(1));
			className = (char*) OUStringToOString(buf.makeStringAndClear(), RTL_TEXTENCODING_ASCII_US ).getStr();
			id = rb_intern(className);

			if (xTd->getTypeClass() == TypeClass_STRUCT || xTd->getTypeClass() == TypeClass_EXCEPTION)
			{
				Any aStruct;
				Reference< XIdlClass > xIdlClass(runtime.getImpl()->xCoreReflection->forName(typeName), UNO_QUERY);
				if (xIdlClass.is())
				{
					// forName method breaks className
					buf.append(tmpName.copy(0, 1).toAsciiUpperCase()).append(tmpName.copy(1));
					className = (char*) OUStringToOString(
						buf.makeStringAndClear(), RTL_TEXTENCODING_ASCII_US).getStr();
					if (rb_const_defined(module, id))
					{
						klass = rb_const_get(module, id);
					}
					else
					{
						if (xTd->getTypeClass() == TypeClass_STRUCT)
							klass = rb_define_class_under(module, className, get_struct_class());
						else
							klass = rb_define_class_under(module, className, get_exception_class());

						VALUE type_name = rb_str_new2(RSTRING_PTR(name));
						rb_define_const(klass, "TYPENAME", type_name);
					}
					return klass;
				}
				rb_raise(rb_eArgError, "unknown type (%s)", RSTRING_PTR(name));
			}
			else if (xTd->getTypeClass() == TypeClass_CONSTANTS)
			{
				if (rb_const_defined(module, id))
					klass = rb_const_get(module, id);
				else
					klass = rb_define_module_under(module, className);

				VALUE value;
				Reference< XConstantsTypeDescription > xDesc(a, UNO_QUERY);
				Sequence< Reference< XConstantTypeDescription > > constants(xDesc->getConstants());
				sal_Int32 nStart = typeName.getLength() + 1;
				for (sal_Int32 i = 0; i < constants.getLength(); i++)
				{
					value = runtime.any_to_VALUE(constants[i]->getConstantValue());
					rb_iv_set(klass, OUStringToOString(constants[i]->getName().copy(nStart), RTL_TEXTENCODING_ASCII_US).getStr(), value);
				}
				return klass;
			}
			else if (xTd->getTypeClass() == TypeClass_ENUM)
			{
				// as module and module constants
				if (rb_const_defined(module, id))
				{
					klass = rb_const_get(module, id);
				}
				else
				{
					klass = rb_define_module_under(module, className);
				}
				Reference< XEnumTypeDescription > xEnumDesc(a, UNO_QUERY);
				Sequence< OUString > names(xEnumDesc->getEnumNames());

				VALUE enum_class = get_enum_class();
				VALUE obj;
				VALUE type_name = ustring2RString(typeName);
				OUString valueName;

				TypeDescription desc(typeName);
				if (!desc.is())
				{
					rb_raise(rb_eArgError, "unknown type name (%s)", RSTRING_PTR(type_name));
				}
				if (desc.get()->eTypeClass != typelib_TypeClass_ENUM)
				{
					rb_raise(rb_eArgError, "wrong type name (%s)", RSTRING_PTR(type_name));
				}
				desc.makeComplete();

				typelib_EnumTypeDescription *pEnumDesc = (typelib_EnumTypeDescription*) desc.get();
				int i = 0;
				for (i = 0; i < pEnumDesc->nEnumValues; i++)
				{
					Any a = Any(&pEnumDesc->pEnumValues[i], desc.get()->pWeakRef);
					RubyunoValue *ptr;
					ptr = new RubyunoValue();
					obj = rb_obj_alloc(enum_class);
					DATA_PTR(obj) = ptr;
					ptr->value = a;

					valueName = (*((OUString*)&pEnumDesc->ppEnumNames[i]));
					rb_iv_set(obj, "@type_name", type_name);
					rb_iv_set(obj, "@value", ustring2RString(valueName));

					rb_iv_set(klass, OUStringToOString(
						valueName, RTL_TEXTENCODING_ASCII_US ).getStr(), obj);
				}
				return klass;
			}
			else if (xTd->getTypeClass() == TypeClass_INTERFACE)
			{
				VALUE klass;
				klass = find_interface(xTd);
				if (NIL_P(klass))
					rb_raise(rb_eArgError, "unknown type name (%s)", RSTRING_PTR(name));
				return klass;
			}
			else
			{
				rb_raise(rb_eRuntimeError, "unsupported type (%s)", RSTRING_PTR(name));
			}
		}
		// direct values like enums and constants
		OUString tmpTypeName, valueName;
		sal_Int32 ends;
		ends = typeName.lastIndexOfAsciiL(".", 1);
		tmpTypeName = typeName.copy(0, ends);
		valueName = typeName.copy(ends + 1);

		Any desc = runtime.getImpl()->xTypeDescription->getByHierarchicalName(tmpTypeName);
		if (desc.getValueTypeClass() == TypeClass_INTERFACE)
		{
			// constants group is not processed here
			Reference< XTypeDescription > xTdm(desc, UNO_QUERY);
			if (xTdm.is() && xTdm->getTypeClass() == TypeClass_ENUM)
			{
				return rb_funcall(get_enum_class(), rb_intern("new"), 2, ustring2RString(tmpTypeName), ustring2RString(valueName));
			}
		}
		ret = runtime.any_to_VALUE(a);
	}
	catch (com::sun::star::container::NoSuchElementException &e)
	{
		rb_raise(rb_eArgError, "unknown value (%s)", RSTRING_PTR(name));
	}
	catch (com::sun::star::script::CannotConvertException &e)
	{
		rb_raise(rb_eRuntimeError, "failed to convert type for (%s)", RSTRING_PTR(name));
	}
	catch (com::sun::star::uno::RuntimeException &e)
	{
		rb_raise(rb_eArgError, "invalid name for value (%s)", RSTRING_PTR(name));
	}
	return ret;
}

/*
 * Import somethings with a call.
 * if argc == 1, value is returned and if multiple arguments are passed,
 * returnes as Array.
 */
static VALUE
rubyuno_uno_multiple_require(int argc, VALUE *argv, VALUE self)
{
	if (argc == 0)
		return Qnil;
	VALUE args;
	rb_scan_args(argc, argv, "*", &args);

	if (argc == 1)
		return rubyuno_uno_require(self, rb_ary_entry(args, 0));

	VALUE ret = rb_ary_new2(argc);
	for (int i = 0; i < argc; i++)
	{
		rb_ary_push(ret, rubyuno_uno_require(self, rb_ary_entry(args, i)));
	}
	return ret;
}


static void
rubyuno_proxy_free(RubyunoInternal *rubyuno)
{
	if (rubyuno)
	{
		rubyuno->wrapped.clear();
		//rubyuno->invocation.clear();
		delete rubyuno;
	}
}


static VALUE
rubyuno_proxy_alloc(VALUE klass)
{
	return Data_Wrap_Struct(klass, 0, rubyuno_proxy_free, 0);
}


VALUE
proxy_attr(const VALUE *self, RubyunoInternal *rubyuno, VALUE *name, OUString propertyName, VALUE &args, int mode)
{
	VALUE ret = Qnil;
	Runtime r;
	Any a;
	try
	{
		if (!mode)
		{
			a = rubyuno->invocation->getValue(propertyName);
			ret = r.any_to_VALUE(a);
		}
		else
		{
			a = r.value_to_any(rb_ary_entry(args, 0));
			rubyuno->invocation->setValue(propertyName, a);
			ret = Qnil;
		}
	}
	catch (com::sun::star::beans::UnknownPropertyException &e)
	{
		raise_rb_exception(makeAny(e));
	}
	catch (com::sun::star::script::CannotConvertException &e)
	{
		raise_rb_exception(makeAny(e));
	}
	catch (com::sun::star::reflection::InvocationTargetException &e)
	{
		com::sun::star::uno::Exception ex;
		e.TargetException >>= ex;
		raise_rb_exception(e.TargetException);
	}
	catch (RuntimeException &e)
	{
		raise_rb_exception(makeAny(e));
	}
	return ret;
}


VALUE
proxy_call_method(VALUE *self, RubyunoInternal *rubyuno, VALUE *name, OUString methodName, VALUE &args)
{
	VALUE ret;
	Sequence< short > aOutParamIndex;
	Sequence< Any > aParams;
	Sequence< Any > aOutParams;
	Sequence< Type > aParamTypes;
	Any anyParams;
	Any outParams;
	Any retValue;
	try
	{
		Runtime runtime;
		anyParams = runtime.value_to_any(args);
		if (anyParams.getValueTypeClass() == com::sun::star::uno::TypeClass_SEQUENCE)
		{
			anyParams >>= aParams;
		}
		else
		{
			aParams.realloc(1);
			aParams[0] <<= anyParams;
		}
		retValue = rubyuno->invocation->invoke(methodName, aParams, aOutParamIndex, aOutParams);

		VALUE tmp = runtime.any_to_VALUE(retValue);
		if (aOutParams.getLength())
		{
			VALUE ret_array = rb_ary_new2(aOutParams.getLength() +1);
			rb_ary_push(ret_array, tmp);
			for (int i = 0; i < aOutParams.getLength(); i++)
			{
				rb_ary_push(ret_array, runtime.any_to_VALUE(aOutParams[i]));
			}
			ret = ret_array;
		} else {
			ret = tmp;
		}
		return ret;
	}
	catch (com::sun::star::reflection::InvocationTargetException &e)
	{
		com::sun::star::uno::Exception ex;
		e.TargetException >>= ex;
		raise_rb_exception(e.TargetException);
	}
	catch (com::sun::star::lang::IllegalArgumentException &e)
	{
		raise_rb_exception(makeAny(e));
	}
	catch (com::sun::star::script::CannotConvertException &e)
	{
		raise_rb_exception(makeAny(e));
	}
	catch (RuntimeException &e)
	{
		raise_rb_exception(makeAny(e));
	}
	return Qnil;
}


static VALUE
proxy_method_call(VALUE name_symbol, VALUE *args, VALUE self)
{
	VALUE name;
	OUString methodName;
	Check_Type(name_symbol, T_SYMBOL);
#ifdef RUBY_RUBY_H
	name = rb_id2str(rb_to_id(name_symbol));
#else
	name = rb_str_new2(rb_id2name(rb_to_id(name_symbol)));
#endif
	methodName = asciiVALUE2OUString(name);

	RubyunoInternal *rubyuno;
	Data_Get_Struct(self, RubyunoInternal, rubyuno);

	if (methodName.endsWithAsciiL("=", 1))
	{
		OUString propertyName(methodName.copy(0, methodName.getLength() - 1));
		if (rubyuno->invocation->hasProperty(propertyName))
			return proxy_attr(&self, rubyuno, &name, propertyName, *args, 1);
	}
	else
	{
		if (rubyuno->invocation->hasMethod(methodName))
		{
			return proxy_call_method(&self, rubyuno, &name, methodName, *args);
		}
		else if (rubyuno->invocation->hasProperty(methodName))
		{
				return proxy_attr(&self, rubyuno, &name, methodName, *args, 0);
		}
	}
	rb_raise(rb_eNameError, "unknown method `%s'", RSTRING_PTR(name));
	return Qnil;
}


static VALUE
rubyuno_proxy_method_missing(int argc, VALUE *argv, VALUE self)
{
	if (argc >= 1)
	{
		rb_secure(2);

		VALUE name_symbol, args;
		// get method symbol
		rb_scan_args(argc, argv, "1*", &name_symbol, &args);

		return proxy_method_call(name_symbol, &args, self);
	}
	rb_raise(rb_eRuntimeError, "unknown error");
	return Qnil;
}

#ifdef HAVE_RUBY_RUBY_H
static VALUE
to_symbol(VALUE str, ID intern, int argc, VALUE *argv)
{
	return rb_funcall(str, intern, 0);
}
#endif

static VALUE
rubyuno_uno_methods(VALUE self)
{
	RubyunoInternal *rubyuno;
	Data_Get_Struct(self, RubyunoInternal, rubyuno);
	if (!rubyuno)
		rb_raise(rb_eRuntimeError, "illegal instance (%s)", rb_obj_classname(self));

	VALUE names, assignment;
	assignment = rb_str_new("=", 1);
	Reference< XInvocation2 > xInv = rubyuno->invocation;
	Sequence< InvocationInfo > infos = xInv->getInfo();

	names = rb_ary_new2((int)infos.getLength());

	for (int i = 0; i < (int)infos.getLength(); i++)
	{
		if (infos[i].eMemberType == MemberType_METHOD)
		{
			rb_ary_push(names, asciiOUString2VALUE(infos[i].aName));
		}
		else if (infos[i].eMemberType == MemberType_PROPERTY)
		{
			if (infos[i].PropertyAttribute & PropertyAttribute::READONLY)
			{
				rb_ary_push(names, asciiOUString2VALUE(infos[i].aName));
			}
			else
			{
				rb_ary_push(names, asciiOUString2VALUE(infos[i].aName));
				rb_ary_push(names, rb_str_cat(asciiOUString2VALUE(infos[i].aName), "=", 1));
			}
		}
		else
		{
			rb_ary_push(names, asciiOUString2VALUE(infos[i].aName));
		}
	}
#ifdef HAVE_RUBY_RUBY_H
	ID intern = rb_intern("intern");
	rb_block_call(names, rb_intern("map!"), 0, 0, (VALUE(*)(...))to_symbol, intern);
	return names;
#else
	return names;
#endif
}


static VALUE
rubyuno_proxy_equal(VALUE self, VALUE object)
{
	if (CLASS_OF(object) != get_proxy_class())
		return Qfalse;

	RubyunoInternal *self_rubyuno, *object_rubyuno;
	Data_Get_Struct(self, RubyunoInternal, self_rubyuno);
	Data_Get_Struct(object, RubyunoInternal, object_rubyuno);

	if (self_rubyuno && object_rubyuno)
	{
		if (self_rubyuno->wrapped == object_rubyuno->wrapped)
			return Qtrue;
	}
	return Qfalse;
}

static VALUE
rubyuno_proxy_inspect(VALUE self)
{
	RubyunoInternal *rubyuno;
	Data_Get_Struct(self, RubyunoInternal, rubyuno);
	if (rubyuno)
	{
		OUStringBuffer buf;
		buf.appendAscii(RTL_CONSTASCII_STRINGPARAM("#<Rubyuno::RubyunoProxy:"));
		buf.append(rubyuno->wrapped.getValueType().getTypeName());
		buf.appendAscii(RTL_CONSTASCII_STRINGPARAM(":"));
		VALUE str;
#ifdef RUBY_RUBY_H
		str = rb_sprintf("%p", (void*)self);
#else
		size_t len;
		len = 16;
		str = rb_str_new(0, len);
		snprintf(RSTRING_PTR(str), len, "0x%lx", self);
#endif
		buf.append(rbString2OUString(str));
		buf.appendAscii(RTL_CONSTASCII_STRINGPARAM(">"));
		return ustring2RString(buf.makeStringAndClear());
	}
	rb_raise(rb_eRuntimeError, "uninitializezd RubyunoProxy.");
	return Qnil;
}

/*
 * Check the UNO object implements specific interface.
 */
static VALUE
rubyuno_proxy_has_interface(VALUE self, VALUE name)
{
	StringValue(name);

	RubyunoInternal *rubyuno;
	Data_Get_Struct(self, RubyunoInternal, rubyuno);
	if (rubyuno)
	{
		Runtime runtime;
		OUString typeName;

		typeName = rbString2OUString(name);
		Reference < XInterface > xInterface = *(Reference < XInterface >*) rubyuno->wrapped.getValue();
		Reference< XIdlClass > xIdlClass = runtime.getImpl()->xCoreReflection->forName(typeName);

		if (xIdlClass.is())
		{
			OUString className = xIdlClass->getName();
			Type classType(xIdlClass->getTypeClass(), className.getStr());

			if (xInterface->queryInterface(classType).hasValue())
				return Qtrue;
		}
	}
	return Qfalse;
}

/*
 * Call method of the object with arguments.
 *
 */
static VALUE
rubyuno_invoke(VALUE self, VALUE object, VALUE name, VALUE args)
{
	if (! (rb_obj_is_kind_of(object, get_proxy_class()) ||
			rb_obj_is_kind_of(object, get_struct_class()) ||
			rb_obj_is_kind_of(object, get_exception_class())) )
		rb_raise(rb_eArgError, "invalid object for source to invoke the function");

	Check_Type(name, T_STRING);
	Check_Type(args, T_ARRAY);
#ifdef HAVE_RUBY_RUBY_H
	return proxy_method_call(ID2SYM(rb_intern_str(name)), &args, object);
#else
	return proxy_method_call(ID2SYM(rb_intern(RSTRING_PTR(name))), &args, object);
#endif
}


static VALUE
rubyuno_struct_alloc(VALUE klass)
{
	RubyunoInternal *rubyuno;
	rubyuno = new RubyunoInternal();
	return Data_Wrap_Struct(klass, 0, rubyuno_proxy_free, rubyuno);
}


/*
 * Fill struct fields according to initial value.
 */
sal_Int32 fill_struct(const typelib_CompoundTypeDescription *pCompType, VALUE *args, Reference< XInvocation2 > xInvocation, Runtime &runtime)
{
	sal_Int32 nIndex = 0;
	if (pCompType->pBaseTypeDescription)
		nIndex = fill_struct(pCompType->pBaseTypeDescription, args, xInvocation, runtime);

	sal_Int32 nLength = RARRAY_LEN(*args);

	int i;
	for (i = 0; i < pCompType->nMembers; i++)
	{
		if (i + nIndex >= nLength)
		{
			rb_raise(rb_eArgError, "too few arguments (%d for %d)", (int)nLength, (int)(nIndex + pCompType->nMembers));
		}
		VALUE item = rb_ary_entry(*args, i);
		Any a = runtime.value_to_any(item);
		xInvocation->setValue(pCompType->ppMemberNames[i], a);
	}

	return i + nIndex;
}

/*
 * for structs inherits RubyunoStruct.
 */
static VALUE
rubyuno_struct_initialize(int argc, VALUE *argv, VALUE self)
{
	VALUE klass, type_name;
	ID id = rb_intern("TYPENAME");
	klass = CLASS_OF(self);
	if (!rb_const_defined(klass, id))
		rb_raise(rb_eRuntimeError, "wrongly initialized class `%s', type_name is not specified", rb_obj_classname(klass));
	type_name = rb_const_get(klass, id);

	Runtime runtime;
	Any aStruct;
	OUString typeName = rbString2OUString(type_name);
	Reference< XIdlClass > idlClass(runtime.getImpl()->xCoreReflection->forName(typeName), UNO_QUERY);
	if (!idlClass.is())
		rb_raise(rb_eRuntimeError, "unknown type name (%s)", RSTRING_PTR(type_name));
	idlClass->createObject(aStruct);

	set_rubyuno_struct(aStruct, runtime.getImpl()->xInvocation, self);

	// set initial values
	if (argc > 1)
	{
		VALUE args;
		rb_scan_args(argc, argv, "*", &args);

		TypeDescription desc(typeName);
		if (desc.is())
		{
			typelib_CompoundTypeDescription *pCompType = (typelib_CompoundTypeDescription *) desc.get();

			RubyunoInternal *rubyuno;
			Data_Get_Struct(self, RubyunoInternal, rubyuno);
			fill_struct(pCompType, &args, rubyuno->invocation, runtime);
		}
	}
	return self;
}

static VALUE
rubyuno_struct_equal(VALUE self, VALUE object)
{
	if (! (rb_obj_is_kind_of(object, get_struct_class()) ||
			rb_obj_is_kind_of(object, get_enum_class()) ) )
		return Qfalse;

	RubyunoInternal *rubyuno_self, *rubyuno_object;
	Data_Get_Struct(self, RubyunoInternal, rubyuno_self);
	Data_Get_Struct(object, RubyunoInternal, rubyuno_object);

	Reference< XMaterialHolder > selfMaterial(rubyuno_self->invocation, UNO_QUERY);
	Reference< XMaterialHolder > objectMaterial(rubyuno_object->invocation, UNO_QUERY);

	if (selfMaterial->getMaterial() == objectMaterial->getMaterial())
		return Qtrue;
	return Qfalse;
}
/*
static VALUE
rubyuno_exception_alloc(VALUE klass)
{
	return Data_Wrap_Struct(klass, 0, rubyuno_proxy_free, 0);
}
*/
static VALUE
rubyuno_exception_initialize(VALUE self, VALUE message)
{
/*
	VALUE klass, type_name;
	ID id = rb_intern("TYPENAME");
	klass = CLASS_OF(self);
	if (!rb_const_defined(klass, id))
		rb_raise(rb_eRuntimeError, "wrongly initialized class `%s', type_name is not specified", rb_obj_classname(klass));

	type_name = rb_const_get(klass, id);
	Runtime runtime;
	Any aStruct;
	OUString typeName = rbString2OUString(type_name);
	Reference< XIdlClass > idlClass(runtime.getImpl()->xCoreReflection->forName(typeName), UNO_QUERY);

	if (!idlClass.is())
		rb_raise(rb_eRuntimeError, "unknown type name (%s)", RSTRING_PTR(type_name));
	idlClass->createObject(aStruct);
	set_rubyuno_struct(aStruct, runtime.getImpl()->xInvocation, self);

	rb_iv_set(self, "mesg", message);
	rb_iv_set(self, "bt", Qnil);
*/
	rb_call_super(1, &message);
	return self;
}

/*
 * Rubyuno::Enum class
 */
static VALUE
rubyuno_enum_initialize(VALUE self, VALUE type_name, VALUE value)
{
	// check args
	Check_Type(type_name, T_STRING);
	Check_Type(value, T_STRING);

	OUString typeName = rbString2OUString(type_name);
	char *strValue = RSTRING_PTR(value);

	TypeDescription desc(typeName);
	if (desc.is())
	{
		if (desc.get()->eTypeClass != typelib_TypeClass_ENUM)
			rb_raise(rb_eArgError, "wrong type name (%s)", RSTRING_PTR(type_name));

		desc.makeComplete();

		typelib_EnumTypeDescription *pEnumDesc = (typelib_EnumTypeDescription*) desc.get();
		int i = 0;
		for (i = 0; i < pEnumDesc->nEnumValues; i++)
		{
			if ((*((OUString*)&pEnumDesc->ppEnumNames[i])).compareToAscii(strValue) == 0)
			{
				break;
			}
		}
		if (i == pEnumDesc->nEnumValues)
			rb_raise(rb_eArgError, "wrong value (%s)", strValue);

		Any a = Any(&pEnumDesc->pEnumValues[i], desc.get()->pWeakRef);
		RubyunoValue *ptr;
		Data_Get_Struct(self, RubyunoValue, ptr);
		ptr->value = a;
	}
	else
	{
		rb_raise(rb_eArgError, "unknown type name (%s)", RSTRING_PTR(type_name));
	}

	rb_iv_set(self, "@type_name", type_name);
	rb_iv_set(self, "@value", value);
	return self;
}

static VALUE
rubyuno_enum_equal(VALUE self, VALUE object)
{
	if (! object == CLASS_OF(get_enum_class()))
		return Qfalse;
	ID id = rb_intern("==");
	VALUE self_type_name, self_value;

	self_type_name = rb_iv_get(self, "@type_name");
	self_value = rb_iv_get(self, "@value");
	if (rb_funcall(self_type_name, id, 1, rb_iv_get(object, "@type_name")) &&
		rb_funcall(self_value, id, 1, rb_iv_get(object, "@value")))
		return Qtrue;
	return Qfalse;
}

static VALUE
rubyuno_enum_inspect(VALUE self)
{
	VALUE desc, type_name, value;
	type_name = rb_iv_get(self, "@type_name");
	value = rb_iv_get(self, "@value");
	Check_Type(type_name, T_STRING);
	Check_Type(value, T_STRING);

	desc = rb_tainted_str_new("#<Rubyuno::Enum:", 13);
	rb_str_cat(desc, RSTRING_PTR(type_name), RSTRING_LEN(type_name));
	rb_str_cat(desc, ".", 1);
	rb_str_cat(desc, RSTRING_PTR(value), RSTRING_LEN(value));
	rb_str_cat(desc, ">", 1);
	return desc;
}


/*
 * Rubyuno::Type class
 */
static VALUE
rubyuno_type_initialize(int argc, VALUE *argv, VALUE self)
{
	VALUE type_name, type_class;
	TypeDescription desc;

	if (argc == 1)
	{
		VALUE value;
		rb_scan_args(argc, argv, "1", &value);

		if (TYPE(value) == T_STRING)
		{
			type_name = value;

			OUString typeName = rbString2OUString(type_name);
			desc = TypeDescription(typeName);
			if (!desc.is())
				rb_raise(rb_eArgError, "wrong type name (%s)", RSTRING_PTR(type_name));

			Runtime runtime;
			type_class = runtime.any_to_VALUE(Any((com::sun::star::uno::TypeClass)desc.get()->eTypeClass));
		}
		else if (CLASS_OF(value) == get_enum_class())
		{
			type_class = value;
			VALUE enum_type_name = rb_iv_get(type_class, "@type_name");

			if (strcmp(RSTRING_PTR(enum_type_name), "com.sun.star.uno.TypeClass"))
				rb_raise(rb_eArgError, "invalid enum (%s)", RSTRING_PTR(value));

			type_name = rb_iv_get(type_class, "@value");
			OUString typeName = rbString2OUString(type_name);
			desc = TypeDescription(typeName.toAsciiLowerCase());
			if (!desc.is())
				rb_raise(rb_eRuntimeError, "wrong enum value (%s)", RSTRING_PTR(type_name));
		}
		else
			rb_raise(rb_eArgError, "wrong type (%s for String or Rubyuno::Enum)", rb_obj_classname(value));
	}
	else if (argc == 2)
	{
		rb_scan_args(argc, argv, "11", &type_name, &type_class);

		Check_Type(type_name, T_STRING);
		if (! CLASS_OF(type_class) == get_enum_class())
			rb_raise(rb_eArgError, "Rubyuno::Enum is desired for 2nd argument.");

		RubyunoValue *value;
		Data_Get_Struct(type_class, RubyunoValue, value);
		Any enumValue = value->value;
		OUString typeName = rbString2OUString(type_name);
		desc = TypeDescription(typeName);
		if (!desc.is())
			rb_raise(rb_eArgError, "wrong type name (%s)", RSTRING_PTR(type_name));
		if (desc.get()->eTypeClass != (typelib_TypeClass)* (sal_Int32*)enumValue.getValue())
			rb_raise(rb_eArgError, "nomatch with type name (%s)", RSTRING_PTR(type_name));
	}
	else
		rb_raise(rb_eArgError, "too much arguments (%d for maximum 2)", argc);


	Type t = desc.get()->pWeakRef;
	Any a;
	a <<= t;
	RubyunoValue *rubyuno;
	Data_Get_Struct(self, RubyunoValue, rubyuno);
	rubyuno->value = a;

	rb_iv_set(self, "@type_name", type_name);
	rb_iv_set(self, "@type_class", type_class);
	return self;
}

static VALUE
rubyuno_type_equal(VALUE self, VALUE object)
{
	if (! object == CLASS_OF(get_type_class()))
		return Qfalse;

	ID id = rb_intern("==");
	VALUE type_name, type_class;
	type_name = rb_iv_get(self, "@type_name");
	type_class = rb_iv_get(self, "@type_class");
	if (rb_funcall(self, id, 1, rb_iv_get(object, "@type_name")) &&
		rb_funcall(self, id, 1, rb_iv_get(object, "@type_class")))
		return Qtrue;

	return Qfalse;
}

static VALUE
rubyuno_type_inspect(VALUE self)
{
	VALUE desc, type_name, type_class, value;
	type_name = rb_iv_get(self, "@type_name");
	type_class = rb_iv_get(self, "@type_class");
	Check_Type(type_name, T_STRING);
	if (!CLASS_OF(type_class) == get_enum_class())
	{
		rb_raise(rb_eRuntimeError, "unknown value for type_class (%s for Rubyuno::RubyunoEnum)", rb_obj_classname(type_class));
	}
	value = rb_iv_get(type_class, "@value");

	desc = rb_tainted_str_new("#<Rubyuno::Type:", 13);
	rb_str_cat(desc, RSTRING_PTR(type_name), RSTRING_LEN(type_name));
	rb_str_cat(desc, "(", 1);
	rb_str_cat(desc, RSTRING_PTR(value), RSTRING_LEN(value));
	rb_str_cat(desc, ")>", 2);
	return desc;
}

/*
 * Rubyuno::Char class
 */
static VALUE
rubyuno_char_initialize(VALUE self, VALUE char_value)
{
	Check_Type(char_value, T_STRING);
	if (RSTRING_LEN(char_value) != 1)
	{
		rb_raise(rb_eArgError, "wrong length (%ld for 1)", RSTRING_LEN(char_value));
	}
	sal_Unicode c = rbString2OUString(char_value).toChar();
	Any a;
	a.setValue(&c, getCharCppuType());
	RubyunoValue *rubyuno;
	Data_Get_Struct(self, RubyunoValue, rubyuno);
	rubyuno->value = a;

	rb_iv_set(self, "@value", char_value);
	return self;
}

static VALUE
rubyuno_char_equal(VALUE self, VALUE object)
{
	if (!(CLASS_OF(object) == get_char_class() ||
		TYPE(object) == T_STRING))
		return Qfalse;

	ID id = rb_intern("==");
	if (rb_funcall(object, id, 1, rb_iv_get(self, "@value")))
		return Qtrue;

	return Qfalse;
}

static VALUE
rubyuno_char_inspect(VALUE self)
{
	VALUE desc, value;
	value = rb_iv_get(self, "@value");
	Check_Type(value, T_STRING);

	desc = rb_tainted_str_new("#<Rubyuno::Char:", 13);
	rb_str_cat(desc, RSTRING_PTR(value), RSTRING_LEN(value));
	rb_str_cat(desc, ">", 1);
	return desc;
}


static VALUE
rubyuno_any_alloc(VALUE klass)
{
	return Data_Wrap_Struct(klass, 0, 0, 0);
}

/*
 * Rubyuno::Any class
 * Any class is not well processed at the constructing time.
 */
static VALUE
rubyuno_any_initialize(VALUE self, VALUE type, VALUE value)
{
	if (rb_obj_is_kind_of(type, get_type_class()))
	{
		rb_iv_set(self, "@type", type);
	}
	else if (TYPE(type) == T_STRING)
	{
		VALUE type_value;
		type_value = rb_funcall(get_type_class(), rb_intern("new"), 1, type);
		rb_iv_set(self, "@type", type_value);
	}
	else
	{
		rb_raise(rb_eArgError, "invalid type for type.");
	}
	rb_iv_set(self, "@value", value);

	return self;
}

static VALUE
rubyuno_any_equal(VALUE self, VALUE object)
{
	if (! CLASS_OF(self) == get_any_class())
		return Qfalse;

	ID id = rb_intern("==");
	VALUE type, value;
	type = rb_iv_get(self, "@type");
	value = rb_iv_get(self, "@value");

	if (rb_funcall(type, id, 1, rb_iv_get(object, "@type")) &&
		rb_funcall(value, id, 1, rb_iv_get(object, "@value")))
		return Qtrue;

	return Qfalse;
}


static void
rubyuno_value_free(RubyunoValue *rubyuno)
{
	if (rubyuno)
	{
		rubyuno->value.clear();
		delete rubyuno;
	}
}

static VALUE
rubyuno_value_alloc(VALUE klass)
{
	RubyunoValue *ptr;
	ptr = new RubyunoValue();
	return Data_Wrap_Struct(klass, 0, rubyuno_value_free, ptr);
}

}

extern "C"
{
void
Init_rubyuno(void)
{
	VALUE Rubyuno;
	Rubyuno = rb_define_module("Rubyuno");
	rb_define_module_function(Rubyuno, "get_component_context", (VALUE(*)(...))rubyuno_getComponentContext, 0);
	rb_define_module_function(Rubyuno, "system_path_to_file_url", (VALUE(*)(...))rubyuno_getFileURLFromSystemPath, 1);
	rb_define_module_function(Rubyuno, "file_url_to_system_path", (VALUE(*)(...))rubyuno_getSystemPathFromFileURL, 1);
	rb_define_module_function(Rubyuno, "absolutize", (VALUE(*)(...))rubyuno_absolutize, 2);
	rb_define_module_function(Rubyuno, "uuid", (VALUE(*)(...))rubyuno_create_uuid, 0);
	rb_define_module_function(Rubyuno, "uno_require", (VALUE(*)(...))rubyuno_uno_multiple_require, -1);
	rb_define_module_function(Rubyuno, "invoke", (VALUE(*)(...))rubyuno_invoke, 3);

	VALUE RubyunoProxy;
	RubyunoProxy = rb_define_class_under(Rubyuno, "RubyunoProxy", rb_cData);
	rb_define_alloc_func(RubyunoProxy, rubyuno_proxy_alloc);
	rb_define_method(RubyunoProxy, "method_missing", (VALUE(*)(...))rubyuno_proxy_method_missing, -1);
	rb_define_method(RubyunoProxy, "inspect", (VALUE(*)(...))rubyuno_proxy_inspect, 0);
	rb_define_method(RubyunoProxy, "==", (VALUE(*)(...))rubyuno_proxy_equal, 1);
	rb_define_method(RubyunoProxy, "uno_methods", (VALUE(*)(...))rubyuno_uno_methods, 0);
	rb_define_method(RubyunoProxy, "has_interface?", (VALUE(*)(...))rubyuno_proxy_has_interface, 1);

	VALUE RubyunoEnum;
	RubyunoEnum = rb_define_class_under(Rubyuno, "Enum", rb_cObject);
	rb_define_alloc_func(RubyunoEnum, rubyuno_value_alloc);
	rb_define_method(RubyunoEnum, "initialize", (VALUE(*)(...))rubyuno_enum_initialize, 2);
	rb_define_method(RubyunoEnum, "==", (VALUE(*)(...))rubyuno_enum_equal, 1);
	rb_define_attr(RubyunoEnum, "type_name", 1, 0);
	rb_define_attr(RubyunoEnum, "value", 1, 0);
	rb_define_method(RubyunoEnum, "inspect", (VALUE(*)(...))rubyuno_enum_inspect, 0);

	VALUE RubyunoType;
	RubyunoType = rb_define_class_under(Rubyuno, "Type", rb_cObject);
	rb_define_alloc_func(RubyunoType, rubyuno_value_alloc);
	rb_define_method(RubyunoType, "initialize", (VALUE(*)(...))rubyuno_type_initialize, -1);
	rb_define_method(RubyunoType, "==", (VALUE(*)(...))rubyuno_type_equal, 1);
	rb_define_attr(RubyunoType, "type_name", 1, 0);
	rb_define_attr(RubyunoType, "type_class", 1, 0);
	rb_define_method(RubyunoType, "inspect", (VALUE(*)(...))rubyuno_type_inspect, 0);

	VALUE RubyunoChar;
	RubyunoChar = rb_define_class_under(Rubyuno, "Char", rb_cObject);
	rb_define_alloc_func(RubyunoChar, rubyuno_value_alloc);
	rb_define_method(RubyunoChar, "initialize", (VALUE(*)(...))rubyuno_char_initialize, 1);
	rb_define_method(RubyunoChar, "==", (VALUE(*)(...))rubyuno_char_equal, 1);
	rb_define_attr(RubyunoChar, "value", 1, 0);
	rb_define_method(RubyunoChar, "inspect", (VALUE(*)(...))rubyuno_char_inspect, 0);

	VALUE RubyunoAny;
	RubyunoAny = rb_define_class_under(Rubyuno, "Any", rb_cObject);
	rb_define_alloc_func(RubyunoAny, rubyuno_any_alloc);
	rb_define_method(RubyunoAny, "initialize", (VALUE(*)(...))rubyuno_any_initialize, 2);
	rb_define_method(RubyunoAny, "==", (VALUE(*)(...))rubyuno_any_equal, 1);
	rb_define_attr(RubyunoAny, "type", 1, 1);
	rb_define_attr(RubyunoAny, "value", 1, 1);

	VALUE RubyunoByteSequence;
	RubyunoByteSequence = rb_define_class_under(Rubyuno, "ByteSequence", rb_cString);

	VALUE RubyunoStruct;
	RubyunoStruct = rb_define_class_under(Rubyuno, "RubyunoStruct", rb_cObject);
	rb_define_alloc_func(RubyunoStruct, rubyuno_struct_alloc);
	rb_define_method(RubyunoStruct, "initialize", (VALUE(*)(...))rubyuno_struct_initialize, -1);
	rb_define_method(RubyunoStruct, "method_missing", (VALUE(*)(...))rubyuno_proxy_method_missing, -1);
	rb_define_method(RubyunoStruct, "==", (VALUE(*)(...))rubyuno_struct_equal, 1);
	rb_define_method(RubyunoStruct, "uno_methods", (VALUE(*)(...))rubyuno_uno_methods, 0);

	VALUE Com, Sun, Star, Uno;
	Com = rb_define_module_under(Rubyuno, "Com");
	Sun = rb_define_module_under(Com, "Sun");
	Star = rb_define_module_under(Sun, "Star");

	Uno = rb_define_module_under(Star, "Uno");

	VALUE RubyunoException;
	RubyunoException = rb_define_class_under(Uno, "Exception", rb_eStandardError);
	//rb_define_alloc_func(RubyunoException, rubyuno_struct_alloc);
	rb_define_method(RubyunoException, "initialize", (VALUE(*)(...))rubyuno_exception_initialize, 1);
	rb_define_method(RubyunoException, "method_missing", (VALUE(*)(...))rubyuno_proxy_method_missing, -1);
	rb_define_method(RubyunoException, "==", (VALUE(*)(...))rubyuno_struct_equal, 1);
	//rb_define_method(RubyunoException, "uno_methods", (VALUE(*)(...))rubyuno_uno_methods, 0);
	//rb_define_attr(RubyunoException, "uno", 1, 1);

/*
	VALUE RubyunoException2;
	RubyunoException2 = rb_define_class_under(Rubyuno, "Exception", rb_eStandardError);
	rb_define_alloc_func(RubyunoException2, rubyuno_struct_alloc);
	rb_define_method(RubyunoException2, "initialize", (VALUE(*)(...))rubyuno_exception2_initialize, 1);
*/

	VALUE XInterface;
	XInterface = rb_define_module_under(Uno, "XInterface");
	rb_define_const(XInterface, "TYPENAME", rb_str_new("com.sun.star.uno.XInterface", 27));

	init_external_encoding();
}
}

