// SPDX-License-Identifier: MIT
// HarfRust callbacks read BDF glyphs and metrics. The empty table directory is
// shaping metadata only: it contains no cmap, glyph outlines, bitmap or font asset.
use harfrust::font::{BuiltinFontFuncs, FontFuncs};
use harfrust::{GlyphExtents, GlyphId};
struct Functions<'a>(&'a super::bitmap::BitmapFont);
impl FontFuncs for Functions<'_> {
    fn nominal_glyph(&mut self, _: &BuiltinFontFuncs, c: u32) -> Option<GlyphId> {
        let c = char::from_u32(c)?;
        let gid = self.0.gid(c);
        (gid != 0).then_some(GlyphId::new(gid))
    }
    fn advance_width(&mut self, _: &BuiltinFontFuncs, g: GlyphId) -> i32 {
        self.0.advance(g.to_u32())
    }
    fn advance_height(&mut self, _: &BuiltinFontFuncs, _: GlyphId) -> i32 {
        -self.0.pixels
    }
    fn vertical_origin(&mut self, _: &BuiltinFontFuncs, g: GlyphId) -> (i32, i32) {
        (self.0.advance(g.to_u32()) / 2, self.0.ascent)
    }
    fn extents(&mut self, _: &BuiltinFontFuncs, g: GlyphId) -> Option<GlyphExtents> {
        let glyph = self.0.glyph(g.to_u32())?;
        Some(GlyphExtents {
            x_bearing: glyph.x,
            y_bearing: glyph.y + glyph.height as i32,
            width: glyph.width as i32,
            height: -(glyph.height as i32),
        })
    }
}
#[derive(Debug)]
pub struct Shaped {
    pub gid: u32,
    pub cluster: usize,
    pub advance: i32,
    pub x: i32,
    pub y: i32,
    #[allow(dead_code)]
    pub source: Option<std::sync::Arc<super::bitmap::BitmapFont>>,
    #[allow(dead_code)]
    pub units_scale: f64,
}
pub fn shape(
    font: &super::bitmap::BitmapFont,
    text: &str,
    features: &[harfrust::Feature],
    rtl: bool,
    before: &str,
    after: &str,
    map: impl Fn(char) -> char,
) -> Vec<Shaped> {
    // Only `head.unitsPerEm` is present so the shaper's fallback mark positioning
    // uses native strike units. All glyph/coverage/extent queries use Functions.
    let mut tables = [0u8; 82];
    tables[0..4].copy_from_slice(&[0, 1, 0, 0]);
    tables[5] = 1;
    tables[12..16].copy_from_slice(b"head");
    tables[23] = 28;
    tables[27] = 54;
    tables[46..48].copy_from_slice(&(font.pixels as u16).to_be_bytes());
    let Ok(reference) = harfrust::FontRef::new(&tables) else {
        return Vec::new();
    };
    let data = harfrust::ShaperData::new(&reference);
    let shaper = data.shaper(&reference).build();
    let mut functions = Functions(font);
    let mut buffer = harfrust::UnicodeBuffer::new();
    for (offset, c) in text.char_indices() {
        buffer.add(map(c), offset as u32);
    }
    buffer.set_pre_context(before);
    buffer.set_post_context(after);
    buffer.guess_segment_properties();
    buffer.set_direction(if rtl {
        harfrust::Direction::RightToLeft
    } else {
        harfrust::Direction::LeftToRight
    });
    let result = shaper.shape(
        buffer,
        harfrust::ShapeOptions::new()
            .features(features)
            .font_funcs(Some(&mut functions)),
    );
    result
        .glyph_infos()
        .iter()
        .zip(result.glyph_positions())
        .map(|(info, p)| Shaped {
            gid: info.glyph_id,
            cluster: info.cluster as usize,
            advance: p.x_advance,
            x: p.x_offset,
            y: p.y_offset,
            source: None,
            units_scale: 1.,
        })
        .collect()
}
// Bidirectional line order for spreadsheet cells and plain PDF furniture.
#[allow(dead_code)]
pub fn line(font: &super::bitmap::BitmapFont, text: &str) -> Vec<Shaped> {
    let bidi = unicode_bidi::BidiInfo::new(text, None);
    let mut out = Vec::new();
    for paragraph in &bidi.paragraphs {
        let (levels, runs) = bidi.visual_runs(paragraph, paragraph.range.clone());
        for range in runs {
            let Some(piece) = text.get(range.clone()) else {
                continue;
            };
            let rtl = levels.get(range.start).is_some_and(|l| l.is_rtl());
            // Keep adjacent characters in the same bitmap face, so joining and
            // combining marks still shape together. Universal fallback is BDF too.
            let mut pieces: Vec<(
                std::ops::Range<usize>,
                Option<std::sync::Arc<super::bitmap::BitmapFont>>,
            )> = Vec::new();
            for (offset, c) in piece.char_indices() {
                let face = if font.gid(c) != 0
                    || matches!(unicode_bidi::bidi_class(c), unicode_bidi::BidiClass::BN)
                {
                    None
                } else {
                    super::bitmap::fallback(c)
                };
                let same = pieces
                    .last()
                    .is_some_and(|(_, previous)| match (previous, &face) {
                        (None, None) => true,
                        (Some(a), Some(b)) => std::sync::Arc::ptr_eq(a, b),
                        _ => false,
                    });
                if same {
                    if let Some((range, _)) = pieces.last_mut() {
                        range.end = offset + c.len_utf8();
                    }
                } else {
                    pieces.push((offset..offset + c.len_utf8(), face));
                }
            }
            if rtl {
                pieces.reverse();
            }
            for (part, source) in pieces {
                let chosen = source.as_deref().unwrap_or(font);
                let start = range.start + part.start;
                let end = range.start + part.end;
                let Some(text_piece) = text.get(start..end) else {
                    continue;
                };
                let mut glyphs = shape(
                    chosen,
                    text_piece,
                    &[],
                    rtl,
                    text.get(..start).unwrap_or(""),
                    text.get(end..).unwrap_or(""),
                    |c| c,
                );
                for g in &mut glyphs {
                    g.cluster += start;
                    g.units_scale = font.pixels as f64 / chosen.pixels as f64;
                    g.source = source.clone();
                }
                out.extend(glyphs);
            }
        }
    }
    out
}
