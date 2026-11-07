/**************************************************************************/
/*  hemlock_script.cpp                                                    */
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

#include "hemlock_script.h"

#include "hemlock_variant.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

HemlockLanguage *HemlockLanguage::singleton = nullptr;

// Script-configuration constants (read from the AST, never run)
static const char *const HML_CONST_EXTENDS = "EXTENDS";
static const char *const HML_CONST_TOOL = "TOOL";
static const char *const HML_CONST_SIGNALS = "SIGNALS";

static bool is_config_constant(const String &p_name) {
	return p_name == HML_CONST_EXTENDS || p_name == HML_CONST_TOOL || p_name == HML_CONST_SIGNALS;
}

// Owned HmlEmbedValue that frees itself
struct HmlValueGuard {
	HmlEmbedValue value = hml_embed_null();
	~HmlValueGuard() { hml_embed_value_free(&value); }
};

static String hml_str(const char *p_utf8) {
	return p_utf8 ? String::utf8(p_utf8) : String();
}

/* ========== HemlockScript ========== */

HemlockScript::~HemlockScript() {
	HemlockLanguage *lang = HemlockLanguage::get_singleton();
	if (lang) {
		MutexLock guard(lang->get_lock());
		_clear_compiled();
	}
}

void HemlockScript::_clear_compiled() {
	if (compiled) {
		hml_script_release(compiled);
		compiled = nullptr;
	}
}

String HemlockScript::_get_abs_path() const {
	String p = get_path();
	if (p.is_empty() || p.contains("::")) {
		return ProjectSettings::get_singleton()->globalize_path("res://").path_join("<built-in>.hml");
	}
	return ProjectSettings::get_singleton()->globalize_path(p);
}

ScriptLanguage *HemlockScript::get_language() const {
	return HemlockLanguage::get_singleton();
}

bool HemlockScript::can_instantiate() const {
#ifdef TOOLS_ENABLED
	return valid && (tool || ScriptServer::is_scripting_enabled());
#else
	return valid;
#endif
}

// Rebuild members / methods / config from `compiled` (lock held)
void HemlockScript::_read_declarations() {
	members.clear();
	member_index.clear();
	methods.clear();
	signal_names.clear();
	tool = false;
	base_type = SNAME("Object");

	for (int i = 0; i < hml_script_variable_count(compiled); i++) {
		String var_name = hml_str(hml_script_variable_name(compiled, i));
		HmlValueGuard def;
		bool has_default = hml_script_variable_default(compiled, i, &def.value) == HML_EMBED_OK;
		bool is_const = hml_script_variable_is_const(compiled, i);

		if (is_const && is_config_constant(var_name)) {
			if (!has_default) {
				ERR_PRINT(vformat("%s: %s must be a literal.", get_path(), var_name));
				continue;
			}
			if (var_name == HML_CONST_EXTENDS && def.value.kind == HML_EMBED_STRING) {
				base_type = StringName(String::utf8(def.value.as.s.data));
			} else if (var_name == HML_CONST_TOOL && def.value.kind == HML_EMBED_BOOL) {
				tool = def.value.as.b;
			} else if (var_name == HML_CONST_SIGNALS && def.value.kind == HML_EMBED_ARRAY) {
				for (int j = 0; j < def.value.as.arr.count; j++) {
					const HmlEmbedValue &s = def.value.as.arr.items[j];
					if (s.kind == HML_EMBED_STRING) {
						signal_names.push_back(StringName(String::utf8(s.as.s.data)));
					}
				}
			} else {
				ERR_PRINT(vformat("%s: %s has the wrong type.", get_path(), var_name));
			}
			continue;
		}

		Member m;
		m.name = StringName(var_name);
		m.exported = hml_script_variable_exported(compiled, i);
		m.is_const = is_const;
		m.line = hml_script_variable_line(compiled, i);
		m.has_default = has_default;
		if (has_default) {
			m.default_value = HemlockVariant::to_variant(def.value);
		}

		Variant::Type type = HemlockVariant::type_for_annotation(hml_script_variable_type(compiled, i));
		if (type == Variant::NIL && has_default) {
			type = m.default_value.get_type();
		}
		m.info = PropertyInfo(type, var_name);
		m.info.usage = m.exported ? (PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SCRIPT_VARIABLE) : PROPERTY_USAGE_SCRIPT_VARIABLE;
		if (type == Variant::NIL) {
			m.info.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
		}
		member_index[m.name] = members.size();
		members.push_back(m);
	}

	if (!ClassDB::class_exists(base_type)) {
		ERR_PRINT(vformat("%s: EXTENDS names unknown class \"%s\"; using Object.", get_path(), base_type));
		base_type = SNAME("Object");
	}

	for (int i = 0; i < hml_script_function_count(compiled); i++) {
		Method m;
		m.line = hml_script_function_line(compiled, i);
		m.info.name = hml_str(hml_script_function_name(compiled, i));
		for (int j = 0; j < hml_script_function_param_count(compiled, i); j++) {
			PropertyInfo arg(Variant::NIL, hml_str(hml_script_function_param_name(compiled, i, j)));
			arg.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
			m.info.arguments.push_back(arg);
		}
		int optional = hml_script_function_param_count(compiled, i) - hml_script_function_arity(compiled, i);
		for (int j = 0; j < optional; j++) {
			m.info.default_arguments.push_back(Variant());
		}
		if (hml_script_function_has_rest(compiled, i)) {
			m.info.flags |= METHOD_FLAG_VARARG;
		}
		m.info.return_val.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
		methods[m.info.name] = m;
	}
}

Error HemlockScript::reload(bool p_keep_state) {
	HemlockLanguage *lang = HemlockLanguage::get_singleton();
	ERR_FAIL_NULL_V(lang, ERR_UNAVAILABLE);
	MutexLock guard(lang->get_lock());

	String abs_path = _get_abs_path();
	CharString src = source.utf8();
	CharString path = abs_path.utf8();
	HmlEmbedStatus status = HML_EMBED_OK;
	HmlScript *fresh = hml_script_compile(lang->get_vm(), src.get_data(), path.get_data(), &status);
	if (!fresh) {
		valid = false;
		lang->report_error("Hemlock: could not compile script");
		return ERR_PARSE_ERROR;
	}

	_clear_compiled();
	compiled = fresh;
	valid = true;
	_read_declarations();

	// Re-run live instances on the new code, carrying over the values of
	// variables that still exist (the script's top level runs again).
	for (HemlockScriptInstance *si : instances) {
		HashMap<StringName, Variant> state;
		for (const Member &m : members) {
			if (!m.is_const) {
				Variant v;
				if (si->get(m.name, v)) {
					state[m.name] = v;
				}
			}
		}
		HmlValueGuard owner_handle;
		owner_handle.value = hml_embed_handle((uint64_t)si->owner->get_instance_id());
		const char *names[] = { "owner" };
		HmlInstance *next = hml_instance_new(compiled, names, &owner_handle.value, 1, &status);
		if (!next) {
			lang->report_error("Hemlock: reloading an instance failed (keeping the old code)");
			continue;
		}
		hml_instance_free(si->instance);
		si->instance = next;
		// Always carried over: a fresh top level would otherwise reset a
		// running game's state on every save (p_keep_state is a GDScript-ism)
		for (const KeyValue<StringName, Variant> &kv : state) {
			si->set(kv.key, kv.value);
		}
	}

#ifdef TOOLS_ENABLED
	_update_placeholders();
#endif
	return OK;
}

ScriptInstance *HemlockScript::instance_create(Object *p_this) {
	ERR_FAIL_COND_V_MSG(!valid, nullptr, vformat("Hemlock script '%s' is not valid.", get_path()));
	if (!p_this->is_class(base_type)) {
		ERR_FAIL_V_MSG(nullptr, vformat("Hemlock script '%s' extends '%s', so it can't be assigned to an object of type '%s'.", get_path(), base_type, p_this->get_class()));
	}

	HemlockLanguage *lang = HemlockLanguage::get_singleton();
	MutexLock guard(lang->get_lock());

	HemlockScriptInstance *si = memnew(HemlockScriptInstance);
	si->owner = p_this;
	si->script = Ref<HemlockScript>(this);
	// Registered before the top level runs: it may call back into the owner
	instances.insert(si);

	HmlValueGuard owner_handle;
	owner_handle.value = hml_embed_handle((uint64_t)p_this->get_instance_id());
	const char *names[] = { "owner" };
	HmlEmbedStatus status = HML_EMBED_OK;
	si->instance = hml_instance_new(compiled, names, &owner_handle.value, 1, &status);
	if (!si->instance) {
		lang->report_error(vformat("Hemlock: top level of '%s' raised", get_path()));
		instances.erase(si);
		si->script.unref();
		memdelete(si);
		return nullptr;
	}
	return si;
}

PlaceHolderScriptInstance *HemlockScript::placeholder_instance_create(Object *p_this) {
#ifdef TOOLS_ENABLED
	PlaceHolderScriptInstance *si = memnew(PlaceHolderScriptInstance(HemlockLanguage::get_singleton(), Ref<Script>(this), p_this));
	placeholders.insert(si);
	List<PropertyInfo> props;
	HashMap<StringName, Variant> values;
	for (const Member &m : members) {
		if (m.exported) {
			props.push_back(m.info);
			values[m.name] = m.default_value;
		}
	}
	si->update(props, values);
	return si;
#else
	return nullptr;
#endif
}

#ifdef TOOLS_ENABLED
void HemlockScript::_placeholder_erased(PlaceHolderScriptInstance *p_placeholder) {
	placeholders.erase(p_placeholder);
}

void HemlockScript::_update_placeholders() {
	List<PropertyInfo> props;
	HashMap<StringName, Variant> values;
	for (const Member &m : members) {
		if (m.exported) {
			props.push_back(m.info);
			values[m.name] = m.default_value;
		}
	}
	for (PlaceHolderScriptInstance *p : placeholders) {
		p->update(props, values);
	}
}
#endif

void HemlockScript::update_exports() {
#ifdef TOOLS_ENABLED
	_update_placeholders();
#endif
}

MethodInfo HemlockScript::get_method_info(const StringName &p_method) const {
	const Method *m = methods.getptr(p_method);
	return m ? m->info : MethodInfo();
}

bool HemlockScript::has_script_signal(const StringName &p_signal) const {
	return signal_names.has(p_signal);
}

void HemlockScript::get_script_signal_list(List<MethodInfo> *r_signals) const {
	for (const StringName &s : signal_names) {
		MethodInfo mi;
		mi.name = s;
		mi.flags |= METHOD_FLAG_VARARG;
		r_signals->push_back(mi);
	}
}

bool HemlockScript::get_property_default_value(const StringName &p_property, Variant &r_value) const {
	const int *idx = member_index.getptr(p_property);
	if (!idx || !members[*idx].has_default) {
		return false;
	}
	r_value = members[*idx].default_value;
	return true;
}

void HemlockScript::get_script_method_list(List<MethodInfo> *r_list) const {
	for (const KeyValue<StringName, Method> &kv : methods) {
		r_list->push_back(kv.value.info);
	}
}

void HemlockScript::get_script_property_list(List<PropertyInfo> *r_list) const {
	for (const Member &m : members) {
		if (!m.is_const) {
			r_list->push_back(m.info);
		}
	}
}

int HemlockScript::get_member_line(const StringName &p_member) const {
	if (const int *idx = member_index.getptr(p_member)) {
		return members[*idx].line;
	}
	if (const Method *m = methods.getptr(p_member)) {
		return m->line;
	}
	return -1;
}

void HemlockScript::get_constants(HashMap<StringName, Variant> *r_constants) {
	for (const Member &m : members) {
		if (m.is_const && m.has_default) {
			(*r_constants)[m.name] = m.default_value;
		}
	}
}

void HemlockScript::get_members(HashSet<StringName> *r_members) {
	for (const Member &m : members) {
		r_members->insert(m.name);
	}
}

/* ========== HemlockScriptInstance ========== */

HemlockScriptInstance::~HemlockScriptInstance() {
	HemlockLanguage *lang = HemlockLanguage::get_singleton();
	MutexLock guard(lang->get_lock());
	if (script.is_valid()) {
		script->instances.erase(this);
	}
	if (instance) {
		hml_instance_free(instance);
		instance = nullptr;
	}
}

ScriptLanguage *HemlockScriptInstance::get_language() {
	return HemlockLanguage::get_singleton();
}

bool HemlockScriptInstance::set(const StringName &p_name, const Variant &p_value) {
	const int *idx = script->member_index.getptr(p_name);
	if (!idx || script->members[*idx].is_const) {
		return false;
	}
	HemlockLanguage *lang = HemlockLanguage::get_singleton();
	MutexLock guard(lang->get_lock());
	HmlValueGuard v;
	v.value = HemlockVariant::to_hemlock(p_value);
	CharString name = String(p_name).utf8();
	HmlEmbedStatus st = hml_instance_set(instance, name.get_data(), &v.value);
	if (st == HML_EMBED_RUNTIME_ERROR) {
		lang->report_error(vformat("Hemlock: setting '%s'", p_name));
		return true; // the property exists; the value was rejected
	}
	return st == HML_EMBED_OK;
}

bool HemlockScriptInstance::get(const StringName &p_name, Variant &r_ret) const {
	if (!script->member_index.has(p_name)) {
		return false;
	}
	HemlockLanguage *lang = HemlockLanguage::get_singleton();
	MutexLock guard(lang->get_lock());
	HmlValueGuard v;
	CharString name = String(p_name).utf8();
	if (hml_instance_get(instance, name.get_data(), &v.value) != HML_EMBED_OK) {
		return false;
	}
	r_ret = HemlockVariant::to_variant(v.value);
	return true;
}

void HemlockScriptInstance::get_property_list(List<PropertyInfo> *r_properties) const {
	script->get_script_property_list(r_properties);
}

Variant::Type HemlockScriptInstance::get_property_type(const StringName &p_name, bool *r_is_valid) const {
	const int *idx = script->member_index.getptr(p_name);
	if (r_is_valid) {
		*r_is_valid = idx != nullptr;
	}
	return idx ? script->members[*idx].info.type : Variant::NIL;
}

bool HemlockScriptInstance::property_can_revert(const StringName &p_name) const {
	const int *idx = script->member_index.getptr(p_name);
	return idx && script->members[*idx].has_default;
}

bool HemlockScriptInstance::property_get_revert(const StringName &p_name, Variant &r_ret) const {
	return script->get_property_default_value(p_name, r_ret);
}

void HemlockScriptInstance::get_method_list(List<MethodInfo> *r_list) const {
	script->get_script_method_list(r_list);
}

bool HemlockScriptInstance::has_method(const StringName &p_method) const {
	return script->methods.has(p_method);
}

Variant HemlockScriptInstance::callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	if (!script->methods.has(p_method)) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	HemlockLanguage *lang = HemlockLanguage::get_singleton();
	MutexLock guard(lang->get_lock());

	LocalVector<HmlEmbedValue> args;
	args.resize(p_argcount);
	for (int i = 0; i < p_argcount; i++) {
		args[i] = HemlockVariant::to_hemlock(*p_args[i]);
	}

	// Keep the script (and so this instance's code) alive across the call:
	// Hemlock code may detach the script from its owner.
	Ref<HemlockScript> keep = script;
	HmlValueGuard result;
	CharString name = String(p_method).utf8();
	HmlEmbedStatus st = hml_instance_call(instance, name.get_data(), args.ptr(), p_argcount, &result.value);
	for (HmlEmbedValue &a : args) {
		hml_embed_value_free(&a);
	}

	r_error.error = Callable::CallError::CALL_OK;
	if (st == HML_EMBED_RUNTIME_ERROR) {
		lang->report_error(vformat("Hemlock: in %s()", p_method));
		return Variant();
	}
	if (st != HML_EMBED_OK) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
		return Variant();
	}
	return HemlockVariant::to_variant(result.value);
}

void HemlockScriptInstance::notification(int p_notification, bool p_reversed) {
	static const StringName notification_name = SNAME("_notification");
	if (!script->methods.has(notification_name)) {
		return;
	}
	Variant what = p_notification;
	const Variant *args[] = { &what };
	Callable::CallError err;
	callp(notification_name, args, 1, err);
}

/* ========== Editor support ========== */

#ifdef TOOLS_ENABLED
bool HemlockEditorLanguage::validate(const String &p_code, const String &p_path, List<ScriptError> *r_errors, List<Warning> *r_warnings, List<String> *r_functions, HashSet<int> *r_safe_lines) const {
	HemlockLanguage *lang = HemlockLanguage::get_singleton();
	MutexLock guard(lang->get_lock());

	String abs_path = p_path.is_empty() ? String("<validate>.hml") : ProjectSettings::get_singleton()->globalize_path(p_path);
	CharString src = p_code.utf8();
	CharString path = abs_path.utf8();
	HmlEmbedStatus status = HML_EMBED_OK;
	HmlScript *s = hml_script_compile(lang->get_vm(), src.get_data(), path.get_data(), &status);
	if (!s) {
		if (r_errors) {
			ScriptError e;
			e.path = p_path;
			e.start_line = MAX(1, hml_vm_last_error_line(lang->get_vm()));
			e.start_column = MAX(1, hml_vm_last_error_column(lang->get_vm()));
			e.message = String::utf8(hml_vm_last_error(lang->get_vm()));
			// A diagnostic from an imported module belongs to that file
			String file = String::utf8(hml_vm_last_error_file(lang->get_vm()) ? hml_vm_last_error_file(lang->get_vm()) : "");
			if (!file.is_empty() && file != abs_path) {
				e.message = ProjectSettings::get_singleton()->localize_path(file) + ": " + e.message;
				e.start_line = 1;
				e.start_column = 1;
			}
			r_errors->push_back(e);
		}
		return false;
	}
	if (r_functions) {
		for (int i = 0; i < hml_script_function_count(s); i++) {
			r_functions->push_back(String::utf8(hml_script_function_name(s, i)) + ":" + itos(hml_script_function_line(s, i)));
		}
	}
	hml_script_release(s);
	return true;
}

int32_t HemlockEditorLanguage::find_function(const String &p_function, const String &p_code) const {
	// Textual, so it works on code that does not compile right now
	Vector<String> lines = p_code.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String l = lines[i].strip_edges();
		if (l.begins_with("export ")) {
			l = l.substr(7).strip_edges();
		}
		if (l.begins_with("async ")) {
			l = l.substr(6).strip_edges();
		}
		if (l.begins_with("fn ")) {
			String rest = l.substr(3).strip_edges();
			if (rest.begins_with(p_function) && rest.substr(p_function.length()).strip_edges().begins_with("(")) {
				return i + 1;
			}
		}
	}
	return -1;
}
#endif

/* ========== HemlockLanguage ========== */

HemlockLanguage::HemlockLanguage() {
	ERR_FAIL_COND(singleton);
	singleton = this;
}

HemlockLanguage::~HemlockLanguage() {
	singleton = nullptr;
}

void HemlockLanguage::report_error(const String &p_context) const {
	String msg = String::utf8(hml_vm_last_error(vm));
	const char *file = hml_vm_last_error_file(vm);
	String where = file ? ProjectSettings::get_singleton()->localize_path(String::utf8(file)) : String("<hemlock>");
	int line = hml_vm_last_error_line(vm);
	CharString where_utf8 = where.utf8();
	_err_print_error(p_context.utf8().get_data(), where_utf8.get_data(), line, msg, false, ERR_HANDLER_SCRIPT);
}

void HemlockLanguage::init() {
	MutexLock guard(lock);

	// @stdlib/ lookup: HEMLOCK_STDLIB, then the project setting, then the
	// checkout the engine was built against.
	String stdlib = OS::get_singleton()->get_environment("HEMLOCK_STDLIB");
	if (stdlib.is_empty()) {
		stdlib = GLOBAL_DEF("hemlock/stdlib_path", "");
		if (!stdlib.is_empty()) {
			stdlib = ProjectSettings::get_singleton()->globalize_path(stdlib);
		}
	}
#ifdef HEMLOCK_DEFAULT_STDLIB
	if (stdlib.is_empty()) {
		stdlib = HEMLOCK_DEFAULT_STDLIB;
	}
#endif
	CharString stdlib_utf8 = stdlib.utf8();
	CharString base_utf8 = ProjectSettings::get_singleton()->globalize_path("res://").utf8();

	HmlVMConfig config;
	hml_vm_config_init(&config);
	config.stdlib_path = stdlib.is_empty() ? nullptr : stdlib_utf8.get_data();
	config.base_dir = base_utf8.get_data();
	vm = hml_vm_new(&config);
	ERR_FAIL_NULL_MSG(vm, "Hemlock: could not create the interpreter.");

	hml_vm_declare_global(vm, "owner");
	hemlock_install_godot_api(vm);
}

void HemlockLanguage::finish() {
	MutexLock guard(lock);
	hemlock_release_godot_api();
	if (vm) {
		hml_vm_free(vm);
		vm = nullptr;
	}
}

Vector<String> HemlockLanguage::get_reserved_words() const {
	static const char *const words[] = {
		// Declarations and modules
		"let",
		"const",
		"fn",
		"define",
		"enum",
		"type",
		"import",
		"export",
		"from",
		"as",
		"extern",
		"async",
		"gpu",
		// Control flow
		"if",
		"else",
		"while",
		"for",
		"in",
		"loop",
		"break",
		"continue",
		"return",
		"switch",
		"case",
		"default",
		"match",
		"try",
		"catch",
		"finally",
		"throw",
		"defer",
		"await",
		// Parameter modifiers and literals
		"ref",
		"self",
		"true",
		"false",
		"null",
		// Types
		"i8",
		"i16",
		"i32",
		"i64",
		"u8",
		"u16",
		"u32",
		"u64",
		"f32",
		"f64",
		"bool",
		"string",
		"rune",
		"ptr",
		"buffer",
		"array",
		"object",
		"integer",
		"number",
		"byte",
		"void",
	};
	Vector<String> out;
	for (const char *w : words) {
		out.push_back(w);
	}
	return out;
}

bool HemlockLanguage::is_control_flow_keyword(const String &p_keyword) const {
	static const char *const words[] = {
		"if",
		"else",
		"while",
		"for",
		"in",
		"loop",
		"break",
		"continue",
		"return",
		"switch",
		"case",
		"default",
		"match",
		"try",
		"catch",
		"finally",
		"throw",
		"defer",
		"await",
	};
	for (const char *w : words) {
		if (p_keyword == w) {
			return true;
		}
	}
	return false;
}

Vector<String> HemlockLanguage::get_comment_delimiters() const {
	Vector<String> out;
	out.push_back("//");
	out.push_back("/* */");
	return out;
}

Vector<String> HemlockLanguage::get_doc_comment_delimiters() const {
	Vector<String> out;
	out.push_back("///");
	return out;
}

Vector<String> HemlockLanguage::get_string_delimiters() const {
	Vector<String> out;
	out.push_back("\" \"");
	out.push_back("' '");
	out.push_back("` `");
	return out;
}

static const char *const HML_DEFAULT_TEMPLATE =
		"// _CLASS_: a Hemlock script for a _BASE_.\n"
		"const EXTENDS = \"_BASE_\";\n"
		"\n"
		"// Called when the node enters the scene tree for the first time.\n"
		"fn _ready() {\n"
		"    godot.print(\"ready: \", owner.get(\"name\"));\n"
		"}\n"
		"\n"
		"// Called every frame. `delta` is the elapsed time since the previous frame.\n"
		"fn _process(delta) {\n"
		"}\n";

Ref<Script> HemlockLanguage::make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
	Ref<HemlockScript> scr;
	scr.instantiate();
	String code = p_template.is_empty() ? String(HML_DEFAULT_TEMPLATE) : p_template;
	code = code.replace("_BASE_", p_base_class_name).replace("_CLASS_", p_class_name.to_pascal_case());
	scr->set_source_code(code);
	return scr;
}

Vector<ScriptLanguage::ScriptTemplate> HemlockLanguage::get_built_in_templates(const StringName &p_object) {
	ScriptTemplate t;
	t.inherit = "Object";
	t.name = "Default";
	t.description = "Lifecycle callbacks and the owner object.";
	t.content = HML_DEFAULT_TEMPLATE;
	t.origin = TEMPLATE_BUILT_IN;
	Vector<ScriptTemplate> out;
	out.push_back(t);
	return out;
}

String HemlockLanguage::make_function(const String &p_class, const String &p_name, const PackedStringArray &p_args) const {
	String args;
	for (int i = 0; i < p_args.size(); i++) {
		if (i > 0) {
			args += ", ";
		}
		args += p_args[i].get_slice(":", 0);
	}
	return "fn " + p_name + "(" + args + ") {\n    \n}\n";
}

void HemlockLanguage::reload_all_scripts() {
	List<Ref<Resource>> cached;
	ResourceCache::get_cached_resources(&cached);
	for (const Ref<Resource> &r : cached) {
		Ref<HemlockScript> s = r;
		if (s.is_valid()) {
			s->reload(true);
		}
	}
}

void HemlockLanguage::reload_scripts(const Array &p_scripts) {
	for (int i = 0; i < p_scripts.size(); i++) {
		Ref<HemlockScript> s = p_scripts[i];
		if (s.is_valid()) {
			s->reload(true);
		}
	}
}

void HemlockLanguage::reload_tool_script(const Ref<Script> &p_script) {
	Ref<HemlockScript> s = p_script;
	if (s.is_valid()) {
		s->reload(true);
	}
}

/* ========== Loader / saver ========== */

Ref<Resource> ResourceFormatLoaderHemlock::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	if (r_error) {
		*r_error = ERR_FILE_CANT_OPEN;
	}
	Error err = OK;
	String source = FileAccess::get_file_as_string(p_path, &err);
	ERR_FAIL_COND_V_MSG(err != OK, Ref<Resource>(), vformat("Cannot load Hemlock script '%s'.", p_path));

	Ref<HemlockScript> scr;
	scr.instantiate();
	scr->set_path_cache(p_original_path.is_empty() ? p_path : p_original_path);
	scr->set_source_code(source);
	// A script that does not compile still loads (the editor shows the
	// error); it just cannot be instantiated.
	scr->reload();
	if (r_error) {
		*r_error = OK;
	}
	return scr;
}

void ResourceFormatLoaderHemlock::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("hml");
}

bool ResourceFormatLoaderHemlock::handles_type(const String &p_type) const {
	return p_type == "Script" || p_type == "HemlockScript";
}

String ResourceFormatLoaderHemlock::get_resource_type(const String &p_path) const {
	return p_path.get_extension().to_lower() == "hml" ? "HemlockScript" : "";
}

Error ResourceFormatSaverHemlock::save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	Ref<HemlockScript> scr = p_resource;
	ERR_FAIL_COND_V(scr.is_null(), ERR_INVALID_PARAMETER);
	Error err = OK;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot save Hemlock script '%s'.", p_path));
	f->store_string(scr->get_source_code());
	if (f->get_error() != OK && f->get_error() != ERR_FILE_EOF) {
		return ERR_CANT_CREATE;
	}
	if (ScriptServer::is_reload_scripts_on_save_enabled()) {
		HemlockLanguage::get_singleton()->reload_tool_script(p_resource);
	}
	return OK;
}

void ResourceFormatSaverHemlock::get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const {
	if (Object::cast_to<HemlockScript>(*p_resource)) {
		p_extensions->push_back("hml");
	}
}

bool ResourceFormatSaverHemlock::recognize(const Ref<Resource> &p_resource) const {
	return Object::cast_to<HemlockScript>(*p_resource) != nullptr;
}
