#![forbid(unsafe_code)]
#[allow(dead_code)]mod bitmap{include!("bitmap.rs");}
#[allow(dead_code)]mod bitmap_harf{include!("harf.rs");}
fn main(){
 let font=bitmap::from_env().unwrap();
 let latin=bitmap_harf::shape(font,"Ab",&[],false,"","",|c|c);
 assert_eq!(latin.iter().map(|g|g.advance).sum::<i32>(),font.advance(font.gid('A'))+font.advance(font.gid('b')));
 let marks=bitmap_harf::shape(font,"e\u{301}",&[],false,"","",|c|c);
 assert!(marks.len()==1 || marks.iter().skip(1).all(|g|g.advance==0));
 let arabic=bitmap_harf::shape(font,"سلام",&[],true,"","",|c|c);
 assert!(!arabic.is_empty()&&arabic.iter().all(|g|g.gid!=0));
 assert!(arabic.iter().any(|g|g.gid>0xfb50),"Arabic must use BDF presentation-form glyphs");
 println!("BDF HarfRust callbacks: Latin advances, combining marks and Arabic joining passed; Arabic glyphs {arabic:?}");
}
