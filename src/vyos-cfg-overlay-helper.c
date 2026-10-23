/*
 * Mounts and unmounts the overlay behind a CLI configuration session.
 *
 * Copyright (C) 2026 VyOS maintainers and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or later as
 * published by the Free Software Foundation.
 *
 * Mounting an overlay needs privilege the CLI itself must not hold. Granting
 * it to the CLI binaries would mean granting the right to ignore file
 * permissions, which is what keeps a user outside the configuration group out
 * of the config store in the first place. So the privilege lives here, in a
 * program that does nothing else, and the CLI runs with none.
 *
 * Everything this touches is derived from a session ID that must be a plain
 * number, joined to paths compiled in below. Nothing from the command line
 * reaches a path, a mount option, or a shell.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/types.h>

#define CFG_ROOT    "/opt/vyatta/config"
#define ACTIVE_DIR  CFG_ROOT "/active"
#define SESSION_DIR CFG_ROOT "/tmp"
#define WORK_ROOT   CFG_ROOT "/ovl_work"
#define WORK_PREFIX SESSION_DIR "/new_config_"
#define UPPER_PREFIX SESSION_DIR "/changes_only_"

/* bounded so a session that is re-stacked repeatedly cannot grow without
 * limit; reset whenever the session is torn down */
#define MAX_GEN 4096

static const char *OVL_OPTS = "index=off,metacopy=off,redirect_dir=off,xino=off";

/* every path here is a compiled-in prefix joined to a session id of at most
 * ten digits. the sizes step up so each use is provably large enough for
 * what is written into it, rather than relying on paths staying short.
 */
#define BASEBUF   128   /* a prefix plus a session id            */
#define PATHBUF   256   /* the above plus a generation number    */
#define CHILDBUF  512   /* the above plus a directory entry name */
#define OPTSBUF  1024   /* three paths plus the option list      */

static int valid_sid(const char *s)
{
  size_t i, n = strlen(s);
  if (n < 1 || n > 10) return 0;
  for (i = 0; i < n; i++) if (!isdigit((unsigned char) s[i])) return 0;
  return 1;
}

/* the caller must already own the session directory. only a member of the
 * configuration group can have created it, so this is what keeps everyone
 * else out - exactly as it was before the overlay existed.
 */
static int caller_owns_session(const char *sid)
{
  char p[BASEBUF];
  struct stat st;
  snprintf(p, sizeof(p), "%s%s", WORK_PREFIX, sid);
  if (lstat(p, &st) != 0) return 0;
  if (!S_ISDIR(st.st_mode)) return 0;
  if (getuid() == 0) return 1;
  return st.st_uid == getuid();
}

/* allocate an unused work directory for this mount. a lazily unmounted
 * overlay still owns the one it was given, so they are never reused.
 */
static int new_workdir(const char *sid, char *out, size_t outlen)
{
  char base[BASEBUF];
  unsigned int gen;
  snprintf(base, sizeof(base), "%s/%s", WORK_ROOT, sid);
  if (mkdir(base, 02775) != 0 && errno != EEXIST) return -1;
  for (gen = 0; gen < MAX_GEN; gen++) {
    snprintf(out, outlen, "%s/%u", base, gen);
    if (mkdir(out, 0755) == 0) return 0;
    if (errno != EEXIST) return -1;
  }
  errno = ENOSPC;
  return -1;
}

static int rm_rf(const char *path)
{
  char child[CHILDBUF];
  struct dirent *e;
  DIR *d = opendir(path);
  if (d) {
    while ((e = readdir(d))) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
      if (unlink(child) != 0) rm_rf(child);
    }
    closedir(d);
  }
  return rmdir(path);
}

static int ovl_mount(const char *sid)
{
  char lower[BASEBUF], upper[BASEBUF], mnt[BASEBUF], work[PATHBUF];
  char opts[OPTSBUF];
  snprintf(lower, sizeof(lower), "%s", ACTIVE_DIR);
  snprintf(upper, sizeof(upper), "%s%s", UPPER_PREFIX, sid);
  snprintf(mnt,   sizeof(mnt),   "%s%s", WORK_PREFIX, sid);
  if (new_workdir(sid, work, sizeof(work)) != 0) {
    fprintf(stderr, "cannot create a work directory: %s\n", strerror(errno));
    return 1;
  }
  snprintf(opts, sizeof(opts), "lowerdir=%s,upperdir=%s,workdir=%s,%s",
           lower, upper, work, OVL_OPTS);
  if (mount("overlay", mnt, "overlay", 0, opts) != 0) {
    fprintf(stderr, "overlay mount failed: %s\n", strerror(errno));
    return 1;
  }
  return 0;
}

static int ovl_umount(const char *sid)
{
  char mnt[BASEBUF], base[BASEBUF];
  int detached = 0;
  snprintf(mnt,  sizeof(mnt),  "%s%s", WORK_PREFIX, sid);
  snprintf(base, sizeof(base), "%s/%s", WORK_ROOT, sid);
  if (umount2(mnt, 0) != 0) {
    if (errno != EBUSY || umount2(mnt, MNT_DETACH) != 0) {
      fprintf(stderr, "overlay umount failed: %s\n", strerror(errno));
      return 1;
    }
    detached = 1;
  }
  /* a lazily unmounted overlay still owns its work directory until the last
   * reference to it goes away; what is left behind goes with the tmpfs on
   * the next boot */
  if (!detached) rm_rf(base);
  return 0;
}

/* point every other live session at the replacement active tree */
static int ovl_restack(const char *self)
{
  DIR *d = opendir(SESSION_DIR);
  struct dirent *e;
  int rc = 0;
  if (!d) return 0;
  while ((e = readdir(d))) {
    const char *sid;
    if (strncmp(e->d_name, "new_config_", 11)) continue;
    sid = e->d_name + 11;
    if (!valid_sid(sid) || !strcmp(sid, self)) continue;
    char mnt[BASEBUF];
    snprintf(mnt, sizeof(mnt), "%s%s", WORK_PREFIX, sid);
    if (umount2(mnt, MNT_DETACH) != 0) { rc = 1; continue; }
    if (ovl_mount(sid) != 0) rc = 1;
  }
  closedir(d);
  return rc;
}

int main(int argc, char **argv)
{
  const char *op, *sid;
  if (argc != 3) {
    fprintf(stderr, "usage: %s mount|umount|restack <session-id>\n", argv[0]);
    return 2;
  }
  op = argv[1];
  sid = argv[2];
  if (!valid_sid(sid)) {
    fprintf(stderr, "not a session id\n");
    return 2;
  }
  if (!caller_owns_session(sid)) {
    fprintf(stderr, "no such session, or it is not yours\n");
    return 2;
  }
  /* the environment is not consulted for anything; drop it so nothing
   * inherited can influence what happens below */
  if (clearenv() != 0) return 2;

  /* Two ways to hold the privilege this needs. Installed with file
   * capabilities, the process never becomes root and carries only the two
   * rights the mount requires. Installed set-user-ID, it becomes root
   * outright. The former is preferred; the latter is the fallback for
   * filesystems that cannot carry capabilities.
   */
  if (geteuid() == 0 && getuid() != 0 && setuid(0) != 0) {
    fprintf(stderr, "cannot acquire privilege: %s\n", strerror(errno));
    return 2;
  }
  if (!strcmp(op, "mount"))   return ovl_mount(sid);
  if (!strcmp(op, "umount"))  return ovl_umount(sid);
  if (!strcmp(op, "restack")) return ovl_restack(sid);
  fprintf(stderr, "unknown operation\n");
  return 2;
}
