//! Run directories, fake daemons and the tool itself, for the tests
//! that run `smartclock-cli` against daemons.

use std::io::BufRead as _;
use std::io::BufReader;
use std::io::Write as _;
use std::os::unix::fs::PermissionsExt as _;
use std::path::Path;
use std::path::PathBuf;
use std::process::Command;
use std::process::Output;
use std::sync::Arc;
use std::sync::Mutex;

use jiff::Timestamp;
use smartclock::link;
use smartclock::protocol::CHECKS_RECEIVER_SERIAL;
use smartclock::protocol::Message;
use smartclock::protocol::Op;
use smartclock::protocol::Request;
use smartclock::snapshot::Freshness;
use smartclock::snapshot::Snapshot;
use smartclock::snapshot::Tier;
use smartclock::wire::Reading;

/// A run directory of the test's own, removed on drop.
pub(crate) struct RunDir(pub(crate) PathBuf);

impl RunDir {
    pub(crate) fn new(name: &str) -> Self {
        let dir =
            std::env::temp_dir().join(format!("smartclock-cli-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).expect("a run directory");
        Self(dir)
    }

    /// An instance directory, made, and where its socket goes.
    pub(crate) fn instance(&self, name: &str) -> PathBuf {
        let dir = self.0.join(name);
        std::fs::create_dir_all(&dir).expect("an instance directory");
        dir.join("socket")
    }

    pub(crate) fn path(&self) -> &str {
        self.0.to_str().expect("a UTF-8 path")
    }
}

impl Drop for RunDir {
    fn drop(&mut self) {
        // A shut instance directory has to be opened to be removed.
        if let Ok(entries) = std::fs::read_dir(&self.0) {
            for entry in entries.flatten() {
                let _ =
                    std::fs::set_permissions(entry.path(), std::fs::Permissions::from_mode(0o700));
            }
        }
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

/// A daemon that answers as smartclockd does, from what it is set up
/// with, and runs every query, note and fact it is sent without
/// checking anything.
pub(crate) struct Fake {
    /// The `*IDN?` it is attached to; empty for none yet.
    pub(crate) identity: &'static str,
    /// Its latest reading's.
    pub(crate) freshness: Freshness,
    /// Whether its `info` says it checks a request's receiver serial,
    /// as a daemon older than that does not.
    pub(crate) checks: bool,
}

/// Every request a fake daemon was sent, in order.
pub(crate) type Asked = Arc<Mutex<Vec<Op>>>;

impl Fake {
    /// A current daemon attached to `identity`, its receiver answering.
    pub(crate) fn attached(identity: &'static str) -> Self {
        Self {
            identity,
            freshness: Freshness::Live,
            checks: true,
        }
    }

    /// Listen at `socket`.
    pub(crate) fn serve(self, socket: &Path) -> Asked {
        let listener = link::listen(socket).expect("listen");
        let asked = Asked::default();
        let log = Arc::clone(&asked);
        std::thread::spawn(move || {
            while let Ok(stream) = listener.accept() {
                let Ok(mut writer) = stream.try_clone() else {
                    continue;
                };
                for line in BufReader::new(stream).lines() {
                    let Ok(line) = line else { break };
                    let Ok(request) = serde_json::from_str::<Request>(&line) else {
                        break;
                    };
                    log.lock().expect("log").push(request.op.clone());
                    let reply = Message::ok(request.id, self.answer(&request.op));
                    let text = serde_json::to_string(&reply).expect("reply");
                    if writeln!(writer, "{text}").is_err() {
                        break;
                    }
                }
            }
        });
        asked
    }

    fn answer(&self, op: &Op) -> serde_json::Value {
        match op {
            Op::Info if self.checks => serde_json::json!({
                "identity": self.identity,
                CHECKS_RECEIVER_SERIAL: true,
            }),
            Op::Info => serde_json::json!({ "identity": self.identity }),
            Op::Latest => {
                let mut snapshot = Snapshot::new(Timestamp::now());
                snapshot.freshness = self.freshness;
                if self.freshness != Freshness::Live {
                    snapshot.polled.failed(Tier::Fast, "no prompt within 5s");
                }
                serde_json::to_value(Reading::from(&snapshot)).expect("reading")
            }
            Op::Query { .. } => serde_json::json!({ "lines": ["+0"] }),
            Op::Note { .. } | Op::Fact { .. } => {
                serde_json::json!({ "receiver": self.identity, "written": true })
            }
            Op::Status => serde_json::json!({}),
        }
    }
}

/// Run `smartclock-cli` with `args`, none of its settings taken from
/// the environment.
pub(crate) fn cli(args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_smartclock-cli"))
        .env_remove("SMARTCLOCK_SERIAL")
        .env_remove("SMARTCLOCK_DEVICE")
        .args(args)
        .output()
        .expect("run the tool")
}
