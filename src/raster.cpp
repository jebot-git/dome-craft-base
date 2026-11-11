// SPDX-License-Identifier: MIT
#include <artcraft/raster.hpp>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <signal.h>
extern char **environ;
namespace artcraft {
std::vector<unsigned char> decode_base64(const std::string &value,size_t limit) {
  if(value.size()%4 || value.size()/4>limit/3+2)throw std::runtime_error("Invalid/oversized base64 payload");
  auto digit=[](unsigned char c)->int {if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;};
  std::vector<unsigned char> bytes;bytes.reserve(std::min(limit,value.size()/4*3));
  for(size_t offset=0;offset<value.size();offset+=4) {
    const int a=digit(value[offset]),b=digit(value[offset+1]);
    const bool pc=value[offset+2]=='=',pd=value[offset+3]=='=';
    const int c=pc?0:digit(value[offset+2]),d=pd?0:digit(value[offset+3]);
    if(a<0||b<0||c<0||d<0||(pc&&!pd)||((pc||pd)&&offset+4!=value.size())||(pc&&(b&15))||(pd&&!pc&&(c&3)))throw std::runtime_error("Malformed base64 payload");
    bytes.push_back((a<<2)|(b>>4));if(!pc)bytes.push_back((b<<4)|(c>>2));if(!pd)bytes.push_back((c<<6)|d);
    if(bytes.size()>limit)throw std::runtime_error("Decoded payload exceeds limit");
  }
  return bytes;
}
std::pair<int,int> png_dimensions(const std::vector<unsigned char> &bytes) {
  static const unsigned char signature[]={137,80,78,71,13,10,26,10};
  if(bytes.size()<24||!std::equal(std::begin(signature),std::end(signature),bytes.begin()))throw std::runtime_error("Invalid page PNG");
  auto integer=[&bytes](size_t offset){return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|(uint32_t(bytes[offset+2])<<8)|bytes[offset+3];};
  auto width=integer(16),height=integer(20);
  if(!width||!height||width>4096||height>4096||uint64_t(width)*height>16*1024*1024)throw std::runtime_error("Page preview exceeds pixel bounds");
  return {int(width),int(height)};
}
std::string encode_base64(const std::vector<unsigned char> &value) {
  static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;result.reserve((value.size()+2)/3*4);
  for(size_t i=0;i<value.size();i+=3) {
    unsigned a=value[i],b=i+1<value.size()?value[i+1]:0,c=i+2<value.size()?value[i+2]:0;
    result+=alphabet[a>>2];result+=alphabet[((a&3)<<4)|(b>>4)];result+=i+1<value.size()?alphabet[((b&15)<<2)|(c>>6)]:'=';result+=i+2<value.size()?alphabet[c&63]:'=';
  }
  return result;
}
std::string bitmap_pdf_png(const std::string &encoded,int page) {
  if(page<0||page>100000)throw std::runtime_error("Invalid preview page");
  const auto bytes=decode_base64(encoded);
  if(bytes.size()<5||std::string(bytes.begin(),bytes.begin()+5)!="%PDF-")throw std::runtime_error("Preview backend returned no PDF");
  auto pattern=(std::filesystem::temp_directory_path()/"dome-office-preview-XXXXXX").string();
  if(!mkdtemp(pattern.data()))throw std::runtime_error("Cannot create private preview directory");
  struct Temporary {std::filesystem::path path;~Temporary(){std::error_code error;std::filesystem::remove_all(path,error);}} temporary{pattern};
  auto input=temporary.path/"input.pdf",prefix=temporary.path/"page";
  {std::ofstream output(input,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!output)throw std::runtime_error("Cannot write preview PDF");}
  std::vector<std::string> args{DOME_OFFICE_PDFTOPPM,"-f",std::to_string(page+1),"-l",std::to_string(page+1),"-r","96","-png","-singlefile",input.string(),prefix.string()};
  std::vector<char*> argv;for(auto &arg:args)argv.push_back(arg.data());argv.push_back(nullptr);
  pid_t child=-1;
  if(posix_spawn(&child,argv.front(),nullptr,nullptr,argv.data(),environ))throw std::runtime_error("Cannot start the PDF preview rasterizer");
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
  int status=0;
  while(true) {
    auto result=waitpid(child,&status,WNOHANG);
    if(result==child)break;
    if(result<0&&errno==EINTR)continue;
    if(result<0||std::chrono::steady_clock::now()>deadline) {kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){};throw std::runtime_error("PDF preview timed out or failed");}
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if(!WIFEXITED(status)||WEXITSTATUS(status)!=0)throw std::runtime_error("PDF preview rasterizer rejected the document");
  const auto output=prefix.string()+".png";
  if(std::filesystem::file_size(output)>16*1024*1024)throw std::runtime_error("PDF preview exceeds 16 MiB");
  std::ifstream png(output,std::ios::binary);std::vector<unsigned char> image((std::istreambuf_iterator<char>(png)),{});
  return encode_base64(image);
}
}
