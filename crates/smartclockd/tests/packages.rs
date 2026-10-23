//! The two Debian packages, smartclockmon (this crate's
//! `[package.metadata.deb]`) and smartclock-utils (smartclock-cli's),
//! name each other's version in Depends, Breaks and Replaces.  cargo-deb
//! has no variable for it, so it is written out; these tests fail when
//! it and the workspace version part, and when a file would be in both
//! packages, which dpkg refuses to install.

use std::collections::BTreeSet;
use std::fs;
use std::path::Path;

use toml::Table;
use toml::Value;

/// The revision cargo-deb gives a release when none is set.
const RELEASE_REVISION: &str = "1";

/// The `[package.metadata.deb]` table of the crate in `crates/<name>`.
fn deb_metadata(name: &str) -> Table {
    let path = format!("{}/../{name}/Cargo.toml", env!("CARGO_MANIFEST_DIR"));
    let text = fs::read_to_string(&path).unwrap_or_else(|e| panic!("{path}: {e}"));
    let manifest: Table = text.parse().unwrap_or_else(|e| panic!("{path}: {e}"));
    manifest["package"]["metadata"]["deb"]
        .as_table()
        .unwrap_or_else(|| panic!("{path}: no [package.metadata.deb]"))
        .clone()
}

/// A string field of a package's metadata.
fn field<'a>(deb: &'a Table, key: &str) -> &'a str {
    deb.get(key)
        .and_then(Value::as_str)
        .unwrap_or_else(|| panic!("no {key} in {deb:?}"))
}

/// The version a release of this workspace is packaged as.
fn release_version() -> String {
    format!("{}-{RELEASE_REVISION}", env!("CARGO_PKG_VERSION"))
}

/// Every path a package installs, as cargo-deb places each asset: a
/// destination ending in `/` is a directory the source keeps its name in.
fn installed(deb: &Table) -> BTreeSet<String> {
    deb["assets"]
        .as_array()
        .expect("assets")
        .iter()
        .map(|asset| {
            let asset = asset
                .as_array()
                .expect("an asset is [source, destination, mode]");
            let source = asset[0].as_str().expect("source");
            let destination = asset[1].as_str().expect("destination");
            if destination.ends_with('/') {
                let name = Path::new(source).file_name().expect("a file name");
                format!("{destination}{}", name.to_string_lossy())
            } else {
                destination.to_owned()
            }
        })
        .collect()
}

#[test]
fn the_monitor_requires_the_tools_of_its_own_version() {
    let monitor = deb_metadata("smartclockd");
    let utils = deb_metadata("smartclock-cli");
    let wanted = format!("{} (= {})", field(&utils, "name"), release_version());
    let depends: Vec<&str> = field(&monitor, "depends")
        .split(',')
        .map(str::trim)
        .collect();
    assert!(
        depends.contains(&wanted.as_str()),
        "{depends:?} lacks {wanted}"
    );
}

#[test]
fn the_tools_take_over_from_an_older_monitor() {
    let monitor = deb_metadata("smartclockd");
    let utils = deb_metadata("smartclock-cli");
    let wanted = format!("{} (<< {})", field(&monitor, "name"), release_version());
    assert_eq!(field(&utils, "breaks"), wanted);
    assert_eq!(field(&utils, "replaces"), wanted);
}

#[test]
fn neither_package_sets_a_revision_of_its_own() {
    for name in ["smartclockd", "smartclock-cli"] {
        assert!(deb_metadata(name).get("revision").is_none(), "{name}");
    }
}

#[test]
fn no_file_is_in_both_packages() {
    let monitor = installed(&deb_metadata("smartclockd"));
    let utils = installed(&deb_metadata("smartclock-cli"));
    let both: Vec<_> = monitor.intersection(&utils).collect();
    assert!(both.is_empty(), "in both packages: {both:?}");
}
