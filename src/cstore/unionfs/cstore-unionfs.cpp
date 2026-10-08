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

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <wait.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <grp.h>

#include <cli_cstore.h>
#include <cstore/unionfs/cstore-unionfs.hpp>
#include <cnode/cnode.hpp>
#include <commit/commit-algorithm.hpp>

namespace cstore { // begin namespace cstore
namespace unionfs { // begin namespace unionfs

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

// Fall-through for Boost's filesystem::copy_file "complexity"
void stream_file( const char* srce_file, const char* dest_file )
{
    std::ifstream srce( srce_file, std::ios::binary ) ;
    std::ofstream dest( dest_file, std::ios::binary ) ;
    dest << srce.rdbuf() ;
}

vector<int> getActiveCommits()
{
  string process_name = "vbash";
  vector<int> pids;

  DIR *dp = opendir("/proc");
  if (dp != NULL) {
    struct dirent *dirp;
    while ((dirp = readdir(dp))) {
      int pid = atoi(dirp->d_name);
      if (pid > 0) {
        string command_path = string("/proc/") + dirp->d_name + "/cmdline";
        std::ifstream command_file(command_path.c_str());
        string command_line;
        getline(command_file, command_line);
        if (!command_line.empty()) {
          size_t pos = command_line.find('\0');
          if (pos != string::npos) {
            command_line = command_line.substr(0, pos);
          }
          pos = command_line.rfind('/');
          if (pos != string::npos) {
            command_line = command_line.substr(pos + 1);
          }
          if (process_name == command_line) {
            pids.push_back(pid);
          }
        }
      }
    }
  }

  closedir(dp);

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
    if (getenv(C_ENV_TMP_ROOT.c_str())) {
      tmp_root = (C_DEF_TMP_PREFIX + sid);
      init_commit_data();
    }
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
}

////// public virtual functions declared in base class
bool
UnionfsCstore::markSessionUnsaved()
{
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
    b_fs::remove(marker.path_cstr());
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
    b_fs::directory_iterator di(work_base.path_cstr());
    for (; di != b_fs::directory_iterator(); ++di) {
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

  if (!path_exists(work_root)) {
    // session doesn't exist. create dirs.
    try {
      b_fs::create_directories(work_root.path_cstr());
      b_fs::create_directories(change_root.path_cstr());
      b_fs::create_directories(tmp_root.path_cstr());
      if (!path_exists(active_root)) {
        // this should only be needed on boot
        b_fs::create_directories(active_root.path_cstr());
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

    // overlay mount
    if (!do_mount(change_root, active_root, work_root)) {
      return false;
    }
  } else if (!path_is_directory(work_root)) {
    output_internal("setup session not dir [%s]\n", work_root.path_cstr());
    return false;
  }

  pids = getActiveCommits();

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
          old_pids.push_back(current_pid);
          output_internal("found inactive config [%d]\n", current_pid);
          output_internal("umount [%s]\n", directories[i].path_cstr());
          if (!do_umount(directories[i])) {
            failed = true;
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
        try {
          if (b_fs::remove_all(directories[i].path_cstr()) == 0) {
            failed = true;
          }
        } catch (...) {
          failed = true;
        }
      }
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

  // unmount the work root (union)
  if (!do_umount(work_root)) {
    return false;
  }

  // remove session directories
  bool ret = false;
  try {
    if (b_fs::remove_all(work_root.path_cstr()) != 0
        && b_fs::remove_all(change_root.path_cstr()) != 0
        && b_fs::remove_all(tmp_root.path_cstr()) != 0) {
      ret = true;
    }
  } catch (...) {
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
          && path_exists(work_root) && path_is_directory(work_root));
}

bool
UnionfsCstore::clearCommittedMarkers()
{
  try {
    b_fs::remove(commit_marker_file.path_cstr());
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
    if (b_fs::remove_all(tap.path_cstr()) < 1) {
      output_internal("rm ta failed\n");
      return false;
    }
    cnode::CfgNode *c = node.getCfgNode();
    if (c && c->isTag()) {
      FsPath p(tap);
      p.pop();
      if (is_directory_empty(p)) {
        output_internal("rm[%s]\n", p.path_cstr());
        if (b_fs::remove_all(p.path_cstr()) < 1) {
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
      } catch (const b_fs::filesystem_error& e) {
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
      } catch (const b_fs::filesystem_error& e) {
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
      if (b_fs::remove_all(d.path_cstr()) < 1) {
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
            b_fs::copy_file(s.path_cstr(), d.path_cstr());
          } catch (const boost::filesystem::filesystem_error& e) {
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
  // make a copy of current "work" dir
  try {
    if (path_exists(tmp_work_root)) {
      output_internal("rm[%s]\n", tmp_work_root.path_cstr());
      if (b_fs::remove_all(tmp_work_root.path_cstr()) < 1) {
        output_internal("rm tw failed\n");
        return false;
      }
    }
    output_internal("cp[%s]->[%s]\n", work_root.path_cstr(),
                    tmp_work_root.path_cstr());

    recursive_copy_dir(work_root, tmp_work_root, true);
  } catch (const b_fs::filesystem_error& e) {
    output_internal("cp w->tw failed[%s]\n", e.what());
    return false;
  } catch (...) {
    output_internal("cp w->tw failed[unknown exception]\n");
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
  } catch (const b_fs::filesystem_error& e) {
    output_internal("cp ta->na failed[%s]\n", e.what());
  } catch (...) {
    output_internal("cp ta->na failed[unknown exception]\n");
  }
  if (staged && !normalize_active_perms(new_active)) {
    output_internal("failed to normalize permissions on [%s]\n",
                    new_active.path_cstr());
    staged = false;
  }

  if (staged
      && syscall(SYS_renameat2, AT_FDCWD, new_active.path_cstr(),
                 AT_FDCWD, active_root.path_cstr(), RENAME_EXCHANGE) != 0) {
    output_internal("failed to swap in new active config [%s][%s]\n",
                    strerror(errno), new_active.path_cstr());
    staged = false;
  }
  if (!staged) {
    // never swapped in, so nothing can be stacked on it
    try {
      b_fs::remove_all(new_active.path_cstr());
    } catch (...) {
    }
    return false;
  }
  /* new_active now refers to the *previous* active tree. other sessions are
   * still stacked on it, so every return below leaves it in place unless
   * they have all been re-stacked.
   */

  if (!do_umount(work_root)) {
    return false;
  }
  bool cleared = false;
  try {
    cleared = (b_fs::remove_all(change_root.path_cstr()) >= 1);
  } catch (...) {
  }
  if (!cleared) {
    output_internal("failed to remove [%s]\n", change_root.path_cstr());
    return invalidate_session("commit");
  }
  try {
    b_fs::create_directories(change_root.path_cstr());
  } catch (...) {
    output_internal("failed to create [%s]\n", change_root.path_cstr());
    return invalidate_session("commit");
  }
  if (!do_mount(change_root, active_root, work_root)) {
    return invalidate_session("commit");
  }

  /* point every other live session at the new active tree. only once that
   * has succeeded is the old tree unreferenced and safe to remove.
   */
  bool restacked = restack_other_sessions(new_active);
  if (!sync_dir(tmp_work_root, work_root, work_root)) {
    return false;
  }
  if (b_fs::remove_all(tmp_work_root.path_cstr()) < 1
      || b_fs::remove_all(tmp_active_root.path_cstr()) < 1) {
    output_user("failed to remove temp directories\n");
    return false;
  }
  if (restacked) {
    try {
      b_fs::remove_all(new_active.path_cstr());
    } catch (...) {
      output_internal("failed to remove previous active config [%s]\n",
                      new_active.path_cstr());
    }
  } else {
    /* a session could not be re-stacked and may still be using the old tree.
     * leave it alone: the config root is a tmpfs and is reclaimed on reboot,
     * whereas pulling it out from under a live overlay is not recoverable.
     */
    output_internal("keeping previous active config [%s], "
                    "not all sessions could be re-stacked\n",
                    new_active.path_cstr());
  }
  // all done
  return true;
}

bool
UnionfsCstore::getCommitLock()
{
  int fd;

  fd = open(C_COMMIT_LOCK_FILE.c_str(),
	    O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC, 0666);
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
    if (!b_fs::create_directory(get_work_path().path_cstr())) {
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
    if (b_fs::remove_all(get_work_path().path_cstr()) != 0) {
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
    /* somehow b_fs::rename() can't be used here as it considers the operation
     * "Invalid cross-device link" and fails with an exception, probably due
     * to unionfs in some way.
     * do it the hard way.
     */
    recursive_copy_dir(opath, npath);
    if (b_fs::remove_all(opath.path_cstr()) == 0) {
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
    b_fs::remove(marker.path_cstr());
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
    b_fs::remove(marker.path_cstr());
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
      vector<b_fs::path> markers;
      b_fs::recursive_directory_iterator di(get_work_path().path_cstr());
      for (; di != b_fs::recursive_directory_iterator(); ++di) {
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
        b_fs::remove(markers[i]);
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
    vector<b_fs::path> markers;
    b_fs::recursive_directory_iterator di(get_work_path().path_cstr());
    for (; di != b_fs::recursive_directory_iterator(); ++di) {
      if (!path_is_regular(di->path().string().c_str())
          || di->path().filename() != C_MARKER_CHANGED) {
        // not marker
        continue;
      }
      markers.push_back(di->path());
    }
    for (size_t i = 0; i < markers.size(); i++) {
      b_fs::remove(markers[i]);
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
    b_fs::remove(cfile.path_cstr());
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
  if (!do_umount(work_root)) {
    return false;
  }

  vector<b_fs::path> files;
  vector<b_fs::path> directories;
  try {
    // iterate through all entries in change root
    b_fs::directory_iterator di(change_root.path_cstr());
    for (; di != b_fs::directory_iterator(); ++di) {
      if (path_is_directory(di->path().string().c_str())) {
        directories.push_back(di->path());
      } else {
        files.push_back(di->path());
      }
    }

    // remove and count
    num_removed = 0;
    for (size_t i = 0; i < files.size(); i++) {
      b_fs::remove(files[i]);
      num_removed++;
    }
    for (size_t i = 0; i < directories.size(); i++) {
      num_removed += b_fs::remove_all(directories[i]);
    }
  } catch (...) {
    output_internal("discard failed [%s]\n", change_root.path_cstr());
    ret = false;
  }

  if (!do_mount(change_root, active_root, work_root)) {
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
    b_fs::directory_iterator di(root.path_cstr());
    for (; di != b_fs::directory_iterator(); ++di) {
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
    b_fs::create_directories(ppath.path_cstr());

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
    if (b_fs::file_size(fpath.path_cstr()) > C_UNIONFS_MAX_FILE_SIZE) {
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
 * will throw exception (from b_fs) if fail.
 */
void
UnionfsCstore::recursive_copy_dir(const FsPath& src, const FsPath& dst,
                                  bool filter_dot_entries)
{
  string src_str = src.path_cstr();
  string dst_str = dst.path_cstr();
  b_fs::create_directories(dst.path_cstr());

  b_fs::recursive_directory_iterator di(src_str);
  for (; di != b_fs::recursive_directory_iterator(); ++di) {
    /* hold the string: path::string() returns by value under
     * std::filesystem, so c_str() of the temporary would dangle.
     */
    const string oname = di->path().string();
    string nname = oname;
    nname.replace(0, src_str.length(), dst_str);
    if (path_is_directory(oname.c_str())) {
      b_fs::create_directory(nname);
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
        b_fs::copy_file(di->path(), nname);
      } catch (const b_fs::filesystem_error& e) {
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

/* the overlayfs workdir must exist and be empty. the kernel maintains a
 * root-owned "work" subdirectory inside it, so always start from scratch
 * rather than inheriting whatever a crashed session left behind.
 */
bool
UnionfsCstore::prepare_ovl_workdir(const FsPath& wbase, FsPath& wdir)
{
  /* Allocate a fresh generation directory under the session's workdir base
   * rather than reusing one path.
   *
   * Re-stacking a session detaches its overlay with MNT_DETACH, which keeps
   * the old superblock alive until the last reference to it goes away. That
   * superblock still owns its workdir, so wiping and recreating the same
   * path underneath it would be a modification of a live overlay layer -
   * exactly what we restructured commitConfig() to avoid. Leave the previous
   * generation alone; it is disposed of together with the whole base
   * directory when the session is torn down, and the config root is a tmpfs
   * that is recreated on boot in any case.
   */
  try {
    b_fs::create_directories(wbase.path_cstr());
    for (unsigned int gen = 0; gen < C_OVLWORK_MAX_GEN; gen++) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%u", gen);
      FsPath cand(wbase);
      cand.push(buf);
      if (path_exists(cand)) {
        continue;
      }
      b_fs::create_directories(cand.path_cstr());
      wdir = cand;
      return true;
    }
  } catch (const b_fs::filesystem_error& e) {
    output_internal("failed to prepare overlay workdir [%s][%s]\n",
                    wbase.path_cstr(), e.what());
    return false;
  } catch (...) {
    output_internal("failed to prepare overlay workdir [%s]\n",
                    wbase.path_cstr());
    return false;
  }
  output_internal("exhausted overlay workdir generations [%s]\n",
                  wbase.path_cstr());
  return false;
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

/* the single place an overlay is mounted:
 *   lowerdir  = active config (shared, read-only)
 *   upperdir  = this session's changes
 *   workdir   = per-session kernel scratch
 *   mountpoint= the working config
 *
 * the feature flags are pinned explicitly rather than inherited from the
 * kernel defaults. in particular metacopy and redirect_dir must stay off:
 * enabling either forfeits the documented allowance for offline changes to
 * the lower tree, which replacing the active config relies on.
 */
bool
UnionfsCstore::ovl_mount(const FsPath& lower, const FsPath& upper,
                         const FsPath& work, const FsPath& mdir)
{
  string mopts = "lowerdir=";
  mopts += lower.path_cstr();
  mopts += ",upperdir=";
  mopts += upper.path_cstr();
  mopts += ",workdir=";
  mopts += work.path_cstr();
  mopts += ",index=off,metacopy=off,redirect_dir=off,xino=off";

  if (mount("overlay", mdir.path_cstr(), "overlay", 0, mopts.c_str()) != 0) {
    if (errno == ENODEV) {
      output_internal("overlay mount failed: overlay filesystem not "
                      "available (is the overlay module loaded?)\n");
    }
    output_internal("overlay mount failed [%s][%s][%s]\n",
                    strerror(errno), mdir.path_cstr(), mopts.c_str());
    return false;
  }
  return true;
}

bool
UnionfsCstore::do_mount(const FsPath& rwdir, const FsPath& rdir,
                        const FsPath& mdir)
{
  if (session_id.empty()) {
    /* constructed from an environment whose working root did not carry a
     * usable session ID, so there is nowhere to put the workdir.
     */
    output_internal("no overlay workdir for [%s]\n", mdir.path_cstr());
    return false;
  }
  FsPath wgen;
  if (!prepare_ovl_workdir(ovl_work_root, wgen)) {
    return false;
  }
  return ovl_mount(rdir, rwdir, wgen, mdir);
}

/* once unmounted and not remounted, the working root is a bare empty
 * directory that still counts as a session. a later commit would read it as
 * an empty config and delete everything, so remove it to end the session.
 */
bool
UnionfsCstore::invalidate_session(const char *after)
{
  try {
    b_fs::remove_all(work_root.path_cstr());
  } catch (...) {
    output_internal("failed to invalidate session [%s]\n",
                    work_root.path_cstr());
  }
  output_user("config session lost after %s, "
              "please leave and re-enter configuration mode\n", after);
  return false;
}

bool
UnionfsCstore::do_umount(const FsPath& mdir)
{
  string sid;
  FsPath wdir;
  bool have_wdir = session_id_from_work_root(mdir, sid);
  if (have_wdir) {
    wdir = ovl_work_root_for(sid);
  }

  bool detached = false;
  if (umount2(mdir.path_cstr(), 0) != 0) {
    /* a session with a command in flight can keep the mount busy. a lazy
     * unmount always frees the mount point for a fresh mount, and anything
     * still holding an open file descriptor keeps working against the old
     * superblock until it closes.
     */
    if (errno != EBUSY || umount2(mdir.path_cstr(), MNT_DETACH) != 0) {
      output_internal("overlay umount failed [%s][%s]\n",
                      strerror(errno), mdir.path_cstr());
      return false;
    }
    detached = true;
  }

  /* only safe once the mount is really gone: a lazily detached one still
   * owns its work directory until the last reference to it drops. what is
   * left behind goes when the config tmpfs is recreated on boot.
   */
  if (have_wdir && !detached) {
    try {
      if (path_exists(wdir)) {
        b_fs::remove_all(wdir.path_cstr());
      }
    } catch (...) {
      // not fatal: the config root is a tmpfs and is recreated on boot
      output_internal("failed to remove overlay workdir [%s]\n",
                      wdir.path_cstr());
    }
  }
  return true;
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
    b_fs::recursive_directory_iterator di(root.path_cstr());
    for (; di != b_fs::recursive_directory_iterator(); ++di) {
      paths.push_back(di->path().string());
    }
    for (size_t i = 0; i < paths.size(); i++) {
      const char *cp = paths[i].c_str();
      bool is_dir = path_is_directory(cp);
      // ignore failures: we may not own every node, and the mode may already
      // be correct, in which case there is nothing to fix up.
      if (gid != (gid_t) -1) {
        if (chown(cp, (uid_t) -1, gid) != 0 && errno != EPERM) {
          output_internal("chgrp failed [%s][%s]\n", strerror(errno), cp);
        }
      }
      mode_t mode = (is_dir ? (S_ISGID | 02775) : 0664);
      if (chmod(cp, mode) != 0 && errno != EPERM) {
        output_internal("chmod failed [%s][%s]\n", strerror(errno), cp);
      }
    }
  } catch (const b_fs::filesystem_error& e) {
    output_internal("normalize_active_perms failed [%s]\n", e.what());
    return false;
  } catch (...) {
    return false;
  }
  return true;
}

/* after the active config has been replaced, every OTHER live session is
 * still stacked on the previous lower tree. re-stack them so that their view
 * of the active config matches reality, which is the behaviour users expect:
 * once somebody commits, "compare" in a concurrent session reflects it.
 */
bool
UnionfsCstore::restack_other_sessions(const FsPath& prev_active)
{
  bool ret = true;
  string work_base_str = C_DEF_WORK_PREFIX;
  work_base_str.erase(work_base_str.find_last_of("/"));

  vector<b_fs::path> sessions;
  try {
    b_fs::directory_iterator di(work_base_str.c_str());
    for (; di != b_fs::directory_iterator(); ++di) {
      sessions.push_back(di->path());
    }
  } catch (...) {
    // nothing to re-stack
    return true;
  }

  for (size_t i = 0; i < sessions.size(); i++) {
    FsPath sdir(sessions[i].string().c_str());
    string sid;
    if (!session_id_from_work_root(sdir, sid) || sid == session_id) {
      continue;
    }
    if (!is_mount_point(sdir)) {
      continue;
    }
    FsPath schange((C_DEF_CHANGE_PREFIX + sid).c_str());
    FsPath swork(ovl_work_root_for(sid));
    if (!path_is_directory(schange)) {
      continue;
    }

    // prepare first, so a failure leaves the session mounted as it was
    FsPath sgen;
    if (!prepare_ovl_workdir(swork, sgen)) {
      ret = false;
      continue;
    }
    // as in do_umount(): detach lazily only if the session is busy
    bool detached = false;
    if (umount2(sdir.path_cstr(), 0) != 0) {
      if (errno != EBUSY || umount2(sdir.path_cstr(), MNT_DETACH) != 0) {
        output_internal("failed to detach session overlay [%s][%s]\n",
                        strerror(errno), sdir.path_cstr());
        ret = false;
        continue;
      }
      detached = true;
    }
    if (ovl_mount(active_root, schange, sgen, sdir)) {
      if (!detached) {
        /* drop every older generation, as do_umount() does after a normal
         * unmount. after a lazy detach they are kept: the detached
         * superblock owns its workdir until the last reference drops.
         */
        try {
          vector<b_fs::path> old_gens;
          b_fs::directory_iterator di(swork.path_cstr());
          for (; di != b_fs::directory_iterator(); ++di) {
            if (di->path().string() != sgen.path_cstr()) {
              old_gens.push_back(di->path());
            }
          }
          for (size_t j = 0; j < old_gens.size(); j++) {
            b_fs::remove_all(old_gens[j]);
          }
        } catch (...) {
          // not fatal: the config root is a tmpfs and is recreated on boot
          output_internal("failed to remove old overlay workdirs [%s]\n",
                          swork.path_cstr());
        }
      }
    } else {
      output_internal("failed to re-stack session [%s]\n", sdir.path_cstr());
      ret = false;
      /* put the session back on the tree it was using rather than leave it
       * unmounted. the caller keeps that tree since we report failure.
       */
      FsPath pgen;
      if (!prepare_ovl_workdir(swork, pgen)
          || !ovl_mount(prev_active, schange, pgen, sdir)) {
        output_internal("session left unmounted [%s]\n", sdir.path_cstr());
      }
    }
  }
  return ret;
}

bool
UnionfsCstore::path_exists(const char *path)
{
  b_fs::file_status result;
  if (!b_fs_get_file_status(path, result)) {
    return false;
  }
  return b_fs::exists(result);
}

bool
UnionfsCstore::path_is_directory(const char *path)
{
  b_fs::file_status result;
  if (!b_fs_get_file_status(path, result)) {
    return false;
  }
  return b_fs::is_directory(result);
}

bool
UnionfsCstore::path_is_regular(const char *path)
{
  b_fs::file_status result;
  if (!b_fs_get_file_status(path, result)) {
    return false;
  }
  return b_fs::is_regular_file(result);
}

bool
UnionfsCstore::remove_dir_content(const char *path)
{
  if (!path_is_directory(path)) {
    return false;
  }

  b_fs::directory_iterator di(path);
  for (; di != b_fs::directory_iterator(); ++di) {
    if (b_fs::remove_all(di->path()) < 1) {
      return false;
    }
  }
  return true;
}

} // end namespace unionfs
} // end namespace cstore


