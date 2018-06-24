#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "type_check.h"

int
main (int argc, char *argv[])
{
  if (argc < 2) {
    printf("usage: vyatta-validate-type [-q] <type> <value>\n");
    return 1;
  }
  if (strcmp(argv[1], "-q") == 0){
    if (validateType(argv[2], argv[3], 1)) {
      return 0; 
    }
    return 1;
  } 
  if (validateType(argv[1], argv[2], 0)) {
    return 0; 
  }
  return 1;
}
