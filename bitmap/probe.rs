#![forbid(unsafe_code)]
#[allow(dead_code)]mod bitmap{include!("bitmap.rs");}
fn main(){
 let path=std::env::args_os().nth(1).unwrap_or_default();
 match bitmap::BitmapFont::read(std::path::Path::new(&path)){
  Ok(f)=>println!("{}: {}px, {} glyphs, 'A' advance {}, '日' glyph {}",f.family,f.pixels,f.glyphs.len(),f.advance(f.gid('A')),f.gid('日')),
  Err(e)=>{eprintln!("{e}");std::process::exit(1);}
 }
}
