//! The simulated receiver on a TCP connection.
//!
//! What the `smartclock-sim` binary serves, and what a test serves when
//! the thing under test opens its receiver by address rather than being
//! handed a transport.  `smartclock-emu` serves its emulated ports the
//! same way.

use std::io::Read;
use std::io::Write;
use std::net::TcpStream;

/// Shuttle bytes between the socket and `port` until the client closes
/// the connection.
///
/// `port` is the simulated receiver or anything else that reads and
/// writes like a serial port.  Its reads must not block: one returns
/// what the port has to send, or nothing, at once, as `SimTransport`'s
/// do, for the socket is read in turns with it.
pub fn serve(mut stream: TcpStream, mut port: impl Read + Write) -> std::io::Result<()> {
    stream.set_nodelay(true)?;
    stream.set_read_timeout(Some(std::time::Duration::from_millis(20)))?;
    let mut from_client = [0u8; 512];
    let mut from_receiver = [0u8; 512];
    loop {
        match stream.read(&mut from_client) {
            Ok(0) => return Ok(()),
            Ok(n) => {
                port.write_all(&from_client[..n])?;
            }
            Err(e)
                if matches!(
                    e.kind(),
                    std::io::ErrorKind::TimedOut | std::io::ErrorKind::WouldBlock
                ) => {}
            Err(e) => return Err(e),
        }
        let n = port.read(&mut from_receiver)?;
        if n > 0 {
            stream.write_all(&from_receiver[..n])?;
            stream.flush()?;
        }
    }
}

#[cfg(test)]
mod tests {
    use std::collections::VecDeque;
    use std::io::Read;
    use std::io::Write;
    use std::net::Shutdown;
    use std::net::TcpListener;
    use std::net::TcpStream;
    use std::thread;

    use super::serve;

    /// A port that sends back what it is sent, and nothing while it has
    /// nothing, without waiting.
    #[derive(Default)]
    struct Echo {
        /// What it was sent and has not sent back.
        pending: VecDeque<u8>,
    }

    impl Read for Echo {
        fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
            let n = buf.len().min(self.pending.len());
            for (slot, byte) in buf.iter_mut().zip(self.pending.drain(..n)) {
                *slot = byte;
            }
            Ok(n)
        }
    }

    impl Write for Echo {
        fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
            self.pending.extend(buf);
            Ok(buf.len())
        }

        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }

    /// A port other than the simulated receiver is served: what the
    /// client sends reaches it, what it sends reaches the client, and
    /// the serving ends when the client closes.
    #[test]
    fn any_port_is_served_until_the_client_closes() {
        let listener = TcpListener::bind("127.0.0.1:0").expect("bind");
        let address = listener.local_addr().expect("an address");
        let server = thread::spawn(move || {
            let (stream, _) = listener.accept().expect("a connection");
            serve(stream, Echo::default())
        });
        let mut client = TcpStream::connect(address).expect("connect");
        client.write_all(b"*IDN?\r\n").expect("the command");
        let mut echoed = [0u8; 7];
        client.read_exact(&mut echoed).expect("the echo");
        assert_eq!(&echoed, b"*IDN?\r\n");
        client.shutdown(Shutdown::Write).expect("close");
        assert!(server.join().expect("the server").is_ok());
    }
}
