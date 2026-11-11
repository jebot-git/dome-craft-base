// SPDX-License-Identifier: MIT
#include <artcraft/application.hpp>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
int main(){
 try{
  artcraft::Application app({"org.domed.Office.FontProbe","Office font probe",400,300,false});
  auto info=app.platform_info();
  std::cout<<nlohmann::json{{"DOME_OFFICE_BDF",info.font_file},{"DOME_OFFICE_BDF_CATALOG",getenv("DOME_OFFICE_BDF_CATALOG")?getenv("DOME_OFFICE_BDF_CATALOG"):""}}.dump()<<'\n';
  return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
