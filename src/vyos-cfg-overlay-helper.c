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
 * set-user-ID program that does nothing else, and the CLI runs with none.
 *
 * Everything this touches is derived from a session ID that must be a plain
 * number, joined to paths compiled in below. Nothing from the command line
 * reaches a path, a mount option, or a shell. Every operation acts on the
 * caller's own session only; no session is ever mounted, unmounted or
 * modified on behalf of another one.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>

#define CFG_ROOT     "/opt/vyatta/config"
/* entries of the config root, only ever opened relative to it */
#define SESSION_DIR  "tmp"
#define ACTIVE_DIR   "active"
#define WORK_ROOT    "ovl_work"
#define WORK_PREFIX  "new_config_"
#define UPPER_PREFIX "changes_only_"

/* one lock per session serializes invocations for it. outside the
 * configuration root, where no group member can replace it, and per session
 * so that a stalled invocation holds up nobody else */
#define LOCK_DIR     "/run/vyos-cfg-overlay-helper"

/* held exclusively by a commit while the set of active trees changes */
#define TREE_LOCK    ".tree.lock"

/* per-session files in the session's work directory base */
#define CURRENT_FILE "current"  /* work directory generation of the mount */
#define LOWER_FILE   "lower"    /* identity of the active tree it stacks on */

/* bounds the work directories of one session. a generation is removed again
 * when its overlay is unmounted cleanly, so only lazily detached overlays -
 * which still own theirs - accumulate */
#define MAX_GEN 4096

static const char *OVL_OPTS = "index=off,metacopy=off,redirect_dir=off,xino=off";

/* every name is a compiled-in prefix joined to a session id of at most ten
 * digits and at most a 17 character suffix; every layer is passed as a
 * /proc/self/fd path */
#define NAMEBUF   64
#define FDBUF     32
#define OPTSBUF  256
#define RECBUF    64

/* the config root is writable by the whole configuration group, so any entry
 * in it may be renamed or replaced by a symlink while we run. everything is
 * opened relative to it and refusing symlinks, and from then on used through
 * descriptors so that what was checked is what is used.
 */
static int cfg_fd = -1, tmp_fd = -1, work_fd = -1, active_fd = -1;

static int valid_number(const char *s)
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

static int same_inode(int a, int b)
{
  struct stat sa, sb;
  return (fstat(a, &sa) == 0 && fstat(b, &sb) == 0
          && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino);
}

/* a directory only we may write to: our own, and writable by nobody else.
 * things are removed in here as root, so anything else is refused. */
static int private_dir(int fd)
{
  struct stat st;
  if (fstat(fd, &st) != 0) return -1;
  if (st.st_uid != geteuid() || (st.st_mode & 022)) {
    errno = EPERM;
    return -1;
  }
  return 0;
}

static int open_roots(void)
{
  cfg_fd = open(CFG_ROOT, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (cfg_fd < 0) return -1;
  if ((tmp_fd = open_dir_at(cfg_fd, SESSION_DIR)) < 0) return -1;
  /* the config root is a fresh tmpfs on every boot */
  if (mkdirat(cfg_fd, WORK_ROOT, 0755) != 0 && errno != EEXIST) return -1;
  if ((work_fd = open_dir_at(cfg_fd, WORK_ROOT)) < 0) return -1;
  return private_dir(work_fd);
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

/* an overlay has a device of its own, so a mounted session differs from the
 * directory it lives in */
static int is_mounted(int mnt_fd)
{
  struct stat ms, ts;
  if (fstat(mnt_fd, &ms) != 0 || fstat(tmp_fd, &ts) != 0) return -1;
  return ms.st_dev != ts.st_dev;
}

/* the session's directory below the work root, created if need be */
static int open_session_base(const char *sid, int create)
{
  int fd;
  if (create && mkdirat(work_fd, sid, 0755) != 0 && errno != EEXIST) return -1;
  if ((fd = open_dir_at(work_fd, sid)) < 0) return -1;
  if (private_dir(fd) != 0) {
    close_keep_errno(fd);
    return -1;
  }
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

static int write_small_file(int dfd, const char *name, const char *text)
{
  int fd = openat(dfd, name, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW
                  | O_CLOEXEC, 0644);
  ssize_t n = (ssize_t) strlen(text);
  if (fd < 0) return -1;
  if (write(fd, text, n) != n) {
    close_keep_errno(fd);
    return -1;
  }
  return close(fd);
}

static void read_small_file(int dfd, const char *name, char *buf, size_t size)
{
  ssize_t n = -1;
  int fd = openat(dfd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd >= 0) {
    n = read(fd, buf, size - 1);
    close(fd);
  }
  buf[n > 0 ? n : 0] = '\0';
}

/* allocate an unused work directory for this mount and record it as the
 * session's current one. a lazily detached overlay still owns the one it was
 * given, so a directory is never reused while it exists.
 */
static int new_workdir(const char *sid)
{
  char gen_name[16];
  unsigned int gen;
  int base_fd, fd = -1;
  if ((base_fd = open_session_base(sid, 1)) < 0) return -1;
  for (gen = 0; gen < MAX_GEN; gen++) {
    snprintf(gen_name, sizeof(gen_name), "%u", gen);
    if (mkdirat(base_fd, gen_name, 0755) == 0) {
      fd = open_dir_at(base_fd, gen_name);
      break;
    }
    if (errno != EEXIST) break;
  }
  if (gen == MAX_GEN) errno = ENOSPC;
  if (fd >= 0 && write_small_file(base_fd, CURRENT_FILE, gen_name) != 0) {
    close_keep_errno(fd);
    fd = -1;
  }
  close_keep_errno(base_fd);
  return fd;
}

/* release the work directory of an overlay that is gone, and the record of
 * what it stacked on. with no detached overlays left, the base goes too. */
static void drop_current_workdir(const char *sid)
{
  char gen_name[16];
  int base = open_session_base(sid, 0);
  if (base < 0) return;
  read_small_file(base, CURRENT_FILE, gen_name, sizeof(gen_name));
  if (valid_number(gen_name)) rm_rf_at(base, gen_name);
  unlinkat(base, CURRENT_FILE, 0);
  unlinkat(base, LOWER_FILE, 0);
  close(base);
  unlinkat(work_fd, sid, AT_REMOVEDIR);
}

/* a detached overlay keeps its work directory; it is no longer the session's */
static void forget_current_workdir(const char *sid)
{
  int base = open_session_base(sid, 0);
  if (base < 0) return;
  unlinkat(base, CURRENT_FILE, 0);
  unlinkat(base, LOWER_FILE, 0);
  close(base);
}

static void format_identity(int fd, char *buf, size_t size)
{
  struct stat st;
  buf[0] = '\0';
  if (fstat(fd, &st) == 0)
    snprintf(buf, size, "%lu:%lu", (unsigned long) st.st_dev,
             (unsigned long) st.st_ino);
}

/* unmount by name relative to the session directory rather than through a
 * descriptor, which would itself keep the mount busy. the name cannot be
 * redirected: the directory is pinned, the last component is not followed,
 * and a mount point cannot be renamed.
 */
static int umount_session(const char *sid, int flags)
{
  char name[NAMEBUF];
  snprintf(name, sizeof(name), "%s%s", WORK_PREFIX, sid);
  if (fchdir(tmp_fd) != 0) return -1;
  return umount2(name, flags | UMOUNT_NOFOLLOW);
}

/* stack the session on the active tree and record which tree that was.
 * the tree can be replaced by a commit between opening and mounting it - and
 * then be removed as unused - so it is checked afterwards and the mount
 * redone on the replacement.
 */
static int ovl_mount(const char *sid, uid_t uid)
{
  char target[FDBUF], opts[OPTSBUF], rec[RECBUF];
  int mnt_fd, upper_fd, active, cur, work, base, attempt, rc = 1;
  if (open_session(sid, uid, &mnt_fd, &upper_fd) != 0) {
    fprintf(stderr, "not a usable session: %s\n", strerror(errno));
    return 1;
  }
  if (is_mounted(mnt_fd) != 0) {
    fprintf(stderr, "session is already mounted\n");
    goto out;
  }
  /* not mounted, yet a work directory may be on record: the previous overlay
   * went away without us, e.g. unmounted by hand */
  drop_current_workdir(sid);
  for (attempt = 0; attempt < 3; attempt++) {
    if ((active = open_dir_at(cfg_fd, ACTIVE_DIR)) < 0) {
      fprintf(stderr, "cannot open the active configuration: %s\n",
              strerror(errno));
      break;
    }
    if ((work = new_workdir(sid)) < 0) {
      fprintf(stderr, "cannot create a work directory: %s\n", strerror(errno));
      close(active);
      break;
    }
    snprintf(opts, sizeof(opts),
             "lowerdir=/proc/self/fd/%d,upperdir=/proc/self/fd/%d,"
             "workdir=/proc/self/fd/%d,%s",
             active, upper_fd, work, OVL_OPTS);
    snprintf(target, sizeof(target), "/proc/self/fd/%d", mnt_fd);
    if (mount("overlay", target, "overlay", MS_NOSUID | MS_NODEV, opts) != 0) {
      fprintf(stderr, "overlay mount failed: %s\n", strerror(errno));
      close(work);
      close(active);
      drop_current_workdir(sid);
      break;
    }
    close(work);
    cur = open_dir_at(cfg_fd, ACTIVE_DIR);
    if (cur >= 0 && same_inode(cur, active)) {
      format_identity(active, rec, sizeof(rec));
      base = open_session_base(sid, 0);
      if (base >= 0 && write_small_file(base, LOWER_FILE, rec) == 0) rc = 0;
      else fprintf(stderr, "cannot record the session's lower layer\n");
      close_keep_errno(base);
    }
    close_keep_errno(cur);
    close(active);
    if (rc == 0) break;
    /* replaced underneath us, or not recorded: do not leave it mounted */
    if (umount_session(sid, 0) != 0) {
      fprintf(stderr, "cannot undo the mount: %s\n", strerror(errno));
      break;
    }
    drop_current_workdir(sid);
  }
out:
  close(mnt_fd);
  close(upper_fd);
  return rc;
}

static int ovl_umount(const char *sid, uid_t uid)
{
  int mnt_fd, upper_fd;
  if (open_session(sid, uid, &mnt_fd, &upper_fd) != 0) {
    fprintf(stderr, "not a usable session: %s\n", strerror(errno));
    return 1;
  }
  close(mnt_fd);
  close(upper_fd);
  if (umount_session(sid, 0) == 0) {
    drop_current_workdir(sid);
    return 0;
  }
  /* a detached overlay keeps its upper layer, so callers that mount again
   * must do so on a fresh one (see ovl_clear) */
  if (errno != EBUSY || umount_session(sid, MNT_DETACH) != 0) {
    fprintf(stderr, "overlay umount failed: %s\n", strerror(errno));
    return 1;
  }
  forget_current_workdir(sid);
  return 0;
}

static int clear_dir_at(int dfd, dev_t dev, uid_t owner);

/* the active tree with the given identity: the current one, or a superseded
 * one kept for the sessions still stacked on it */
static int open_active_tree(const char *identity)
{
  char id[RECBUF];
  struct dirent *e;
  DIR *d;
  int fd = -1, dup_fd = dup(cfg_fd);
  if (dup_fd < 0 || !(d = fdopendir(dup_fd))) {
    close_keep_errno(dup_fd);
    return -1;
  }
  rewinddir(d);  // a dup shares its position with the original
  while ((e = readdir(d))) {
    if (strcmp(e->d_name, ACTIVE_DIR)
        && strncmp(e->d_name, ACTIVE_DIR ".new.", strlen(ACTIVE_DIR) + 5))
      continue;
    if ((fd = open_dir_at(cfg_fd, e->d_name)) < 0) continue;
    format_identity(fd, id, sizeof(id));
    if (identity[0] && !strcmp(id, identity)) break;
    close(fd);
    fd = -1;
  }
  closedir(d);
  if (fd < 0) errno = ENOENT;
  return fd;
}

static int open_entry_at(int dfd, const char *name, int flags)
{
  if (dfd < 0) {
    errno = ENOENT;
    return -1;
  }
  return openat(dfd, name, flags | O_NOFOLLOW | O_CLOEXEC);
}

static int same_entry(int a, int b, const char *name);

/* 1 if two directories hold the same entries with the same contents */
static int same_dir(int a, int b)
{
  struct dirent *e;
  struct stat st;
  DIR *d;
  int rc = 1, pass, dup_fd;
  for (pass = 0; pass < 2 && rc == 1; pass++) {
    int from = pass ? b : a, other = pass ? a : b;
    if ((dup_fd = dup(from)) < 0 || !(d = fdopendir(dup_fd))) {
      close_keep_errno(dup_fd);
      return -1;
    }
    rewinddir(d);
    while (rc == 1) {
      errno = 0;
      if (!(e = readdir(d))) {
        // an entry that could not be read is not the end of the directory
        if (errno) rc = -1;
        break;
      }
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      if (pass == 0)
        rc = same_entry(a, b, e->d_name);
      else if (fstatat(other, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
        rc = (errno == ENOENT) ? 0 : -1;
    }
    closedir(d);
  }
  return rc;
}

/* 1 if name is the same in both directories, either of which may be absent
 * (-1): absent from both, or the same file contents, or directories holding
 * the same. 0 if not, -1 if that cannot be told. the trees compared here are
 * on the configuration filesystem, which is mounted nodev. */
static int same_entry(int a, int b, const char *name)
{
  struct stat sa, sb;
  char ba[4096], bb[4096];
  int ea, eb, fa = -1, fb = -1, rc = 0;
  ssize_t na, nb;
  ea = (a >= 0 && fstatat(a, name, &sa, AT_SYMLINK_NOFOLLOW) == 0);
  if (a >= 0 && !ea && errno != ENOENT) return -1;
  eb = (b >= 0 && fstatat(b, name, &sb, AT_SYMLINK_NOFOLLOW) == 0);
  if (b >= 0 && !eb && errno != ENOENT) return -1;
  if (!ea || !eb) return (!ea && !eb);
  if ((sa.st_mode & S_IFMT) != (sb.st_mode & S_IFMT)) return 0;
  if (S_ISDIR(sa.st_mode)) {
    if ((fa = open_entry_at(a, name, O_RDONLY | O_DIRECTORY)) >= 0
        && (fb = open_entry_at(b, name, O_RDONLY | O_DIRECTORY)) >= 0)
      rc = same_dir(fa, fb);
    else
      rc = -1;
  } else if (S_ISREG(sa.st_mode)) {
    if (sa.st_size != sb.st_size) return 0;
    if ((fa = open_entry_at(a, name, O_RDONLY | O_NONBLOCK)) < 0
        || (fb = open_entry_at(b, name, O_RDONLY | O_NONBLOCK)) < 0
        || fstat(fa, &sa) != 0 || fstat(fb, &sb) != 0
        || !S_ISREG(sa.st_mode) || !S_ISREG(sb.st_mode)) {
      rc = -1;
    } else {
      rc = 1;
      do {
        na = read(fa, ba, sizeof(ba));
        nb = read(fb, bb, sizeof(bb));
        if (na < 0 || nb < 0) rc = -1;
        else if (na != nb || memcmp(ba, bb, na)) rc = 0;
      } while (rc == 1 && na > 0);
    }
  }
  /* nothing else belongs in a configuration tree: never the same */
  close_keep_errno(fa);
  close_keep_errno(fb);
  return rc;
}

/* an entry in the upper layer to be dropped, and whether the user had
 * changed it - a file is copied up when written, even with what it held */
struct conflict {
  char *path;
  int edited;
};

struct conflicts {
  struct conflict *v;
  size_t n, cap;
};

static int add_conflict(struct conflicts *c, const char *path, int edited)
{
  if (c->n == c->cap) {
    size_t cap = c->cap ? 2 * c->cap : 16;
    struct conflict *v = realloc(c->v, cap * sizeof(*v));
    if (!v) return -1;
    c->v = v;
    c->cap = cap;
  }
  if (!(c->v[c->n].path = strdup(path))) return -1;
  c->v[c->n++].edited = edited;
  return 0;
}

static int is_opaque(int fd)
{
  char v;
  return (fgetxattr(fd, "trusted.overlay.opaque", &v, 1) == 1 && v == 'y');
}

/* everything in the session's upper layer that hides something changed
 * between the tree the session was based on and the current one: a file, a
 * deletion (whiteout) or a replaced (opaque) directory over a changed
 * subtree. left in place it would commit the session's stale view over the
 * newer one. */
static int find_conflicts(int up, int old, int now, const char *path,
                          struct conflicts *out)
{
  struct dirent *e;
  struct stat st;
  DIR *d;
  int rc = 0, dup_fd = dup(up);
  if (dup_fd < 0 || !(d = fdopendir(dup_fd))) {
    close_keep_errno(dup_fd);
    return -1;
  }
  rewinddir(d);  // a dup shares its position with the original
  while (rc == 0) {
    char *p;
    int same, fd = -1, o, n, edited = 1, descend = 0;
    errno = 0;
    if (!(e = readdir(d))) {
      // a conflict list cut short by a read error is not a complete one
      if (errno) rc = -1;
      break;
    }
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if (fstatat(up, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0
        || asprintf(&p, "%s%s%s", path, path[0] ? "/" : "", e->d_name) < 0) {
      rc = -1;
      break;
    }
    if (S_ISDIR(st.st_mode)) {
      struct stat os, ns;
      if ((fd = open_dir_at(up, e->d_name)) < 0) {
        rc = -1;
      } else {
        int in_old = (old >= 0
                      && fstatat(old, e->d_name, &os, AT_SYMLINK_NOFOLLOW) == 0);
        int in_now = (now >= 0
                      && fstatat(now, e->d_name, &ns, AT_SYMLINK_NOFOLLOW) == 0);
        /* merged with what is below: only its contents can hide anything -
         * unless a commit removed it since, which whatever the session has in
         * it would bring back */
        descend = (!is_opaque(fd)
                   && (!in_old || S_ISDIR(os.st_mode))
                   && (!in_now || S_ISDIR(ns.st_mode))
                   && (in_now || !in_old));
      }
    }
    if (rc == 0 && descend) {
      o = open_entry_at(old, e->d_name, O_RDONLY | O_DIRECTORY);
      n = open_entry_at(now, e->d_name, O_RDONLY | O_DIRECTORY);
      rc = find_conflicts(fd, o, n, p, out);
      close_keep_errno(o);
      close_keep_errno(n);
    } else if (rc == 0 && (same = same_entry(old, now, e->d_name)) != 1) {
      if (same < 0) {
        rc = -1;
      } else {
        if (S_ISREG(st.st_mode) && same_entry(up, old, e->d_name) == 1)
          edited = 0;
        rc = add_conflict(out, p, edited);
      }
    }
    close_keep_errno(fd);
    free(p);
  }
  closedir(d);
  return rc;
}

/* remove one entry from the session's upper layer, offline */
static int drop_entry(int up, const char *path, dev_t dev, uid_t owner)
{
  char *copy, *name, *slash;
  int dir = dup(up), next, rc = -1;
  if (dir < 0 || !(copy = strdup(path))) {
    close_keep_errno(dir);
    return -1;
  }
  name = copy;
  while ((slash = strchr(name, '/'))) {
    *slash = '\0';
    next = open_dir_at(dir, name);
    close(dir);
    if ((dir = next) < 0) goto out;
    name = slash + 1;
  }
  if (unlinkat(dir, name, 0) == 0 || errno == ENOENT) {
    rc = 0;
  } else if ((next = open_dir_at(dir, name)) >= 0) {
    struct stat st;
    if (fstat(next, &st) == 0 && st.st_dev == dev
        && clear_dir_at(next, dev, owner) == 0
        && unlinkat(dir, name, AT_REMOVEDIR) == 0)
      rc = 0;
    close(next);
  }
out:
  close_keep_errno(dir);
  free(copy);
  return rc;
}

/* shared, so that no active tree is swapped in or removed while it is held.
 * the configuration root is writable by the group, so whatever has been put
 * there under the name must be a plain file. */
static int lock_trees(void)
{
  struct stat st;
  int fd = openat(cfg_fd, TREE_LOCK, O_RDONLY | O_CREAT | O_NOFOLLOW
                  | O_NONBLOCK | O_CLOEXEC, 0644);
  if (fd < 0) return -1;
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
    close(fd);
    errno = EINVAL;
    return -1;
  }
  if (flock(fd, LOCK_SH) != 0) {
    close_keep_errno(fd);
    return -1;
  }
  return fd;
}

/* print an upper layer path as the configuration path it holds. this is for
 * the user, on stdout: the CLI sends its own stderr to a log while working */
static void print_config_path(const char *path)
{
  const char *last = strrchr(path, '/');
  const char *leaf = last ? last + 1 : path;
  size_t i, len = strlen(path);
  if (!strcmp(leaf, "node.val") || !strcmp(leaf, ".comment")
      || !strcmp(leaf, "def") || !strcmp(leaf, ".disable"))
    len = last ? (size_t) (last - path) : 0;
  fputs("  ", stdout);
  for (i = 0; i < len; i++) {
    if (path[i] == '/') fputc(' ', stdout);
    else if (!strncmp(path + i, "%2F", 3)) { fputc('/', stdout); i += 2; }
    else if (!strncmp(path + i, "%25", 3)) { fputc('%', stdout); i += 2; }
    else if (!strncmp(path + i, "%%%", 3)) { fputs("''", stdout); i += 2; }
    else fputc(path[i], stdout);
  }
  fputc('\n', stdout);
}

/* move a session that is stacked on a superseded active tree onto the
 * current one. its upper layer - the pending changes - is kept, except where
 * it hides what a commit changed since: those changes are dropped, so the
 * newer configuration shows through, and listed (exit status 4). a session
 * in use cannot be unmounted and is left where it is (exit status 3).
 */
static int ovl_refresh(const char *sid, uid_t uid)
{
  char rec[RECBUF], now[RECBUF];
  struct conflicts found = { NULL, 0, 0 };
  struct stat us;
  int mnt_fd, upper_fd, active = -1, base, old, m, edited = 0, rc = 1;
  int trees = -1;
  size_t i;
  if (open_session(sid, uid, &mnt_fd, &upper_fd) != 0) {
    fprintf(stderr, "not a usable session: %s\n", strerror(errno));
    return 1;
  }
  m = is_mounted(mnt_fd);
  close(mnt_fd);
  if (m != 1 || fstat(upper_fd, &us) != 0) {
    fprintf(stderr, "session is not mounted\n");
    close(upper_fd);
    return 1;
  }
  rec[0] = now[0] = '\0';
  if ((base = open_session_base(sid, 0)) >= 0) {
    read_small_file(base, LOWER_FILE, rec, sizeof(rec));
    close(base);
  }
  /* held until the session is mounted again: the tree compared with below
   * must be the one it is mounted on */
  if ((trees = lock_trees()) < 0) {
    fprintf(stderr, "cannot lock the active trees: %s\n", strerror(errno));
    goto out;
  }
  if ((active = open_dir_at(cfg_fd, ACTIVE_DIR)) >= 0) {
    format_identity(active, now, sizeof(now));
  }
  if (active < 0 || (now[0] && !strcmp(rec, now))) {
    rc = (active < 0);
    goto out;
  }
  /* while the session is still mounted on it, its tree is kept */
  if ((old = open_active_tree(rec)) < 0) {
    fprintf(stderr, "cannot find the configuration the session is based on\n");
    goto out;
  }
  m = find_conflicts(upper_fd, old, active, "", &found);
  close(old);
  if (m != 0) {
    fprintf(stderr, "cannot compare the session with the active "
            "configuration: %s\n", strerror(errno));
    goto out;
  }
  if (umount_session(sid, 0) != 0) {
    fprintf(stderr, "cannot move the session: %s\n", strerror(errno));
    rc = (errno == EBUSY) ? 3 : 1;
    goto out;
  }
  drop_current_workdir(sid);
  for (i = 0; i < found.n; i++) {
    if (drop_entry(upper_fd, found.v[i].path, us.st_dev, us.st_uid) != 0) {
      /* mounted again, it would still hide the newer configuration */
      fprintf(stderr, "cannot drop a stale change: %s\n", strerror(errno));
      goto out;
    }
    edited |= found.v[i].edited;
  }
  rc = ovl_mount(sid, uid);
  if (rc == 0 && edited) {
    for (i = 0; i < found.n; i++)
      if (found.v[i].edited) print_config_path(found.v[i].path);
    rc = 4;
  }
out:
  for (i = 0; i < found.n; i++) free(found.v[i].path);
  free(found.v);
  close_keep_errno(active);
  close_keep_errno(trees);
  close(upper_fd);
  return rc;
}

/* empty a tree the session's owner controls, as root. the owner can move
 * directories into it while this runs, so never follow one onto another
 * filesystem (a mounted session), into the configuration directories
 * themselves, or into anybody else's - only the owner's own and those root
 * wrote into the session through its mount. */
static int clear_dir_at(int dfd, dev_t dev, uid_t owner)
{
  struct dirent *e;
  struct stat st;
  DIR *d;
  int rc = 0, fd, dup_fd = dup(dfd);
  if (dup_fd < 0 || !(d = fdopendir(dup_fd))) {
    close_keep_errno(dup_fd);
    return -1;
  }
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if (unlinkat(dfd, e->d_name, 0) == 0) continue;
    if ((fd = open_dir_at(dfd, e->d_name)) < 0) { rc = -1; continue; }
    if (fstat(fd, &st) != 0 || st.st_dev != dev
        || (st.st_uid != owner && st.st_uid != 0) || same_inode(fd, tmp_fd)
        || same_inode(fd, cfg_fd) || same_inode(fd, work_fd)
        || same_inode(fd, active_fd)) {
      close(fd);
      rc = -1;
      continue;
    }
    if (clear_dir_at(fd, dev, owner) != 0) rc = -1;
    close(fd);
    if (unlinkat(dfd, e->d_name, AT_REMOVEDIR) != 0) rc = -1;
  }
  closedir(d);
  return rc;
}

/* give an unmounted session an empty upper layer. it is a new directory
 * rather than the old one emptied: a lazily detached overlay may still hold
 * the old one, and two overlays sharing an upper layer is undefined. the old
 * layer can also hold nodes its user cannot remove (written by root through
 * the merged mount), which is why this is done here at all.
 */
static int ovl_clear(const char *sid, uid_t uid)
{
  char upper[NAMEBUF], fresh[NAMEBUF];
  unsigned char rnd[8];
  struct stat us, fs, cs;
  int mnt_fd, upper_fd, fd, cur, m, rc = 1;
  if (open_session(sid, uid, &mnt_fd, &upper_fd) != 0) {
    fprintf(stderr, "not a usable session: %s\n", strerror(errno));
    return 1;
  }
  m = is_mounted(mnt_fd);
  if (m != 0 || fstat(upper_fd, &us) != 0) {
    fprintf(stderr, "session is mounted or cannot be examined\n");
    goto out;
  }
  active_fd = open_dir_at(cfg_fd, ACTIVE_DIR);
  /* an unpredictable name, so nobody can occupy it beforehand */
  if (getrandom(rnd, sizeof(rnd), 0) != (ssize_t) sizeof(rnd)) {
    fprintf(stderr, "no randomness: %s\n", strerror(errno));
    goto out;
  }
  snprintf(upper, sizeof(upper), "%s%s", UPPER_PREFIX, sid);
  snprintf(fresh, sizeof(fresh), "%s%s.%02x%02x%02x%02x%02x%02x%02x%02x",
           UPPER_PREFIX, sid, rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5],
           rnd[6], rnd[7]);
  /* the session directory is writable by the whole group, so the name may be
   * replaced at any moment: set the new directory up through a descriptor,
   * and only after making sure it is the one just created here */
  if (mkdirat(tmp_fd, fresh, 0700) != 0
      || (fd = open_dir_at(tmp_fd, fresh)) < 0) {
    fprintf(stderr, "cannot create a new upper layer: %s\n", strerror(errno));
    goto out;
  }
  if (fstat(fd, &fs) != 0 || fs.st_uid != geteuid() || fs.st_nlink != 2
      || fchown(fd, us.st_uid, us.st_gid) != 0
      || fchmod(fd, us.st_mode & 07777) != 0) {
    fprintf(stderr, "cannot set up a new upper layer: %s\n", strerror(errno));
    close(fd);
    unlinkat(tmp_fd, fresh, AT_REMOVEDIR);
    goto out;
  }
  if (renameat2(tmp_fd, fresh, tmp_fd, upper, RENAME_EXCHANGE) != 0) {
    fprintf(stderr, "cannot swap in the new upper layer: %s\n", strerror(errno));
    close(fd);
    unlinkat(tmp_fd, fresh, AT_REMOVEDIR);
    goto out;
  }
  /* swapped: make sure what is in place now is what was set up */
  cur = open_dir_at(tmp_fd, upper);
  if (cur < 0 || fstat(cur, &cs) != 0
      || cs.st_dev != fs.st_dev || cs.st_ino != fs.st_ino) {
    fprintf(stderr, "upper layer was replaced while swapping it\n");
    close_keep_errno(cur);
    close(fd);
    goto out;
  }
  close(cur);
  close(fd);
  rc = 0;
  /* the old layer is under the temporary name now, and still upper_fd -
   * unless its owner moved it elsewhere meanwhile; then it is left alone */
  cur = open_dir_at(tmp_fd, fresh);
  if (cur >= 0 && same_inode(cur, upper_fd)) {
    if (clear_dir_at(upper_fd, us.st_dev, us.st_uid) != 0
        || unlinkat(tmp_fd, fresh, AT_REMOVEDIR) != 0)
      fprintf(stderr, "cannot remove the old upper layer: %s\n",
              strerror(errno));
  }
  close_keep_errno(cur);
out:
  close(mnt_fd);
  close(upper_fd);
  return rc;
}

/* the caller controls the environment this starts in. none of it may decide
 * whether an operation runs to completion. */
static int harden(void)
{
  static const int unlimited[] = { RLIMIT_FSIZE, RLIMIT_CPU, RLIMIT_AS,
                                   RLIMIT_DATA, RLIMIT_STACK };
  struct rlimit rl;
  sigset_t all;
  size_t i;
  int fd;
  /* descriptors the caller left open would count against our limit, and
   * running out halfway through an operation leaves it half done */
  if (close_range(3, ~0U, 0) != 0) return -1;
  if (clearenv() != 0) return -1;
  umask(022);
  sigfillset(&all);
  if (sigprocmask(SIG_BLOCK, &all, NULL) != 0) return -1;
  /* nothing written below may land in a file the caller left open as one
   * of the standard descriptors */
  while ((fd = open("/dev/null", O_RDWR)) >= 0 && fd <= 2);
  if (fd < 0) return -1;
  close(fd);
  for (i = 0; i < sizeof(unlimited) / sizeof(unlimited[0]); i++) {
    rl.rlim_cur = rl.rlim_max = RLIM_INFINITY;
    if (setrlimit(unlimited[i], &rl) != 0) return -1;
  }
  if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < 1024) {
    rl.rlim_cur = 1024;
    if (rl.rlim_max < 1024) rl.rlim_max = 1024;
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) return -1;
  }
  return 0;
}

/* the caller's stdout and stderr can be pipes nobody reads, and a write to
 * one would then block with the locks below held - and with them every later
 * call for the session, or every commit. output is kept in memory instead,
 * and passed on once no lock is held any more (deliver_output()). */
static int caller_fd[3] = { -1, -1, -1 };

static int buffer_output(void)
{
  int fd, buf;
  for (fd = 1; fd <= 2; fd++) {
    if ((buf = memfd_create("output", MFD_CLOEXEC)) < 0) return -1;
    caller_fd[fd] = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (caller_fd[fd] < 0 || dup2(buf, fd) < 0) {
      close_keep_errno(buf);
      return -1;
    }
    close(buf);
  }
  return 0;
}

static void deliver_output(void)
{
  char buf[4096];
  ssize_t n, w, off;
  int fd;
  fflush(stdout);
  fflush(stderr);
  for (fd = 1; fd <= 2; fd++) {
    if (caller_fd[fd] < 0 || lseek(fd, 0, SEEK_SET) != 0) continue;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
      for (off = 0; off < n; off += w) {
        if ((w = write(caller_fd[fd], buf + off, n - off)) < 0) {
          if (errno != EINTR) break;  // nobody to tell
          w = 0;
        }
      }
      if (off < n) break;
    }
  }
}

/* one invocation per session at a time, held until exit. only taken for a
 * session the caller owns, so nobody can hold up another's. */
static int session_lock = -1;

static int lock_session(const char *sid)
{
  char name[NAMEBUF];
  int dir;
  if (mkdir(LOCK_DIR, 0700) != 0 && errno != EEXIST) return -1;
  dir = open(LOCK_DIR, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (dir < 0 || private_dir(dir) != 0) {
    close_keep_errno(dir);
    return -1;
  }
  snprintf(name, sizeof(name), "%s.lock", sid);
  session_lock = openat(dir, name, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
                        0600);
  close(dir);
  if (session_lock < 0) return -1;
  return flock(session_lock, LOCK_EX);
}

static int run(int argc, char **argv)
{
  const char *op, *sid;
  int mnt_fd, upper_fd;
  uid_t uid;
  if (argc != 3) {
    fprintf(stderr, "usage: %s mount|umount|refresh|clear <session-id>\n",
            argv[0]);
    return 2;
  }
  op = argv[1];
  sid = argv[2];
  if (!valid_number(sid)) {
    fprintf(stderr, "not a session id\n");
    return 2;
  }
  /* who asked, recorded before any of this can change it: becoming root
   * replaces the real uid, and the session ownership checks below would then
   * pass for anybody */
  uid = (getuid() == 0) ? (uid_t) -1 : getuid();
  if (geteuid() != 0 || setuid(0) != 0) {
    fprintf(stderr, "cannot acquire privilege: %s\n", strerror(errno));
    return 2;
  }
  if (harden() != 0 || buffer_output() != 0) {
    fprintf(stderr, "cannot set up a safe environment: %s\n", strerror(errno));
    return 2;
  }
  if (strcmp(op, "mount") && strcmp(op, "umount") && strcmp(op, "refresh")
      && strcmp(op, "clear")) {
    fprintf(stderr, "unknown operation\n");
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
  if (lock_session(sid) != 0) {
    fprintf(stderr, "cannot lock the session: %s\n", strerror(errno));
    return 2;
  }
  if (!strcmp(op, "mount"))   return ovl_mount(sid, uid);
  if (!strcmp(op, "umount"))  return ovl_umount(sid, uid);
  if (!strcmp(op, "refresh")) return ovl_refresh(sid, uid);
  return ovl_clear(sid, uid);
}

int main(int argc, char **argv)
{
  int rc = run(argc, argv);
  if (session_lock >= 0) close(session_lock);
  deliver_output();
  return rc;
}
