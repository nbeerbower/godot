/**************************************************************************/
/*  hemlock_variant.h                                                     */
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

#include "core/variant/variant.h"

#include <hemlock_embed.h>

// Godot Variant <-> Hemlock embedding value.
//
// Scalars, strings, arrays and dictionaries map structurally. Objects travel
// as handles (their ObjectID); scripts see them through the prelude's
// wrapper (`owner.get("position")`, ...). Math types become typed Hemlock
// objects whose `typeof()` is the Godot type name, e.g. Vector2 -> an object
// of type "Vector2" with fields x and y, and convert back by type name.
// Anything else crosses as its string form (with a warning when it goes the
// other way and cannot be rebuilt).
namespace HemlockVariant {

HmlEmbedValue to_hemlock(const Variant &p_value);
Variant to_variant(const HmlEmbedValue &p_value);

// Godot type an annotation such as "f64" or "Vector2" stands for, or NIL.
Variant::Type type_for_annotation(const char *p_annotation);

} // namespace HemlockVariant
