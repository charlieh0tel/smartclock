//! The emulator's JavaScript API, through wasm-bindgen: an `Emulator`
//! started from its arguments, its files and UTC now, stepped by
//! `frame` from `requestAnimationFrame`, the panel page's requests
//! answered by `respond`, and the EEPROM's cells and protection given
//! by `eeprom`;
//! `onPanic`, where a panic is reported.  The trace's lines go to the
//! console, `console.log`.

use std::cell::RefCell;
use std::sync::Once;
use std::time::Duration;

use js_sys::Function;
use smartclock::http::Method;
use smartclock::http::Request;
use smartclock::http::Response;
use smartclock_emu::mc683xx::KeptEeprom;
use smartclock_emu::mc683xx::Step;
use smartclock_emu::trace;
use wasm_bindgen::JsValue;
use wasm_bindgen::prelude::wasm_bindgen;

use crate::error::Error;
use crate::values::body_of;
use crate::values::files_of;

/// Milliseconds a second, for the page's clock readings.
const MILLISECONDS_PER_SECOND: f64 = 1000.0;

#[wasm_bindgen]
extern "C" {
    /// `console.log`.
    #[wasm_bindgen(js_namespace = console, js_name = log)]
    fn console_log(text: &str);
    /// `console.error`.
    #[wasm_bindgen(js_namespace = console, js_name = error)]
    fn console_error(text: &str);
}

thread_local! {
    /// What a panic is reported to, `onPanic`'s.
    static PANIC_REPORT: RefCell<Option<Function>> = const { RefCell::new(None) };
}

/// The panic hook, set once.
static PANIC_HOOK: Once = Once::new();
/// The trace's sink, set once.
static TRACE_SINK: Once = Once::new();

/// Each panic from now on written to the console, `console.error`, and
/// given to `report` as its message, a string, for the page to show: the
/// run is over, and the module is not to be called again.
#[wasm_bindgen(js_name = onPanic)]
pub fn on_panic(report: Function) {
    PANIC_REPORT.with(|kept| *kept.borrow_mut() = Some(report));
    PANIC_HOOK.call_once(|| {
        std::panic::set_hook(Box::new(|info| {
            let message = info.to_string();
            console_error(&message);
            PANIC_REPORT.with(|kept| {
                if let Some(report) = kept.borrow().as_ref() {
                    // nothing to report a failure to
                    let _ = report.call1(&JsValue::NULL, &JsValue::from_str(&message));
                }
            });
        }));
    });
}

/// The emulator, a run in the page.
#[wasm_bindgen]
#[derive(Debug)]
pub struct Emulator {
    /// The run.
    emulator: crate::emulator::Emulator,
}

#[wasm_bindgen]
impl Emulator {
    /// The run `args`, an array of strings, asks for, as the command
    /// line's without the program's name: the image by its path in
    /// `files`, `--eeprom-protected` (`true` or `false`), the EEPROM's
    /// protection a run before kept, and the options every host takes
    /// (`--model`, `--board`, `--gps-latitude`, ...).  `files` is a `Map`
    /// or an object from a path to a `Uint8Array`: the image, the board
    /// file and what it names (the EEPROM's image: the dump, or the
    /// cells a run before kept), a board file's
    /// `../` taken out as a file system would take it.  `now` is UTC, milliseconds since 1970 (`Date.now()`),
    /// the synthetic engine's start without `--gps-start`.
    ///
    /// # Errors
    ///
    /// Files of another shape, a time that is none, and as
    /// `emulator::Emulator::start`.
    #[wasm_bindgen(constructor)]
    pub fn new(
        args: Vec<String>,
        #[wasm_bindgen(
            unchecked_param_type = "Map<string, Uint8Array> | Record<string, Uint8Array>"
        )]
        files: &JsValue,
        now: f64,
    ) -> Result<Self, Error> {
        TRACE_SINK.call_once(|| {
            // a sink already given is the one the lines go to
            let _ = trace::set_sink(Box::new(|text| console_log(&text.to_string())));
        });
        let files = files_of(files)?;
        let now = utc(now)?;
        Ok(Self {
            emulator: crate::emulator::Emulator::start(&args, files, now)?,
        })
    }

    /// A frame of the run, to the board time due by the page's clock
    /// reading `wall`, milliseconds (`performance.now()`, or the time
    /// `requestAnimationFrame` gives), but no more than 50 ms of board
    /// time, so that a tab hidden a while drops the time it missed.
    /// The first frame runs nothing: it starts the pacing.
    ///
    /// # Errors
    ///
    /// A reading that is no time, or a board that stalled or halted.
    pub fn frame(&mut self, wall: f64) -> Result<Frame, Error> {
        Ok(self.emulator.frame(wall_of(wall)?)?.into())
    }

    /// The answer to the panel page's request: `method` (`GET`, `HEAD`,
    /// `POST`, or any other), `target`, its path and query, and `body`,
    /// a `Uint8Array` or nothing.
    ///
    /// # Errors
    ///
    /// A body that is not a `Uint8Array`.
    pub fn respond(
        &self,
        method: &str,
        target: &str,
        #[wasm_bindgen(unchecked_param_type = "Uint8Array | null | undefined")] body: &JsValue,
    ) -> Result<Answer, Error> {
        let request = Request {
            method: method_of(method),
            target: target.to_owned(),
            body: body_of(body)?,
        };
        Ok(self.emulator.respond(&request).into())
    }

    /// The EEPROM as it stands, its `cells` and whether it is
    /// `protected`, to keep and give a later run as the board file's
    /// EEPROM image and its `--eeprom-protected`; undefined once the run
    /// has failed.
    ///
    /// # Errors
    ///
    /// The EEPROM not on the board.
    pub fn eeprom(&mut self) -> Result<Option<Eeprom>, Error> {
        Ok(self.emulator.eeprom()?.map(|kept| Eeprom { kept }))
    }
}

/// The EEPROM as a run leaves it (Rust's `KeptEeprom`).
#[wasm_bindgen]
#[derive(Debug)]
pub struct Eeprom {
    /// Its cells and protection.
    kept: KeptEeprom,
}

#[wasm_bindgen]
impl Eeprom {
    /// Its cells, a `Uint8Array` of the part's 8 KiB.
    #[wasm_bindgen(getter)]
    #[must_use]
    pub fn cells(&self) -> Vec<u8> {
        self.kept.cells.clone()
    }

    /// Whether its software data protection is on.
    #[wasm_bindgen(getter)]
    #[must_use]
    pub fn protected(&self) -> bool {
        self.kept.protected
    }
}

/// What a frame left (Rust's `Step`).
#[wasm_bindgen]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Frame {
    /// Whether the run goes on.
    running: bool,
    /// How far the board is behind the page's clock, milliseconds.
    behind: f64,
}

#[wasm_bindgen]
impl Frame {
    /// Whether the run goes on: false once its time is up or the board
    /// stopped it.
    #[wasm_bindgen(getter)]
    #[must_use]
    pub fn running(&self) -> bool {
        self.running
    }

    /// How far the board is behind the page's clock, milliseconds: zero
    /// once it has caught up.
    #[wasm_bindgen(getter)]
    #[must_use]
    pub fn behind(&self) -> f64 {
        self.behind
    }
}

impl From<Step> for Frame {
    fn from(step: Step) -> Self {
        match step {
            Step::Running { behind } => Self {
                running: true,
                behind: behind.as_secs_f64() * MILLISECONDS_PER_SECOND,
            },
            Step::Ended => Self {
                running: false,
                behind: 0.0,
            },
        }
    }
}

/// An answer to the page (Rust's `Response`).
#[wasm_bindgen]
#[derive(Debug)]
pub struct Answer {
    /// The answer.
    response: Response,
}

#[wasm_bindgen]
impl Answer {
    /// The status code: 200, 400, 404, ...
    #[wasm_bindgen(getter)]
    #[must_use]
    pub fn status(&self) -> u16 {
        self.response.status.code()
    }

    /// The media type, `Content-Type`'s value.
    #[wasm_bindgen(getter)]
    #[must_use]
    pub fn kind(&self) -> String {
        self.response.kind.to_owned()
    }

    /// The body.
    #[wasm_bindgen(getter)]
    #[must_use]
    pub fn body(&self) -> String {
        self.response.body.clone()
    }

    /// The methods the path takes, `Allow`'s value, with a 405;
    /// undefined otherwise.
    #[wasm_bindgen(getter)]
    #[must_use]
    pub fn allow(&self) -> Option<String> {
        self.response.allow().map(str::to_owned)
    }
}

impl From<Response> for Answer {
    fn from(response: Response) -> Self {
        Self { response }
    }
}

/// `method` as the page's routes tell methods apart: `GET` and `HEAD`
/// alike, `POST`, any other; in any case, as HTTP's methods are not.
fn method_of(method: &str) -> Method {
    match method {
        "GET" | "HEAD" => Method::Get,
        "POST" => Method::Post,
        _ => Method::Other,
    }
}

/// The page's clock reading `wall`, milliseconds, as a time since its
/// origin.
fn wall_of(wall: f64) -> Result<Duration, Error> {
    Duration::try_from_secs_f64(wall / MILLISECONDS_PER_SECOND).map_err(|_| Error::Wall(wall))
}

/// UTC `milliseconds` since 1970.
fn utc(milliseconds: f64) -> Result<jiff::Timestamp, Error> {
    if !milliseconds.is_finite() {
        return Err(Error::Utc(milliseconds));
    }
    // a finite reading, whole milliseconds; jiff refuses one past its range
    jiff::Timestamp::from_millisecond(milliseconds as i64).map_err(|_| Error::Utc(milliseconds))
}

#[cfg(test)]
mod tests {
    use smartclock::http::Method;

    use super::method_of;
    use super::utc;
    use super::wall_of;

    #[test]
    fn a_method_is_its_case_and_head_is_a_get() {
        assert_eq!(method_of("GET"), Method::Get);
        assert_eq!(method_of("HEAD"), Method::Get);
        assert_eq!(method_of("POST"), Method::Post);
        assert_eq!(method_of("post"), Method::Other);
        assert_eq!(method_of("PUT"), Method::Other);
    }

    #[test]
    fn a_clock_reading_is_a_finite_time_not_before_its_origin() {
        assert_eq!(
            wall_of(1500.0).expect("a time"),
            std::time::Duration::from_millis(1500)
        );
        for wall in [-1.0, f64::NAN, f64::INFINITY] {
            assert!(wall_of(wall).is_err(), "{wall}");
        }
        assert!(utc(1_792_670_400_000.0).is_ok());
        assert!(utc(f64::NAN).is_err());
        assert!(utc(1e300).is_err());
    }
}
