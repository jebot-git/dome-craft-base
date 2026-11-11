// SPDX-License-Identifier: MIT
//! Format-seam probe, compiled against the same cached Skrifa as the engines.
#![forbid(unsafe_code)]
#![deny(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
use std::io::Read;
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = std::env::args().nth(1).ok_or("provide a native BDF font path")?;
    let mut bytes = Vec::new();
    std::fs::File::open(&path)?.take(64 * 1024 * 1024 + 1).read_to_end(&mut bytes)?;
    if bytes.len() > 64 * 1024 * 1024 {
        return Err("font exceeds the 64 MiB probe limit".into());
    }
    if !bytes.starts_with(b"STARTFONT") {
        return Err("input is not a plain BDF font".into());
    }
    // A minimal SFNT directory is enough to exercise format admission without
    // importing or bundling any outline font or proprietary font asset.
    let sfnt_header = [0_u8, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    println!("SFNT directory admitted: {}", skrifa::FontRef::new(&sfnt_header).is_ok());
    match skrifa::FontRef::new(&bytes) {
        Ok(_) => return Err("BDF was unexpectedly admitted; revisit the format assessment".into()),
        Err(error) => println!("Native BDF rejected by upstream FontRef: {error:?}"),
    }
    println!("Probe input: {path} ({} bytes)", bytes.len());
    Ok(())
}
