// SPDX-License-Identifier: MIT
#include "../ports/command-form.hpp"
#include <artcraft/application.hpp>
#include <FL/Fl.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Check_Button.H>
#include <iostream>
#include <stdexcept>
namespace {
void values(Fl_Group &group,int &number){
 for(int i=0;i<group.children();++i){auto *child=group.child(i);
  if(auto *input=dynamic_cast<Fl_Float_Input*>(child))input->value(++number==1?"2":"3");
  if(auto *check=dynamic_cast<Fl_Check_Button*>(child))if(check->active())check->value(1);
  if(auto *nested=dynamic_cast<Fl_Group*>(child))values(*nested,number);
 }
}
void apply(void*){
 auto *window=Fl::first_window();if(!window){Fl::repeat_timeout(.05,apply);return;}
 int number=0;values(*window,number);
 for(int i=0;i<window->children();++i)if(auto *button=dynamic_cast<Fl_Button*>(window->child(i)))if(button->label()&&std::string(button->label())=="Apply"){button->do_callback();return;}
 throw std::runtime_error("No native Apply action");
}
}
int main(){try{
 Fl::add_timeout(.2,apply);
 auto result=dome_office::command_form("org.domed.Office.FormTest",{{"label","Insert table"},{"params","{\"rows\": n, \"cols\": n, \"header\"?: bool}"}});
 if(!result||!result->at("rows").is_number_integer()||result->at("rows")!=2||result->at("cols")!=3||result->at("header")!=true)throw std::runtime_error("Native dialog produced incorrect typed parameters");
 std::cout<<"Native bitmap/GPU command dialog produces integer and boolean parameters\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
