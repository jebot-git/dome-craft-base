// SPDX-License-Identifier: MIT
// Shared by the three downstream engines. No SFNT conversion, font assets or dependencies.
use std::collections::BTreeMap;
use std::io::Read;
use std::sync::{Arc, OnceLock};

const MAX_BYTES: u64 = 32 * 1024 * 1024;
const MAX_GLYPHS: usize = 131072;
#[derive(Clone, Debug)]
pub struct Glyph {
    pub advance: i32,
    pub width: usize,
    pub height: usize,
    pub x: i32,
    pub y: i32,
    bits: Vec<u8>,
}
#[derive(Debug)]
pub struct BitmapFont {
    pub data: Arc<Vec<u8>>,
    pub family: String,
    pub style: String,
    pub weight: f32,
    pub italic: bool,
    pub pixels: i32,
    pub ascent: i32,
    pub descent: i32,
    pub glyphs: BTreeMap<u32, Glyph>,
    default: u32,
}
fn number(s: Option<&str>) -> Result<i32, String> {
    s.ok_or("Missing BDF integer")?
        .parse()
        .map_err(|_| "Invalid BDF integer".into())
}
impl BitmapFont {
    pub fn read(path: &std::path::Path) -> Result<Self, String> {
        let mut bytes = Vec::new();
        std::fs::File::open(path)
            .map_err(|e| format!("BDF {}: {e}", path.display()))?
            .take(MAX_BYTES + 1)
            .read_to_end(&mut bytes)
            .map_err(|e| e.to_string())?;
        if bytes.len() as u64 > MAX_BYTES {
            return Err("BDF exceeds 32 MiB".into());
        }
        Self::parse(&bytes)
    }
    pub fn parse(bytes: &[u8]) -> Result<Self, String> {
        if bytes.len() as u64 > MAX_BYTES {
            return Err("BDF exceeds 32 MiB".into());
        }
        let source = std::str::from_utf8(bytes).map_err(|_| "BDF must be UTF-8/ASCII")?;
        if !source.starts_with("STARTFONT 2.") {
            return Err("Expected a BDF 2.x bitmap font".into());
        }
        let mut font = Self {
            data: Arc::new(bytes.to_vec()),
            family: "DomeD Bitmap".into(),
            style: "Regular".into(),
            weight: 400.,
            italic: false,
            pixels: 0,
            ascent: 0,
            descent: 0,
            glyphs: BTreeMap::new(),
            default: 0xfffd,
        };
        let mut current: Option<Glyph> = None;
        let mut cp = -1;
        let mut bitmap = false;
        let mut rows = 0;
        let mut declared = None;
        let mut ended = false;
        let mut seen = 0;
        let mut flags = 0;
        let mut unicode = false;
        let mut encoding = false;
        for line in source.lines() {
            if line.len() > 4096 {
                return Err("BDF line exceeds 4096 bytes".into());
            }
            let line = line.trim();
            let mut fields = line.split_whitespace();
            let key = fields.next().unwrap_or("");
            if key == "ENDCHAR" {
                let glyph = current.take().ok_or("ENDCHAR without STARTCHAR")?;
                if !bitmap || rows != glyph.height || flags != 7 {
                    return Err("BDF bitmap row count differs from BBX".into());
                }
                if cp >= 0 && char::from_u32(cp as u32).is_some() {
                    if font.glyphs.len() >= MAX_GLYPHS {
                        return Err("Too many BDF glyphs".into());
                    }
                    if font.glyphs.insert(cp as u32, glyph).is_some() {
                        return Err("Duplicate BDF encoding".into());
                    }
                }
                seen += 1;
                bitmap = false;
                continue;
            }
            if bitmap {
                let glyph = current.as_mut().ok_or("BITMAP without glyph")?;
                let stride = glyph.width.div_ceil(8);
                if rows >= glyph.height || line.len() != stride * 2 {
                    return Err("Invalid BDF bitmap row size".into());
                }
                for pair in line.as_bytes().chunks_exact(2) {
                    let hi = (pair[0] as char)
                        .to_digit(16)
                        .ok_or("Invalid BDF bitmap hex")?;
                    let lo = (pair[1] as char)
                        .to_digit(16)
                        .ok_or("Invalid BDF bitmap hex")?;
                    glyph.bits.push((hi * 16 + lo) as u8);
                }
                rows += 1;
                continue;
            }
            match key {
                "WEIGHT_NAME" => {
                    let name = fields
                        .next()
                        .unwrap_or("")
                        .trim_matches('"')
                        .to_ascii_lowercase();
                    font.weight = if name.contains("bold") {
                        700.
                    } else if name.contains("black") {
                        900.
                    } else if name.contains("light") {
                        300.
                    } else {
                        400.
                    };
                }
                "SLANT" => {
                    font.italic = fields
                        .next()
                        .is_some_and(|v| matches!(v.trim_matches('"'), "I" | "O" | "i" | "o"))
                }
                "CHARSET_REGISTRY" => {
                    unicode = fields
                        .next()
                        .is_some_and(|v| v.trim_matches('"').eq_ignore_ascii_case("ISO10646"))
                }
                "CHARSET_ENCODING" => {
                    encoding = fields.next().is_some_and(|v| v.trim_matches('"') == "1")
                }
                "FAMILY_NAME" => {
                    font.family = line
                        .strip_prefix("FAMILY_NAME")
                        .unwrap_or("")
                        .trim()
                        .trim_matches('"')
                        .chars()
                        .take(256)
                        .collect();
                }
                "PIXEL_SIZE" => font.pixels = number(fields.next())?,
                "FONT_ASCENT" => font.ascent = number(fields.next())?,
                "FONT_DESCENT" => font.descent = number(fields.next())?,
                "DEFAULT_CHAR" => {
                    let n = number(fields.next())?;
                    if n >= 0 {
                        font.default = n as u32;
                    }
                }
                "FONTBOUNDINGBOX" => {
                    let _w = number(fields.next())?;
                    let h = number(fields.next())?;
                    let _x = number(fields.next())?;
                    let y = number(fields.next())?;
                    if !(1..=256).contains(&h) || !(-256..=256).contains(&y) {
                        return Err("Invalid BDF font bounds".into());
                    }
                    if font.pixels == 0 {
                        font.pixels = h;
                    }
                    if font.ascent == 0 {
                        font.ascent = h + y;
                    }
                    if font.descent == 0 {
                        font.descent = (-y).max(0);
                    }
                }
                "CHARS" => {
                    let count = number(fields.next())?;
                    if count < 1 || count as usize > MAX_GLYPHS {
                        return Err("Invalid BDF CHARS count".into());
                    }
                    declared = Some(count as usize);
                }
                "STARTCHAR" => {
                    if current.is_some() {
                        return Err("Nested BDF glyph".into());
                    }
                    current = Some(Glyph {
                        advance: 0,
                        width: 0,
                        height: 0,
                        x: 0,
                        y: 0,
                        bits: Vec::new(),
                    });
                    cp = -1;
                    flags = 0;
                    rows = 0;
                }
                "ENCODING" => {
                    cp = number(fields.next())?;
                    if cp < -1 || (cp >= 0 && char::from_u32(cp as u32).is_none()) {
                        return Err("BDF encoding is outside Unicode".into());
                    }
                    flags |= 4;
                }
                "DWIDTH" => {
                    flags |= 1;
                    let advance = number(fields.next())?;
                    if !(-256..=512).contains(&advance) {
                        return Err("BDF advance exceeds bounds".into());
                    }
                    current.as_mut().ok_or("DWIDTH without glyph")?.advance = advance;
                }
                "BBX" => {
                    flags |= 2;
                    let w = number(fields.next())?;
                    let h = number(fields.next())?;
                    let x = number(fields.next())?;
                    let y = number(fields.next())?;
                    if !(0..=128).contains(&w)
                        || !(0..=256).contains(&h)
                        || !(-256..=256).contains(&x)
                        || !(-256..=256).contains(&y)
                    {
                        return Err("BDF glyph bounds exceed limits".into());
                    }
                    let glyph = current.as_mut().ok_or("BBX without glyph")?;
                    glyph.width = w as usize;
                    glyph.height = h as usize;
                    glyph.x = x;
                    glyph.y = y;
                }
                "BITMAP" => {
                    if current.is_none() {
                        return Err("BITMAP without glyph".into());
                    }
                    bitmap = true;
                }
                "ENDFONT" => {
                    ended = true;
                    break;
                }
                _ => {}
            }
        }
        if !unicode || !encoding {
            return Err("BDF must declare ISO10646-1 Unicode encoding".into());
        }
        if !ended || current.is_some() || font.glyphs.is_empty() || declared != Some(seen) {
            return Err("Incomplete BDF font".into());
        }
        if !(1..=256).contains(&font.pixels)
            || !(0..=256).contains(&font.ascent)
            || !(0..=256).contains(&font.descent)
            || font.ascent + font.descent == 0
        {
            return Err("Invalid BDF strike metrics".into());
        }
        if !font.glyphs.contains_key(&font.default) {
            font.default = if font.glyphs.contains_key(&0xfffd) {
                0xfffd
            } else if font.glyphs.contains_key(&63) {
                63
            } else {
                return Err("BDF requires DEFAULT_CHAR, U+FFFD or '?' for missing glyphs".into());
            };
        }
        font.style = match (font.weight >= 600., font.italic) {
            (true, true) => "Bold Italic",
            (true, false) => "Bold",
            (false, true) => "Italic",
            _ => "Regular",
        }
        .into();
        Ok(font)
    }
    // Unicode+1 keeps glyph zero reserved for the BDF's explicit missing glyph.
    pub fn gid(&self, c: char) -> u32 {
        if self.glyphs.contains_key(&(c as u32)) {
            c as u32 + 1
        } else {
            0
        }
    }
    pub fn glyph(&self, gid: u32) -> Option<&Glyph> {
        self.glyphs.get(&gid.checked_sub(1).unwrap_or(self.default))
    }
    pub fn advance(&self, gid: u32) -> i32 {
        self.glyph(gid).map_or(0, |g| g.advance)
    }
    pub fn width(&self, text: &str, size: f64) -> f64 {
        text.chars()
            .map(|c| self.advance(self.gid(c)) as f64)
            .sum::<f64>()
            * size
            / self.pixels as f64
    }
    /// Horizontal set-bit runs, in native strike units, y down from the baseline.
    /// Adjacent pixels stay rectangles; they are never fitted to an outline font.
    pub fn rectangles(&self, gid: u32) -> Vec<[f64; 4]> {
        let Some(g) = self.glyph(gid) else {
            return Vec::new();
        };
        let stride = g.width.div_ceil(8);
        let mut out = Vec::new();
        for row in 0..g.height {
            let set = |col: usize| {
                g.bits
                    .get(row * stride + col / 8)
                    .is_some_and(|b| b & (0x80 >> (col % 8)) != 0)
            };
            let mut col = 0;
            while col < g.width {
                if !set(col) {
                    col += 1;
                    continue;
                }
                let start = col;
                while col < g.width && set(col) {
                    col += 1;
                }
                out.push([
                    (g.x + start as i32) as f64,
                    (-g.y - g.height as i32 + row as i32) as f64,
                    (col - start) as f64,
                    1.,
                ]);
            }
        }
        out
    }
}
pub fn configured() -> bool {
    std::env::var_os("DOME_OFFICE_BDF").is_some()
}
pub fn from_env() -> Result<&'static Arc<BitmapFont>, String> {
    static FONT: OnceLock<Result<Arc<BitmapFont>, String>> = OnceLock::new();
    FONT.get_or_init(|| {
        let path = std::env::var_os("DOME_OFFICE_BDF")
            .ok_or("DOME_OFFICE_BDF must name the installed DomeD BDF font")?;
        BitmapFont::read(std::path::Path::new(&path)).map(Arc::new)
    })
    .as_ref()
    .map_err(Clone::clone)
}
#[cfg(test)]
mod tests {
    use super::*;
    fn fixture() -> String {
        "STARTFONT 2.1\nFONTBOUNDINGBOX 8 8 0 -2\nCHARSET_REGISTRY \"ISO10646\"\nCHARSET_ENCODING \"1\"\nFAMILY_NAME \"Test\"\nPIXEL_SIZE 8\nFONT_ASCENT 6\nFONT_DESCENT 2\nDEFAULT_CHAR 63\nCHARS 2\nSTARTCHAR question\nENCODING 63\nDWIDTH 8 0\nBBX 8 2 0 0\nBITMAP\n80\n40\nENDCHAR\nSTARTCHAR A\nENCODING 65\nDWIDTH 6 0\nBBX 8 2 -1 1\nBITMAP\n70\n88\nENDCHAR\nENDFONT\n".into()
    }
    #[test]
    fn metrics_pixels_and_missing() {
        let f = BitmapFont::parse(fixture().as_bytes()).unwrap();
        assert_eq!(f.gid('A'), 66);
        assert_eq!(f.gid('日'), 0);
        assert_eq!(f.advance(66), 6);
        assert_eq!(f.width("AA", 8.), 12.);
        assert_eq!(
            f.rectangles(66),
            vec![[0., -3., 3., 1.], [-1., -2., 1., 1.], [3., -2., 1., 1.]]
        );
        assert_eq!(f.rectangles(0).len(), 2);
    }
    #[test]
    fn rejects_corruption() {
        for s in [
            fixture().replace("BBX 8 2", "BBX 8000 2"),
            fixture().replace("70\n", "GG\n"),
            fixture().replace("88\n", ""),
            fixture().replace("ENDFONT", ""),
            fixture().replace("ENCODING 65", "ENCODING 63"),
            fixture().replace("PIXEL_SIZE 8", "PIXEL_SIZE 0"),
        ] {
            assert!(BitmapFont::parse(s.as_bytes()).is_err());
        }
    }
    #[test]
    fn hostile_input_is_bounded() {
        let s = fixture();
        for end in 0..s.len() {
            let _ = BitmapFont::parse(&s.as_bytes()[..end]);
        }
        assert!(BitmapFont::parse(b"OTTO").is_err());
    }
}

#[derive(Clone, Debug)]
pub struct CatalogFace {
    pub file: String,
    pub family: String,
    pub style: String,
    pub pixels: i32,
}
pub fn catalog() -> &'static Vec<CatalogFace> {
    static CATALOG: OnceLock<Vec<CatalogFace>> = OnceLock::new();
    CATALOG.get_or_init(|| {
        let data = std::env::var("DOME_OFFICE_BDF_CATALOG").unwrap_or_default();
        if data.len() > 120 * 1024 {
            return Vec::new();
        }
        data.lines()
            .take(2048)
            .filter_map(|line| {
                let fields: Vec<_> = line.split('\t').collect();
                if fields.len() != 4
                    || fields[0].len() > 4096
                    || fields[1].len() > 256
                    || fields[2].len() > 256
                {
                    return None;
                }
                let pixels = fields[3].parse::<i32>().ok()?;
                if !(1..=256).contains(&pixels) || !std::path::Path::new(fields[0]).is_absolute() {
                    return None;
                }
                Some(CatalogFace {
                    file: fields[0].into(),
                    family: fields[1].into(),
                    style: fields[2].into(),
                    pixels,
                })
            })
            .collect()
    })
}
pub fn families() -> Vec<String> {
    let mut names: Vec<_> = catalog().iter().map(|f| f.family.clone()).collect();
    if let Ok(font) = from_env() {
        names.push(font.family.clone());
    }
    names.sort();
    names.dedup();
    names
}
pub fn styles(family: &str) -> Vec<String> {
    let mut styles: Vec<_> = catalog()
        .iter()
        .filter(|f| f.family.eq_ignore_ascii_case(family))
        .map(|f| f.style.clone())
        .collect();
    styles.sort();
    styles.dedup();
    styles
}
fn loaded(path: &str) -> Option<Arc<BitmapFont>> {
    if std::env::var_os("DOME_OFFICE_BDF").is_some_and(|p| p == std::ffi::OsStr::new(path)) {
        return from_env().ok().cloned();
    }
    static CACHE: std::sync::Mutex<(usize, BTreeMap<String, Arc<BitmapFont>>)> =
        std::sync::Mutex::new((0, BTreeMap::new()));
    let mut cache = CACHE
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner);
    if let Some(font) = cache.1.get(path) {
        return Some(font.clone());
    }
    if cache.1.len() >= 128 {
        return None;
    }
    let font = Arc::new(BitmapFont::read(std::path::Path::new(path)).ok()?);
    let bytes = font.data.len()
        + font.glyphs.len() * 96
        + font.glyphs.values().map(|g| g.bits.len()).sum::<usize>();
    if cache.0.saturating_add(bytes) > 128 * 1024 * 1024 {
        return None;
    }
    cache.0 += bytes;
    cache.1.insert(path.into(), font.clone());
    Some(font)
}
pub fn named(family: &str, style: &str) -> Option<Arc<BitmapFont>> {
    let pixels = from_env().ok()?.pixels;
    let mut choices: Vec<_> = catalog()
        .iter()
        .filter(|f| f.family.eq_ignore_ascii_case(family))
        .collect();
    choices.sort_by_key(|f| {
        (
            !f.style.eq_ignore_ascii_case(style),
            (f.pixels - pixels).abs(),
        )
    });
    choices.into_iter().find_map(|f| loaded(&f.file))
}
pub fn fallback(c: char) -> Option<Arc<BitmapFont>> {
    // The installed universal strikes are the normal fallback. Do not scan the
    // machine for outline fonts or eagerly load every bitmap family.
    catalog()
        .iter()
        .filter(|f| f.family.to_ascii_lowercase().contains("unifont"))
        .filter_map(|f| loaded(&f.file))
        .find(|f| f.gid(c) != 0)
}
thread_local! {static ACTIVE:std::cell::RefCell<Option<Arc<BitmapFont>>>=const{std::cell::RefCell::new(None)};}
pub struct FamilyGuard(Option<Arc<BitmapFont>>);
impl Drop for FamilyGuard {
    fn drop(&mut self) {
        ACTIVE.with(|a| *a.borrow_mut() = self.0.take());
    }
}
pub fn select_family(family: &str) -> FamilyGuard {
    select_style(family, "Regular")
}
pub fn select_style(family: &str, style: &str) -> FamilyGuard {
    let font = named(family, style).or_else(|| from_env().ok().cloned());
    FamilyGuard(ACTIVE.with(|a| std::mem::replace(&mut *a.borrow_mut(), font)))
}
pub fn active() -> Result<Arc<BitmapFont>, String> {
    ACTIVE
        .with(|a| a.borrow().clone())
        .map_or_else(|| from_env().cloned(), Ok)
}
