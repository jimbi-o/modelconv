#include "modelconv/modelconv.h"
int main(const int argc, const char* args[]) {
  if (argc == 3) {
    modelconv::OutputToDirectory(args[1], args[2]);
  }
}
