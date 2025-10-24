// the OnyxRing Beguile/Inform Transpiler
#include <iostream>

#include "settings.h" 
#include "orbit.h" 

int main(int argc, char* argv[]) {
  orbit app;
  try{
    app.go(argc, argv);
  }
  catch(std::runtime_error &e){
    std::cerr<<std::endl<<e.what()<<std::endl;
  }
  return 0;
}