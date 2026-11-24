/*
 * Copyright (C) 2010 Vyatta, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <set>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <wait.h>
#include <sys/wait.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <grp.h>
#include <time.h>

#include <filesystem>
#include <system_error>

#include <cli_cstore.h>
#include <cstore/unionfs/cstore-unionfs.hpp>
#include <cnode/cnode.hpp>
#include <commit/commit-algorithm.hpp>

namespace cstore { // begin namespace cstore
namespace unionfs { // begin namespace unionfs

namespace fs = std::filesystem;

////// constants
// environment vars defining root dirs
const string UnionfsCstore::C_ENV_TMPL_ROOT = "VYATTA_CONFIG_TEMPLATE";
const string UnionfsCstore::C_ENV_WORK_ROOT = "VYATTA_TEMP_CONFIG_DIR";
const string UnionfsCstore::C_ENV_ACTIVE_ROOT
  = "VYATTA_ACTIVE_CONFIGURATION_DIR";
const string UnionfsCstore::C_ENV_CHANGE_ROOT = "VYATTA_CHANGES_ONLY_DIR";
const string UnionfsCstore::C_ENV_TMP_ROOT = "VYATTA_CONFIG_TMP";

// default root dirs/paths
const string UnionfsCstore::C_DEF_TMPL_ROOT
  = "/opt/vyatta/share/vyatta-cfg/templates";
const string UnionfsCstore::C_DEF_CFG_ROOT
  = "/opt/vyatta/config";
const string UnionfsCstore::C_DEF_ACTIVE_ROOT
  = UnionfsCstore::C_DEF_CFG_ROOT + "/active";
const string UnionfsCstore::C_DEF_CHANGE_PREFIX 
  = UnionfsCstore::C_DEF_CFG_ROOT + "/tmp/changes_only_";
const string UnionfsCstore::C_DEF_WORK_PREFIX
  = UnionfsCstore::C_DEF_CFG_ROOT + "/tmp/new_config_";
const string UnionfsCstore::C_DEF_TMP_PREFIX
  = UnionfsCstore::C_DEF_CFG_ROOT + "/tmp/tmp_";
/* the overlayfs workdir must live on the same filesystem as the upperdir but
 * must NOT be inside it. it is deliberately kept outside of
 * <cfg_root>/tmp as well: setupSession() enumerates and removes everything
 * under that directory when reaping stale sessions, and the kernel creates a
 * root-owned "work" subdirectory in here that an unprivileged session cannot
 * remove.
 */
const string UnionfsCstore::C_DEF_OVLWORK_PREFIX
  = UnionfsCstore::C_DEF_CFG_ROOT + "/ovl_work/";
/* the only privileged component; the CLI itself holds no capabilities */
#ifndef OVL_HELPER_PATH
#error "OVL_HELPER_PATH must name the installed overlay helper (Makefile.am)"
#endif
const string UnionfsCstore::C_OVL_HELPER = OVL_HELPER_PATH;

// markers
const string UnionfsCstore::C_MARKER_DEF_VALUE  = "def";
const string UnionfsCstore::C_MARKER_DEACTIVATE = ".disable";
const string UnionfsCstore::C_MARKER_CHANGED = ".modified";
const string UnionfsCstore::C_MARKER_UNSAVED = ".unsaved";
const string UnionfsCstore::C_COMMITTED_MARKER_FILE = ".changes";
const string UnionfsCstore::C_COMMENT_FILE = ".comment";
const string UnionfsCstore::C_TAG_NAME = "node.tag";
const string UnionfsCstore::C_VAL_NAME = "node.val";
const string UnionfsCstore::C_DEF_NAME = "node.def";
const string UnionfsCstore::C_COMMIT_LOCK_FILE = "/opt/vyatta/config/.lock";
const string UnionfsCstore::C_TREE_LOCK_FILE = "/opt/vyatta/config/.tree.lock";


////// static
static MapT<char, string> _fs_escape_chars;
static MapT<string, char> _fs_unescape_chars;
static void
_init_fs_escape_chars()
{
  _fs_escape_chars[-1] = "\%\%\%";
  _fs_escape_chars['%'] = "\%25";
  _fs_escape_chars['/'] = "\%2F";

  _fs_unescape_chars["\%\%\%"] = -1;
  _fs_unescape_chars["\%25"] = '%';
  _fs_unescape_chars["\%2F"] = '/';
}

static string
_escape_char(char c)
{
  MapT<char, string>::iterator p = _fs_escape_chars.find(c);
  if (p != _fs_escape_chars.end()) {
    return p->second;
  } else {
    return string(1, c);
  }
}

static MapT<string, string> _escape_path_name_cache;

static string
_escape_path_name(const string& path)
{
  MapT<string, string>::iterator p
    = _escape_path_name_cache.find(path);
  if (p != _escape_path_name_cache.end()) {
    // found escaped string in cache. just return it.
    return p->second;
  }

  // special case for empty string
  string npath = (path.size() == 0) ? _fs_escape_chars[-1] : "";
  for (size_t i = 0; i < path.size(); i++) {
    npath += _escape_char(path[i]);
  }

  // cache it before return
  _escape_path_name_cache[path] = npath;
  return npath;
}

// an unset variable matches: not every caller exports every root
static bool
env_root_matches(const string& var, const string& expected)
{
  const char *val = getenv(var.c_str());
  return (!val || FsPath(val) == FsPath(expected));
}

static MapT<string, string> _unescape_path_name_cache;

static string
_unescape_path_name(const string& path)
{
  MapT<string, string>::iterator p
    = _unescape_path_name_cache.find(path);
  if (p != _unescape_path_name_cache.end()) {
    // found unescaped string in cache. just return it.
    return p->second;
  }

  // assume all escape patterns are 3-char
  string npath = "";
  for (size_t i = 0; i < path.size(); i++) {
    if ((path.size() - i) < 3) {
      npath += path.substr(i);
      break;
    }
    string s = path.substr(i, 3);
    MapT<string, char>::iterator p = _fs_unescape_chars.find(s);
    if (p != _fs_unescape_chars.end()) {
      char c = p->second;
      if (path.size() == 3 && c == -1) {
        // special case for empty string
        npath = "";
        break;
      }
      npath += string(1, _fs_unescape_chars[s]);
      // skip the escape sequence
      i += 2;
    } else {
      npath += path.substr(i, 1);
    }
  }
  // cache it before return
  _unescape_path_name_cache[path] = npath;
  return npath;
}

/* Fall-through for copy_file: it has been seen to fail across a union mount
 * where a plain read/write copy succeeds.
 */
void stream_file( const char* srce_file, const char* dest_file )
{
    std::ifstream srce( srce_file, std::ios::binary ) ;
    std::ofstream dest( dest_file, std::ios::binary ) ;
    dest << srce.rdbuf() ;
}

/* session IDs whose process still runs. a session is normally held by a
 * vbash, but programs keep sessions of their own too (vyos.configsession),
 * so the process name says nothing about whether the session is in use.
 */
vector<int> getActiveCommits()
{
  vector<int> pids;

  DIR *dp = opendir("/proc");
  if (dp != NULL) {
    struct dirent *dirp;
    while ((dirp = readdir(dp))) {
      int pid = atoi(dirp->d_name);
      if (pid > 0) {
        pids.push_back(pid);
      }
    }
    closedir(dp);
  }

  return pids;
}

////// constructor/destructor
/* "current session" constructor.
 * this constructor sets up the object from environment.
 * used when environment is already set up, i.e., when operating on the
 * "current" config session. e.g., in the following scenarios
 *   configure commands
 *   perl module
 *   shell "current session" api
 *
 * note: this also applies when using the cstore in operational mode,
 *       in which case only the template root and the active root will be
 *       valid.
 */
UnionfsCstore::UnionfsCstore(bool use_edit_level)
{
  // set up root dir strings
  char *val;
  if ((val = getenv(C_ENV_TMPL_ROOT.c_str()))) {
    tmpl_path = val;
  } else {
    tmpl_path = C_DEF_TMPL_ROOT;
  }
  tmpl_root = tmpl_path; // save a copy of tmpl root
  session_lost = false;
  holds_session_lock = false;
  last_refresh_status = 0;
  /* the session roots are what the capability-holding binaries mount,
   * rename and remove, so they are derived from the session ID rather than
   * taken from the environment. an environment that disagrees with the
   * derived roots does not get a session.
   */
  active_root = C_DEF_ACTIVE_ROOT;
  string sid;
  if ((val = getenv(C_ENV_WORK_ROOT.c_str()))
      && session_id_from_work_root(FsPath(val), sid)
      && env_root_matches(C_ENV_CHANGE_ROOT, C_DEF_CHANGE_PREFIX + sid)
      && env_root_matches(C_ENV_TMP_ROOT, C_DEF_TMP_PREFIX + sid)
      && env_root_matches(C_ENV_ACTIVE_ROOT, C_DEF_ACTIVE_ROOT)) {
    work_root = (C_DEF_WORK_PREFIX + sid);
    set_session_id(sid);
    if (getenv(C_ENV_CHANGE_ROOT.c_str())) {
      change_root = (C_DEF_CHANGE_PREFIX + sid);
    }
    /* the scratch directory holds the session's markers, which every process
     * of the session must find in the same place - also one whose
     * environment lacks it, as vyos-configd's does */
    tmp_root = (C_DEF_TMP_PREFIX + sid);
    init_commit_data();
  } else if (val) {
    output_internal("ignoring invalid config session environment [%s]\n",
                    val);
  }
  /* note: the original perl API module does not use the edit levels
   *       from environment. only the actual CLI operations use them.
   *       so here make it an option.
   */
  mutable_cfg_path = "/";
  if (use_edit_level) {
    // set up path strings
    if ((val = getenv(C_ENV_EDIT_LEVEL.c_str()))) {
      mutable_cfg_path = val;
    }
    if ((val = getenv(C_ENV_TMPL_LEVEL.c_str())) && val[0] && val[1]) {
      /* no need to append root (i.e., "/"). level (if exists) always
       * starts with '/', so only append it if it is at least two chars
       * (i.e., it is not "/").
       */
      FsPath tlvl(val);
      tmpl_path /= tlvl;
    }
  }
  orig_mutable_cfg_path = mutable_cfg_path;
  orig_tmpl_path = tmpl_path;
  _init_fs_escape_chars();

  /* everything working on this session holds its lock for as long as it
   * runs, so that moving the session onto a new active config never pulls
   * the mount from under an operation in progress. a session behind the
   * active config is moved first - best effort; a commit and a load insist.
   */
  open_session_lock(30000);
  if (holds_session_lock && inSession() && !session_current()) {
    // one attempt; this may well be a child of something holding the lock
    refresh_session(0);
  }
}

/* "specific session" constructor.
 * this constructor sets up the object for the specified session ID and
 * returns an environment string that can be "evaled" to set up the
 * shell environment.
 *
 * used when the session environment needs to be established. this is
 * mainly for the shell functions that set up configuration sessions.
 * i.e., the "vyatta-cfg-cmd-wrapper" (on boot or for GUI etc.) and
 * the cfg completion script (when entering configure mode).
 *
 *   sid: session ID.
 *   env: (output) environment string.
 *
 * note: this does NOT set up the session. caller needs to use the
 *       explicit session setup/teardown functions as needed.
 */
UnionfsCstore::UnionfsCstore(const string& sid, string& env)
  : Cstore(env)
{
  tmpl_root = C_DEF_TMPL_ROOT;
  tmpl_path = tmpl_root;
  session_lost = false;
  holds_session_lock = false;
  last_refresh_status = 0;
  active_root = C_DEF_ACTIVE_ROOT;
  work_root = (C_DEF_WORK_PREFIX + sid);
  change_root = (C_DEF_CHANGE_PREFIX + sid);
  tmp_root = (C_DEF_TMP_PREFIX + sid);
  set_session_id(sid);
  init_commit_data();

  string declr = " declare -x -r "; // readonly vars
  env += " umask 002; {";
  env += (declr + C_ENV_ACTIVE_ROOT + "=" + active_root.path_cstr());
  env += (declr + C_ENV_CHANGE_ROOT + "=" + change_root.path_cstr() + ";");
  env += (declr + C_ENV_WORK_ROOT + "=" + work_root.path_cstr() + ";");
  env += (declr + C_ENV_TMP_ROOT + "=" + tmp_root.path_cstr() + ";");
  env += (declr + C_ENV_TMPL_ROOT + "=" + tmpl_root.path_cstr() + ";");
  env += " } >&/dev/null || true";

  // set up path strings using level vars
  char *val;
  mutable_cfg_path = "/";
  if ((val = getenv(C_ENV_EDIT_LEVEL.c_str()))) {
    mutable_cfg_path = val;
  }
  if ((val = getenv(C_ENV_TMPL_LEVEL.c_str())) && val[0] && val[1]) {
    // see comment in the other constructor
    FsPath tlvl(val);
    tmpl_path /= tlvl;
  }
  orig_mutable_cfg_path = mutable_cfg_path;
  orig_tmpl_path = tmpl_path;
  _init_fs_escape_chars();
}

UnionfsCstore::~UnionfsCstore()
{
  close_session_lock();
}

////// public virtual functions declared in base class
bool
UnionfsCstore::markSessionUnsaved()
{
  if (session_lost) {
    // nothing left to mark, and the commit itself went through
    return true;
  }
  FsPath marker = work_root;
  marker.push(C_MARKER_UNSAVED);
  if (path_exists(marker)) {
    // already marked. treat as success.
    return true;
  }
  if (!create_file(marker)) {
    output_internal("failed to mark unsaved [%s]\n", marker.path_cstr());
    return false;
  }
  return true;
}

bool
UnionfsCstore::unmarkSessionUnsaved()
{
  FsPath marker = work_root;
  marker.push(C_MARKER_UNSAVED);
  if (!path_exists(marker)) {
    // not marked. treat as success.
    return true;
  }
  try {
    fs::remove(marker.path_cstr());
  } catch (...) {
    output_internal("failed to unmark unsaved [%s]\n", marker.path_cstr());
    return false;
  }
  return true;
}

bool
UnionfsCstore::sessionUnsaved()
{
  FsPath marker = work_root;
  marker.push(C_MARKER_UNSAVED);
  return path_exists(marker);
}

bool
UnionfsCstore::sessionChanged()
{
  FsPath marker = work_root;
  marker.push(C_MARKER_CHANGED);
  return path_exists(marker);
}

/* set up the session associated with this object.
 * the session comes from either the environment or the session ID
 * (see the two different constructors).
 */
bool
UnionfsCstore::setupSession()
{
  vector<FsPath> directories;
  vector<int> pids;
  vector<int> old_pids;
  FsPath old_config;
  FsPath work_base;

  string work_string = work_root.path_cstr();
  work_base = work_string.erase(work_string.find_last_of("/"));

  try {
    fs::directory_iterator di(work_base.path_cstr());
    for (; di != fs::directory_iterator(); ++di) {
      old_config = di->path().string().c_str();
      if (path_is_directory(old_config)) {
        directories.push_back(old_config);
      }
    }
  } catch (...) {
    if (path_exists(active_root)) {
      output_internal("no session directories found [%s]\n", work_root.path_cstr());
    }
  }

  /* what an invalidated session leaves behind (see invalidate_session()) is
   * cleared away, so this ID can start afresh */
  if (path_exists(work_root)
      && !path_is_directory((C_DEF_TMP_PREFIX + session_id).c_str())) {
    if (session_mounted()) {
      run_ovl_helper("umount", session_id);
    }
    if (session_mounted()) {
      output_internal("cannot reset session [%s]\n", work_root.path_cstr());
      return false;
    }
    run_ovl_helper("clear", session_id);
    try {
      fs::remove_all(work_root.path_cstr());
      fs::remove_all(change_root.path_cstr());
    } catch (...) {
      output_internal("cannot reset session [%s]\n", work_root.path_cstr());
      return false;
    }
  }

  /* set while a new session's upper layer still has to be cleared. a session
   * is only ever mounted once that is done, so a failed setup can be retried
   * without mounting what a previous session of this ID left behind */
  FsPath reset_marker(tmp_root);
  reset_marker.push(".session.reset");

  if (!path_exists(work_root)) {
    // session doesn't exist. create dirs.
    try {
      // the marker goes in before the working root, which a retry checks
      fs::create_directories(tmp_root.path_cstr());
      if (!create_file(reset_marker)) {
        output_internal("cannot mark session for reset [%s]\n",
                        reset_marker.path_cstr());
        return false;
      }
      fs::create_directories(work_root.path_cstr());
      fs::create_directories(change_root.path_cstr());
      if (!path_exists(active_root)) {
        // this should only be needed on boot
        fs::create_directories(active_root.path_cstr());
      }
    } catch (...) {
      output_internal("setup session failed to create session directories\n");
      return false;
    }

    /* guard against stacking a second overlay on top of a session that was
     * not cleanly torn down and whose directories were removed underneath
     * a live mount.
     */
    if (is_mount_point(work_root)) {
      output_internal("session already mounted [%s]\n", work_root.path_cstr());
      return false;
    }

    /* a session that ended without being torn down can leave its upper
     * layer behind, and a new session can get the same ID */
    if (!run_ovl_helper("clear", session_id)
        || !do_mount(change_root, active_root, work_root)) {
      return false;
    }
    unlink(reset_marker.path_cstr());
  } else if (!path_is_directory(work_root)) {
    output_internal("setup session not dir [%s]\n", work_root.path_cstr());
    return false;
  } else {
    /* a session ID is a process ID and gets reused. a session left behind by
     * somebody else is not to be taken over. */
    struct stat ws;
    if (stat(work_root.path_cstr(), &ws) != 0
        || (geteuid() != 0 && ws.st_uid != geteuid())) {
      output_internal("session [%s] belongs to somebody else\n",
                      work_root.path_cstr());
      return false;
    }
    if (!session_mounted()) {
      /* see session_mounted(). an earlier setup that failed before its reset
       * is completed now - having perhaps failed before even creating the
       * upper layer, which the reset needs; otherwise the upper layer holds
       * this session's pending edits and is kept */
      bool reset = path_exists(reset_marker);
      if (reset) {
        try {
          fs::create_directories(change_root.path_cstr());
        } catch (...) {
          output_internal("cannot create [%s]\n", change_root.path_cstr());
          return false;
        }
      }
      if ((reset && !run_ovl_helper("clear", session_id))
          || !do_mount(change_root, active_root, work_root)) {
        return false;
      }
    } else {
      // best effort, a commit insists on it
      refresh_session(0);
    }
    // mounted, so the reset is done
    unlink(reset_marker.path_cstr());
  }

  pids = getActiveCommits();
  vector<int> reaped_fds;

  struct stat config_info;
  stat(work_root.path_cstr(), &config_info);
  int current_uid = (int) config_info.st_uid;
  bool failed = false;

  for (size_t i = 0; i < directories.size(); i++) {
    struct stat directory_info;
    int directory_uid;
    int current_pid = 0;

    // find uid for the current directory and the active config directory

    stat(directories[i].path_cstr(), &directory_info);
    directory_uid = (int) directory_info.st_uid;

    // remove old config session directories but only for the current user

    if (directory_uid == current_uid && directory_uid != 0 ) {
      string config_match = work_base.path_cstr() + std::string("/new_config_");
      string current_path = directories[i].path_cstr();

      if (current_path.find(config_match) != std::string::npos) {
        current_pid = atoi(current_path.erase(current_path.find(config_match), config_match.length()).c_str());

        // umount only inactive config session directory, don't touch active sessions

        if (std::find(pids.begin(), pids.end(), current_pid) == pids.end()) {
          output_internal("found inactive config [%d]\n", current_pid);
          /* its process is gone, but others may still work on the session
           * (they hold its lock), and it is removed only once unmounted:
           * removing through the mount would delete every node in it */
          string lf = C_DEF_TMP_PREFIX + std::to_string(current_pid)
                      + "/.session.lock";
          string gf = C_DEF_TMP_PREFIX + std::to_string(current_pid)
                      + "/.session.gate";
          int lfd = open(lf.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
          int gfd = open(gf.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
          /* the gate as well: somebody converting their lock does not hold
           * the lock itself for a moment, but holds the gate */
          if ((gfd >= 0 && flock(gfd, LOCK_EX | LOCK_NB) != 0)
              || (lfd >= 0 && flock(lfd, LOCK_EX | LOCK_NB) != 0)) {
            output_internal("inactive config [%d] still in use\n", current_pid);
            if (lfd >= 0) close(lfd);
            if (gfd >= 0) close(gfd);
            continue;
          }
          output_internal("umount [%s]\n", directories[i].path_cstr());
          struct stat ms, ps;
          do_umount(directories[i]);
          if (stat(directories[i].path_cstr(), &ms) == 0
              && stat(work_base.path_cstr(), &ps) == 0
              && ms.st_dev == ps.st_dev) {
            old_pids.push_back(current_pid);
            // held until its directories are gone
            reaped_fds.push_back(lfd);
            reaped_fds.push_back(gfd);
          } else {
            failed = true;
            if (lfd >= 0) close(lfd);
            if (gfd >= 0) close(gfd);
          }
        }
      }
    }
  }

  if (!old_pids.empty()) {
    for (size_t i = 0; i < directories.size(); i++) {

      int current_pid;
      string current_path;

      current_path = directories[i].path_cstr();
      current_pid = atoi(current_path.erase(0, (current_path.find_last_of("_")) + 1).c_str());

      if (std::find(old_pids.begin(), old_pids.end(), current_pid) != old_pids.end()) {
        struct stat ds, ps;
        if (stat(directories[i].path_cstr(), &ds) == 0
            && stat(work_base.path_cstr(), &ps) == 0 && ds.st_dev != ps.st_dev) {
          // mounted again meanwhile: removing would delete through it
          failed = true;
          continue;
        }
        try {
          /* the throwing overload is deliberate here and everywhere else the
           * removal count is tested: the error_code overload reports failure
           * as a count of -1, which would read as success.
           */
          if (fs::remove_all(directories[i].path_cstr()) == 0) {
            failed = true;
          }
        } catch (...) {
          failed = true;
        }
      }
    }
  }

  for (size_t i = 0; i < reaped_fds.size(); i++) {
    if (reaped_fds[i] >= 0) {
      close(reaped_fds[i]);
    }
  }
  if (failed) {
    output_internal("failed to remove old config session directories\n");
  }

  return true;
}

/* tear down the session associated with this object.
 * the session comes from either the environment or the session ID
 * (see the two different constructors).
 */
bool
UnionfsCstore::teardownSession()
{
  // check if session exists
  string wstr = work_root.path_cstr();
  if (wstr.empty() || wstr.find(C_DEF_WORK_PREFIX) != 0
      || !path_exists(work_root) || !path_is_directory(work_root)) {
    // no session
    output_internal("teardown invalid session [%s]\n", wstr.c_str());
    return false;
  }

  /* never remove a session something still works on; it is left for the
   * clean-up of abandoned sessions in setupSession() */
  if (!lock_session_exclusive(10000)) {
    output_internal("session in use, not torn down [%s]\n", wstr.c_str());
    return false;
  }

  /* unmount the work root (union). one that lost its mount already, e.g.
   * after a failed remount, is still removed */
  if ((session_mounted() && !do_umount(work_root)) || session_mounted()) {
    unlock_session_exclusive();
    return false;
  }

  // see commitConfig() for why this goes through the helper
  if (!run_ovl_helper("clear", session_id)) {
    output_internal("failed to clear [%s]\n", change_root.path_cstr());
  }

  // remove session directories
  bool ret = false;
  try {
    if (fs::remove_all(work_root.path_cstr()) != 0
        && fs::remove_all(change_root.path_cstr()) != 0
        && fs::remove_all(tmp_root.path_cstr()) != 0) {
      ret = true;
    }
  } catch (const fs::filesystem_error& e) {
    output_internal("teardown: %s\n", e.what());
  }
  if (!ret) {
    output_internal("failed to remove session directories\n");
  }
  return ret;
}

/* whether an actual config session is associated with this object.
 * the session comes from either the environment or the session ID
 * (see the two different constructors).
 */
bool
UnionfsCstore::inSession()
{
  string wstr = work_root.path_cstr();
  return (!wstr.empty() && wstr.find(C_DEF_WORK_PREFIX) == 0
          && path_exists(work_root) && path_is_directory(work_root)
          && session_mounted()
          && path_is_directory((C_DEF_TMP_PREFIX + session_id).c_str())
          && session_locked());
}


/* a session directory without its overlay reads as an empty configuration,
 * and committing from it deletes everything else. it must never count as a
 * session.
 */
bool
UnionfsCstore::session_mounted()
{
  struct stat ws, ps;
  string parent = work_root.path_cstr();
  parent.erase(parent.find_last_of("/"));
  return (stat(work_root.path_cstr(), &ws) == 0
          && stat(parent.c_str(), &ps) == 0 && ws.st_dev != ps.st_dev);
}

/* whether the session is stacked on the current active tree, according to
 * the helper's record of the tree it mounted. unknown counts as not current.
 */
bool
UnionfsCstore::session_current()
{
  FsPath rec = ovl_work_root;
  rec.push("lower");
  string data;
  struct stat as;
  if (!read_whole_file(rec, data) || stat(active_root.path_cstr(), &as) != 0) {
    return false;
  }
  char id[64];
  snprintf(id, sizeof(id), "%lu:%lu", (unsigned long) as.st_dev,
           (unsigned long) as.st_ino);
  return (data == id);
}

/* the session lock. every process working on a session holds it shared for
 * as long as it runs; moving the session onto another active tree, or
 * removing it, needs it exclusive, so that no mount is pulled from under an
 * operation in progress.
 *
 * a flock is per open file description, so one per process and session is
 * shared by all objects in the process - two would conflict with each
 * other. a second lock, the gate, is taken by whoever waits for the lock
 * exclusively and briefly by everybody taking it shared, so that a steady
 * stream of short operations cannot starve the wait.
 *
 * both live in the session's scratch directory, derived from the session ID
 * rather than the environment: not every process working on the session is
 * handed all of it.
 */
static struct {
  string sid;
  string path;
  int lock_fd;
  int gate_fd;
  int refs;
  bool exclusive;
} _slock = { "", "", -1, -1, 0, false };

static bool
_flock_wait(int fd, int op, int timeout_ms)
{
  struct timespec pause = { 0, 50 * 1000 * 1000 };
  for (int waited = 0; ; waited += 50) {
    if (flock(fd, op | LOCK_NB) == 0) {
      return true;
    }
    if (errno != EWOULDBLOCK || waited >= timeout_ms) {
      return false;
    }
    nanosleep(&pause, NULL);
  }
}

/* whether a lock descriptor still refers to the file at its path. a
 * session's files are removed when it ends or is invalidated, and set up
 * afresh under the same ID; a lock on the old file excludes nobody. */
static bool
_lock_is_current(int fd, const string& path)
{
  struct stat fs, ps;
  return (fstat(fd, &fs) == 0 && stat(path.c_str(), &ps) == 0
          && fs.st_dev == ps.st_dev && fs.st_ino == ps.st_ino);
}

void
UnionfsCstore::open_session_lock(int timeout_ms)
{
  if (session_id.empty() || holds_session_lock) {
    return;
  }
  if (_slock.refs > 0) {
    if (_slock.sid == session_id) {
      _slock.refs++;
      holds_session_lock = true;
    } else {
      output_internal("already working on session %s\n", _slock.sid.c_str());
    }
    return;
  }
  string dir = C_DEF_TMP_PREFIX + session_id;
  if (!path_is_directory(dir.c_str())) {
    return;
  }
  string lpath = dir + "/.session.lock";
  int fd = open(lpath.c_str(), O_RDONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
                0664);
  int gate = open((dir + "/.session.gate").c_str(),
                  O_RDONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0664);
  /* bounded: whoever holds it exclusively runs the helper meanwhile, and a
   * helper that never returns must not hang every later command. without
   * the lock this is no session (inSession()). */
  if (fd < 0 || gate < 0 || !_flock_wait(gate, LOCK_SH, timeout_ms)
      || !_flock_wait(fd, LOCK_SH, timeout_ms)
      || !_lock_is_current(fd, lpath)) {
    output_internal("cannot lock session %s [%s]\n", session_id.c_str(),
                    strerror(errno));
    if (fd >= 0) close(fd);
    if (gate >= 0) close(gate);
    return;
  }
  flock(gate, LOCK_UN);
  _slock.sid = session_id;
  _slock.path = lpath;
  _slock.lock_fd = fd;
  _slock.gate_fd = gate;
  _slock.refs = 1;
  _slock.exclusive = false;
  holds_session_lock = true;
}

void
UnionfsCstore::close_session_lock()
{
  if (!holds_session_lock) {
    return;
  }
  holds_session_lock = false;
  if (--_slock.refs == 0) {
    close(_slock.lock_fd);
    close(_slock.gate_fd);
    _slock.lock_fd = _slock.gate_fd = -1;
    _slock.sid.clear();
  }
}

/* wait until nobody else works on the session. converting a flock is not
 * atomic, so after giving up the shared lock is taken again. */
bool
UnionfsCstore::lock_session_exclusive(int timeout_ms)
{
  open_session_lock(timeout_ms);
  if (!holds_session_lock) {
    return false;
  }
  if (_slock.exclusive) {
    return true;
  }
  if (!_flock_wait(_slock.gate_fd, LOCK_EX, timeout_ms)) {
    return false;
  }
  bool ok = _flock_wait(_slock.lock_fd, LOCK_EX, timeout_ms);
  if (!ok && flock(_slock.lock_fd, LOCK_SH) != 0) {
    output_internal("lost session lock [%s]\n", strerror(errno));
  }
  flock(_slock.gate_fd, LOCK_UN);
  _slock.exclusive = ok;
  return ok;
}

void
UnionfsCstore::unlock_session_exclusive()
{
  if (holds_session_lock && _slock.exclusive) {
    flock(_slock.lock_fd, LOCK_SH);
    _slock.exclusive = false;
  }
}

/* working on a session without holding its lock could have the mount pulled
 * from under the operation, so it does not count as being in the session.
 * the session's directories may have been set up after this object was
 * made, so try once more without waiting. */
bool
UnionfsCstore::session_locked()
{
  if (!holds_session_lock) {
    open_session_lock(0);
  }
  return (holds_session_lock && _lock_is_current(_slock.lock_fd, _slock.path));
}

/* move the session onto the current active tree if a commit replaced it.
 * with the session lock held exclusively nothing else is working on the
 * session, so unmounting it cannot lose a change being made. a session is
 * only ever moved by its own operations, never by another session's commit.
 */
bool
UnionfsCstore::refresh_session(int timeout_ms)
{
  last_refresh_status = 0;
  if (!inSession()) {
    return false;
  }
  if (session_current()) {
    return true;
  }
  if (!lock_session_exclusive(timeout_ms)) {
    last_refresh_status = 3;
    return false;
  }
  if (path_exists(remount_marker())) {
    finish_deferred_remount();
  } else if (!session_current()) {
    last_refresh_status = ovl_helper_status("refresh", session_id);
  }
  bool recorded = true;
  if (last_refresh_status == 4) {
    /* kept until the next commit, whatever ran into this, so that the
     * commit can be refused once and the dropped edits reviewed first */
    recorded = create_file(dropped_marker());
    output_user("another session committed changes to the nodes listed "
                "above, so this session's edits to them were dropped\n");
  }
  bool ok = (recorded && session_mounted() && session_current());
  if (!session_mounted()) {
    // unmounted, but not mounted again
    invalidate_session("updating it");
  } else if (!recorded) {
    /* nothing would stop the next commit before the dropped edits are
     * reviewed, in this process or any other */
    output_internal("cannot record dropped edits [%s]\n",
                    dropped_marker().path_cstr());
    invalidate_session("dropping edits to it");
  }
  unlock_session_exclusive();
  return ok;
}

/* remount the session on a fresh upper layer over the active tree, and with
 * restore put back what the working copy taken beforehand (tmp_work_root)
 * holds beyond it: what a commit left uncommitted. */
bool
UnionfsCstore::remount_fresh(bool restore)
{
  if (!do_umount(work_root)) {
    return false;
  }
  /* the upper layer can hold nodes this user may not remove (anything
   * written by root through the merged mount), hence the helper. */
  if (!run_ovl_helper("clear", session_id)) {
    output_internal("failed to clear [%s]\n", change_root.path_cstr());
    return false;
  }
  if (!do_mount(change_root, active_root, work_root)) {
    return false;
  }
  return (!restore || sync_dir(tmp_work_root, work_root, work_root));
}

/* the remount a commit had to leave, done with the session lock held
 * exclusively. the session still stacks on the tree the commit replaced,
 * with everything it committed in its upper layer, so its working view now
 * differs from the committed tree only by what is still pending. that is
 * kept - unless another commit came in between, as the view would then also
 * revert that one. */
void
UnionfsCstore::finish_deferred_remount()
{
  string committed;
  struct stat as;
  char id[64] = "";
  read_whole_file(remount_marker(), committed);
  if (stat(active_root.path_cstr(), &as) == 0) {
    snprintf(id, sizeof(id), "%lu:%lu", (unsigned long) as.st_dev,
             (unsigned long) as.st_ino);
  }
  bool keep = (id[0] && committed == id);
  bool ok = false;
  try {
    if (keep) {
      if (path_exists(tmp_work_root)) {
        fs::remove_all(tmp_work_root.path_cstr());
      }
      recursive_copy_dir(work_root, tmp_work_root, true);
    }
    ok = remount_fresh(false);
    if (ok && keep) {
      /* another commit can replace the committed tree before the mount, and
       * the copy would then revert it: restore only onto the tree the helper
       * recorded as mounted */
      FsPath rec = ovl_work_root;
      rec.push("lower");
      string mounted;
      keep = (read_whole_file(rec, mounted) && mounted == committed);
      if (keep) {
        ok = sync_dir(tmp_work_root, work_root, work_root);
      }
    }
    if (path_exists(tmp_work_root)) {
      fs::remove_all(tmp_work_root.path_cstr());
    }
  } catch (const fs::filesystem_error& e) {
    output_internal("deferred remount failed [%s]\n", e.what());
  }
  unlink(remount_marker().path_cstr());
  if (!ok) {
    invalidate_session("commit");
  } else if (!keep) {
    output_user("this session was in use when it committed, and the active "
                "configuration has changed since; its uncommitted edits were "
                "discarded\n");
  }
}

bool
UnionfsCstore::sessionCurrent()
{
  return refresh_session(10000);
}

/* called with the commit lock held, before the working config is read.
 * committing from a view of a superseded active tree would revert whatever
 * replaced it.
 */
bool
UnionfsCstore::prepareCommit()
{
  if (!inSession()) {
    output_user("config session lost, "
                "please leave and re-enter configuration mode\n");
    return false;
  }
  if (refresh_session(30000)) {
    if (path_exists(dropped_marker())) {
      unlink(dropped_marker().path_cstr());
      output_user("edits to nodes another session changed were dropped; "
                  "review and re-apply them, then commit again\n");
      return false;
    }
    return true;
  }
  if (session_lost) {
    // already told
    return false;
  }
  if (last_refresh_status == 3) {
    output_user("the active configuration was changed by another session, "
                "and this session cannot be updated while it is in use; "
                "retry when nothing is using it\n");
  } else {
    output_user("cannot update this session to the current active "
                "configuration, please leave and re-enter configuration "
                "mode\n");
  }
  return false;
}

bool
UnionfsCstore::clearCommittedMarkers()
{
  try {
    fs::remove(commit_marker_file.path_cstr());
  } catch (...) {
    output_internal("failed to clear committed markers\n");
    return false;
  }
  return true;
}

bool
UnionfsCstore::construct_commit_active(commit::PrioNode& node)
{
  #if __GNUC__ < 6
  auto_ptr<SavePaths> save(create_save_paths());
  #else
  unique_ptr<SavePaths> save(create_save_paths());
  #endif
  reset_paths();
  append_cfg_path(node.getCommitPath());

  FsPath ap(get_active_path());
  FsPath wp(get_work_path());
  FsPath tap(tmp_active_root);
  tap /= mutable_cfg_path;

  if (path_exists(tap)) {
    output_internal("rm[%s]\n", tap.path_cstr());
    if (fs::remove_all(tap.path_cstr()) < 1) {
      output_internal("rm ta failed\n");
      return false;
    }
    cnode::CfgNode *c = node.getCfgNode();
    if (c && c->isTag()) {
      FsPath p(tap);
      p.pop();
      if (is_directory_empty(p)) {
        output_internal("rm[%s]\n", p.path_cstr());
        if (fs::remove_all(p.path_cstr()) < 1) {
          output_internal("rm tag failed\n");
          return false;
        }
      }
    }
  } else {
    output_internal("no tap[%s]\n", tap.path_cstr());
  }
  if (node.succeeded()) {
    // prio subtree succeeded
    if (path_exists(wp)) {
      output_internal("cp[%s]->[%s]\n", wp.path_cstr(), tap.path_cstr());
      try {
        recursive_copy_dir(wp, tap, true);
      } catch (const fs::filesystem_error& e) {
        output_internal("cp w->ta failed[%s]\n", e.what());
        return false;
      } catch (...) {
        output_internal("cp w->ta failed[unknown exception]\n");
        return false;
      }
    } else {
      output_internal("no wp[%s]\n", wp.path_cstr());
    }
    if (!node.hasSubtreeFailure()) {
      // whole subtree succeeded => stop recursion
      return true;
    }
    // failure present in subtree
  } else {
    // prio subtree failed
    if (path_exists(ap)) {
      output_internal("cp[%s]->[%s]\n", ap.path_cstr(), tap.path_cstr());
      try {
        recursive_copy_dir(ap, tap, false);
      } catch (const fs::filesystem_error& e) {
        output_internal("cp a->ta failed[%s]\n", e.what());
        return false;
      } catch (...) {
        output_internal("cp a->ta failed[unknown exception]\n");
        return false;
      }
    } else {
      output_internal("no ap[%s]\n", ap.path_cstr());
    }
    if (!node.hasSubtreeSuccess()) {
      // whole subtree failed => stop recursion
      return true;
    }
    // success present in subtree
  }
  for (size_t i = 0; i < node.numChildNodes(); i++) {
    if (!construct_commit_active(*(node.childAt(i)))) {
      return false;
    }
  }
  return true;
}

bool
UnionfsCstore::mark_dir_changed(const FsPath& d, const FsPath& root)
{
  if (!path_is_directory(d)) {
    output_internal("mark_dir_changed on non-directory [%s]\n",
                    d.path_cstr());
    return false;
  }

  FsPath marker(d);
  while (marker.size() >= root.size()) {
    marker.push(C_MARKER_CHANGED);
    if (path_exists(marker)) {
      // reached a node already marked => done
      break;
    }
    if (!create_file(marker)) {
      output_internal("failed to mark changed [%s]\n", marker.path_cstr());
      return false;
    }
    marker.pop();
    marker.pop();
  }
  return true;
}

bool
UnionfsCstore::sync_dir(const FsPath& src, const FsPath& dst,
                        const FsPath& root)
{
  if (!path_exists(src) || !path_exists(dst)) {
    output_user("sync_dir with non-existing dir(s)[%s][%s]\n",
                src.path_cstr(), dst.path_cstr());
    return false;
  }
  MapT<string, bool> smap;
  MapT<string, bool> dmap;
  vector<string> sentries;
  vector<string> dentries;
  check_dir_entries(src, &sentries, false);
  check_dir_entries(dst, &dentries, false);
  for (size_t i = 0; i < sentries.size(); i++) {
    smap[sentries[i]] = true;
  }
  for (size_t i = 0; i < dentries.size(); i++) {
    dmap[dentries[i]] = true;
    if (smap.find(dentries[i]) == smap.end()) {
      // entry in dst but not in src => delete
      FsPath d(dst);
      if (!mark_dir_changed(d, root)) {
        return false;
      }
      push_path(d, dentries[i].c_str());
      if (fs::remove_all(d.path_cstr()) < 1) {
        return false;
      }
    } else {
      // entry in both src and dst
      FsPath s(src);
      FsPath d(dst);
      push_path(s, dentries[i].c_str());
      push_path(d, dentries[i].c_str());
      if (path_is_regular(s) && path_is_regular(d)) {
        // it's file => compare and replace if necessary
        string ds, dd;
        if (!read_whole_file(s, ds) || !read_whole_file(d, dd)) {
          // error
          output_user("failed to replace file [%s][%s]\n",
                      s.path_cstr(), d.path_cstr());
          return false;
        }
        if (ds != dd) {
          // need to replace
          if (!write_file(d, ds)) {
            output_user("failed to write file [%s]\n", d.path_cstr());
            return false;
          }
          d.pop();
          if (!mark_dir_changed(d, root)) {
            return false;
          }
        }
      } else if (path_is_directory(s) && path_is_directory(d)) {
        // it's dir => recurse
        if (!sync_dir(s, d, root)) {
          return false;
        }
      } else {
        // something is wrong
        output_user("inconsistent config entry [%s][%s]\n",
                    s.path_cstr(), d.path_cstr());
        return false;
      }
    }
  }
  for (size_t i = 0; i < sentries.size(); i++) {
    if (dmap.find(sentries[i]) == dmap.end()) {
      // entry in src but not in dst => copy
      FsPath s(src);
      FsPath d(dst);
      push_path(s, sentries[i].c_str());
      push_path(d, sentries[i].c_str());
      try {
        if (path_is_regular(s)) {
          // it's file
          try {
            fs::copy_file(s.path_cstr(), d.path_cstr());
          } catch (const fs::filesystem_error& e) {
            output_internal("syncdir failed due to %s in copy_file. Falling back to internal stream_file\n", e.what());
            stream_file(s.path_cstr(), d.path_cstr());
          }
        } else {
          // dir
          recursive_copy_dir(s, d, true);
        }
        d.pop();
        if (!mark_dir_changed(d, root)) {
          return false;
        }
      } catch (...) {
        output_user("copy failed [%s][%s]\n", s.path_cstr(), d.path_cstr());
        return false;
      }
    }
  }

  return true;
}

bool
UnionfsCstore::commitConfig(commit::PrioNode& node)
{
  /* the working config read for this commit must be this session's view of
   * the current active config; anything else is a different diff */
  if (!session_mounted() || !session_current()) {
    output_user("config session changed during commit\n");
    return false;
  }
  if (!construct_commit_active(node)) {
    return false;
  }

  /* build the replacement active tree beside the current one and swap it in
   * atomically.
   *
   * the active tree is the overlay lowerdir of EVERY live config session.
   * rewriting it in place would be a modification of a live lower layer,
   * which the kernel leaves undefined. with RENAME_EXCHANGE the old tree
   * stays fully intact behind the other sessions' mounts until we re-stack
   * them below, so there is no instant at which anybody observes a partially
   * written active config.
   */
  /* a unique name per commit: a previous tree kept behind by an incomplete
   * re-stack may still be some session's lowerdir and must not be reused.
   */
  string na = active_root.path_cstr();
  na += ".new.XXXXXX";
  if (!mkdtemp(&na[0])) {
    output_internal("failed to create staging dir [%s][%s]\n",
                    strerror(errno), na.c_str());
    return false;
  }
  FsPath new_active(na);
  bool staged = false;
  try {
    recursive_copy_dir(tmp_active_root, new_active, true);
    staged = true;
  } catch (const fs::filesystem_error& e) {
    output_internal("cp ta->na failed[%s]\n", e.what());
  } catch (...) {
    output_internal("cp ta->na failed[unknown exception]\n");
  }
  if (staged && !normalize_active_perms(new_active)) {
    output_internal("failed to normalize permissions on [%s]\n",
                    new_active.path_cstr());
    staged = false;
  }

  int trees = staged ? lock_trees() : -1;
  if (staged
      && syscall(SYS_renameat2, AT_FDCWD, new_active.path_cstr(),
                 AT_FDCWD, active_root.path_cstr(), RENAME_EXCHANGE) != 0) {
    output_internal("failed to swap in new active config [%s][%s]\n",
                    strerror(errno), new_active.path_cstr());
    staged = false;
  }
  if (trees >= 0) {
    close(trees);
  }
  if (!staged) {
    // never swapped in, so nothing can be stacked on it
    try {
      fs::remove_all(new_active.path_cstr());
    } catch (...) {
    }
    return false;
  }
  /* the new configuration is active from here on. whatever fails below
   * concerns this session only and must not be reported as a failed commit:
   * the configuration was applied, and the swap is not undone.
   *
   * other sessions stay stacked on the previous tree. they are moved onto
   * this one by their own next change or commit (see refreshSession()); this
   * commit never touches them, as unmounting a session can lose a change
   * that is being written into it at that moment.
   */
  /* the upper layer holds everything just committed. left in place it would
   * be re-applied over later commits, so the session is remounted on a
   * fresh one - once nothing else is working on it: remounting under
   * something that is would lose what it writes. failing that, the session
   * is left as it is and remounted by its next operation (see
   * finish_deferred_remount()). */
  if (lock_session_exclusive(30000)) {
    /* the copy to restore from is taken under the same lock as the remount,
     * so nothing written to the session in between is lost. the session
     * still stacks on the tree just replaced, and no other commit can come
     * in meanwhile, so the copy differs from what was committed only by
     * what is still pending */
    bool copied = false;
    try {
      if (path_exists(tmp_work_root)) {
        fs::remove_all(tmp_work_root.path_cstr());
      }
      recursive_copy_dir(work_root, tmp_work_root, true);
      copied = true;
    } catch (const fs::filesystem_error& e) {
      output_internal("cp w->tw failed[%s]\n", e.what());
    }
    if (!copied || !remount_fresh(true)) {
      output_user("configuration committed, but ");
      invalidate_session("commit");
    }
    unlock_session_exclusive();
  } else {
    struct stat as;
    char id[64] = "";
    if (stat(active_root.path_cstr(), &as) == 0) {
      snprintf(id, sizeof(id), "%lu:%lu", (unsigned long) as.st_dev,
               (unsigned long) as.st_ino);
    }
    if (!id[0] || !write_file(remount_marker(), id)) {
      output_user("configuration committed, but this session is in use and "
                  "could not be updated; please leave and re-enter "
                  "configuration mode\n");
    } else {
      output_internal("session in use, remounting it on its next use\n");
    }
  }
  try {
    fs::remove_all(tmp_work_root.path_cstr());
    fs::remove_all(tmp_active_root.path_cstr());
  } catch (const fs::filesystem_error& e) {
    output_internal("failed to remove temp directories [%s]\n", e.what());
  }
  // without the lock a refresh may be moving a session onto one of them
  if ((trees = lock_trees()) >= 0) {
    remove_unused_active_trees();
    close(trees);
  }
  return true;
}

/* held exclusively while the set of active trees changes: one swapped in,
 * unused ones removed. the overlay helper holds it shared from comparing a
 * session with the active tree until the session is mounted on it, so that
 * the tree compared is the tree mounted. never held across a helper call. */
int
UnionfsCstore::lock_trees()
{
  int fd = open(C_TREE_LOCK_FILE.c_str(),
                O_RDONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0664);
  if (fd < 0 || flock(fd, LOCK_EX) != 0) {
    output_internal("cannot lock the active trees [%s]\n", strerror(errno));
    if (fd >= 0) {
      close(fd);
    }
    return -1;
  }
  return fd;
}

/* superseded active trees are kept while a mounted session is stacked on one
 * of them. the helper records which tree each mount uses; a mounted session
 * without such a record could be on any of them, so then nothing is removed.
 */
void
UnionfsCstore::remove_unused_active_trees()
{
  string root = C_DEF_CFG_ROOT;
  string prefix = (active_root.path_cstr() + string(".new."));
  vector<string> trees;
  std::set<string> pinned;
  try {
    for (const auto& e : fs::directory_iterator(root)) {
      string p = e.path().string();
      if (p.compare(0, prefix.length(), prefix) == 0) {
        trees.push_back(p);
      }
    }
    if (trees.empty()) {
      return;
    }
    string wprefix = C_DEF_WORK_PREFIX;
    string wbase = wprefix.substr(0, wprefix.find_last_of("/"));
    struct stat bs;
    if (stat(wbase.c_str(), &bs) != 0) {
      return;
    }
    for (const auto& e : fs::directory_iterator(wbase)) {
      string p = e.path().string();
      string sid;
      struct stat ms;
      if (!session_id_from_work_root(FsPath(p.c_str()), sid)
          || lstat(p.c_str(), &ms) != 0 || !S_ISDIR(ms.st_mode)
          || stat(p.c_str(), &ms) != 0 || ms.st_dev == bs.st_dev) {
        continue; // not a mounted session
      }
      FsPath rec = ovl_work_root_for(sid);
      rec.push("lower");
      string data;
      if (!read_whole_file(rec, data) || data.empty()) {
        output_internal("session %s has no lower layer record, "
                        "keeping old active trees\n", sid.c_str());
        return;
      }
      pinned.insert(data);
    }
  } catch (const fs::filesystem_error& e) {
    output_internal("cannot look for unused active trees [%s]\n", e.what());
    return;
  }
  for (size_t i = 0; i < trees.size(); i++) {
    struct stat ts;
    if (lstat(trees[i].c_str(), &ts) != 0 || !S_ISDIR(ts.st_mode)) {
      continue;
    }
    char id[64];
    snprintf(id, sizeof(id), "%lu:%lu", (unsigned long) ts.st_dev,
             (unsigned long) ts.st_ino);
    if (pinned.count(id)) {
      continue;
    }
    try {
      fs::remove_all(trees[i]);
    } catch (const fs::filesystem_error& e) {
      output_internal("cannot remove unused active tree [%s]\n", e.what());
    }
  }
}

bool
UnionfsCstore::getCommitLock()
{
  /* one descriptor per process: a POSIX lock is released when any
   * descriptor of the file is closed, so none may ever be closed, and
   * opening one per attempt would leak them */
  static int fd = -1;

  if (fd < 0) {
    fd = open(C_COMMIT_LOCK_FILE.c_str(), O_WRONLY|O_CREAT|O_CLOEXEC, 0666);
  }
  if (fd < 0) {
    // should not happen since all commit processes should have write access
    output_internal("getCommitLock() failed to open lock file\n");
    return false;
  }
  if (lockf(fd, F_TLOCK, 0) < 0) {
    // locked by someone else
    return false;
  }
  // got the lock
  return true;
}


////// virtual functions defined in base class
/* check if current tmpl_path is a valid tmpl dir.
 * return true if valid. otherwise return false.
 */
bool
UnionfsCstore::tmpl_node_exists()
{
  return (path_exists(tmpl_path) && path_is_directory(tmpl_path));
}

typedef MapT<FsPath, tr1::shared_ptr<vtw_def>, FsPathHash> ParsedTmplCacheT;
static ParsedTmplCacheT _parsed_tmpl_cache;

/* parse template at current tmpl_path and return an allocated Ctemplate
 * pointer if successful. otherwise return 0.
 */
Ctemplate *
UnionfsCstore::tmpl_parse()
{
  FsPath tp = tmpl_path;
  tp.push(C_DEF_NAME);
  if (!path_exists(tp) || !path_is_regular(tp)) {
    // invalid
    return 0;
  }

  ParsedTmplCacheT::iterator p = _parsed_tmpl_cache.find(tp);
  if (p != _parsed_tmpl_cache.end()) {
    // found in cache
    return (new Ctemplate(p->second));
  }

  // new template => parse
  tr1::shared_ptr<vtw_def> def(new vtw_def);
  vtw_def *_def = def.get();
  if (_def && parse_def(_def, tp.path_cstr(), 0) == 0) {
    // succes => cache and return
    _parsed_tmpl_cache[tp] = def;
    return (new Ctemplate(def));
  }
  return 0;
}

bool
UnionfsCstore::cfg_node_exists(bool active_cfg)
{
  FsPath p = (active_cfg ? get_active_path() : get_work_path());
  return (path_exists(p) && path_is_directory(p));
}

bool
UnionfsCstore::add_node()
{
  bool ret = true;
  try {
    if (!fs::create_directory(get_work_path().path_cstr())) {
      // already exists. shouldn't call this function.
      ret = false;
    }
  } catch (...) {
    ret = false;
  }
  if (!ret) {
    output_internal("failed to add node [%s]\n",
                    get_work_path().path_cstr());
  }
  return ret;
}

bool
UnionfsCstore::remove_node()
{
  if (!path_exists(get_work_path())
      || !path_is_directory(get_work_path())) {
    output_internal("remove non-existent node [%s]\n",
                    get_work_path().path_cstr());
    return false;
  }
  bool ret = false;
  try {
    if (fs::remove_all(get_work_path().path_cstr()) != 0) {
      ret = true;
    }
  } catch (...) {
    ret = false;
  }
  if (!ret) {
    output_internal("failed to remove node [%s]\n",
                    get_work_path().path_cstr());
  }
  return ret;
}

void
UnionfsCstore::get_all_child_node_names_impl(vector<string>& cnodes,
                                             bool active_cfg)
{
  FsPath p = (active_cfg ? get_active_path() : get_work_path());
  get_all_child_dir_names(p, cnodes);

  /* XXX special cases to emulate original perl API behavior.
   *     original perl listNodes() and listOrigNodes() return everything
   *     under a node (except for ".*"), including "node.val" and "def".
   *
   *     perl API should operate at abstract level and should not access
   *     such implementation-specific details. however, currently
   *     things like config output depend on this behavior, so this
   *     function needs to return them for now.
   *
   *     use a whilelist-approach, i.e., only add the following:
   *       node.val
   *       def
   *
   * FIXED: perl scripts have been changed to eliminate the use of "def"
   * and "node.val", so they no longer need to be returned.
   */
}

bool
UnionfsCstore::read_value_vec(vector<string>& vvec, bool active_cfg)
{
  FsPath vpath = (active_cfg ? get_active_path() : get_work_path());
  vpath.push(C_VAL_NAME);

  string ostr;
  if (!read_whole_file(vpath, ostr)) {
    return false;
  }

  /* XXX original implementation used to remove a trailing '\n' after
   *     a read. it was only necessary because it was adding a '\n' when
   *     writing the file. don't remove anything now since we shouldn't
   *     be writing it any more.
   */
  // separate values using newline as delimiter
  size_t start_idx = 0, idx = 0;
  for (; idx < ostr.size(); idx++) {
    if (ostr[idx] == '\n') {
      // got a value
      vvec.push_back(ostr.substr(start_idx, (idx - start_idx)));
      start_idx = idx + 1;
    }
  }
  if (start_idx < ostr.size()) {
    vvec.push_back(ostr.substr(start_idx, (idx - start_idx)));
  } else {
    // last char is a newline => another empty value
    vvec.push_back("");
  }
  return true;
}

bool
UnionfsCstore::write_value_vec(const vector<string>& vvec, bool active_cfg)
{
  FsPath wp = (active_cfg ? get_active_path() : get_work_path());
  wp.push(C_VAL_NAME);

  if (path_exists(wp) && !path_is_regular(wp)) {
    // not a file
    output_internal("failed to write node value (file) [%s]\n",
                    wp.path_cstr());
    return false;
  }

  string ostr = "";
  for (size_t i = 0; i < vvec.size(); i++) {
    if (i > 0) {
      // subsequent values require delimiter
      ostr += "\n";
    }
    ostr += vvec[i];
  }

  if (!write_file(wp, ostr)) {
    output_internal("failed to write node value (write) [%s]\n",
                    wp.path_cstr());
    return false;
  }

  return true;
}

bool
UnionfsCstore::rename_child_node(const char *oname, const char *nname)
{
  FsPath opath = get_work_path();
  opath.push(oname);
  FsPath npath = get_work_path();
  npath.push(nname);
  if (!path_exists(opath) || !path_is_directory(opath)
      || path_exists(npath)) {
    output_internal("cannot rename node [%s,%s,%s]\n",
                    get_work_path().path_cstr(), oname, nname);
    return false;
  }
  bool ret = true;
  try {
    /* somehow fs::rename() can't be used here as it considers the operation
     * "Invalid cross-device link" and fails with an exception, probably due
     * to unionfs in some way.
     * do it the hard way.
     */
    recursive_copy_dir(opath, npath);
    if (fs::remove_all(opath.path_cstr()) == 0) {
      ret = false;
    }
  } catch (...) {
    ret = false;
  }
  if (!ret) {
    output_internal("failed to rename node [%s,%s]\n", opath.path_cstr(),
                    npath.path_cstr());
  }
  return ret;
}

bool
UnionfsCstore::copy_child_node(const char *oname, const char *nname)
{
  FsPath opath = get_work_path();
  opath.push(oname);
  FsPath npath = get_work_path();
  npath.push(nname);
  if (!path_exists(opath) || !path_is_directory(opath)
      || path_exists(npath)) {
    output_internal("cannot copy node [%s,%s,%s]\n",
                    get_work_path().path_cstr(), oname, nname);
    return false;
  }
  try {
    recursive_copy_dir(opath, npath);
  } catch (...) {
    output_internal("failed to copy node [%s,%s,%s]\n",
                    get_work_path().path_cstr(), oname, nname);
    return false;
  }
  return true;
}

bool
UnionfsCstore::mark_display_default()
{
  FsPath marker = get_work_path();
  marker.push(C_MARKER_DEF_VALUE);
  if (path_exists(marker)) {
    // already marked. treat as success.
    return true;
  }
  if (!create_file(marker)) {
    output_internal("failed to mark default [%s]\n",
                    get_work_path().path_cstr());
    return false;
  }
  return true;
}

bool
UnionfsCstore::unmark_display_default()
{
  FsPath marker = get_work_path();
  marker.push(C_MARKER_DEF_VALUE);
  if (!path_exists(marker)) {
    // not marked. treat as success.
    return true;
  }
  try {
    fs::remove(marker.path_cstr());
  } catch (...) {
    output_internal("failed to unmark default [%s]\n",
                    get_work_path().path_cstr());
    return false;
  }
  return true;
}

bool
UnionfsCstore::marked_display_default(bool active_cfg)
{
  FsPath marker = (active_cfg ? get_active_path() : get_work_path());
  marker.push(C_MARKER_DEF_VALUE);
  return path_exists(marker);
}

bool
UnionfsCstore::marked_deactivated(bool active_cfg)
{
  FsPath marker = (active_cfg ? get_active_path() : get_work_path());
  marker.push(C_MARKER_DEACTIVATE);
  return path_exists(marker);
}

bool
UnionfsCstore::mark_deactivated()
{
  FsPath marker = get_work_path();
  marker.push(C_MARKER_DEACTIVATE);
  if (path_exists(marker)) {
    // already marked. treat as success.
    return true;
  }
  if (!create_file(marker)) {
    output_internal("failed to mark deactivated [%s]\n",
                    get_work_path().path_cstr());
    return false;
  }
  return true;
}

bool
UnionfsCstore::unmark_deactivated()
{
  FsPath marker = get_work_path();
  marker.push(C_MARKER_DEACTIVATE);
  if (!path_exists(marker)) {
    // not deactivated. treat as success.
    return true;
  }
  try {
    fs::remove(marker.path_cstr());
  } catch (...) {
    output_internal("failed to unmark deactivated [%s]\n",
                    get_work_path().path_cstr());
    return false;
  }
  return true;
}

bool
UnionfsCstore::unmark_deactivated_descendants()
{
  bool ret = false;
  do {
    // sanity check
    if (!path_is_directory(get_work_path())) {
      break;
    }

    try {
      vector<fs::path> markers;
      fs::recursive_directory_iterator di(get_work_path().path_cstr());
      for (; di != fs::recursive_directory_iterator(); ++di) {
        if (!path_is_regular(di->path().string().c_str())
            || di->path().filename() != C_MARKER_DEACTIVATE) {
          // not marker
          continue;
        }
        /* hold the string: path::string() returns by value under
         * std::filesystem, so c_str() of the temporary would dangle.
         */
        const string ppath = di->path().parent_path().string();
        if (strcmp(ppath.c_str(), get_work_path().path_cstr()) == 0) {
          // don't unmark the node itself
          continue;
        }
        markers.push_back(di->path());
      }
      for (size_t i = 0; i < markers.size(); i++) {
        fs::remove(markers[i]);
      }
    } catch (...) {
      break;
    }
    ret = true;
  } while (0);
  if (!ret) {
    output_internal("failed to unmark deactivated descendants [%s]\n",
                    get_work_path().path_cstr());
  }
  return ret;
}

// mark current work path and all ancestors as "changed"
bool
UnionfsCstore::mark_changed_with_ancestors()
{
  FsPath opath = mutable_cfg_path; // use a copy
  bool done = false;
  while (!done) {
    FsPath marker = work_root;
    if (opath.has_parent_path()) {
      marker /= opath;
      pop_path(opath);
    } else {
      done = true;
    }
    if (!path_exists(marker) || !path_is_directory(marker)) {
      // don't do anything if the node is not there
      continue;
    }
    marker.push(C_MARKER_CHANGED);
    if (path_exists(marker)) {
      // reached a node already marked => done
      break;
    }
    if (!create_file(marker)) {
      output_internal("failed to mark changed [%s]\n", marker.path_cstr());
      return false;
    }
  }
  return true;
}

/* remove all "changed" markers under the current work path. this is used,
 * e.g., at the end of "commit" to reset a subtree.
 */
bool
UnionfsCstore::unmark_changed_with_descendants()
{
  try {
    vector<fs::path> markers;
    fs::recursive_directory_iterator di(get_work_path().path_cstr());
    for (; di != fs::recursive_directory_iterator(); ++di) {
      if (!path_is_regular(di->path().string().c_str())
          || di->path().filename() != C_MARKER_CHANGED) {
        // not marker
        continue;
      }
      markers.push_back(di->path());
    }
    for (size_t i = 0; i < markers.size(); i++) {
      fs::remove(markers[i]);
    }
  } catch (...) {
    output_internal("failed to unmark changed with descendants [%s]\n",
                    get_work_path().path_cstr());
    return false;
  }
  return true;
}

// remove the comment at the current work path
bool
UnionfsCstore::remove_comment()
{
  FsPath cfile = get_work_path();
  cfile.push(C_COMMENT_FILE);
  if (!path_exists(cfile)) {
    return false;
  }
  try {
    fs::remove(cfile.path_cstr());
  } catch (...) {
    output_internal("failed to remove comment [%s]\n", cfile.path_cstr());
    return false;
  }
  return true;
}

// set comment at the current work path
bool
UnionfsCstore::set_comment(const string& comment)
{
  FsPath cfile = get_work_path();
  cfile.push(C_COMMENT_FILE);
  return write_file(cfile, comment);
}

// discard all changes in working config
bool
UnionfsCstore::discard_changes(unsigned long long& num_removed)
{
  // need to keep unsaved marker
  bool unsaved = sessionUnsaved();
  bool ret = true;

  /* the change root is the overlay upperdir. modifying a layer of a live
   * overlay is undefined, so unmount first and remount afterwards.
   *
   * note that discarding cannot be done through the merged mount: removing a
   * node there creates a whiteout rather than exposing the active config
   * again, which would leave an empty config instead of the active one.
   */
  if (!lock_session_exclusive(10000)) {
    output_user("cannot discard, the session is in use\n");
    return false;
  }
  if (!do_umount(work_root)) {
    unlock_session_exclusive();
    return false;
  }

  num_removed = 0;
  try {
    fs::recursive_directory_iterator di(change_root.path_cstr());
    for (; di != fs::recursive_directory_iterator(); ++di) {
      num_removed++;
    }
  } catch (const fs::filesystem_error& e) {
    output_internal("discard: cannot count changes [%s]\n", e.what());
  }
  /* see commitConfig() for why this goes through the helper. without a
   * fresh upper layer the session cannot be mounted again: a detached
   * overlay may still be using the old one */
  bool remounted = run_ovl_helper("clear", session_id)
                   && do_mount(change_root, active_root, work_root);
  unlock_session_exclusive();
  if (!remounted) {
    output_internal("discard failed [%s]\n", change_root.path_cstr());
    return invalidate_session("discard");
  }

  if (unsaved) {
    /* restore unsaved marker. must happen after the remount: the marker is
     * written through the merged mount.
     */
    num_removed--;
    markSessionUnsaved();
  }
  return ret;
}

// get comment at the current work or active path
bool
UnionfsCstore::get_comment(string& comment, bool active_cfg)
{
  FsPath cfile = (active_cfg ? get_active_path() : get_work_path());
  cfile.push(C_COMMENT_FILE);
  return read_whole_file(cfile, comment);
}

// whether current work path is "changed"
bool
UnionfsCstore::cfg_node_changed()
{
  FsPath marker = get_work_path();
  marker.push(C_MARKER_CHANGED);
  return path_exists(marker);
}

void
UnionfsCstore::get_edit_level(Cpath& pcomps) {
  FsPath opath = mutable_cfg_path; // use a copy
  vector<string> tmp;
  while (opath.has_parent_path()) {
    string last;
    pop_path(opath, last);
    tmp.push_back(last);
  }
  while (tmp.size() > 0) {
    pcomps.push(tmp.back());
    tmp.pop_back();
  }
}

bool
UnionfsCstore::marked_committed(bool is_delete)
{
  string marker;
  get_committed_marker(is_delete, marker);
  return find_line_in_file(commit_marker_file, marker);
}

bool
UnionfsCstore::mark_committed(bool is_delete)
{
  string marker;
  get_committed_marker(is_delete, marker);
  // write one marker per line
  return write_file(commit_marker_file, marker + "\n", true);
}

string
UnionfsCstore::cfg_path_to_str() {
  string cpath = mutable_cfg_path.path_cstr();
  if (cpath.length() == 0) {
    cpath = "/";
  }
  return cpath;
}

string
UnionfsCstore::tmpl_path_to_str() {
  // return only the mutable part
  string tpath = tmpl_path.path_cstr();
  tpath.erase(0, tmpl_root.length());
  if (tpath.length() == 0) {
    tpath = "/";
  }
  return tpath;
}


////// private functions
void
UnionfsCstore::push_path(FsPath& old_path, const char *new_comp)
{
  string comp = _escape_path_name(new_comp);
  old_path.push(comp);
}

void
UnionfsCstore::pop_path(FsPath& path)
{
  path.pop();
}

void
UnionfsCstore::pop_path(FsPath& path, string& last)
{
  path.pop(last);
  last = _unescape_path_name(last);
}

bool
UnionfsCstore::check_dir_entries(const FsPath& root, vector<string> *cnodes,
                                 bool filter_nodes, bool empty_check)
{
  if (!path_exists(root) || !path_is_directory(root)) {
    // not a valid root => treat as empty
    return false;
  }
  bool found = false;
  try {
    fs::directory_iterator di(root.path_cstr());
    for (; di != fs::directory_iterator(); ++di) {
      string cname = di->path().filename().string();
      if (filter_nodes) {
        // must be directory
        if (!path_is_directory(di->path().string().c_str())) {
          continue;
        }
        // name cannot start with "."
        if (cname.length() < 1 || cname[0] == '.') {
          continue;
        }
      }
      // found one
      if (empty_check) {
        // only checking and directory is not empty
        return true;
      }
      if (cnodes) {
        cnodes->push_back(_unescape_path_name(cname));
      } else {
        found = true;
      }
    }
  } catch (...) {
    // skip the rest
  }
  return (cnodes ? (cnodes->size() > 0) : found);
}

bool
UnionfsCstore::write_file(const char *file, const string& data, bool append)
{
  if (data.size() > C_UNIONFS_MAX_FILE_SIZE) {
    output_internal("write_file too large\n");
    return false;
  }
  try {
    // make sure the path exists
    FsPath ppath(file);
    ppath.pop();
    fs::create_directories(ppath.path_cstr());

    // write the file
    std::ofstream fout;
    fout.exceptions(std::ofstream::failbit | std::ofstream::badbit);
    ios_base::openmode mflags = ios_base::out;
    mflags |= ((!append || !path_exists(file))
               ? ios_base::trunc : ios_base::app); // truncate or append
    fout.open(file, mflags);
    fout << data;
    fout.close();
  } catch (...) {
    return false;
  }
  return true;
}

bool
UnionfsCstore::read_whole_file(const FsPath& fpath, string& data)
{
  /* must exist, be a regular file, and smaller than limit (we're going
   * to read the whole thing).
   */
  if (!path_exists(fpath) || !path_is_regular(fpath)) {
    return false;
  }
  try {
    if (fs::file_size(fpath.path_cstr()) > C_UNIONFS_MAX_FILE_SIZE) {
      output_internal("read_whole_file too large\n");
      return false;
    }

    stringbuf sbuf;
    std::ifstream fin(fpath.path_cstr());
    fin >> &sbuf;
    fin.close();
    /* note: if file contains just a newline => (eof() && fail())
     *       so only checking bad() and eof() (we want whole file).
     */
    if (fin.bad() || !fin.eof()) {
      // read failed
      return false;
    }
    data = sbuf.str();
  } catch (...) {
    return false;
  }
  return true;
}

/* recursively copy source directory to destination.
 * will throw exception (from the filesystem library) if fail.
 */
void
UnionfsCstore::recursive_copy_dir(const FsPath& src, const FsPath& dst,
                                  bool filter_dot_entries)
{
  string src_str = src.path_cstr();
  string dst_str = dst.path_cstr();
  fs::create_directories(dst.path_cstr());

  fs::recursive_directory_iterator di(src_str);
  for (; di != fs::recursive_directory_iterator(); ++di) {
    /* hold the string: path::string() returns by value under
     * std::filesystem, so c_str() of the temporary would dangle.
     */
    const string oname = di->path().string();
    string nname = oname;
    nname.replace(0, src_str.length(), dst_str);
    if (path_is_directory(oname.c_str())) {
      fs::create_directory(nname);
    } else {
      if (filter_dot_entries) {
        string of = di->path().filename().string();
        if (!of.empty() && of.at(0) == '.') {
          // filter dot files (with exceptions)
          if (of != C_COMMENT_FILE) {
            continue;
          }
        }
      }
      try {
        fs::copy_file(di->path(), nname);
      } catch (const fs::filesystem_error& e) {
        output_internal("recursive_copy_dir failed due to %s in copy_file. Falling back to internal stream_file\n", e.what());
        stream_file(di->path().string().c_str(), nname.c_str());
      }
    }
  }
}

void
UnionfsCstore::get_committed_marker(bool is_delete, string& marker)
{
  marker = (is_delete ? "-" : "");
  marker += mutable_cfg_path.path_cstr();
}

bool
UnionfsCstore::find_line_in_file(const FsPath& file, const string& line)
{
  bool ret = false;
  try {
    std::ifstream fin(file.path_cstr());
    while (!fin.eof() && !fin.bad() && !fin.fail()) {
      string in;
      getline(fin, in);
      if (in == line) {
        ret = true;
        break;
      }
    }
    fin.close();
  } catch (...) {
    ret = false;
  }
  return ret;
}

/* derive the overlayfs workdir path for a given session ID. */
FsPath
UnionfsCstore::ovl_work_root_for(const string& sid)
{
  return FsPath((C_DEF_OVLWORK_PREFIX + sid).c_str());
}

/* recover the session ID from a working root path, i.e. the "1234" in
 * "<cfg_root>/tmp/new_config_1234". returns false if the path does not look
 * like a session working root.
 */
bool
UnionfsCstore::session_id_from_work_root(const FsPath& wroot, string& sid)
{
  string wstr = wroot.path_cstr();
  if (wstr.find(C_DEF_WORK_PREFIX) != 0) {
    return false;
  }
  sid = wstr.substr(C_DEF_WORK_PREFIX.length());
  if (sid.empty() || sid.find_first_not_of("0123456789") != string::npos) {
    return false;
  }
  return true;
}

void
UnionfsCstore::set_session_id(const string& sid)
{
  session_id = sid;
  ovl_work_root = ovl_work_root_for(sid);
}

/* whether the given path is a mount point, per /proc/self/mountinfo.
 * used to avoid stacking a second overlay on a session that was not cleanly
 * torn down, and to decide whether another session actually needs unmounting.
 */
bool
UnionfsCstore::is_mount_point(const FsPath& p)
{
  bool ret = false;
  string target = p.path_cstr();
  std::ifstream fin("/proc/self/mountinfo");
  if (!fin.is_open()) {
    /* cannot tell; report mounted so a caller does not stack a second
     * overlay on a path that may already carry one.
     */
    output_internal("cannot read the mount table\n");
    return true;
  }
  string line;
  while (getline(fin, line)) {
    /* mountinfo field 5 is the mount point. fields are space separated and
     * the mount point is octal-escaped, but our paths contain no characters
     * that would be escaped.
     */
    std::istringstream iss(line);
    string fld;
    for (int i = 0; i < 5; i++) {
      if (!(iss >> fld)) {
        fld.clear();
        break;
      }
    }
    if (fld == target) {
      ret = true;
      break;
    }
  }
  return ret;
}

/* run the privileged helper for one overlay operation.
 *
 * mounting an overlay needs privilege this process must not hold: granting
 * it here would mean granting the right to disregard file permissions, and
 * those permissions are what keep a user outside the configuration group out
 * of the config store. the helper refuses to act on a session the caller
 * does not already own, so that boundary survives.
 */
bool
UnionfsCstore::run_ovl_helper(const char *op, const string& sid)
{
  return (ovl_helper_status(op, sid) == 0);
}

// exit status of the helper, -1 if it did not run to completion
int
UnionfsCstore::ovl_helper_status(const char *op, const string& sid)
{
  if (sid.empty()) {
    output_internal("no session for overlay %s\n", op);
    return -1;
  }
  /* what the helper has for the user comes on its stdout, which while a CLI
   * operation runs would otherwise go to the operation's log */
  int out[2];
  if (pipe2(out, O_CLOEXEC) != 0) {
    output_internal("cannot run the overlay helper [%s]\n", strerror(errno));
    return -1;
  }
  pid_t p = fork();
  if (p < 0) {
    output_internal("cannot run the overlay helper [%s]\n", strerror(errno));
    close(out[0]);
    close(out[1]);
    return -1;
  }
  if (p == 0) {
    if (dup2(out[1], STDOUT_FILENO) < 0) {
      _exit(127);
    }
    execl(C_OVL_HELPER.c_str(), "vyos-cfg-overlay-helper", op, sid.c_str(),
          (char *) NULL);
    _exit(127);
  }
  close(out[1]);
  // read to the end before waiting, or a long message could block the helper
  string message;
  char buf[4096];
  ssize_t n;
  while ((n = read(out[0], buf, sizeof(buf))) != 0) {
    if (n > 0) {
      message.append(buf, n);
    } else if (errno != EINTR) {
      break;
    }
  }
  close(out[0]);
  if (!message.empty()) {
    output_user("%s", message.c_str());
  }
  int st = 0;
  if (waitpid(p, &st, 0) < 0 || !WIFEXITED(st)) {
    output_internal("overlay %s did not complete for session %s\n", op,
                    sid.c_str());
    return -1;
  }
  if (WEXITSTATUS(st) != 0) {
    output_internal("overlay %s failed for session %s\n", op, sid.c_str());
  }
  return WEXITSTATUS(st);
}

bool
UnionfsCstore::do_mount(const FsPath& rwdir, const FsPath& rdir,
                        const FsPath& mdir)
{
  (void) rwdir; (void) rdir; (void) mdir;
  return run_ovl_helper("mount", session_id);
}

/* once unmounted and not remounted, the working root is a bare empty
 * directory that still counts as a session. a later commit would read it as
 * an empty config and delete everything, so remove it to end the session.
 */
bool
UnionfsCstore::invalidate_session(const char *after)
{
  /* a session without its scratch directory is no session (inSession()),
   * whether it could be unmounted or not: one that cannot is stacked with
   * an upper layer that must never be used again. removing the working root
   * through a live mount would delete every node in it, so it only goes once
   * unmounted, with the upper layer. */
  if (session_mounted()) {
    run_ovl_helper("umount", session_id);
  }
  if (!session_mounted()) {
    run_ovl_helper("clear", session_id);
    try {
      fs::remove_all(work_root.path_cstr());
      fs::remove_all(change_root.path_cstr());
    } catch (...) {
      output_internal("failed to remove session [%s]\n",
                      work_root.path_cstr());
    }
  } else {
    output_internal("cannot unmount [%s]\n", work_root.path_cstr());
  }
  try {
    fs::remove_all((C_DEF_TMP_PREFIX + session_id).c_str());
  } catch (...) {
    output_internal("failed to invalidate session [%s]\n",
                    work_root.path_cstr());
  }
  output_user("config session lost after %s, "
              "please leave and re-enter configuration mode\n", after);
  session_lost = true;
  return false;
}

bool
UnionfsCstore::do_umount(const FsPath& mdir)
{
  string sid;
  if (!session_id_from_work_root(mdir, sid)) {
    output_internal("not a session working root [%s]\n", mdir.path_cstr());
    return false;
  }
  return run_ovl_helper("umount", sid);
}

/* overlayfs, unlike unionfs-fuse, enforces the real DAC of the lower layer:
 * a copy-up is refused outright if the caller may not write the underlying
 * file. the active config must therefore stay group-writable by vyattacfg.
 *
 * recursive_copy_dir() preserves the source mode, so a single stray 0644 file
 * anywhere in the pipeline would become permanently unwritable for a
 * non-root session. normalize the whole tree instead of trusting umask.
 */
bool
UnionfsCstore::normalize_active_perms(const FsPath& root)
{
  struct group *grp = getgrnam("vyattacfg");
  gid_t gid = (grp ? grp->gr_gid : (gid_t) -1);

  try {
    vector<string> paths;
    paths.push_back(root.path_cstr());
    fs::recursive_directory_iterator di(root.path_cstr());
    for (; di != fs::recursive_directory_iterator(); ++di) {
      paths.push_back(di->path().string());
    }
    for (size_t i = 0; i < paths.size(); i++) {
      const char *cp = paths[i].c_str();
      bool is_dir = path_is_directory(cp);
      mode_t mode = (is_dir ? (S_ISGID | 02775) : 0664);
      bool group_set = (gid == (gid_t) -1
                        || chown(cp, (uid_t) -1, gid) == 0);
      bool mode_set = (chmod(cp, mode) == 0);
      if (!group_set || !mode_set) {
        // we may not own every node; fine if it already is as required
        int err = errno;
        struct stat st;
        if (stat(cp, &st) != 0 || (gid != (gid_t) -1 && st.st_gid != gid)
            || (st.st_mode & 07777) != mode) {
          output_internal("cannot make [%s] writable by the configuration "
                          "group [%s]\n", cp, strerror(err));
          return false;
        }
      }
    }
  } catch (const fs::filesystem_error& e) {
    output_internal("normalize_active_perms failed [%s]\n", e.what());
    return false;
  } catch (...) {
    return false;
  }
  return true;
}

/* query the status of a path without throwing. returns false if the status
 * could not be determined at all; a path that simply does not exist is not
 * an error and yields a "not found" status.
 */
static bool
get_file_status(const char *path, fs::file_status& fstat)
{
  std::error_code ec;
  fstat = fs::status(path, ec);
  return (!ec);
}

bool
UnionfsCstore::path_exists(const char *path)
{
  fs::file_status result;
  if (!get_file_status(path, result)) {
    return false;
  }
  return fs::exists(result);
}

bool
UnionfsCstore::path_is_directory(const char *path)
{
  fs::file_status result;
  if (!get_file_status(path, result)) {
    return false;
  }
  return fs::is_directory(result);
}

bool
UnionfsCstore::path_is_regular(const char *path)
{
  fs::file_status result;
  if (!get_file_status(path, result)) {
    return false;
  }
  return fs::is_regular_file(result);
}

bool
UnionfsCstore::remove_dir_content(const char *path)
{
  if (!path_is_directory(path)) {
    return false;
  }

  fs::directory_iterator di(path);
  for (; di != fs::directory_iterator(); ++di) {
    if (fs::remove_all(di->path()) < 1) {
      return false;
    }
  }
  return true;
}

} // end namespace unionfs
} // end namespace cstore


