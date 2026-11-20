//! The little HTTP server the exporter and the web view share, on
//! `tiny_http`, behind the `http` feature.
//!
//! Not a web framework: it hands the caller a request's target, or its
//! method, target and body, and writes back a status, a media type and
//! a body.  Shared rather than copied because the copies had drifted
//! once -- one sent `Cache-Control`, the other did not.
//!
//! `tiny_http` parses the request: the line, every header, and requests
//! that arrive in pieces.  The in-house server it replaced read the
//! request line alone and closed, so a client whose headers were still
//! arriving was reset.  That server also capped connections, timed out
//! slow clients and capped the request's size; `tiny_http` has no option
//! for any of the three, and they went with it (`PLAN.md`).

use std::borrow::Cow;
use std::io;
use std::io::Read as _;
use std::net::TcpListener;
use std::ops::ControlFlow;
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use tiny_http::Header;
use tiny_http::Server;

/// Sent with every answer: each is the state of the moment, and a
/// cached one is a stale reading shown as current.
const CACHE_CONTROL: (&str, &str) = ("Cache-Control", "no-store");
/// The media type of the answers this module writes itself.
const TEXT: &str = "text/plain; charset=utf-8";
/// The most of a request's body read: past it the request is refused.
/// No caller takes more than a line.
const BODY_LIMIT: u64 = 64 * 1024;
/// How long the server waits before it starts accepting again on a
/// listener that failed: long enough that a failure that lasts, no file
/// descriptor free, is not a busy loop, short enough that a page
/// polling it hardly notices.
const ACCEPT_RETRY: Duration = Duration::from_millis(100);

/// Why the server could not start, or start again.
#[derive(Debug, thiserror::Error)]
pub enum ServeError {
    /// The address could not be bound: taken, not this host's, or not
    /// permitted.
    #[error("binding {listen}: {source}")]
    Bind {
        /// The address asked for.
        listen: String,
        /// What the bind said.
        #[source]
        source: std::io::Error,
    },
    /// `tiny_http` would not serve the listener bound, or the listener
    /// could not be shared with a server started again.
    #[error("serving: {0}")]
    Start(#[source] Box<dyn std::error::Error + Send + Sync>),
}

/// An answer's status.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Status {
    /// 200: here it is.
    Ok,
    /// 400: the request asked for nothing served.
    BadRequest,
    /// 404: nothing is served at that path.
    NotFound,
    /// 405: something is served at that path, but not to its method.
    MethodNotAllowed,
    /// 409 Conflict: the request conflicts with the current state of
    /// what it addresses, and could succeed once that state changes.
    Conflict,
    /// 413: its body is past what is read.
    TooLarge,
    /// 503 Service Unavailable: the server cannot handle the request
    /// now, as when what it needs is not ready, but may later.
    Unavailable,
}

impl Status {
    /// The status code on the wire.
    #[must_use]
    pub fn code(self) -> u16 {
        match self {
            Self::Ok => 200,
            Self::BadRequest => 400,
            Self::NotFound => 404,
            Self::MethodNotAllowed => 405,
            Self::Conflict => 409,
            Self::TooLarge => 413,
            Self::Unavailable => 503,
        }
    }
}

/// What the caller wants sent back.
#[derive(Debug)]
pub struct Response {
    /// The status.
    pub status: Status,
    /// The media type, including any charset.
    pub kind: &'static str,
    /// The body.
    pub body: String,
    /// The methods the path takes, `Allow`'s value, sent with a 405.
    allow: Option<&'static str>,
}

impl Response {
    /// A successful reply.
    pub fn ok(kind: &'static str, body: String) -> Self {
        Self {
            status: Status::Ok,
            kind,
            body,
            allow: None,
        }
    }

    /// A plain-text reply of `status`.
    pub fn text(status: Status, body: impl Into<String>) -> Self {
        Self {
            status,
            kind: TEXT,
            body: body.into(),
            allow: None,
        }
    }

    /// Nothing is served at that path.
    pub fn not_found() -> Self {
        Self::text(Status::NotFound, "not found\n")
    }

    /// The path is served, but only to `allow`, its methods as `Allow`
    /// lists them: `GET, HEAD` or `POST`.
    pub fn method_not_allowed(allow: &'static str) -> Self {
        Self {
            allow: Some(allow),
            ..Self::text(Status::MethodNotAllowed, format!("{allow} only\n"))
        }
    }

    /// The methods the path takes, `Allow`'s value, with a 405.
    #[must_use]
    pub fn allow(&self) -> Option<&'static str> {
        self.allow
    }

    /// The answer as `tiny_http` writes it.
    fn into_tiny(self) -> tiny_http::Response<std::io::Cursor<Vec<u8>>> {
        let allow = self.allow.map(|allow| ("Allow", allow));
        let headers = [("Content-Type", self.kind), CACHE_CONTROL]
            .into_iter()
            .chain(allow)
            // Only a field or value that is not ASCII is refused, and
            // these are all constants.
            .filter_map(|(field, value)| Header::from_bytes(field, value).ok());
        headers.fold(
            tiny_http::Response::from_string(self.body).with_status_code(self.status.code()),
            tiny_http::Response::with_header,
        )
    }
}

/// A request's method, as far as a caller tells them apart.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Method {
    /// GET, or HEAD.
    Get,
    /// POST.
    Post,
    /// Any other.
    Other,
}

/// A request as `serve_requests_on` hands it to the caller.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Request {
    /// Its method.
    pub method: Method,
    /// Its target: the path and any query string.
    pub target: String,
    /// Its body, at most `BODY_LIMIT` bytes.
    pub body: Vec<u8>,
}

/// The `key=value` pairs of a query string, in order, each key and
/// value percent-decoded, a `+` decoded as a space.  A pair with no `=`
/// is skipped, so that a bare `?receiver` reads as no receiver named
/// rather than one named with an empty serial.  An escape that is not
/// well formed is kept as written.
pub fn pairs(query: &str) -> impl Iterator<Item = (Cow<'_, str>, Cow<'_, str>)> {
    query
        .split('&')
        .filter(|pair| pair.contains('='))
        .flat_map(|pair| form_urlencoded::parse(pair.as_bytes()))
}

/// The decoded value of the first `key=` in a query string.
pub fn value(query: &str, key: &str) -> Option<String> {
    pairs(query)
        .find(|(k, _)| k == key)
        .map(|(_, value)| value.into_owned())
}

/// The listener `serve` would serve on `listen`, bound now, so that a
/// caller serving it on a thread of its own hears of a bind that fails
/// before the thread starts, and can say where it bound port 0.
pub fn bind(listen: &str) -> Result<TcpListener, ServeError> {
    TcpListener::bind(listen).map_err(|source| ServeError::Bind {
        listen: listen.to_owned(),
        source,
    })
}

/// Serve `answer` on `listen` until the process ends or the listener
/// fails.
///
/// `answer` is given the request's target, its path and any query
/// string, and returns what to send.  It runs on a thread of its own
/// per request, so it may block.
pub fn serve<F>(listen: &str, answer: F) -> Result<(), ServeError>
where
    F: Fn(&str) -> Response + Send + Sync + 'static,
{
    serve_on(bind(listen)?, answer)
}

/// Serve `answer` on `listener`, as `serve` does.
fn serve_on<F>(listener: TcpListener, answer: F) -> Result<(), ServeError>
where
    F: Fn(&str) -> Response + Send + Sync + 'static,
{
    serve_requests_on(listener, move |request| answer(&request.target))
}

/// Serve `answer` on `listener`, as `serve_on` does, `answer` given the
/// whole request: its method, target and body.  A body past
/// `BODY_LIMIT` is refused 413 without asking `answer`.
///
/// `tiny_http` stops accepting at its listener's first error, an accept
/// that fails for want of a file descriptor among them, so a server
/// whose accepting fails is replaced by one on the same listener,
/// `ACCEPT_RETRY` later, rather than the page going dark for good.
pub fn serve_requests_on<F>(listener: TcpListener, answer: F) -> Result<(), ServeError>
where
    F: Fn(&Request) -> Response + Send + Sync + 'static,
{
    let answer = Arc::new(answer);
    keep_accepting(
        || {
            let shared = listener
                .try_clone()
                .map_err(|error| ServeError::Start(error.into()))?;
            Server::from_listener(shared, None).map_err(ServeError::Start)
        },
        Server::recv,
        |request| {
            answer_on_a_thread(request, Arc::clone(&answer));
            ControlFlow::Continue(())
        },
        ACCEPT_RETRY,
    )
}

/// What each server `start` gives accepts, by `accept`, handed to
/// `handle`; a server whose `accept` fails is dropped, and after `retry`
/// another started.  Until `start` fails, or `handle` says to stop.
fn keep_accepting<S, T>(
    mut start: impl FnMut() -> Result<S, ServeError>,
    mut accept: impl FnMut(&S) -> io::Result<T>,
    mut handle: impl FnMut(T) -> ControlFlow<()>,
    retry: Duration,
) -> Result<(), ServeError> {
    loop {
        let server = start()?;
        let failed = loop {
            match accept(&server) {
                Ok(accepted) => {
                    if handle(accepted).is_break() {
                        return Ok(());
                    }
                }
                Err(error) => break error,
            }
        };
        eprintln!("accepting failed, serving again: {failed}");
        drop(server);
        thread::sleep(retry);
    }
}

/// `request` answered by `answer` on a thread of its own.
fn answer_on_a_thread<F>(mut request: tiny_http::Request, answer: Arc<F>)
where
    F: Fn(&Request) -> Response + Send + Sync + 'static,
{
    let spawned = thread::Builder::new()
        .name("http".to_owned())
        .spawn(move || {
            let response = match read(&mut request) {
                Ok(read) => answer(&read),
                Err(refusal) => refusal,
            };
            // A client that left before its answer is not the
            // server's failure.
            let _ = request.respond(response.into_tiny());
        });
    if let Err(e) = spawned {
        eprintln!("could not serve a request: {e}");
    }
}

/// `request` as the caller is given it, or the refusal of one whose
/// body is past `BODY_LIMIT` or cannot be read.
fn read(request: &mut tiny_http::Request) -> Result<Request, Response> {
    let method = match request.method() {
        tiny_http::Method::Get | tiny_http::Method::Head => Method::Get,
        tiny_http::Method::Post => Method::Post,
        _ => Method::Other,
    };
    let mut body = Vec::new();
    request
        .as_reader()
        .take(BODY_LIMIT + 1)
        .read_to_end(&mut body)
        .map_err(|_| Response::text(Status::BadRequest, "the body could not be read\n"))?;
    if body.len() as u64 > BODY_LIMIT {
        return Err(Response::text(
            Status::TooLarge,
            format!("the body is past {BODY_LIMIT} bytes\n"),
        ));
    }
    Ok(Request {
        method,
        target: request.url().to_owned(),
        body,
    })
}

#[cfg(test)]
mod tests {
    use std::io::Read as _;
    use std::io::Write as _;
    use std::net::TcpStream;
    use std::thread;
    use std::time::Duration;

    use std::cell::RefCell;
    use std::collections::VecDeque;
    use std::io;
    use std::ops::ControlFlow;

    use super::BODY_LIMIT;
    use super::Method;
    use super::Response;
    use super::ServeError;
    use super::Status;
    use super::bind;
    use super::keep_accepting;
    use super::pairs;
    use super::serve_on;
    use super::serve_requests_on;
    use super::value;

    /// What a browser actually sends for a list.
    ///
    /// `URLSearchParams` encodes the separator, so a server that does
    /// not decode sees one column named `efc_percent%2Ctemperature_c`.
    #[test]
    fn a_percent_encoded_list_decodes() {
        assert_eq!(
            value("columns=efc_percent%2Ctemperature_c", "columns").as_deref(),
            Some("efc_percent,temperature_c")
        );
    }

    #[test]
    fn a_plus_is_a_space_and_a_percent_2b_is_a_plus() {
        assert_eq!(value("a=one+two", "a").as_deref(), Some("one two"));
        assert_eq!(value("a=one%2Btwo", "a").as_deref(), Some("one+two"));
    }

    /// A malformed escape is kept rather than refused: a stray percent
    /// in a value is not worth failing a request over.
    #[test]
    fn a_broken_escape_survives() {
        assert_eq!(value("a=100%", "a").as_deref(), Some("100%"));
        assert_eq!(value("a=%zz", "a").as_deref(), Some("%zz"));
        assert_eq!(value("a=%2", "a").as_deref(), Some("%2"));
    }

    #[test]
    fn multibyte_characters_survive_the_round_trip() {
        assert_eq!(value("a=%C2%B5s", "a").as_deref(), Some("\u{b5}s"));
    }

    #[test]
    fn pairs_are_split_and_decoded_and_the_first_value_wins() {
        let query = "receiver=3625A01487&columns=a%2Cb&bare&receiver=other";
        assert_eq!(value(query, "receiver").as_deref(), Some("3625A01487"));
        assert_eq!(value(query, "columns").as_deref(), Some("a,b"));
        assert_eq!(value(query, "bare"), None);
        assert_eq!(value(query, "missing"), None);
        assert_eq!(pairs(query).count(), 3);
    }

    /// A request whose headers arrive after its request line, as a
    /// client writing its line and then its headers sends one, is
    /// answered.  The in-house server this replaced read the line,
    /// answered and closed, and the headers it never read reset the
    /// connection.
    #[test]
    fn a_request_in_two_writes_is_answered() {
        let listener = bind("127.0.0.1:0").expect("bind");
        let address = listener.local_addr().expect("an address");
        thread::spawn(move || {
            serve_on(listener, |target| {
                Response::ok("text/plain", target.to_owned())
            })
        });
        let mut stream = TcpStream::connect(address).expect("connect");
        stream
            .write_all(b"GET /state?t=1 HTTP/1.1\r\n")
            .expect("the line");
        thread::sleep(Duration::from_millis(200));
        stream
            .write_all(b"Host: localhost\r\nConnection: close\r\n\r\n")
            .expect("the headers");
        let mut answer = String::new();
        stream.read_to_string(&mut answer).expect("an answer");
        let (head, body) = answer.split_once("\r\n\r\n").expect("a head and a body");
        assert!(head.starts_with("HTTP/1.1 200 OK\r\n"), "{head}");
        assert!(head.contains("\r\nContent-Type: text/plain\r\n"), "{head}");
        assert!(head.contains("\r\nCache-Control: no-store\r\n"), "{head}");
        assert_eq!(body, "/state?t=1");
    }

    /// A POST's body reaches the caller whole, with its method.
    #[test]
    fn a_posts_body_reaches_the_caller() {
        let listener = bind("127.0.0.1:0").expect("bind");
        let address = listener.local_addr().expect("an address");
        thread::spawn(move || {
            serve_requests_on(listener, |request| {
                let method = if request.method == Method::Post {
                    "post"
                } else {
                    "other"
                };
                Response::ok(
                    "text/plain",
                    format!(
                        "{method} {} {}",
                        request.target,
                        String::from_utf8_lossy(&request.body)
                    ),
                )
            })
        });
        let mut stream = TcpStream::connect(address).expect("connect");
        stream
            .write_all(
                b"POST /console?client=a HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\nConnection: close\r\n\r\n*IDN?",
            )
            .expect("the request");
        let mut answer = String::new();
        stream.read_to_string(&mut answer).expect("an answer");
        let (head, body) = answer.split_once("\r\n\r\n").expect("a head and a body");
        assert!(head.starts_with("HTTP/1.1 200 OK\r\n"), "{head}");
        assert_eq!(body, "post /console?client=a *IDN?");
    }

    /// A body past the limit is refused as too large, 413, not as a bad
    /// request.
    #[test]
    fn a_body_past_the_limit_is_too_large() {
        let listener = bind("127.0.0.1:0").expect("bind");
        let address = listener.local_addr().expect("an address");
        thread::spawn(move || {
            serve_requests_on(listener, |_| Response::ok("text/plain", String::new()))
        });
        let length = BODY_LIMIT + 1;
        let mut stream = TcpStream::connect(address).expect("connect");
        stream
            .write_all(
                format!(
                    "POST /console HTTP/1.1\r\nHost: localhost\r\nContent-Length: {length}\r\nConnection: close\r\n\r\n"
                )
                .as_bytes(),
            )
            .expect("the head");
        stream
            .write_all(&vec![b'x'; usize::try_from(length).expect("a length")])
            .expect("the body");
        let mut answer = String::new();
        stream.read_to_string(&mut answer).expect("an answer");
        assert!(answer.starts_with("HTTP/1.1 413 "), "{answer}");
    }

    /// A server whose accepting fails is replaced, and what the next
    /// accepts is handled: the server serves on rather than stopping at
    /// its listener's first error.
    #[test]
    fn a_failed_accept_starts_another_server() {
        // what each server started accepts, in turn
        let mut servers: VecDeque<Vec<io::Result<u32>>> = VecDeque::from([
            vec![Ok(1), Err(io::Error::other("no file descriptor"))],
            vec![Ok(2), Ok(3)],
        ]);
        let mut started = 0;
        let mut handled = Vec::new();
        let served = keep_accepting(
            || {
                started += 1;
                servers
                    .pop_front()
                    .map(|accepts| RefCell::new(VecDeque::from(accepts)))
                    .ok_or_else(|| ServeError::Start("no more servers".into()))
            },
            |server| {
                server
                    .borrow_mut()
                    .pop_front()
                    .unwrap_or_else(|| Err(io::Error::other("nothing more")))
            },
            |accepted| {
                handled.push(accepted);
                if accepted == 3 {
                    ControlFlow::Break(())
                } else {
                    ControlFlow::Continue(())
                }
            },
            Duration::ZERO,
        );
        assert!(served.is_ok(), "{served:?}");
        assert_eq!(started, 2);
        assert_eq!(handled, [1, 2, 3]);
    }

    /// A 405 names the methods the path takes.
    #[test]
    fn a_method_not_allowed_says_which_are() {
        let listener = bind("127.0.0.1:0").expect("bind");
        let address = listener.local_addr().expect("an address");
        thread::spawn(move || {
            serve_requests_on(listener, |_| Response::method_not_allowed("POST"))
        });
        let mut stream = TcpStream::connect(address).expect("connect");
        stream
            .write_all(b"GET /key HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            .expect("the request");
        let mut answer = String::new();
        stream.read_to_string(&mut answer).expect("an answer");
        let (head, body) = answer.split_once("\r\n\r\n").expect("a head and a body");
        assert!(head.starts_with("HTTP/1.1 405 "), "{head}");
        assert!(head.contains("\r\nAllow: POST\r\n"), "{head}");
        assert_eq!(body, "POST only\n");
    }

    /// A 409 and a 503 go out with their codes and reason phrases, and
    /// their bodies.
    #[test]
    fn a_conflict_and_an_unavailable_are_sent_as_such() {
        for (status, line) in [
            (Status::Conflict, "HTTP/1.1 409 Conflict\r\n"),
            (Status::Unavailable, "HTTP/1.1 503 Service Unavailable\r\n"),
        ] {
            let listener = bind("127.0.0.1:0").expect("bind");
            let address = listener.local_addr().expect("an address");
            thread::spawn(move || {
                serve_requests_on(listener, move |_| Response::text(status, "not now\n"))
            });
            let mut stream = TcpStream::connect(address).expect("connect");
            stream
                .write_all(b"POST /key HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                .expect("the request");
            let mut answer = String::new();
            stream.read_to_string(&mut answer).expect("an answer");
            let (head, body) = answer.split_once("\r\n\r\n").expect("a head and a body");
            assert!(head.starts_with(line), "{head}");
            assert_eq!(body, "not now\n");
        }
    }
}
