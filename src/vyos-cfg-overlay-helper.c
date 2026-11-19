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
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/types.h>

#define CFG_ROOT     "/opt/vyatta/config"
/* entries of the config root, only ever opened relative to it */
#define SESSION_DIR  "tmp"
#define ACTIVE_DIR   "active"
#define WORK_ROOT    "ovl_work"
#define WORK_PREFIX  "new_config_"
#define UPPER_PREFIX "changes_only_"

/* bounded so a session that is re-stacked repeatedly cannot grow without
 * limit; reset whenever the session is torn down */
#define MAX_GEN 4096

static const char *OVL_OPTS = "index=off,metacopy=off,redirect_dir=off,xino=off";

/* every name here is a compiled-in prefix joined to a session id of at most
 * ten digits, and every layer is passed as a /proc/self/fd path. the sizes
 * are provably large enough for that rather than relying on paths staying
 * short.
 */
#define NAMEBUF   32    /* a prefix plus a session id            */
#define FDBUF     32    /* /proc/self/fd/ plus a descriptor      */
#define OPTSBUF  256    /* three descriptor paths plus options   */

/* the config root is writable by the whole configuration group, so any entry
 * in it may be renamed or replaced by a symlink while we run. everything is
 * opened once, relative to the root and refusing symlinks, and from then on
 * used through these descriptors so that what was checked is what is used.
 */
static int cfg_fd = -1, tmp_fd = -1, active_fd = -1, work_fd = -1;

static int valid_sid(const char *s)
{
  size_t i, n = strlen(s);
  if (n < 1 || n > 10) return 0;
  for (i = 0; i < n; i++) if (!isdigit((unsigned char) s[i])) return 0;
  return 1;
}

static int open_dir_at(int at, const char *name)
{
  return openat(at, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static void close_keep_errno(int fd)
{
  int e = errno;
  if (fd >= 0) close(fd);
  errno = e;
}

static int open_roots(void)
{
  struct stat st;
  cfg_fd = open(CFG_ROOT, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (cfg_fd < 0) return -1;
  if ((tmp_fd = open_dir_at(cfg_fd, SESSION_DIR)) < 0) return -1;
  if ((active_fd = open_dir_at(cfg_fd, ACTIVE_DIR)) < 0) return -1;
  /* the config root is a fresh tmpfs on every boot, so the work root has to
   * be created here. only we may write to it, so it must be our own. */
  if (mkdirat(cfg_fd, WORK_ROOT, 0755) != 0 && errno != EEXIST) return -1;
  if ((work_fd = open_dir_at(cfg_fd, WORK_ROOT)) < 0) return -1;
  if (fstat(work_fd, &st) != 0) return -1;
  if (st.st_uid != geteuid()) {
    errno = EPERM;
    return -1;
  }
  return 0;
}

/* open a session's mount point and upper layer. both must be real directories
 * with the same owner, and if uid is not -1 that owner must be uid. only a
 * member of the configuration group can have created them, so this is what
 * keeps everyone else out - exactly as it was before the overlay existed.
 */
static int open_session(const char *sid, uid_t uid, int *mnt_fd, int *upper_fd)
{
  char name[NAMEBUF];
  struct stat ms, us;
  *mnt_fd = *upper_fd = -1;
  snprintf(name, sizeof(name), "%s%s", WORK_PREFIX, sid);
  if ((*mnt_fd = open_dir_at(tmp_fd, name)) < 0) goto fail;
  snprintf(name, sizeof(name), "%s%s", UPPER_PREFIX, sid);
  if ((*upper_fd = open_dir_at(tmp_fd, name)) < 0) goto fail;
  if (fstat(*mnt_fd, &ms) != 0 || fstat(*upper_fd, &us) != 0) goto fail;
  if (ms.st_uid != us.st_uid || (uid != (uid_t) -1 && ms.st_uid != uid)) {
    errno = EPERM;
    goto fail;
  }
  return 0;
fail:
  close_keep_errno(*mnt_fd);
  close_keep_errno(*upper_fd);
  *mnt_fd = *upper_fd = -1;
  return -1;
}

/* allocate an unused work directory for this mount. a lazily unmounted
 * overlay still owns the one it was given, so they are never reused.
 */
static int new_workdir(const char *sid)
{
  char gen_name[16];
  unsigned int gen;
  int base_fd, fd = -1;
  if (mkdirat(work_fd, sid, 02775) != 0 && errno != EEXIST) return -1;
  if ((base_fd = open_dir_at(work_fd, sid)) < 0) return -1;
  for (gen = 0; gen < MAX_GEN; gen++) {
    snprintf(gen_name, sizeof(gen_name), "%u", gen);
    if (mkdirat(base_fd, gen_name, 0755) == 0) {
      fd = open_dir_at(base_fd, gen_name);
      break;
    }
    if (errno != EEXIST) break;
  }
  if (gen == MAX_GEN) errno = ENOSPC;
  close_keep_errno(base_fd);
  return fd;
}

static int rm_rf_at(int at, const char *name)
{
  struct dirent *e;
  DIR *d;
  int fd = open_dir_at(at, name);
  if (fd >= 0) {
    if ((d = fdopendir(fd))) {
      while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (unlinkat(fd, e->d_name, 0) != 0) rm_rf_at(fd, e->d_name);
      }
      closedir(d);
    } else {
      close(fd);
    }
  }
  return unlinkat(at, name, AT_REMOVEDIR);
}

static int ovl_mount(const char *sid, uid_t uid)
{
  char target[FDBUF], opts[OPTSBUF];
  int mnt_fd, upper_fd, work, rc = 0;
  if (open_session(sid, uid, &mnt_fd, &upper_fd) != 0) {
    fprintf(stderr, "not a usable session: %s\n", strerror(errno));
    return 1;
  }
  if ((work = new_workdir(sid)) < 0) {
    fprintf(stderr, "cannot create a work directory: %s\n", strerror(errno));
    rc = 1;
  } else {
    snprintf(opts, sizeof(opts),
             "lowerdir=/proc/self/fd/%d,upperdir=/proc/self/fd/%d,"
             "workdir=/proc/self/fd/%d,%s",
             active_fd, upper_fd, work, OVL_OPTS);
    snprintf(target, sizeof(target), "/proc/self/fd/%d", mnt_fd);
    if (mount("overlay", target, "overlay", MS_NOSUID | MS_NODEV, opts) != 0) {
      fprintf(stderr, "overlay mount failed: %s\n", strerror(errno));
      rc = 1;
    }
    close(work);
  }
  close(mnt_fd);
  close(upper_fd);
  return rc;
}

/* unmount by name relative to the session directory rather than through a
 * descriptor, which would itself keep the mount busy. the name cannot be
 * redirected: the directory is pinned, the last component is not followed,
 * and a mount point cannot be renamed into place.
 */
static int umount_session(const char *sid, int flags)
{
  char name[NAMEBUF];
  snprintf(name, sizeof(name), "%s%s", WORK_PREFIX, sid);
  if (fchdir(tmp_fd) != 0) return -1;
  return umount2(name, flags | UMOUNT_NOFOLLOW);
}

static int ovl_umount(const char *sid, uid_t uid)
{
  int mnt_fd, upper_fd, detached = 0;
  if (open_session(sid, uid, &mnt_fd, &upper_fd) != 0) {
    fprintf(stderr, "not a usable session: %s\n", strerror(errno));
    return 1;
  }
  close(mnt_fd);
  close(upper_fd);
  if (umount_session(sid, 0) != 0) {
    if (errno != EBUSY || umount_session(sid, MNT_DETACH) != 0) {
      fprintf(stderr, "overlay umount failed: %s\n", strerror(errno));
      return 1;
    }
    detached = 1;
  }
  /* a lazily unmounted overlay still owns its work directory until the last
   * reference to it goes away; what is left behind goes with the tmpfs on
   * the next boot */
  if (!detached) rm_rf_at(work_fd, sid);
  return 0;
}

/* point every other live session at the replacement active tree */
static int ovl_restack(const char *self)
{
  DIR *d;
  struct dirent *e;
  int rc = 0, fd = open_dir_at(tmp_fd, ".");
  if (fd < 0 || !(d = fdopendir(fd))) {
    close_keep_errno(fd);
    return 0;
  }
  while ((e = readdir(d))) {
    const char *sid;
    int mnt_fd, upper_fd;
    if (strncmp(e->d_name, WORK_PREFIX, strlen(WORK_PREFIX))) continue;
    sid = e->d_name + strlen(WORK_PREFIX);
    if (!valid_sid(sid) || !strcmp(sid, self)) continue;
    if (open_session(sid, (uid_t) -1, &mnt_fd, &upper_fd) != 0) {
      rc = 1;
      continue;
    }
    close(mnt_fd);
    close(upper_fd);
    if (umount_session(sid, MNT_DETACH) != 0) { rc = 1; continue; }
    if (ovl_mount(sid, (uid_t) -1) != 0) rc = 1;
  }
  closedir(d);
  return rc;
}

int main(int argc, char **argv)
{
  const char *op, *sid;
  int mnt_fd, upper_fd;
  uid_t uid;
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
  /* the environment is not consulted for anything; drop it so nothing
   * inherited can influence what happens below */
  if (clearenv() != 0) return 2;

  /* who asked, recorded before any of this can change it: acquiring the
   * privilege set-user-ID way replaces the real uid with root's, and the
   * session ownership check below would then pass for anybody
   */
  uid = (getuid() == 0) ? (uid_t) -1 : getuid();

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
  if (open_roots() != 0) {
    fprintf(stderr, "cannot open the directories in %s: %s\n", CFG_ROOT,
            strerror(errno));
    return 1;
  }
  if (open_session(sid, uid, &mnt_fd, &upper_fd) != 0) {
    fprintf(stderr, "no such session, or it is not yours\n");
    return 2;
  }
  close(mnt_fd);
  close(upper_fd);
  if (!strcmp(op, "mount"))   return ovl_mount(sid, uid);
  if (!strcmp(op, "umount"))  return ovl_umount(sid, uid);
  if (!strcmp(op, "restack")) return ovl_restack(sid);
  fprintf(stderr, "unknown operation\n");
  return 2;
}
