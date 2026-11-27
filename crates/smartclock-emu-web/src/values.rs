//! What JavaScript passes, checked before it is used: the files, a `Map`
//! or an object of `Uint8Array`s, of this realm or another; a body's
//! bytes.
//!
//! Lifted from mc683xx's JavaScript bindings (`crates/wasm/src/board.rs`,
//! <https://github.com/charlieh0tel/mc683xx>), Copyright (c) 2026
//! Christopher Hoover, MIT OR Apache-2.0, whose MIT license reads:
//!
//!     Permission is hereby granted, free of charge, to any person
//!     obtaining a copy of this software and associated documentation
//!     files (the "Software"), to deal in the Software without
//!     restriction, including without limitation the rights to use,
//!     copy, modify, merge, publish, distribute, sublicense, and/or sell
//!     copies of the Software, and to permit persons to whom the
//!     Software is furnished to do so, subject to the following
//!     conditions:
//!
//!     The above copyright notice and this permission notice shall be
//!     included in all copies or substantial portions of the Software.
//!
//!     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//!     EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
//!     OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
//!     NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
//!     HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
//!     WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//!     FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
//!     OTHER DEALINGS IN THE SOFTWARE.

use std::path::Path;

use js_sys::Array;
use js_sys::Function;
use js_sys::Map;
use js_sys::Object;
use js_sys::Reflect;
use js_sys::Symbol;
use js_sys::Uint8Array;
use smartclock_emu::files::InMemory;
use wasm_bindgen::JsCast;
use wasm_bindgen::JsValue;

use crate::error::Error;

/// The files in `files`, a `Map` or an object from a path to a
/// `Uint8Array`; none for `undefined` or `null`.
pub(crate) fn files_of(files: &JsValue) -> Result<InMemory, Error> {
    let entries = if files.is_undefined() || files.is_null() {
        return Ok(InMemory::default());
    } else if let Some(map) = map(files) {
        Array::from(&map.entries())
    } else if files.is_object() {
        Object::entries(files.unchecked_ref())
    } else {
        return Err(Error::Files);
    };
    entries
        .iter()
        .try_fold(InMemory::default(), |files, entry| {
            let entry: Array = entry.unchecked_into();
            let path = entry.get(0);
            let path = path
                .as_string()
                .ok_or_else(|| Error::FilePath(described(&path)))?;
            let bytes = entry.get(1);
            let bytes = uint8_array(&bytes).ok_or_else(|| Error::FileBytes(path.clone()))?;
            Ok(files.with(Path::new(&path), bytes.to_vec()))
        })
}

/// The bytes of `body`, a `Uint8Array`; none for `undefined` or `null`.
pub(crate) fn body_of(body: &JsValue) -> Result<Vec<u8>, Error> {
    if body.is_undefined() || body.is_null() {
        return Ok(Vec::new());
    }
    uint8_array(body)
        .map(Uint8Array::to_vec)
        .ok_or_else(|| Error::Body(described(body)))
}

/// `value` if it is a `Uint8Array`, from this realm or another (an
/// iframe's, a `vm` context's), where `instanceof`, which compares
/// constructors, fails: by the getter of
/// `%TypedArray%.prototype[%Symbol.toStringTag%]` (ECMA-262, Properties
/// of the `%TypedArray%` Intrinsic Object), the name of a typed array
/// of any realm, undefined for anything else.
fn uint8_array(value: &JsValue) -> Option<&Uint8Array> {
    let typed_array =
        Object::get_prototype_of(&Object::get_prototype_of(&Uint8Array::new_with_length(0)));
    let name = slot_getter(&typed_array, &Symbol::to_string_tag(), value)?;
    (name.as_string()? == "Uint8Array").then(|| value.unchecked_ref())
}

/// `value` if it is a `Map`, of any realm, as `uint8_array`: by the
/// getter `Map.prototype.size` (ECMA-262, Properties of the Map
/// Prototype Object), which throws for anything else.
fn map(value: &JsValue) -> Option<&Map> {
    let prototype = Object::get_prototype_of(&Map::new());
    slot_getter(&prototype, &JsValue::from_str("size"), value)?;
    Some(value.unchecked_ref())
}

/// `prototype`'s own getter `key` called on `value`, `None` if it
/// throws: a built-in's getter checks an internal slot of `value`, which
/// any realm's object of its kind has.
fn slot_getter(prototype: &Object, key: &JsValue, value: &JsValue) -> Option<JsValue> {
    let descriptor = Object::get_own_property_descriptor(prototype, key);
    let getter = Reflect::get(&descriptor, &JsValue::from_str("get")).ok()?;
    getter.dyn_ref::<Function>()?.call0(value).ok()
}

/// What `value` is, for a message: `null`, `undefined`, a primitive's
/// type (`a string`, `a number`), or an object's or a function's tag (`an
/// Array`, `an ArrayBuffer`, `an Object`), as
/// `Object.prototype.toString` gives it.
fn described(value: &JsValue) -> String {
    if value.is_null() {
        return "null".to_owned();
    }
    if value.is_undefined() {
        return "undefined".to_owned();
    }
    let kind = if value.is_object() || value.is_function() {
        tag(value).unwrap_or_else(|| "object".to_owned())
    } else {
        value.js_typeof().as_string().unwrap_or_default()
    };
    let article = if kind.starts_with(VOWELS) { "an" } else { "a" };
    format!("{article} {kind}")
}

/// Letters that take `an`, in `described`.
const VOWELS: [char; 10] = ['A', 'E', 'I', 'O', 'U', 'a', 'e', 'i', 'o', 'u'];

/// What `Object.prototype.toString` gives an object, `[object Array]`,
/// around its tag.
const TAG_PREFIX: &str = "[object ";
/// See `TAG_PREFIX`.
const TAG_SUFFIX: &str = "]";

/// `value`'s tag (`Array`), as `Object.prototype.toString` gives it,
/// `None` if it throws (a revoked proxy).
fn tag(value: &JsValue) -> Option<String> {
    let prototype = Object::get_prototype_of(&Object::new());
    let to_string = Reflect::get(&prototype, &JsValue::from_str("toString")).ok()?;
    let text = to_string
        .dyn_ref::<Function>()?
        .call0(value)
        .ok()?
        .as_string()?;
    Some(
        text.strip_prefix(TAG_PREFIX)?
            .strip_suffix(TAG_SUFFIX)?
            .to_owned(),
    )
}
