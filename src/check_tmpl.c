#include <stdlib.h>
#include <stdio.h>

#include "cli_val.h"

int
check_line_continuation(FILE *ftmpl)
{
  char buf[256];
  int line = 0;
  while (fgets(buf, 256, ftmpl) != NULL) {
    int len = strlen(buf);
    ++line;
    if (len < 2 || buf[len - 1] != '\n' || !isblank(buf[len - 2])) {
      continue;
    }
    len -= 3;
    while (len >= 0 && isblank(buf[len])) {
      --len;
    }
    if (len >= 0 && buf[len] == '\\') {
      printf("Warning: \"backslash + space\" detected at the end of line %d.\n"
             "         This will not work as line continuation.\n",
             line);
      continue;
    }
  }
  return 0;
}

int
main(int argc, char **argv)
{
  int status = 0;
  vtw_def def;
  FILE *ftmpl = NULL;
  
  if (argc != 2) {
    printf("Usage: check_tmpl <tmpl_file>\n");
    exit(-1);
  }

  memset(&def, 0, sizeof(def));
  status = parse_def(&def, argv[1], 0);
  if (status == -5) {
    printf("Cannot open [%s]\n", argv[1]);
    exit(-1);
  } else if (status != 0) {
    printf("Parse error in [%s]\n", argv[1]);
    exit(-1);
  }

  if ((ftmpl = fopen(argv[1], "r")) == NULL) {
    printf("Cannot open [%s]\n", argv[1]);
    exit(-1);
  }
 
  /* check for other errors */
  if (check_line_continuation(ftmpl) != 0) {
    exit(-1);
  }
  fseek(ftmpl, 0, SEEK_SET);

  fclose(ftmpl);
  
  printf("OK\n");
  return 0;
}

