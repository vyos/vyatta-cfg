/* vi: set sw=4 ts=4: */
/*
 * Module: rl_passwd.cc
 *
 * **** License ****
 * Version: VPL 1.0
 *
 * The contents of this file are subject to the Vyatta Public License
 * Version 1.0 ("License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.vyatta.com/vpl
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * This code was originally developed by Vyatta, Inc.
 * Portions created by Vyatta are Copyright (C) 2005, 2006, 2007 Vyatta, Inc.
 * All Rights Reserved.
 *
 * Author: Michael Larson
 * Date: 2005
 * Description:
 *
 * **** End License ****
 *
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <syslog.h>
#include <time.h>
#include <sys/types.h>
#include <shadow.h>
#include <pwd.h>
#include <sys/resource.h>
#include <errno.h>

static char crypt_passwd[128];

static int new_password(char * password);
static char *pw_encrypt(const char *clear, const char *salt);

void usage()
{
  printf("vyatta_pwd plainpwd username\n");

}

int main(int argc, char **argv)
{
  char *name;
  char * password;
  
  if (argc != 3) {
    usage();
    return(1);
  }

  /* "name" is unused */
  name = argv[2];
  password = argv[1];

  if (new_password(password)) {
    printf("error in password encrypt\n");
    return(1);
  }
  //  printf("%s, %s\n",name,password);
  printf("%s",crypt_passwd);
  return (0);
}



static int i64c(int i)
{
	if (i <= 0)
		return ('.');
	if (i == 1)
		return ('/');
	if (i >= 2 && i < 12)
		return ('0' - 2 + i);
	if (i >= 12 && i < 38)
		return ('A' - 12 + i);
	if (i >= 38 && i < 63)
		return ('a' - 38 + i);
	return ('z');
}

static char *crypt_make_salt(void)
{
	time_t now;
	static unsigned long x;
	static char result[3];

	time(&now);
	x += now + getpid() + clock();
	result[0] = i64c(((x >> 18) ^ (x >> 6)) & 077);
	result[1] = i64c(((x >> 12) ^ x) & 077);
	result[2] = '\0';
	return result;
}


static int new_password(char * password)
{
	char *cp;
	char salt[12]; /* "$N$XXXXXXXX" or "XX" */
	char orig[200];
	char pass[200];

	orig[0] = '\0';

	cp = (char*)password;

	strncpy(pass, cp, sizeof(pass));
	memset(cp, 0, strlen(cp));
	memset(cp, 0, strlen(cp));
	memset(orig, 0, sizeof(orig));
	memset(salt, 0, sizeof(salt));

	strcpy(salt, "$1$");
	strcat(salt, crypt_make_salt());
	strcat(salt, crypt_make_salt());
	strcat(salt, crypt_make_salt());
	
	strcat(salt, crypt_make_salt());
	cp = pw_encrypt(pass, salt);

	memset(pass, 0, sizeof pass);
	strncpy(crypt_passwd, cp, sizeof(crypt_passwd));
	return 0;
}

char *pw_encrypt(const char *clear, const char *salt)
{
	static char cipher[128];
	char *cp;
	cp = (char *) crypt(clear, salt);
	/* if crypt (a nonstandard crypt) returns a string too large,
	   truncate it so we don't overrun buffers and hope there is
	   enough security in what's left */
	strncpy(cipher, cp, sizeof(cipher));
	return cipher;
}

