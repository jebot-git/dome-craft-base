// SPDX-License-Identifier: MIT
#include "printing.hpp"
#include <artcraft/raster.hpp>
#include <FL/Fl_Printer.H>
#include <FL/Fl_PNG_Image.H>
#include <algorithm>
#include <stdexcept>
namespace dome_office {
void print_pages(const nlohmann::json &pages) {
  if(!pages.is_array()||pages.empty()||pages.size()>1000)throw std::runtime_error("Invalid print page set");
  Fl_Printer printer;
  int from=1,to=int(pages.size());
  if(printer.start_job(int(pages.size()),&from,&to))return;
  struct Job {Fl_Printer &printer;~Job(){printer.end_job();}} job{printer};
  from=std::clamp(from,1,int(pages.size()));to=std::clamp(to,from,int(pages.size()));
  for(int page=from;page<=to;++page) {
    auto bytes=artcraft::decode_base64(pages.at(page-1).at("png"),16*1024*1024);
    artcraft::png_dimensions(bytes);
    Fl_PNG_Image image(nullptr,bytes.data(),int(bytes.size()));
    if(image.fail()||image.w()<=0||image.h()<=0||image.w()>4096||image.h()>4096)throw std::runtime_error("Invalid print raster");
    if(printer.start_page())throw std::runtime_error("Printer could not start a page");
    int width=0,height=0;printer.printable_rect(&width,&height);
    printer.scale(std::min(float(width)/image.w(),float(height)/image.h()));
    image.draw(0,0);printer.end_page();
  }
}
}
