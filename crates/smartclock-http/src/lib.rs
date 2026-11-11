//! The little HTTP server the exporter and the web view share.
//!
//! Not a web framework and not trying to be: it accepts a connection,
//! hands the caller a path, and writes back a status and a body.  That
//! is the whole of what either program needs, and an async runtime and
//! a routing crate would be a large dependency for it.
//!
//! It exists as a crate rather than as two copies because the two
//! copies had already drifted -- one sent `Cache-Control`, the other
//! did not -- and because the limits below are the kind of thing that
//! gets added to one server and forgotten in the other.  The daemon's
//! own socket server learned all three of them the hard way.

use std::io::BufRead;
use std::io::BufReader;
use std::io::Read as _;
use std::io::Write as _;
use std::net::Shutdown;
use std::net::TcpListener;
use std::net::TcpStream;
use std::ops::ControlFlow;
use std::sync::Arc;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;
use std::thread;
use std::time::Duration;
use std::time::Instant;

/// The longest request line served.
///
/// A request line is a method, a path and a version.  Without a cap,
/// `read_line` grows a string until the client sends a newline, which
/// a hostile one never does: on a local network that is memory filling
/// at wire speed from a single connection.
const MAX_REQUEST: u64 = 8192;

/// How long a client may take to send its request, or to read its
/// answer, before it is dropped.
///
/// Without these a connection that sends nothing holds its thread for
/// as long as the process runs, and a client that never reads holds one
/// in `write`.  Both are one line of shell to arrange.
///
/// The request's is a deadline for the whole of it, not a timeout per
/// read.  A timeout per read restarts with every byte, so a client
/// sending one byte every few seconds held a thread for as long as it
/// liked, and thirty-two of them held all of them.
const REQUEST_DEADLINE: Duration = Duration::from_secs(10);
const WRITE_TIMEOUT: Duration = Duration::from_secs(30);

/// How many requests may be in flight at once.
///
/// Each takes a thread.  A scrape and a page load are short, so this is
/// about surviving a flood rather than about throughput; past it,
/// clients are told the server is busy instead of the machine being
/// taken down.
const MAX_INFLIGHT: usize = 32;

/// The longest request body accepted, by its `Content-Length`: past
/// it the request is refused 413.  No caller takes a body, so one
/// within it is read and dropped, and the limit is there so that a
/// client sending a large one is told so rather than left writing.
const BODY_LIMIT: u64 = 64 * 1024;

/// How long the accept loop waits after a failed accept before it
/// accepts again.  An error that persists -- out of file descriptors,
/// say -- comes straight back, and without the pause the loop spun on
/// it; long enough that it costs nothing, short enough that a page
/// polling the server hardly notices.
const ACCEPT_RETRY: Duration = Duration::from_millis(100);

/// The media type of the server's own answers.
const TEXT: &str = "text/plain; charset=utf-8";

/// Why the server could not start.
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
}

/// What the caller wants sent back.
#[derive(Debug)]
pub struct Response {
    /// The status line, such as `200 OK`.
    pub status: &'static str,
    /// The media type, including any charset.
    pub kind: &'static str,
    /// The body.  Sent as bytes; `Content-Length` counts bytes, not
    /// characters, which is why this is a String rather than a &str.
    pub body: String,
}

impl Response {
    /// A successful reply.
    pub fn ok(kind: &'static str, body: String) -> Self {
        Self {
            status: "200 OK",
            kind,
            body,
        }
    }

    /// Nothing is served at that path.
    pub fn not_found() -> Self {
        Self::text("404 Not Found", "not found\n")
    }

    /// A plain-text reply, as the server's own refusals are.
    fn text(status: &'static str, body: &str) -> Self {
        Self {
            status,
            kind: TEXT,
            body: body.to_owned(),
        }
    }
}

/// The `key=value` pairs of a query string, in order, each value
/// percent-decoded.  A pair with no `=` is skipped.
pub fn pairs(query: &str) -> impl Iterator<Item = (&str, String)> {
    query
        .split('&')
        .filter_map(|pair| pair.split_once('='))
        .map(|(key, value)| (key, decode(value)))
}

/// The decoded value of the first `key=` in a query string.
pub fn value(query: &str, key: &str) -> Option<String> {
    pairs(query)
        .find(|(k, _)| *k == key)
        .map(|(_, value)| value)
}

/// Percent-decode one query-string value.
///
/// The parser here splits the raw target on `&` and `=` and did no
/// decoding at all, which was invisible while the only parameter that
/// mattered was a single column name with nothing to encode.  A list
/// broke it immediately: `URLSearchParams` writes the separator as
/// `%2C`, so the server was handed one column called
/// `efc_percent%2Ctemperature_c` and said, correctly, that it does not
/// serve it.
///
/// Bytes rather than chars, because a percent escape encodes a byte and
/// a multi-byte character arrives as several of them.  Anything that
/// is not a well-formed escape is kept as written: a stray `%` in a
/// value is not worth refusing a request over.
fn decode(value: &str) -> String {
    let bytes = value.as_bytes();
    let mut out: Vec<u8> = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'+' => {
                // A literal plus is `%2B`; a bare one is a space in
                // form encoding, and a space is not a column name.
                out.push(b' ');
                i += 1;
            }
            b'%' if i + 2 < bytes.len() => {
                let hex = std::str::from_utf8(&bytes[i + 1..i + 3]).ok();
                match hex.and_then(|h| u8::from_str_radix(h, 16).ok()) {
                    Some(byte) => {
                        out.push(byte);
                        i += 3;
                    }
                    None => {
                        out.push(bytes[i]);
                        i += 1;
                    }
                }
            }
            byte => {
                out.push(byte);
                i += 1;
            }
        }
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// One connected client, counted for as long as this is held.
///
/// A guard rather than a decrement at the end of the thread, so a
/// panic while answering cannot leak a slot.
struct Slot(Arc<AtomicUsize>);

impl Drop for Slot {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::Relaxed);
    }
}

/// Serve `answer` on `listen` until the process ends.
///
/// `answer` is given the request path, including any query string, and
/// returns what to send.  It runs on its own thread per request, so it
/// may block; the timeouts above bound how long a client can make it
/// wait, not how long it may take.
pub fn serve<F>(listen: &str, answer: F) -> Result<(), ServeError>
where
    F: Fn(&str) -> Response + Send + Sync + 'static,
{
    let listener = TcpListener::bind(listen).map_err(|source| ServeError::Bind {
        listen: listen.to_owned(),
        source,
    })?;
    let answer = Arc::new(answer);
    let inflight = Arc::new(AtomicUsize::new(0));

    keep_accepting(
        || listener.accept().map(|(stream, _)| stream),
        |stream| {
            dispatch(stream, &answer, &inflight);
            ControlFlow::Continue(())
        },
        ACCEPT_RETRY,
    );
    Ok(())
}

/// Each connection `accept` gives handed to `handle`, until `handle`
/// says to stop.  A failed accept is waited out for `retry` and then
/// accepting goes on; a run of failures is reported once.
fn keep_accepting<T>(
    mut accept: impl FnMut() -> std::io::Result<T>,
    mut handle: impl FnMut(T) -> ControlFlow<()>,
    retry: Duration,
) {
    let mut failing = false;
    loop {
        match accept() {
            Ok(accepted) => {
                failing = false;
                if handle(accepted).is_break() {
                    return;
                }
            }
            // One client failing to connect is not a reason to stop
            // serving the others.
            Err(e) => {
                if !failing {
                    eprintln!("rejected a connection: {e}");
                    failing = true;
                }
                thread::sleep(retry);
            }
        }
    }
}

/// `stream` answered on a thread of its own, or refused busy if
/// `MAX_INFLIGHT` are already being answered.
fn dispatch<F>(stream: TcpStream, answer: &Arc<F>, inflight: &Arc<AtomicUsize>)
where
    F: Fn(&str) -> Response + Send + Sync + 'static,
{
    if inflight.load(Ordering::Relaxed) >= MAX_INFLIGHT {
        // Answered rather than dropped: a bare reset reads as the
        // server having died.
        let _ = write_response(
            &stream,
            &Response::text("503 Service Unavailable", "busy\n"),
        );
        return;
    }
    inflight.fetch_add(1, Ordering::Relaxed);
    let slot = Slot(Arc::clone(inflight));
    let answer = Arc::clone(answer);
    let spawned = thread::Builder::new()
        .name("smartclock-http".to_owned())
        .spawn(move || {
            let _slot = slot;
            handle(&stream, answer.as_ref());
        });
    if let Err(e) = spawned {
        eprintln!("could not serve a request: {e}");
    }
}

/// Read one request and write one answer.
fn handle<F>(stream: &TcpStream, answer: &F)
where
    F: Fn(&str) -> Response,
{
    handle_within(stream, answer, REQUEST_DEADLINE);
}

/// As [`handle`], with the whole request due within `deadline`.
fn handle_within<F>(stream: &TcpStream, answer: &F, deadline: Duration)
where
    F: Fn(&str) -> Response,
{
    let _ = stream.set_write_timeout(Some(WRITE_TIMEOUT));

    let mut reader = BufReader::new(Deadline {
        stream,
        until: Instant::now() + deadline,
    });
    let mut line = String::new();
    // Capped, so a line that never ends costs 8 KiB rather than the
    // machine.
    let read = match (&mut reader).take(MAX_REQUEST + 1).read_line(&mut line) {
        Ok(0) | Err(_) => return,
        Ok(read) => read,
    };
    if read as u64 > MAX_REQUEST {
        let _ = write_response(
            stream,
            &Response::text(
                "431 Request Header Fields Too Large",
                "request line too long\n",
            ),
        );
        return;
    }

    // "GET /metrics HTTP/1.1".  Only the path is of interest, and of
    // the headers only the length of the body, which neither server
    // reads.  A line with no version has no headers to read.
    let mut words = line.split_whitespace();
    let path = words.nth(1).unwrap_or("/");
    if words.next().is_some() {
        match declared_body(&mut reader) {
            Err(_) => return,
            Ok(Err(refusal)) => {
                refuse(stream, &mut reader, &refusal);
                return;
            }
            Ok(Ok(length)) => {
                // Read rather than left: closing on unread bytes resets
                // the connection, which can lose the answer.
                if std::io::copy(&mut (&mut reader).take(length), &mut std::io::sink()).is_err() {
                    return;
                }
            }
        }
    }
    let _ = write_response(stream, &answer(path));
}

/// The length of the body the headers declare, read up to the blank
/// line that ends them; or the refusal of a header line past
/// `MAX_REQUEST`, of a length that is not one, of a body past
/// `BODY_LIMIT`, or of a body sent with a `Transfer-Encoding`, which is
/// refused 411 rather than decoded: no caller takes a body, and one
/// whose length is not declared cannot be held to the limit.
fn declared_body(reader: &mut impl BufRead) -> std::io::Result<Result<u64, Response>> {
    let mut length = 0;
    // Bytes, not a String: a header is not obliged to be UTF-8.
    let mut line = Vec::new();
    loop {
        line.clear();
        let read = reader
            .by_ref()
            .take(MAX_REQUEST + 1)
            .read_until(b'\n', &mut line)?;
        if read as u64 > MAX_REQUEST {
            return Ok(Err(Response::text(
                "431 Request Header Fields Too Large",
                "a header line is too long\n",
            )));
        }
        // The blank line, or the client's end of the connection.
        let header = line.trim_ascii();
        if header.is_empty() {
            break;
        }
        let Some(colon) = header.iter().position(|&byte| byte == b':') else {
            continue;
        };
        let (name, value) = (&header[..colon], &header[colon + 1..]);
        if name.eq_ignore_ascii_case(b"transfer-encoding") {
            return Ok(Err(Response::text(
                "411 Length Required",
                "a body must be sent with a Content-Length\n",
            )));
        }
        if name.eq_ignore_ascii_case(b"content-length") {
            let value = std::str::from_utf8(value).ok();
            match value.and_then(|value| value.trim().parse().ok()) {
                Some(declared) => length = declared,
                None => {
                    return Ok(Err(Response::text(
                        "400 Bad Request",
                        "the Content-Length is not a length\n",
                    )));
                }
            }
        }
    }
    if length > BODY_LIMIT {
        return Ok(Err(Response::text(
            "413 Content Too Large",
            &format!("the body is past {BODY_LIMIT} bytes\n"),
        )));
    }
    Ok(Ok(length))
}

/// Write `refusal` and close, reading what the client still sends until
/// it stops or the request's deadline passes: closing on unread bytes
/// resets the connection, and the client may never see why it was
/// refused.
fn refuse(stream: &TcpStream, reader: &mut impl BufRead, refusal: &Response) {
    if write_response(stream, refusal).is_err() {
        return;
    }
    let _ = stream.shutdown(Shutdown::Write);
    let _ = std::io::copy(reader, &mut std::io::sink());
}

/// A stream that stops reading at a fixed moment, however the bytes
/// trickle in.
struct Deadline<'a> {
    stream: &'a TcpStream,
    until: Instant,
}

impl std::io::Read for Deadline<'_> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let left = self.until.saturating_duration_since(Instant::now());
        if left.is_zero() {
            return Err(std::io::ErrorKind::TimedOut.into());
        }
        self.stream.set_read_timeout(Some(left))?;
        let mut stream = self.stream;
        stream.read(buf)
    }
}

fn write_response(mut stream: &TcpStream, response: &Response) -> std::io::Result<()> {
    write!(
        stream,
        "HTTP/1.1 {}\r\n\
         Content-Type: {}\r\n\
         Content-Length: {}\r\n\
         Cache-Control: no-store\r\n\
         Connection: close\r\n\r\n{}",
        response.status,
        response.kind,
        response.body.len(),
        response.body
    )
}

#[cfg(test)]
mod tests {
    use super::decode;
    use super::pairs;
    use super::value;

    /// What a browser actually sends for a list.
    ///
    /// `URLSearchParams` encodes the separator, so the server saw one
    /// column named `efc_percent%2Ctemperature_c` and refused it.  The
    /// single-column parameter this replaced had nothing to encode,
    /// which is why the missing decode went unnoticed.
    #[test]
    fn a_percent_encoded_list_decodes() {
        assert_eq!(
            decode("efc_percent%2Ctemperature_c"),
            "efc_percent,temperature_c"
        );
    }

    #[test]
    fn a_plus_is_a_space_and_a_percent_2b_is_a_plus() {
        assert_eq!(decode("one+two"), "one two");
        assert_eq!(decode("one%2Btwo"), "one+two");
    }

    /// A malformed escape is kept rather than refused.  A stray percent
    /// in a value is not worth failing a request over, and the column
    /// check downstream rejects anything that is not a real column
    /// anyway.
    #[test]
    fn a_broken_escape_survives() {
        assert_eq!(decode("100%"), "100%");
        assert_eq!(decode("%zz"), "%zz");
        assert_eq!(decode("%2"), "%2");
    }

    #[test]
    fn multibyte_characters_survive_the_round_trip() {
        assert_eq!(decode("%C2%B5s"), "\u{b5}s");
    }

    #[test]
    fn pairs_are_split_and_decoded_and_the_first_value_wins() {
        let query = "receiver=3625A01487&columns=a%2Cb&bare&receiver=other";
        assert_eq!(value(query, "receiver").as_deref(), Some("3625A01487"));
        assert_eq!(value(query, "columns").as_deref(), Some("a,b"));
        assert_eq!(value(query, "missing"), None);
        assert_eq!(pairs(query).count(), 3);
    }

    use super::BODY_LIMIT;
    use super::Response;
    use super::handle_within;
    use super::keep_accepting;
    use std::collections::VecDeque;
    use std::io::Read as _;
    use std::io::Write as _;
    use std::net::TcpListener;
    use std::net::TcpStream;
    use std::ops::ControlFlow;
    use std::time::Duration;
    use std::time::Instant;

    #[test]
    fn a_request_that_trickles_in_is_cut_off_at_the_deadline() {
        let listener = TcpListener::bind("127.0.0.1:0").expect("bind");
        let address = listener.local_addr().expect("an address");
        // A byte every 200 ms for four seconds: each read succeeds
        // well inside any per-read timeout.
        let client = std::thread::spawn(move || {
            let mut stream = TcpStream::connect(address).expect("connect");
            for _ in 0..20 {
                if stream.write_all(b"G").is_err() {
                    return;
                }
                std::thread::sleep(Duration::from_millis(200));
            }
        });
        let (server, _) = listener.accept().expect("accept");
        let started = Instant::now();
        handle_within(
            &server,
            &|_: &str| Response::not_found(),
            Duration::from_secs(1),
        );
        let took = started.elapsed();
        drop(server);
        let _ = client.join();
        assert!(took < Duration::from_secs(2), "held for {took:?}");
    }

    /// `request` sent whole, and the answer read to the end, from a
    /// server that answers each path with itself.
    fn exchange(request: Vec<u8>) -> String {
        let listener = TcpListener::bind("127.0.0.1:0").expect("bind");
        let address = listener.local_addr().expect("an address");
        let client = std::thread::spawn(move || {
            let mut stream = TcpStream::connect(address).expect("connect");
            stream.write_all(&request).expect("the request");
            let mut answer = String::new();
            stream.read_to_string(&mut answer).expect("an answer");
            answer
        });
        let (server, _) = listener.accept().expect("accept");
        handle_within(
            &server,
            &|path: &str| Response::ok("text/plain", path.to_owned()),
            Duration::from_secs(5),
        );
        drop(server);
        client.join().expect("the client")
    }

    /// A request whose head declares a body of `length` bytes, followed
    /// by that body.
    fn post(length: u64) -> Vec<u8> {
        let mut request = format!(
            "POST /metrics HTTP/1.1\r\nHost: localhost\r\ncontent-length: {length}\r\n\r\n"
        )
        .into_bytes();
        request.resize(
            request.len() + usize::try_from(length).expect("a length"),
            b'x',
        );
        request
    }

    /// A body past the limit is refused as too large, and the client
    /// is told so although it sent the body the server did not read.
    #[test]
    fn a_body_past_the_limit_is_refused_413() {
        let answer = exchange(post(BODY_LIMIT + 1));
        assert!(answer.starts_with("HTTP/1.1 413 "), "{answer}");
    }

    /// A body within the limit is read past, and the path answered.
    #[test]
    fn a_body_within_the_limit_is_answered() {
        let answer = exchange(post(BODY_LIMIT));
        assert!(answer.starts_with("HTTP/1.1 200 "), "{answer}");
        assert!(answer.ends_with("\r\n\r\n/metrics"), "{answer}");
    }

    #[test]
    fn a_length_that_is_not_a_number_is_a_bad_request() {
        let answer = exchange(b"GET / HTTP/1.1\r\nContent-Length: lots\r\n\r\n".to_vec());
        assert!(answer.starts_with("HTTP/1.1 400 "), "{answer}");
    }

    /// A chunked body is refused, its length being required, and the
    /// client is told so although it sent the body the server did not
    /// read.
    #[test]
    fn a_chunked_body_is_refused_411() {
        let chunk = "x".repeat(4096);
        let mut request =
            b"POST /metrics HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
                .to_vec();
        for _ in 0..32 {
            request.extend_from_slice(format!("{:x}\r\n{chunk}\r\n", chunk.len()).as_bytes());
        }
        request.extend_from_slice(b"0\r\n\r\n");
        let answer = exchange(request);
        assert!(answer.starts_with("HTTP/1.1 411 "), "{answer}");
    }

    /// A failed accept, out of file descriptors say, does not stop the
    /// server: it waits and accepts again, and what it accepts next is
    /// handled.
    #[test]
    fn a_failed_accept_is_waited_out_and_accepting_goes_on() {
        let retry = Duration::from_millis(20);
        // what each accept gives, in turn
        let mut accepts: VecDeque<std::io::Result<u32>> = VecDeque::from([
            Ok(1),
            Err(std::io::Error::other("no file descriptor")),
            Err(std::io::Error::other("no file descriptor")),
            Ok(2),
            Ok(3),
        ]);
        let mut handled = Vec::new();
        let started = Instant::now();
        keep_accepting(
            || accepts.pop_front().expect("an accept left"),
            |accepted| {
                handled.push(accepted);
                if accepted == 3 {
                    ControlFlow::Break(())
                } else {
                    ControlFlow::Continue(())
                }
            },
            retry,
        );
        assert_eq!(handled, [1, 2, 3]);
        assert!(started.elapsed() >= 2 * retry, "no pause after a failure");
    }
}
