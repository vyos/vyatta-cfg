/*
 * Program to set sysfs value - similar to sysctl commmand
 */

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define SYS "/sys"

static void get(const char *name)
{
	char path[PATH_MAX];
	char buf[BUFSIZ];
	FILE *f;

	snprintf(path, PATH_MAX, SYS "/%s", name);
	f = fopen(path, "r");
	if (f == NULL) {
		fprintf(stderr, "%s : %s\n", path, strerror(errno));
		exit(1);
	}

	while (fgets(buf, BUFSIZ, f) != NULL)
		fputs(buf, stdout);

	if (ferror(f)) {
		fprintf(stderr, "%s : read %s\n", path, strerror(errno));
		exit(1);
	}
	fclose(f);
}

static void set(const char *name, const char *val)
{
	FILE *f;
	char path[PATH_MAX];

	snprintf(path, PATH_MAX, SYS "/%s", name);
	f = fopen(path, "w");
	if (f == NULL) {
		fprintf(stderr, "%s : %s\n", path, strerror(errno));
		exit(1);
	}

	fprintf(f, "%s\n", val);
	fflush(f);

	if (ferror(f)) {
		fprintf(stderr, "%s : read %s\n", path, strerror(errno));
		exit(1);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	if (argc == 1) {
		fprintf(stderr, "Usage: %s variable\n", argv[0]);
		fprintf(stderr, "       %s variable=value\n", argv[0]);
		return 1;
	}

	while (--argc) {
		char *ep, *arg = *++argv;

		ep = strchr(arg, '=');
		if (!ep)
			get(arg);
		else {
			*ep++ = '\0';
			set(arg, ep);
		}
	}

	return 0;
}
