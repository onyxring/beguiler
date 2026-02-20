//OBIT: the OnyxRing Beguile-Inform Transpiler
#include <iostream>
#include "orbit.h" 

//The application entry point, here we just create an instance of ORBIT's main class and pass the command line arguments to it.
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