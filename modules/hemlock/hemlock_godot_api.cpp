/**************************************************************************/
/*  hemlock_godot_api.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

// The engine as Hemlock scripts see it: natives (`__godot_*`, called through
// the prelude) and the prelude itself, which defines
//
//   owner                     the object an instance belongs to (bound per instance)
//   <object>.get(name) / .set(name, value) / .call(method, ...args)
//   <object>.get_node(path) / .emit(signal, ...args) / .connect(signal, target, method)
//   <object>.is_valid() / .class_name() / .is_class(name) / .free()
//   godot.print(...)          print to Godot's output (stdout and the editor's Output panel)
//   godot.singleton(name)     an engine singleton (Input, Engine, ...)
//   godot.instantiate(class)  a new object of a native class
//   godot.load(path)          a resource
//   godot.keep(obj) / godot.release(obj)
//   godot.vec2(x, y), vec2i, vec3, vec3i, vec4, color(r, g, b, a?), rect2,
//   godot.callable(obj, method), godot.node_path(path)
//
// Objects cross as handles (ObjectIDs): a script never holds a pointer, and
// a freed object reads as invalid rather than crashing. RefCounted objects
// are borrowed for the duration of a call unless they were kept: values from
// godot.instantiate() / godot.load() are kept for you, anything else needs
// godot.keep(obj) to outlive the call, and every keep wants a release. No
// hidden refcounts: you hold what you asked to hold.

#include "hemlock_script.h"
#include "hemlock_variant.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "core/string/print_string.h"
#include "core/templates/hash_map.h"

#include <cstring>

// RefCounted objects Hemlock code holds (godot.keep / instantiate / load)
static HashMap<ObjectID, Ref<RefCounted>> kept;

#define HML_NATIVE(m_name) static int m_name(void *p_userdata, const HmlEmbedValue *p_args, int p_argc, HmlEmbedValue *r_result, char *r_error, size_t p_error_size)

static int fail(char *r_error, size_t p_error_size, const String &p_message) {
	CharString utf8 = p_message.utf8();
	snprintf(r_error, p_error_size, "%s", utf8.get_data());
	return 1;
}

static String as_string(const HmlEmbedValue &p_value) {
	if (p_value.kind == HML_EMBED_STRING) {
		return String::utf8(p_value.as.s.data, (int)p_value.as.s.len);
	}
	return HemlockVariant::to_variant(p_value).operator String();
}

// The object a handle argument names, or an error
static Object *object_arg(const HmlEmbedValue *p_args, int p_argc, int p_index, char *r_error, size_t p_error_size) {
	if (p_index >= p_argc || p_args[p_index].kind != HML_EMBED_HANDLE) {
		fail(r_error, p_error_size, "expected a Godot object");
		return nullptr;
	}
	Object *obj = ObjectDB::get_instance(ObjectID(p_args[p_index].as.handle));
	if (!obj) {
		fail(r_error, p_error_size, "the Godot object was freed");
	}
	return obj;
}

static void keep_object(Object *p_obj) {
	RefCounted *rc = Object::cast_to<RefCounted>(p_obj);
	if (rc) {
		kept[p_obj->get_instance_id()] = Ref<RefCounted>(rc);
	}
}

HML_NATIVE(native_get) {
	Object *obj = object_arg(p_args, p_argc, 0, r_error, p_error_size);
	if (!obj) {
		return 1;
	}
	if (p_argc != 2) {
		return fail(r_error, p_error_size, "get(name) takes one argument");
	}
	String name = as_string(p_args[1]);
	bool valid = false;
	Variant v = obj->get(StringName(name), &valid);
	if (!valid) {
		return fail(r_error, p_error_size, vformat("%s has no property '%s'", obj->get_class(), name));
	}
	*r_result = HemlockVariant::to_hemlock(v);
	return 0;
}

HML_NATIVE(native_set) {
	Object *obj = object_arg(p_args, p_argc, 0, r_error, p_error_size);
	if (!obj) {
		return 1;
	}
	if (p_argc != 3) {
		return fail(r_error, p_error_size, "set(name, value) takes two arguments");
	}
	String name = as_string(p_args[1]);
	bool valid = false;
	obj->set(StringName(name), HemlockVariant::to_variant(p_args[2]), &valid);
	if (!valid) {
		return fail(r_error, p_error_size, vformat("cannot set '%s' on %s (no such property, or wrong type)", name, obj->get_class()));
	}
	return 0;
}

HML_NATIVE(native_call) {
	Object *obj = object_arg(p_args, p_argc, 0, r_error, p_error_size);
	if (!obj) {
		return 1;
	}
	if (p_argc != 3 || p_args[2].kind != HML_EMBED_ARRAY) {
		return fail(r_error, p_error_size, "call(method, ...args)");
	}
	StringName method = StringName(as_string(p_args[1]));
	const HmlEmbedValue &list = p_args[2];
	LocalVector<Variant> vargs;
	vargs.resize(list.as.arr.count);
	LocalVector<const Variant *> ptrs;
	ptrs.resize(list.as.arr.count);
	for (int i = 0; i < list.as.arr.count; i++) {
		vargs[i] = HemlockVariant::to_variant(list.as.arr.items[i]);
		ptrs[i] = &vargs[i];
	}
	Callable::CallError ce;
	Variant ret = obj->callp(method, ptrs.ptr(), (int)ptrs.size(), ce);
	if (ce.error != Callable::CallError::CALL_OK) {
		return fail(r_error, p_error_size, Variant::get_call_error_text(obj, method, ptrs.ptr(), (int)ptrs.size(), ce));
	}
	*r_result = HemlockVariant::to_hemlock(ret);
	return 0;
}

HML_NATIVE(native_valid) {
	*r_result = hml_embed_bool(p_argc == 1 && p_args[0].kind == HML_EMBED_HANDLE && ObjectDB::get_instance(ObjectID(p_args[0].as.handle)) != nullptr);
	return 0;
}

HML_NATIVE(native_class) {
	Object *obj = object_arg(p_args, p_argc, 0, r_error, p_error_size);
	if (!obj) {
		return 1;
	}
	*r_result = hml_embed_cstring(String(obj->get_class()).utf8().get_data());
	return 0;
}

HML_NATIVE(native_print) {
	String line;
	if (p_argc == 1 && p_args[0].kind == HML_EMBED_ARRAY) {
		for (int i = 0; i < p_args[0].as.arr.count; i++) {
			line += as_string(p_args[0].as.arr.items[i]);
		}
	}
	print_line(line);
	return 0;
}

HML_NATIVE(native_singleton) {
	if (p_argc != 1 || p_args[0].kind != HML_EMBED_STRING) {
		return fail(r_error, p_error_size, "singleton(name)");
	}
	String name = as_string(p_args[0]);
	Object *obj = Engine::get_singleton()->get_singleton_object(name);
	if (!obj) {
		return fail(r_error, p_error_size, vformat("no engine singleton '%s'", name));
	}
	*r_result = hml_embed_handle((uint64_t)obj->get_instance_id());
	return 0;
}

HML_NATIVE(native_instantiate) {
	if (p_argc != 1 || p_args[0].kind != HML_EMBED_STRING) {
		return fail(r_error, p_error_size, "instantiate(class_name)");
	}
	String name = as_string(p_args[0]);
	if (!ClassDB::can_instantiate(name)) {
		return fail(r_error, p_error_size, vformat("cannot instantiate '%s'", name));
	}
	Object *obj = ClassDB::instantiate(name);
	if (!obj) {
		return fail(r_error, p_error_size, vformat("instantiating '%s' failed", name));
	}
	keep_object(obj);
	*r_result = hml_embed_handle((uint64_t)obj->get_instance_id());
	return 0;
}

HML_NATIVE(native_load) {
	if (p_argc != 1 || p_args[0].kind != HML_EMBED_STRING) {
		return fail(r_error, p_error_size, "load(path)");
	}
	String path = as_string(p_args[0]);
	Ref<Resource> res = ResourceLoader::load(path);
	if (res.is_null()) {
		return fail(r_error, p_error_size, vformat("cannot load '%s'", path));
	}
	keep_object(res.ptr());
	*r_result = hml_embed_handle((uint64_t)res->get_instance_id());
	return 0;
}

HML_NATIVE(native_keep) {
	Object *obj = object_arg(p_args, p_argc, 0, r_error, p_error_size);
	if (!obj) {
		return 1;
	}
	keep_object(obj);
	*r_result = p_args[0].kind == HML_EMBED_HANDLE ? hml_embed_handle(p_args[0].as.handle) : hml_embed_null();
	return 0;
}

HML_NATIVE(native_release) {
	if (p_argc != 1 || p_args[0].kind != HML_EMBED_HANDLE) {
		return fail(r_error, p_error_size, "release(object)");
	}
	if (!kept.erase(ObjectID(p_args[0].as.handle))) {
		return fail(r_error, p_error_size, "release() of an object that was not kept");
	}
	return 0;
}

HML_NATIVE(native_free) {
	Object *obj = object_arg(p_args, p_argc, 0, r_error, p_error_size);
	if (!obj) {
		return 1;
	}
	if (Object::cast_to<RefCounted>(obj)) {
		// Refcounted objects die with their last reference: drop ours
		if (!kept.erase(obj->get_instance_id())) {
			return fail(r_error, p_error_size, "free() of a RefCounted object that was not kept");
		}
		return 0;
	}
	memdelete(obj);
	return 0;
}

// __godot_typed(type_name, fields): a typed object (Vector2, Color, ...).
// Components are coerced to the type's component kind so typeof() and
// arithmetic agree with what the engine hands back for the same type.
HML_NATIVE(native_typed) {
	if (p_argc != 2 || p_args[0].kind != HML_EMBED_STRING || p_args[1].kind != HML_EMBED_OBJECT) {
		return fail(r_error, p_error_size, "__godot_typed(type, fields)");
	}
	const char *type = p_args[0].as.s.data;
	enum { PASS,
		FLOATS,
		INTS } components = PASS;
	static const char *const float_types[] = { "Vector2", "Vector3", "Vector4", "Quaternion", "Color" };
	for (const char *t : float_types) {
		if (strcmp(type, t) == 0) {
			components = FLOATS;
		}
	}
	if (strcmp(type, "Vector2i") == 0 || strcmp(type, "Vector3i") == 0) {
		components = INTS;
	}

	const HmlEmbedValue &fields = p_args[1];
	HmlEmbedValue out = hml_embed_object(type, fields.as.obj.count);
	for (int i = 0; i < fields.as.obj.count; i++) {
		const HmlEmbedValue &v = fields.as.obj.values[i];
		HmlEmbedValue copy;
		if (components == FLOATS && v.kind == HML_EMBED_INT) {
			copy = hml_embed_float((double)v.as.i);
		} else if (components == INTS && v.kind == HML_EMBED_FLOAT) {
			copy = hml_embed_int((int64_t)v.as.f);
		} else if (components != PASS && v.kind != HML_EMBED_INT && v.kind != HML_EMBED_FLOAT) {
			hml_embed_value_free(&out);
			return fail(r_error, p_error_size, vformat("%s component '%s' must be a number", type, fields.as.obj.keys[i]));
		} else {
			copy = hml_embed_value_copy(&v);
		}
		hml_embed_object_set(&out, fields.as.obj.keys[i], copy);
	}
	*r_result = out;
	return 0;
}

static const char *const PRELUDE = R"HEMLOCK(
// Godot prelude: how scripts see engine objects (handles) and helpers.
fn __godot_wrap(h) {
    return {
        __handle: h,
        fn get(name) { return __godot_get(self, name); },
        fn set(name, value) { return __godot_set(self, name, value); },
        fn call(method, ...args) { return __godot_call(self, method, args); },
        fn get_node(path) { return __godot_call(self, "get_node", [path]); },
        fn emit(signal, ...args) {
            let all = [signal];
            for (a in args) { all.push(a); }
            return __godot_call(self, "emit_signal", all);
        },
        fn connect(signal, target, method) {
            return __godot_call(self, "connect", [signal, godot.callable(target, method)]);
        },
        fn is_valid() { return __godot_valid(self); },
        fn class_name() { return __godot_class(self); },
        fn is_class(name) { return __godot_call(self, "is_class", [name]); },
        fn free() { return __godot_free(self); },
    };
}

let godot = {
    fn print(...args) { return __godot_print(args); },
    fn singleton(name) { return __godot_singleton(name); },
    fn instantiate(class_name) { return __godot_instantiate(class_name); },
    fn load(path) { return __godot_load(path); },
    fn keep(obj) { return __godot_keep(obj); },
    fn release(obj) { return __godot_release(obj); },
    fn vec2(x, y) { return __godot_typed("Vector2", { x: x, y: y }); },
    fn vec2i(x, y) { return __godot_typed("Vector2i", { x: x, y: y }); },
    fn vec3(x, y, z) { return __godot_typed("Vector3", { x: x, y: y, z: z }); },
    fn vec3i(x, y, z) { return __godot_typed("Vector3i", { x: x, y: y, z: z }); },
    fn vec4(x, y, z, w) { return __godot_typed("Vector4", { x: x, y: y, z: z, w: w }); },
    fn color(r, g, b, a?: 1.0) { return __godot_typed("Color", { r: r, g: g, b: b, a: a }); },
    fn rect2(position, size) { return __godot_typed("Rect2", { position: position, size: size }); },
    fn callable(obj, method) { return __godot_typed("Callable", { object: obj, method: method }); },
    fn node_path(path) { return __godot_typed("NodePath", { path: path }); },
};
)HEMLOCK";

void hemlock_install_godot_api(HmlVM *p_vm) {
	static const struct {
		const char *name;
		HmlNativeFn fn;
	} natives[] = {
		{ "__godot_get", native_get },
		{ "__godot_set", native_set },
		{ "__godot_call", native_call },
		{ "__godot_valid", native_valid },
		{ "__godot_class", native_class },
		{ "__godot_print", native_print },
		{ "__godot_singleton", native_singleton },
		{ "__godot_instantiate", native_instantiate },
		{ "__godot_load", native_load },
		{ "__godot_keep", native_keep },
		{ "__godot_release", native_release },
		{ "__godot_free", native_free },
		{ "__godot_typed", native_typed },
	};
	for (const auto &n : natives) {
		if (hml_vm_define_native(p_vm, n.name, n.fn, nullptr) != HML_EMBED_OK) {
			ERR_PRINT(vformat("Hemlock: defining %s failed: %s", n.name, String::utf8(hml_vm_last_error(p_vm))));
		}
	}
	if (hml_vm_eval(p_vm, PRELUDE, "<godot-prelude>.hml") != HML_EMBED_OK) {
		ERR_PRINT(vformat("Hemlock: the Godot prelude failed (line %d): %s", hml_vm_last_error_line(p_vm), String::utf8(hml_vm_last_error(p_vm))));
	}
	hml_vm_set_handle_wrapper(p_vm, "__godot_wrap");
}

void hemlock_release_godot_api() {
	kept.clear();
}
