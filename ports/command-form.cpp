// SPDX-License-Identifier: MIT
#include "command-form.hpp"
#include <artcraft/application.hpp>
#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>
#include <cctype>
#include <cmath>
#include <set>
#include <vector>
namespace dome_office {
namespace {
using Json=nlohmann::json;
std::string trim(std::string value){auto a=value.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};return value.substr(a,value.find_last_not_of(" \t\r\n")-a+1);}
std::string label(std::string value){for(size_t i=1;i<value.size();++i)if(std::isupper(static_cast<unsigned char>(value[i]))&&std::islower(static_cast<unsigned char>(value[i-1]))){value.insert(i," ");++i;}if(!value.empty())value[0]=std::toupper(static_cast<unsigned char>(value[0]));return value;}
struct Field {std::string key,type;bool optional=false;Fl_Check_Button *include=nullptr,*boolean=nullptr;Fl_Input *input=nullptr;Fl_Choice *choice=nullptr;std::vector<std::string> choices;};
std::vector<Field> fields(const std::string &description){
  std::vector<Field> out;std::set<std::string> seen;auto at=description.find('{');if(at==std::string::npos)return out;
  ++at;
  while(at<description.size()&&out.size()<32){
    while(at<description.size()&&(std::isspace(static_cast<unsigned char>(description[at]))||description[at]==','))++at;
    if(at==description.size()||description[at]=='}')break;
    bool quoted=description[at]=='"';if(quoted)++at;auto start=at;
    while(at<description.size()&&(quoted?description[at]!='"':std::isalnum(static_cast<unsigned char>(description[at]))||description[at]=='_'))++at;
    auto key=description.substr(start,at-start);if(quoted&&at<description.size())++at;
    bool optional=at<description.size()&&description[at]=='?';if(optional)++at;
    while(at<description.size()&&std::isspace(static_cast<unsigned char>(description[at])))++at;
    if(at==description.size()||description[at]!=':')break;
    start=++at;int depth=0;bool string=false,escaped=false;
    for(;at<description.size();++at){char c=description[at];if(escaped){escaped=false;continue;}if(c=='\\'&&string){escaped=true;continue;}if(c=='"'){string=!string;continue;}if(string)continue;if(c=='['||c=='{')++depth;else if(c==']'||c=='}'){if(!depth)break;--depth;}if(c==','&&!depth)break;}
    if(!key.empty()&&seen.insert(key).second)out.push_back({key,trim(description.substr(start,at-start)),optional});
  }
  return out;
}
}
std::optional<Json> command_form(const std::string &app,const Json &command){
  auto description=command.value("params",std::string());auto spec=fields(description);
  if(spec.empty()&&(description.empty()||trim(description)=="{}"))return Json::object();
  bool fallback=spec.empty();if(fallback)spec.push_back({"parameters","object",false});
  artcraft::Application dialog({app+".CommandDialog",command.value("label",std::string("Command")),640,std::min(690,220+int(spec.size())*54),false});
  dialog.window().set_modal();dialog.window().begin();const int top=dialog.content_top();
  auto *help=new Fl_Box(12,top+8,616,64);help->copy_label(description.c_str());help->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_WRAP|FL_ALIGN_CLIP);
  auto *scroll=new Fl_Scroll(12,top+78,616,dialog.window().h()-top-138);
  int y=top+92;
  for(auto &field:spec){
    field.include=new Fl_Check_Button(22,y,190,28);field.include->copy_label(label(field.key).c_str());field.include->value(!field.optional);if(!field.optional)field.include->deactivate();
    if(field.type.starts_with("bool")||field.type.starts_with("true")||field.type.starts_with("false")){field.boolean=new Fl_Check_Button(222,y,370,28,"Enabled");}
    else {
      if(field.type.starts_with('"')&&field.type.find('|')!=std::string::npos){
        std::string choices=field.type;size_t at=0;
        while(at<choices.size()){auto end=choices.find('|',at);if(end==std::string::npos)end=choices.size();auto value=trim(choices.substr(at,end-at));while(!value.empty()&&(value.front()=='"'||value.front()=='\''))value.erase(value.begin());while(!value.empty()&&(value.back()=='"'||value.back()=='\''))value.pop_back();if(!value.empty())field.choices.push_back(value);at=end+1;}
        field.choice=new Fl_Choice(222,y,370,28);for(const auto &choice:field.choices)field.choice->add(choice.c_str());field.choice->value(0);
      }else{
        bool number=field.type.starts_with("number")||field.type.starts_with("int")||field.type=="n"||field.type.starts_with("pt")||field.type.starts_with("ms")||(!field.type.empty()&&(std::isdigit(static_cast<unsigned char>(field.type[0]))||field.type[0]=='-'));
        field.input=field.key.find("password")!=std::string::npos?static_cast<Fl_Input*>(new Fl_Secret_Input(222,y,370,28)):number?static_cast<Fl_Input*>(new Fl_Float_Input(222,y,370,28)):new Fl_Input(222,y,370,28);
        field.input->tooltip(field.type.c_str());
        if(field.type.starts_with('['))field.input->value("[]");else if(field.type.starts_with('{')||field.type=="object"||field.type=="Pos")field.input->value("{}");
        if(number){size_t used=0;try{double value=std::stod(field.type,&used);if(std::isfinite(value)){auto text=field.type.substr(0,used);field.input->value(text.c_str());}}catch(const std::exception&){}}
        if(field.key=="path"||field.key=="file"){
          field.input->resize(222,y,280,28);auto *button=new Fl_Button(510,y,82,28,"Browse");
          button->callback([](Fl_Widget*,void *data){Fl_Native_File_Chooser chooser;chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);if(!chooser.show()&&chooser.filename())static_cast<Fl_Input*>(data)->value(chooser.filename());},field.input);
        }
      }
    }
    y+=50;
  }
  scroll->end();bool apply=false;
  dialog.add_command({"command.apply","&Command/&Apply",FL_CTRL+FL_Enter,false,[&]{apply=true;dialog.quit();},{}});
  dialog.add_command({"app.close","&Command/&Cancel",FL_Escape,false,[&]{dialog.quit();},{}});
  auto *button=new Fl_Button(12,dialog.window().h()-48,130,32,"Apply");button->callback([](Fl_Widget*,void *data){static_cast<artcraft::Application*>(data)->invoke("command.apply");},&dialog);
  auto *cancel=new Fl_Button(160,dialog.window().h()-48,130,32,"Cancel");cancel->callback([](Fl_Widget*,void *data){static_cast<artcraft::Application*>(data)->quit();},&dialog);
  dialog.window().resizable(scroll);dialog.window().end();dialog.run();if(!apply)return {};
  Json params=Json::object();
  for(const auto &field:spec){
    if(field.optional&&!field.include->value())continue;
    if(field.boolean)params[field.key]=bool(field.boolean->value());
    else if(field.choice)params[field.key]=field.choices.at(field.choice->value());
    else {
      std::string value=field.input->value();if(value.size()>128*1024)throw std::runtime_error("Command field exceeds 128 KiB");auto type=field.type;
      if(type.starts_with('[')||type.starts_with('{')||type=="object"||type=="Pos"){
        auto parsed=Json::parse(value,[](int depth,Json::parse_event_t,Json&){if(depth>32)throw std::runtime_error("Command value is too deeply nested");return true;},false);if(parsed.is_discarded())throw std::runtime_error("Invalid structured value for "+field.key);params[field.key]=std::move(parsed);
      }else if(dynamic_cast<Fl_Float_Input*>(field.input)){
        size_t used=0;double number=std::stod(value,&used);if(used!=value.size()||!std::isfinite(number))throw std::runtime_error("Invalid number for "+field.key);if(std::floor(number)==number&&std::abs(number)<9e15)params[field.key]=static_cast<int64_t>(number);else params[field.key]=number;
      }else params[field.key]=value;
    }
  }
  if(fallback)return params.at("parameters");return params;
}
}
