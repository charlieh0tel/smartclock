//! The link to a `SmartClock`'s engine port: a TCP connection made or
//! taken, or a serial port.  Each holds one connection at a time, makes
//! it again when it drops, and in the meantime drops what is written.

use std::io;
use std::io::ErrorKind;
use std::io::Read;
use std::io::Write;
use std::net::SocketAddr;
use std::net::TcpListener;
use std::net::TcpStream;
use std::net::ToSocketAddrs;
use std::path::PathBuf;
use std::sync::mpsc;
use std::sync::mpsc::Receiver;
use std::sync::mpsc::RecvTimeoutError;
use std::thread;
use std::time::Duration;
use std::time::Instant;

use anyhow::Context as _;
use anyhow::Result;
use serialport::DataBits;
use serialport::FlowControl;
use serialport::Parity;
use serialport::SerialPort;
use serialport::StopBits;
use socket2::SockRef;
use socket2::TcpKeepalive;

use crate::cli::TransportSpec;
use crate::event;

/// The engine port's rate, with eight data bits, no parity and one stop
/// bit, whichever the engine: an Oncore's in Motorola binary (TRM0003
/// table 3.7, p. 3.24,
/// `third_party/motorola/oncore-ut-users-guide/ch3.pdf`), and the
/// Z3815A's to its GT-74, which `duart_init` (`0x2e00c`,
/// `third_party/firmware/decompiled/z3815a-4010/z3815a-4010.c`) sets on
/// DUART channel A: CSRA `0xbb` (9600 each way, baud-rate set 2 by ACR
/// `0xa0`), MR1A `0x13` (no parity, eight bits) and MR2A `0x07` (one
/// stop bit).
const BAUD: u32 = 9600;
/// How much is read at once.
const CHUNK: usize = 512;
/// The shortest wait a socket takes: it refuses a timeout of zero.
const SHORTEST_WAIT: Duration = Duration::from_millis(1);
/// How often a listener with no connection looks for one.
const ACCEPT_POLL: Duration = Duration::from_millis(50);
/// How long a connection is tried for.
const CONNECT_TIMEOUT: Duration = Duration::from_secs(5);
/// How long a write may wait for room, as for a peer that has stopped
/// reading, before the connection is dropped: under a second, so the
/// loop is not held up past the slack it allows a second.
const WRITE_TIMEOUT: Duration = Duration::from_millis(500);
/// A TCP connection with nothing to send is probed after this long
/// idle, again at the interval, and dropped after this many probes go
/// unanswered: a peer gone without a word, as one powered off, is found
/// in under half a minute.
const KEEPALIVE_IDLE: Duration = Duration::from_secs(10);
const KEEPALIVE_INTERVAL: Duration = Duration::from_secs(5);
const KEEPALIVE_PROBES: u32 = 3;
/// How long what was sent may go unacknowledged before the connection
/// is dropped, where probes are not sent for there being data in
/// flight.
#[cfg(target_os = "linux")]
const UNACKNOWLEDGED: Duration = Duration::from_secs(30);
/// The first wait before connecting again, and the longest.
pub(crate) const FIRST_WAIT: Duration = Duration::from_secs(1);
pub(crate) const LONGEST_WAIT: Duration = Duration::from_secs(30);

/// The link to the engine port, as the feed uses it.
pub(crate) trait Transport {
    /// Waits up to `timeout` for bytes from the instrument, and appends
    /// what comes to `into`; connects first if it can.
    fn read(&mut self, into: &mut Vec<u8>, timeout: Duration);

    /// Sends `bytes` to the instrument, or drops them with no
    /// connection or when the write fails; whether they went.
    fn write(&mut self, bytes: &[u8]) -> bool;
}

/// A wait before connecting again that doubles with each failure, up to
/// a limit.
#[derive(Debug, Clone, Copy)]
pub(crate) struct Backoff {
    first: Duration,
    longest: Duration,
    /// The next wait.
    next: Duration,
}

impl Backoff {
    pub(crate) const fn new(first: Duration, longest: Duration) -> Self {
        Self {
            first,
            longest,
            next: first,
        }
    }

    /// The wait before the next attempt; the one after it is longer.
    pub(crate) fn next_wait(&mut self) -> Duration {
        let wait = self.next;
        self.next = (self.next * 2).min(self.longest);
        wait
    }

    /// After a connection: the next wait is the first again.
    pub(crate) const fn reset(&mut self) {
        self.next = self.first;
    }
}

impl Default for Backoff {
    fn default() -> Self {
        Self::new(FIRST_WAIT, LONGEST_WAIT)
    }
}

/// A connection that can wait for bytes, or for room for them, for a
/// while.
trait Link {
    /// Waits up to `timeout` for bytes: `Ok(None)` when none came,
    /// `Ok(Some(0))` at the end of the stream.
    fn read_within(&mut self, buffer: &mut [u8], timeout: Duration) -> io::Result<Option<usize>>;

    /// Writes all of `bytes`, each write waiting up to `timeout`.
    fn write_within(&mut self, bytes: &[u8], timeout: Duration) -> io::Result<()>;
}

pub(crate) fn timed_out(error: &io::Error) -> bool {
    matches!(error.kind(), ErrorKind::WouldBlock | ErrorKind::TimedOut)
}

impl Link for TcpStream {
    fn read_within(&mut self, buffer: &mut [u8], timeout: Duration) -> io::Result<Option<usize>> {
        self.set_read_timeout(Some(timeout.max(SHORTEST_WAIT)))?;
        match self.read(buffer) {
            Ok(count) => Ok(Some(count)),
            Err(error) if timed_out(&error) => Ok(None),
            Err(error) => Err(error),
        }
    }

    fn write_within(&mut self, bytes: &[u8], timeout: Duration) -> io::Result<()> {
        self.set_write_timeout(Some(timeout.max(SHORTEST_WAIT)))?;
        self.write_all(bytes)?;
        self.flush()
    }
}

/// Readies a TCP connection: sent without delay, and dropped when its
/// peer has gone without a word ([`KEEPALIVE_IDLE`]).
pub(crate) fn ready(stream: &TcpStream) -> io::Result<()> {
    stream.set_nodelay(true)?;
    let socket = SockRef::from(stream);
    socket.set_tcp_keepalive(
        &TcpKeepalive::new()
            .with_time(KEEPALIVE_IDLE)
            .with_interval(KEEPALIVE_INTERVAL)
            .with_retries(KEEPALIVE_PROBES),
    )?;
    #[cfg(target_os = "linux")]
    socket.set_tcp_user_timeout(Some(UNACKNOWLEDGED))?;
    Ok(())
}

impl Link for Box<dyn SerialPort> {
    fn read_within(&mut self, buffer: &mut [u8], timeout: Duration) -> io::Result<Option<usize>> {
        self.set_timeout(timeout.max(SHORTEST_WAIT))?;
        match self.read(buffer) {
            // A serial port has no end; nothing read is a wait.
            Ok(0) => Ok(None),
            Ok(count) => Ok(Some(count)),
            Err(error) if timed_out(&error) => Ok(None),
            Err(error) => Err(error),
        }
    }

    /// With no flow control the port always drains, at its rate; the
    /// one timeout bounds reads and writes alike.
    fn write_within(&mut self, bytes: &[u8], timeout: Duration) -> io::Result<()> {
        self.set_timeout(timeout.max(SHORTEST_WAIT))?;
        self.write_all(bytes)?;
        self.flush()
    }
}

/// Reads from `link` into `into`; `false` if the connection is to be
/// dropped.
fn read_link(link: &mut dyn Link, into: &mut Vec<u8>, timeout: Duration, name: &str) -> bool {
    let mut buffer = [0; CHUNK];
    match link.read_within(&mut buffer, timeout) {
        Ok(None) => true,
        Ok(Some(0)) => {
            event(format_args!("{name}: closed"));
            false
        }
        Ok(Some(count)) => {
            into.extend_from_slice(&buffer[..count]);
            true
        }
        Err(error) => {
            event(format_args!("{name}: {error}"));
            false
        }
    }
}

/// Writes `bytes` to `link`; `false` if the connection is to be dropped.
fn write_link(link: &mut dyn Link, bytes: &[u8], name: &str) -> bool {
    match link.write_within(bytes, WRITE_TIMEOUT) {
        Ok(()) => true,
        Err(error) => {
            event(format_args!("{name}: {error}"));
            false
        }
    }
}

/// How a [`Dialer`] makes its connection, on a thread of its own.
trait Dial: Clone + Send + 'static {
    type Link: Link + Send + 'static;
    /// What it connects to, for the log.
    fn name(&self) -> String;
    fn dial(&self) -> io::Result<Self::Link>;
}

/// A TCP server to connect to.
#[derive(Debug, Clone)]
struct TcpDial(String);

impl Dial for TcpDial {
    type Link = TcpStream;

    fn name(&self) -> String {
        self.0.clone()
    }

    fn dial(&self) -> io::Result<TcpStream> {
        let stream = connect(&self.0)?;
        ready(&stream)?;
        Ok(stream)
    }
}

/// Connects to `address`, trying each address it names for
/// [`CONNECT_TIMEOUT`] in turn.
pub(crate) fn connect(address: impl ToSocketAddrs) -> io::Result<TcpStream> {
    let mut last = io::Error::new(ErrorKind::NotFound, "no address");
    for address in address.to_socket_addrs()? {
        match TcpStream::connect_timeout(&address, CONNECT_TIMEOUT) {
            Ok(stream) => return Ok(stream),
            Err(error) => last = error,
        }
    }
    Err(last)
}

/// A serial port to open.
#[derive(Debug, Clone)]
struct SerialDial(PathBuf);

impl Dial for SerialDial {
    type Link = Box<dyn SerialPort>;

    fn name(&self) -> String {
        self.0.display().to_string()
    }

    fn dial(&self) -> io::Result<Box<dyn SerialPort>> {
        serialport::new(self.0.to_string_lossy(), BAUD)
            .data_bits(DataBits::Eight)
            .parity(Parity::None)
            .stop_bits(StopBits::One)
            .flow_control(FlowControl::None)
            .open()
            .map_err(io::Error::from)
    }
}

/// A connection it makes itself, made again after it drops.  It is
/// made on a thread of its own, a name looked up and each address tried
/// for [`CONNECT_TIMEOUT`], while the reads go on waiting no longer
/// than they are asked to.
#[derive(Debug)]
struct Dialer<D: Dial> {
    dial: D,
    link: Option<D::Link>,
    /// The connection being made, which its thread sends when made or
    /// failed.
    dialing: Option<Receiver<io::Result<D::Link>>>,
    backoff: Backoff,
    /// When it may try again.
    next_attempt: Instant,
}

impl<D: Dial> Dialer<D> {
    fn new(dial: D, backoff: Backoff) -> Self {
        Self {
            dial,
            link: None,
            dialing: None,
            backoff,
            next_attempt: Instant::now(),
        }
    }

    /// Starts making the connection.
    fn start(&mut self) {
        let (sender, receiver) = mpsc::channel();
        let dial = self.dial.clone();
        thread::spawn(move || {
            // Nobody is waiting if the dialer has gone.
            let _ = sender.send(dial.dial());
        });
        self.dialing = Some(receiver);
    }

    /// Waits up to `timeout` for the connection being made.
    fn await_dial(&mut self, timeout: Duration) {
        let Some(dialing) = &self.dialing else {
            return;
        };
        let made = match dialing.recv_timeout(timeout.max(SHORTEST_WAIT)) {
            Err(RecvTimeoutError::Timeout) => return,
            Ok(made) => made,
            Err(RecvTimeoutError::Disconnected) => Err(io::Error::other("connecting failed")),
        };
        self.dialing = None;
        match made {
            Ok(link) => {
                event(format_args!("{}: connected", self.dial.name()));
                self.backoff.reset();
                self.link = Some(link);
            }
            Err(error) => {
                event(format_args!("{}: {error}", self.dial.name()));
                self.wait();
            }
        }
    }

    /// Drops the connection, and waits before the next.
    fn drop_link(&mut self) {
        self.link = None;
        self.wait();
    }

    fn wait(&mut self) {
        let wait = self.backoff.next_wait();
        event(format_args!(
            "{}: connecting again in {} ms",
            self.dial.name(),
            wait.as_millis()
        ));
        self.next_attempt = Instant::now() + wait;
    }
}

impl<D: Dial> Transport for Dialer<D> {
    fn read(&mut self, into: &mut Vec<u8>, timeout: Duration) {
        if self.link.is_none() {
            if self.dialing.is_none() && Instant::now() >= self.next_attempt {
                self.start();
            }
            if self.dialing.is_some() {
                self.await_dial(timeout);
                return;
            }
        }
        let name = self.dial.name();
        match &mut self.link {
            Some(link) => {
                if !read_link(link, into, timeout, &name) {
                    self.drop_link();
                }
            }
            None => {
                let until = self.next_attempt.saturating_duration_since(Instant::now());
                thread::sleep(timeout.min(until).max(SHORTEST_WAIT));
            }
        }
    }

    fn write(&mut self, bytes: &[u8]) -> bool {
        let name = self.dial.name();
        let Some(link) = &mut self.link else {
            return false;
        };
        let sent = write_link(link, bytes, &name);
        if !sent {
            self.drop_link();
        }
        sent
    }
}

/// A TCP address it takes connections on, one at a time: another while
/// it has one is closed.
#[derive(Debug)]
struct Server {
    listener: TcpListener,
    link: Option<(TcpStream, SocketAddr)>,
}

impl Server {
    fn bind(address: &str) -> Result<Self> {
        let listener =
            TcpListener::bind(address).with_context(|| format!("listening on {address}"))?;
        listener.set_nonblocking(true)?;
        event(format_args!("listening on {}", listener.local_addr()?));
        Ok(Self {
            listener,
            link: None,
        })
    }

    /// Takes a connection waiting, or closes it if it has one.
    fn accept(&mut self) {
        match self.listener.accept() {
            Ok((stream, peer)) if self.link.is_none() => {
                if let Err(error) = stream.set_nonblocking(false).and_then(|()| ready(&stream)) {
                    event(format_args!("{peer}: {error}"));
                    return;
                }
                event(format_args!("{peer}: connected"));
                self.link = Some((stream, peer));
            }
            Ok((_, peer)) => event(format_args!(
                "{peer}: closed, a connection being held already"
            )),
            Err(error) if timed_out(&error) => {}
            Err(error) => event(format_args!("accepting: {error}")),
        }
    }
}

impl Transport for Server {
    fn read(&mut self, into: &mut Vec<u8>, timeout: Duration) {
        self.accept();
        match &mut self.link {
            Some((stream, peer)) => {
                let name = peer.to_string();
                if !read_link(stream, into, timeout, &name) {
                    self.link = None;
                }
            }
            None => thread::sleep(timeout.clamp(SHORTEST_WAIT, ACCEPT_POLL)),
        }
    }

    fn write(&mut self, bytes: &[u8]) -> bool {
        let Some((stream, peer)) = &mut self.link else {
            return false;
        };
        let name = peer.to_string();
        let sent = write_link(stream, bytes, &name);
        if !sent {
            self.link = None;
        }
        sent
    }
}

/// Opens the link `spec` names.  A listener is bound at once, so an
/// address in use is an error here; a connection or a port is made in
/// the first read, and again whenever it drops.
pub(crate) fn open(spec: &TransportSpec) -> Result<Box<dyn Transport>> {
    Ok(match spec {
        TransportSpec::Connect(address) => {
            Box::new(Dialer::new(TcpDial(address.clone()), Backoff::default()))
        }
        TransportSpec::Listen(address) => Box::new(Server::bind(address)?),
        TransportSpec::Serial(path) => {
            Box::new(Dialer::new(SerialDial(path.clone()), Backoff::default()))
        }
    })
}

#[cfg(test)]
mod tests {
    use super::Backoff;
    use super::Dial;
    use super::Dialer;
    use super::Server;
    use super::TcpDial;
    use super::Transport;
    use super::WRITE_TIMEOUT;
    use socket2::SockRef;
    use std::io::Read as _;
    use std::io::Write as _;
    use std::net::TcpListener;
    use std::net::TcpStream;
    use std::time::Duration;
    use std::time::Instant;

    /// How long a test waits for a socket.
    const PATIENCE: Duration = Duration::from_secs(5);
    const STEP: Duration = Duration::from_millis(20);

    /// Reads from `transport` until `want` bytes have come.
    fn read_until(transport: &mut dyn Transport, want: usize) -> Vec<u8> {
        let mut into = Vec::new();
        let started = Instant::now();
        while into.len() < want {
            assert!(started.elapsed() < PATIENCE, "nothing came");
            transport.read(&mut into, STEP);
        }
        into
    }

    #[test]
    fn the_wait_doubles_to_a_limit() {
        let mut backoff = Backoff::new(Duration::from_secs(1), Duration::from_secs(5));
        let waits: Vec<u64> = (0..5).map(|_| backoff.next_wait().as_secs()).collect();
        assert_eq!(waits, [1, 2, 4, 5, 5]);
        backoff.reset();
        assert_eq!(backoff.next_wait(), Duration::from_secs(1));
    }

    /// A connection made drops when the far end goes, and is made again.
    #[test]
    fn a_dialer_connects_again() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap().to_string();
        let mut dialer = Dialer::new(
            TcpDial(address),
            Backoff::new(Duration::from_millis(10), Duration::from_millis(10)),
        );
        for greeting in [&b"one"[..], b"two"] {
            let started = Instant::now();
            while dialer.link.is_none() {
                assert!(started.elapsed() < PATIENCE, "never connected");
                dialer.read(&mut Vec::new(), STEP);
            }
            let (mut bridge, _) = listener.accept().unwrap();
            bridge.write_all(greeting).unwrap();
            assert_eq!(read_until(&mut dialer, greeting.len()), greeting);
            assert!(dialer.write(b"back"));
            let mut back = [0; 4];
            bridge.read_exact(&mut back).unwrap();
            assert_eq!(&back, b"back");
            drop(bridge);
            // The end of the stream drops the connection.
            let started = Instant::now();
            while dialer.link.is_some() {
                assert!(started.elapsed() < PATIENCE, "never dropped");
                dialer.read(&mut Vec::new(), STEP);
            }
            // Written with none, it is dropped, and says so.
            assert!(!dialer.write(b"lost"));
        }
    }

    /// A server slow to answer, as one whose name is slow to look up or
    /// which drops what is sent to it.
    #[derive(Clone)]
    struct Slow(TcpDial);

    impl Dial for Slow {
        type Link = TcpStream;

        fn name(&self) -> String {
            self.0.name()
        }

        fn dial(&self) -> std::io::Result<TcpStream> {
            std::thread::sleep(Duration::from_secs(1));
            self.0.dial()
        }
    }

    /// A connection slow to make does not hold up the reads: each
    /// waits no longer than it was asked to, and the connection is
    /// taken once made.
    #[test]
    fn a_dialer_does_not_wait_for_its_connection() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap().to_string();
        let mut dialer = Dialer::new(Slow(TcpDial(address)), Backoff::default());
        let started = Instant::now();
        while dialer.link.is_none() {
            assert!(started.elapsed() < PATIENCE, "never connected");
            let read = Instant::now();
            dialer.read(&mut Vec::new(), STEP);
            assert!(
                read.elapsed() < STEP * 5,
                "a read waited {:?}",
                read.elapsed()
            );
        }
        let (mut bridge, _) = listener.accept().unwrap();
        bridge.write_all(b"made").unwrap();
        assert_eq!(read_until(&mut dialer, 4), b"made");
    }

    /// A connection made or taken is probed when idle, so a peer gone
    /// without a word is dropped and, for a listener, makes way for the
    /// next.
    #[test]
    fn connections_are_probed_when_idle() {
        let mut server = Server::bind("127.0.0.1:0").unwrap();
        let address = server.listener.local_addr().unwrap();
        let mut dialer = Dialer::new(TcpDial(address.to_string()), Backoff::default());
        let started = Instant::now();
        while dialer.link.is_none() || server.link.is_none() {
            assert!(started.elapsed() < PATIENCE, "never connected");
            dialer.read(&mut Vec::new(), STEP);
            server.read(&mut Vec::new(), STEP);
        }
        assert!(
            SockRef::from(dialer.link.as_ref().unwrap())
                .keepalive()
                .unwrap()
        );
        assert!(
            SockRef::from(&server.link.as_ref().unwrap().0)
                .keepalive()
                .unwrap()
        );
    }

    /// A peer that stops reading is dropped once what is written to it
    /// has nowhere to go: no write waits much past its timeout.
    #[test]
    fn a_peer_that_stops_reading_is_dropped() {
        const BLOCK: usize = 1 << 20;
        let mut server = Server::bind("127.0.0.1:0").unwrap();
        let address = server.listener.local_addr().unwrap();
        let _deaf = TcpStream::connect(address).unwrap();
        let started = Instant::now();
        while server.link.is_none() {
            assert!(started.elapsed() < PATIENCE, "never connected");
            server.read(&mut Vec::new(), STEP);
        }
        let block = vec![0; BLOCK];
        while server.link.is_some() {
            assert!(started.elapsed() < PATIENCE * 4, "never dropped");
            let write = Instant::now();
            let _ = server.write(&block);
            assert!(
                write.elapsed() < WRITE_TIMEOUT * 4,
                "a write waited {:?}",
                write.elapsed()
            );
        }
    }

    /// A listener holds one connection, closes a second, and takes the
    /// next once the first goes.
    #[test]
    fn a_server_holds_one_connection_at_a_time() {
        let mut server = Server::bind("127.0.0.1:0").unwrap();
        let address = server.listener.local_addr().unwrap();
        let mut first = TcpStream::connect(address).unwrap();
        first.write_all(b"first").unwrap();
        assert_eq!(read_until(&mut server, 5), b"first");

        let mut second = TcpStream::connect(address).unwrap();
        second.set_read_timeout(Some(PATIENCE)).unwrap();
        server.read(&mut Vec::new(), STEP);
        let mut nothing = [0; 1];
        assert_eq!(second.read(&mut nothing).unwrap(), 0, "a second was held");

        assert!(server.write(b"to first"));
        let mut back = [0; 8];
        first.read_exact(&mut back).unwrap();
        assert_eq!(&back, b"to first");

        drop(first);
        let started = Instant::now();
        while server.link.is_some() {
            assert!(started.elapsed() < PATIENCE, "never dropped");
            server.read(&mut Vec::new(), STEP);
        }
        let mut third = TcpStream::connect(address).unwrap();
        third.write_all(b"third").unwrap();
        assert_eq!(read_until(&mut server, 5), b"third");
    }
}
