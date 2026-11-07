/**************************************************************************/
/*  hemlock_script.h                                                      */
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

#pragma once

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/script_language.h"
#include "core/os/mutex.h"
#include "core/templates/hash_set.h"

#ifdef TOOLS_ENABLED
#include "core/object/editor_language.h"
#endif

#include <hemlock_embed.h>

class HemlockScriptInstance;
class HemlockLanguage;

// A .hml file attached to an object.
//
// The whole file is the script. Its top level runs once per instance (when
// the script is attached), so top-level `let`s are per-object state and
// top-level `fn`s are its methods: `fn _ready()`, `fn _process(delta)`, ...
// `export let` makes a variable an inspector property. Three optional
// top-level constants configure the script, read without running it:
//
//   const EXTENDS = "CharacterBody2D";  // native class it attaches to (default: Object)
//   const TOOL = true;                  // run in the editor too
//   const SIGNALS = ["hit", "died"];    // signals the script declares
//
// `owner` is the object the instance belongs to; `godot` holds the engine
// helpers (see hemlock_godot_api.cpp).
class HemlockScript : public Script {
	GDCLASS(HemlockScript, Script);

	friend class HemlockScriptInstance;
	friend class HemlockLanguage;

	String source;
	HmlScript *compiled = nullptr; // nullptr when the source does not compile
	bool valid = false;
	bool tool = false;
	StringName base_type;
	Vector<StringName> signal_names;

	struct Member {
		StringName name;
		PropertyInfo info;
		Variant default_value; // NIL when the initializer is not a literal
		bool has_default = false;
		bool exported = false;
		bool is_const = false;
		int line = 0;
	};
	Vector<Member> members;
	HashMap<StringName, int> member_index;

	struct Method {
		MethodInfo info;
		int line = 0;
	};
	HashMap<StringName, Method> methods;

	HashSet<HemlockScriptInstance *> instances;
#ifdef TOOLS_ENABLED
	HashSet<PlaceHolderScriptInstance *> placeholders;
	virtual void _placeholder_erased(PlaceHolderScriptInstance *p_placeholder) override;
	void _update_placeholders();
#endif

	void _clear_compiled();
	void _read_declarations();
	String _get_abs_path() const;

protected:
	static void _bind_methods() {}

public:
	virtual bool can_instantiate() const override;

	virtual Ref<Script> get_base_script() const override { return Ref<Script>(); }
	virtual StringName get_global_name() const override { return StringName(); }
	virtual bool inherits_script(const Ref<Script> &p_script) const override { return p_script.ptr() == this; }

	virtual StringName get_instance_base_type() const override { return base_type; }
	virtual ScriptInstance *instance_create(Object *p_this) override;
	virtual PlaceHolderScriptInstance *placeholder_instance_create(Object *p_this) override;

	virtual bool has_source_code() const override { return !source.is_empty(); }
	virtual String get_source_code() const override { return source; }
	virtual void set_source_code(const String &p_code) override { source = p_code; }
	virtual Error reload(bool p_keep_state = false) override;

#ifdef TOOLS_ENABLED
	virtual StringName get_doc_class_name() const override { return StringName(); }
	virtual Vector<DocData::ClassDoc> get_documentation() const override { return Vector<DocData::ClassDoc>(); }
	virtual String get_class_icon_path() const override { return String(); }
#endif

	virtual bool has_method(const StringName &p_method) const override { return methods.has(p_method); }
	virtual MethodInfo get_method_info(const StringName &p_method) const override;

	virtual bool is_tool() const override { return tool; }
	virtual bool is_script_valid() const override { return valid; }
	virtual bool is_abstract() const override { return false; }

	virtual ScriptLanguage *get_language() const override;

	virtual bool has_script_signal(const StringName &p_signal) const override;
	virtual void get_script_signal_list(List<MethodInfo> *r_signals) const override;

	virtual bool get_property_default_value(const StringName &p_property, Variant &r_value) const override;

	virtual void update_exports() override;
	virtual void get_script_method_list(List<MethodInfo> *r_list) const override;
	virtual void get_script_property_list(List<PropertyInfo> *r_list) const override;
	virtual int get_member_line(const StringName &p_member) const override;
	virtual void get_constants(HashMap<StringName, Variant> *r_constants) override;
	virtual void get_members(HashSet<StringName> *r_members) override;

	virtual const Variant get_rpc_config() const override { return Variant(); }

	HemlockScript() {}
	~HemlockScript();
};

class HemlockScriptInstance : public ScriptInstance {
	friend class HemlockScript;

	Object *owner = nullptr;
	Ref<HemlockScript> script;
	HmlInstance *instance = nullptr;

public:
	virtual bool set(const StringName &p_name, const Variant &p_value) override;
	virtual bool get(const StringName &p_name, Variant &r_ret) const override;
	virtual void get_property_list(List<PropertyInfo> *r_properties) const override;
	virtual Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid = nullptr) const override;
	virtual void validate_property(PropertyInfo &p_property) const override {}

	virtual bool property_can_revert(const StringName &p_name) const override;
	virtual bool property_get_revert(const StringName &p_name, Variant &r_ret) const override;

	virtual Object *get_owner() override { return owner; }

	virtual void get_method_list(List<MethodInfo> *r_list) const override;
	virtual bool has_method(const StringName &p_method) const override;
	virtual Variant callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) override;
	virtual void notification(int p_notification, bool p_reversed = false) override;

	virtual Script *get_script() const override { return script.ptr(); }
	virtual ScriptLanguage *get_language() override;

	virtual const Variant get_rpc_config() const override { return Variant(); }

	~HemlockScriptInstance();
};

#ifdef TOOLS_ENABLED
class HemlockEditorLanguage : public EditorLanguage {
public:
	virtual bool validate(const String &p_code, const String &p_path, List<ScriptError> *r_errors, List<Warning> *r_warnings, List<String> *r_functions, HashSet<int> *r_safe_lines) const override;
	virtual int32_t find_function(const String &p_function, const String &p_code) const override;
};
#endif

class HemlockLanguage : public ScriptLanguage {
	GDSOFTCLASS(HemlockLanguage, ScriptLanguage);

	static HemlockLanguage *singleton;

	HmlVM *vm = nullptr;
	// Recursive: Hemlock calls the engine, which can call back into Hemlock
	mutable Mutex lock;

#ifdef TOOLS_ENABLED
	HemlockEditorLanguage editor_language;
#endif

public:
	static HemlockLanguage *get_singleton() { return singleton; }

	// The VM, under `lock` (callers take a MutexLock first)
	HmlVM *get_vm() const { return vm; }
	Mutex &get_lock() const { return lock; }

	// Print the VM's last error through Godot's error channel
	void report_error(const String &p_context) const;

	virtual String get_name() const override { return "Hemlock"; }

	virtual void init() override;
	virtual String get_type() const override { return "HemlockScript"; }
	virtual String get_extension() const override { return "hml"; }
	virtual void finish() override;

#ifdef TOOLS_ENABLED
	virtual EditorLanguage *get_editor_language() override { return &editor_language; }
#endif

	virtual Vector<String> get_reserved_words() const override;
	virtual bool is_control_flow_keyword(const String &p_keyword) const override;
	virtual Vector<String> get_comment_delimiters() const override;
	virtual Vector<String> get_doc_comment_delimiters() const override;
	virtual Vector<String> get_string_delimiters() const override;
	virtual Ref<Script> make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const override;
	virtual Vector<ScriptTemplate> get_built_in_templates(const StringName &p_object) override;
	virtual bool is_using_templates() override { return true; }
	virtual bool supports_builtin_mode() const override { return false; }
	virtual String make_function(const String &p_class, const String &p_name, const PackedStringArray &p_args) const override;
	virtual void add_global_constant(const StringName &p_variable, const Variant &p_value) override {}

	virtual String debug_get_error() const override { return String(); }
	virtual int debug_get_stack_level_count() const override { return 0; }
	virtual int debug_get_stack_level_line(int p_level) const override { return 0; }
	virtual String debug_get_stack_level_function(int p_level) const override { return String(); }
	virtual String debug_get_stack_level_source(int p_level) const override { return String(); }
	virtual void debug_get_stack_level_locals(int p_level, List<String> *r_locals, List<Variant> *r_values, int p_max_subitems = -1, int p_max_depth = -1) override {}
	virtual void debug_get_stack_level_members(int p_level, List<String> *r_members, List<Variant> *r_values, int p_max_subitems = -1, int p_max_depth = -1) override {}
	virtual void debug_get_globals(List<String> *r_globals, List<Variant> *r_values, int p_max_subitems = -1, int p_max_depth = -1) override {}
	virtual String debug_parse_stack_level_expression(int p_level, const String &p_expression, int p_max_subitems = -1, int p_max_depth = -1) override { return String(); }

	virtual void reload_all_scripts() override;
	virtual void reload_scripts(const Array &p_scripts) override;
	virtual void reload_tool_script(const Ref<Script> &p_script) override;

	virtual void get_public_functions(List<MethodInfo> *r_functions) const override {}
	virtual void get_public_constants(List<Pair<String, Variant>> *r_constants) const override {}
	virtual void get_public_annotations(List<MethodInfo> *r_annotations) const override {}

	virtual void profiling_start() override {}
	virtual void profiling_stop() override {}
	virtual void profiling_set_save_native_calls(bool p_enable) override {}
	virtual int profiling_get_accumulated_data(ProfilingInfo *r_info_arr, int p_info_max) override { return 0; }
	virtual int profiling_get_frame_data(ProfilingInfo *r_info_arr, int p_info_max) override { return 0; }

	HemlockLanguage();
	~HemlockLanguage();
};

class ResourceFormatLoaderHemlock : public ResourceFormatLoader {
	GDSOFTCLASS(ResourceFormatLoaderHemlock, ResourceFormatLoader);

public:
	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override;
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual bool handles_type(const String &p_type) const override;
	virtual String get_resource_type(const String &p_path) const override;
};

class ResourceFormatSaverHemlock : public ResourceFormatSaver {
	GDSOFTCLASS(ResourceFormatSaverHemlock, ResourceFormatSaver);

public:
	virtual Error save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags = 0) override;
	virtual void get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const override;
	virtual bool recognize(const Ref<Resource> &p_resource) const override;
};

// hemlock_godot_api.cpp: the natives and prelude every script sees
void hemlock_install_godot_api(HmlVM *p_vm);
void hemlock_release_godot_api(); // drop objects scripts kept (before the VM goes)
