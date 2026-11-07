/**************************************************************************/
/*  hemlock_variant.cpp                                                   */
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

#include "hemlock_variant.h"

#include "core/object/object.h"
#include "core/variant/array.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"

#include <cstring>

namespace HemlockVariant {

// Nesting past this becomes null (Godot containers can be cyclic)
static constexpr int MAX_DEPTH = 64;

static HmlEmbedValue string_value(const String &p_string) {
	CharString utf8 = p_string.utf8();
	return hml_embed_string(utf8.get_data(), (size_t)utf8.length());
}

static void set_float(HmlEmbedValue &r_obj, const char *p_key, double p_value) {
	hml_embed_object_set(&r_obj, p_key, hml_embed_float(p_value));
}

static void set_int(HmlEmbedValue &r_obj, const char *p_key, int64_t p_value) {
	hml_embed_object_set(&r_obj, p_key, hml_embed_int(p_value));
}

template <typename T>
static HmlEmbedValue packed_array(const T &p_array) {
	HmlEmbedValue out = hml_embed_array(0);
	for (int i = 0; i < p_array.size(); i++) {
		hml_embed_array_push(&out, to_hemlock(p_array[i]));
	}
	return out;
}

static HmlEmbedValue to_hemlock_depth(const Variant &p_value, int p_depth);

HmlEmbedValue to_hemlock(const Variant &p_value) {
	return to_hemlock_depth(p_value, 0);
}

static HmlEmbedValue to_hemlock_depth(const Variant &p_value, int p_depth) {
	if (p_depth > MAX_DEPTH) {
		return hml_embed_null();
	}
	switch (p_value.get_type()) {
		case Variant::NIL:
			return hml_embed_null();
		case Variant::BOOL:
			return hml_embed_bool(p_value.operator bool());
		case Variant::INT:
			return hml_embed_int(p_value.operator int64_t());
		case Variant::FLOAT:
			return hml_embed_float(p_value.operator double());
		case Variant::STRING:
		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
			return string_value(p_value.operator String());
		case Variant::VECTOR2: {
			Vector2 v = p_value;
			HmlEmbedValue out = hml_embed_object("Vector2", 2);
			set_float(out, "x", v.x);
			set_float(out, "y", v.y);
			return out;
		}
		case Variant::VECTOR2I: {
			Vector2i v = p_value;
			HmlEmbedValue out = hml_embed_object("Vector2i", 2);
			set_int(out, "x", v.x);
			set_int(out, "y", v.y);
			return out;
		}
		case Variant::VECTOR3: {
			Vector3 v = p_value;
			HmlEmbedValue out = hml_embed_object("Vector3", 3);
			set_float(out, "x", v.x);
			set_float(out, "y", v.y);
			set_float(out, "z", v.z);
			return out;
		}
		case Variant::VECTOR3I: {
			Vector3i v = p_value;
			HmlEmbedValue out = hml_embed_object("Vector3i", 3);
			set_int(out, "x", v.x);
			set_int(out, "y", v.y);
			set_int(out, "z", v.z);
			return out;
		}
		case Variant::VECTOR4: {
			Vector4 v = p_value;
			HmlEmbedValue out = hml_embed_object("Vector4", 4);
			set_float(out, "x", v.x);
			set_float(out, "y", v.y);
			set_float(out, "z", v.z);
			set_float(out, "w", v.w);
			return out;
		}
		case Variant::QUATERNION: {
			Quaternion q = p_value;
			HmlEmbedValue out = hml_embed_object("Quaternion", 4);
			set_float(out, "x", q.x);
			set_float(out, "y", q.y);
			set_float(out, "z", q.z);
			set_float(out, "w", q.w);
			return out;
		}
		case Variant::COLOR: {
			Color c = p_value;
			HmlEmbedValue out = hml_embed_object("Color", 4);
			set_float(out, "r", c.r);
			set_float(out, "g", c.g);
			set_float(out, "b", c.b);
			set_float(out, "a", c.a);
			return out;
		}
		case Variant::RECT2: {
			Rect2 r = p_value;
			HmlEmbedValue out = hml_embed_object("Rect2", 2);
			hml_embed_object_set(&out, "position", to_hemlock(r.position));
			hml_embed_object_set(&out, "size", to_hemlock(r.size));
			return out;
		}
		case Variant::OBJECT: {
			Object *obj = p_value.get_validated_object();
			if (!obj) {
				return hml_embed_null();
			}
			return hml_embed_handle((uint64_t)obj->get_instance_id());
		}
		case Variant::CALLABLE: {
			Callable c = p_value;
			if (c.is_standard() && c.get_object()) {
				HmlEmbedValue out = hml_embed_object("Callable", 2);
				hml_embed_object_set(&out, "object", hml_embed_handle((uint64_t)c.get_object_id()));
				hml_embed_object_set(&out, "method", string_value(c.get_method()));
				return out;
			}
			return string_value(p_value.operator String());
		}
		case Variant::ARRAY: {
			Array a = p_value;
			HmlEmbedValue out = hml_embed_array(0);
			for (int i = 0; i < a.size(); i++) {
				hml_embed_array_push(&out, to_hemlock_depth(a[i], p_depth + 1));
			}
			return out;
		}
		case Variant::DICTIONARY: {
			Dictionary d = p_value;
			HmlEmbedValue out = hml_embed_object(nullptr, 0);
			for (const KeyValue<Variant, Variant> &kv : d) {
				CharString key = kv.key.operator String().utf8();
				hml_embed_object_set(&out, key.get_data(), to_hemlock_depth(kv.value, p_depth + 1));
			}
			return out;
		}
		case Variant::PACKED_BYTE_ARRAY:
			return packed_array(p_value.operator PackedByteArray());
		case Variant::PACKED_INT32_ARRAY:
			return packed_array(p_value.operator PackedInt32Array());
		case Variant::PACKED_INT64_ARRAY:
			return packed_array(p_value.operator PackedInt64Array());
		case Variant::PACKED_FLOAT32_ARRAY:
			return packed_array(p_value.operator PackedFloat32Array());
		case Variant::PACKED_FLOAT64_ARRAY:
			return packed_array(p_value.operator PackedFloat64Array());
		case Variant::PACKED_STRING_ARRAY:
			return packed_array(p_value.operator PackedStringArray());
		case Variant::PACKED_VECTOR2_ARRAY:
			return packed_array(p_value.operator PackedVector2Array());
		case Variant::PACKED_VECTOR3_ARRAY:
			return packed_array(p_value.operator PackedVector3Array());
		case Variant::PACKED_COLOR_ARRAY:
			return packed_array(p_value.operator PackedColorArray());
		case Variant::PACKED_VECTOR4_ARRAY:
			return packed_array(p_value.operator PackedVector4Array());
		default:
			// Transforms, Basis, AABB, Plane, RID, Signal, ...: string form
			return string_value(p_value.operator String());
	}
}

static double num(const HmlEmbedValue &p_obj, const char *p_key, double p_default = 0.0) {
	const HmlEmbedValue *f = hml_embed_object_get(&p_obj, p_key);
	if (!f) {
		return p_default;
	}
	if (f->kind == HML_EMBED_FLOAT) {
		return f->as.f;
	}
	if (f->kind == HML_EMBED_INT) {
		return (double)f->as.i;
	}
	return p_default;
}

static int64_t inum(const HmlEmbedValue &p_obj, const char *p_key) {
	const HmlEmbedValue *f = hml_embed_object_get(&p_obj, p_key);
	if (!f) {
		return 0;
	}
	if (f->kind == HML_EMBED_INT) {
		return f->as.i;
	}
	if (f->kind == HML_EMBED_FLOAT) {
		return (int64_t)f->as.f;
	}
	return 0;
}

static Variant typed_object(const HmlEmbedValue &p_value, bool &r_handled) {
	r_handled = true;
	const char *t = p_value.as.obj.type_name;
	if (strcmp(t, "Vector2") == 0) {
		return Vector2(num(p_value, "x"), num(p_value, "y"));
	}
	if (strcmp(t, "Vector2i") == 0) {
		return Vector2i(inum(p_value, "x"), inum(p_value, "y"));
	}
	if (strcmp(t, "Vector3") == 0) {
		return Vector3(num(p_value, "x"), num(p_value, "y"), num(p_value, "z"));
	}
	if (strcmp(t, "Vector3i") == 0) {
		return Vector3i(inum(p_value, "x"), inum(p_value, "y"), inum(p_value, "z"));
	}
	if (strcmp(t, "Vector4") == 0) {
		return Vector4(num(p_value, "x"), num(p_value, "y"), num(p_value, "z"), num(p_value, "w"));
	}
	if (strcmp(t, "Quaternion") == 0) {
		return Quaternion(num(p_value, "x"), num(p_value, "y"), num(p_value, "z"), num(p_value, "w", 1.0));
	}
	if (strcmp(t, "Color") == 0) {
		return Color(num(p_value, "r"), num(p_value, "g"), num(p_value, "b"), num(p_value, "a", 1.0));
	}
	if (strcmp(t, "Rect2") == 0) {
		const HmlEmbedValue *pos = hml_embed_object_get(&p_value, "position");
		const HmlEmbedValue *size = hml_embed_object_get(&p_value, "size");
		return Rect2(pos ? to_variant(*pos).operator Vector2() : Vector2(), size ? to_variant(*size).operator Vector2() : Vector2());
	}
	if (strcmp(t, "Callable") == 0) {
		const HmlEmbedValue *obj = hml_embed_object_get(&p_value, "object");
		const HmlEmbedValue *method = hml_embed_object_get(&p_value, "method");
		if (obj && obj->kind == HML_EMBED_HANDLE && method && method->kind == HML_EMBED_STRING) {
			return Callable(ObjectID(obj->as.handle), StringName(String::utf8(method->as.s.data, (int)method->as.s.len)));
		}
		return Callable();
	}
	if (strcmp(t, "NodePath") == 0) {
		const HmlEmbedValue *path = hml_embed_object_get(&p_value, "path");
		if (path && path->kind == HML_EMBED_STRING) {
			return NodePath(String::utf8(path->as.s.data, (int)path->as.s.len));
		}
		return NodePath();
	}
	r_handled = false;
	return Variant();
}

Variant to_variant(const HmlEmbedValue &p_value) {
	switch (p_value.kind) {
		case HML_EMBED_NULL:
		case HML_EMBED_OPAQUE:
			return Variant();
		case HML_EMBED_BOOL:
			return (bool)p_value.as.b;
		case HML_EMBED_INT:
			return p_value.as.i;
		case HML_EMBED_FLOAT:
			return p_value.as.f;
		case HML_EMBED_STRING:
			return String::utf8(p_value.as.s.data, (int)p_value.as.s.len);
		case HML_EMBED_HANDLE: {
			Object *obj = ObjectDB::get_instance(ObjectID(p_value.as.handle));
			return obj ? Variant(obj) : Variant();
		}
		case HML_EMBED_ARRAY: {
			Array a;
			a.resize(p_value.as.arr.count);
			for (int i = 0; i < p_value.as.arr.count; i++) {
				a[i] = to_variant(p_value.as.arr.items[i]);
			}
			return a;
		}
		case HML_EMBED_OBJECT: {
			if (p_value.as.obj.type_name) {
				bool handled = false;
				Variant v = typed_object(p_value, handled);
				if (handled) {
					return v;
				}
			}
			Dictionary d;
			for (int i = 0; i < p_value.as.obj.count; i++) {
				if (p_value.as.obj.values[i].kind == HML_EMBED_OPAQUE) {
					continue; // methods of a Hemlock object do not cross
				}
				d[String::utf8(p_value.as.obj.keys[i])] = to_variant(p_value.as.obj.values[i]);
			}
			return d;
		}
	}
	return Variant();
}

Variant::Type type_for_annotation(const char *p_annotation) {
	if (!p_annotation) {
		return Variant::NIL;
	}
	String t = String::utf8(p_annotation);
	if (t.ends_with("?")) {
		return Variant::NIL; // nullable: anything goes in the inspector
	}
	static const char *const ints[] = { "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "rune", "integer", "byte" };
	for (const char *i : ints) {
		if (t == i) {
			return Variant::INT;
		}
	}
	if (t == "f32" || t == "f64" || t == "number") {
		return Variant::FLOAT;
	}
	if (t == "bool") {
		return Variant::BOOL;
	}
	if (t == "string") {
		return Variant::STRING;
	}
	if (t.begins_with("array")) {
		return Variant::ARRAY;
	}
	if (t == "object") {
		return Variant::DICTIONARY;
	}
	static const struct {
		const char *name;
		Variant::Type type;
	} math[] = {
		{ "Vector2", Variant::VECTOR2 },
		{ "Vector2i", Variant::VECTOR2I },
		{ "Vector3", Variant::VECTOR3 },
		{ "Vector3i", Variant::VECTOR3I },
		{ "Vector4", Variant::VECTOR4 },
		{ "Quaternion", Variant::QUATERNION },
		{ "Color", Variant::COLOR },
		{ "Rect2", Variant::RECT2 },
		{ "NodePath", Variant::NODE_PATH },
	};
	for (const auto &m : math) {
		if (t == m.name) {
			return m.type;
		}
	}
	return Variant::NIL;
}

} // namespace HemlockVariant
