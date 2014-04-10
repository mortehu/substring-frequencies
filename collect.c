#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <err.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sysexits.h>
#include <unistd.h>

int main(int argc, char **argv) {
  int i, ret, fd;

  if (argc < 2) errx(EX_USAGE, "Usage: %s FILE...", argv[0]);

  for (i = 1; i < argc; ++i) {
    if (i > 1) {
      if (1 != (ret = write(STDOUT_FILENO, "", 1))) {
        if (ret == 0)
          errx(EXIT_FAILURE,
               "write returned 0 while writing NUL byte to standard output");
        else
          err(EXIT_FAILURE, "Failed to write to standard output");
      }
    }

    if (-1 == (fd = open(argv[i], O_RDONLY)))
      err(EXIT_FAILURE, "Failed to open '%s' for reading", argv[i]);

    while (0 < (ret = sendfile(STDOUT_FILENO, fd, NULL, INT_MAX)))
      ;

    if (ret == -1) err(EXIT_FAILURE, "sendfile failed");

    close(fd);
  }

  return EXIT_SUCCESS;
}
