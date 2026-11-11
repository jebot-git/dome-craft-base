// SPDX-License-Identifier: MIT
#include <artcraft/raster.hpp>
#include <stdexcept>
#include <iostream>
int main(){
 try{
  for(auto bytes:std::vector<std::vector<unsigned char>>{{},{0},{0,255},{0,1,2},{0,1,2,255,254}})
   if(artcraft::decode_base64(artcraft::encode_base64(bytes))!=bytes)throw std::runtime_error("Base64 round trip failed");
  for(auto value:{"A","!!!!","A===","AA=A","AAA=AAAA","AB==","AAB="}){
   bool rejected=false;try{artcraft::decode_base64(value);}catch(const std::exception&){rejected=true;}
   if(!rejected)throw std::runtime_error("Malformed base64 admitted");
  }
  bool rejected=false;try{artcraft::decode_base64("AAAA",1);}catch(const std::exception&){rejected=true;}
  if(!rejected)throw std::runtime_error("Decoded image budget was bypassed");
  return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
