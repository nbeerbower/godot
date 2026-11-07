/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "hemlock_script.h"

#include "core/object/class_db.h"

static HemlockLanguage *hemlock_language = nullptr;
static Ref<ResourceFormatLoaderHemlock> hemlock_loader;
static Ref<ResourceFormatSaverHemlock> hemlock_saver;

void initialize_hemlock_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}
	GDREGISTER_CLASS(HemlockScript);

	hemlock_language = memnew(HemlockLanguage);
	ScriptServer::register_language(hemlock_language);

	hemlock_loader.instantiate();
	ResourceLoader::add_resource_format_loader(hemlock_loader);

	hemlock_saver.instantiate();
	ResourceSaver::add_resource_format_saver(hemlock_saver);
}

void uninitialize_hemlock_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}
	ScriptServer::unregister_language(hemlock_language);

	ResourceLoader::remove_resource_format_loader(hemlock_loader);
	hemlock_loader.unref();

	ResourceSaver::remove_resource_format_saver(hemlock_saver);
	hemlock_saver.unref();

	memdelete(hemlock_language);
	hemlock_language = nullptr;
}
