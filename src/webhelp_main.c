/* webhelp standalone entry point. */
#include "ew.h"

int main(int argc, char** argv) {
  return webhelp_main(argc - 1, argv + 1);
}
