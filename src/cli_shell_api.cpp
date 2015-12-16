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
#include <vector>
#include <string>
#include <getopt.h>

#include <cli_cstore.h>
#include <cstore/cstore.hpp>
#include <cstore/util.hpp>
#include <cnode/cnode.hpp>
#include <cnode/cnode-algorithm.hpp>
#include <commit/commit-algorithm.hpp>
#include <cparse/cparse.hpp>

using namespace cstore;

/* This program provides an API for shell scripts (e.g., snippets in
 * templates, standalone scripts, etc.) to access the CLI cstore library.
 * It is installed in "/opt/vyatta/sbin", but a symlink "/bin/cli-shell-api"
 * is installed by the package so that it can be invoked simply as
 * "cli-shell-api" (as long as "/bin" is in "PATH").
 *
 * The API functions communicate with the caller using a combination of
 * output and exit status. For example, a "boolean" function "returns true"
 * by exiting with status 0, and non-zero exit status means false. The
 * functions are documented below when necessary.
 */

/* util function: prints a vector with the specified "separator" and "quote"
 * characters.
 */
static void
print_vec(const vector<string>& vec, const char *sep, const char *quote)
{
  for (unsigned int i = 0; i < vec.size(); i++) {
    printf("%s%s%s%s", ((i > 0) ? sep : ""), quote, vec[i].c_str(), quote);
  }
}

//// options
// showCfg options
int op_show_active_only = 0;
int op_show_show_defaults = 0;
int op_show_hide_secrets = 0;
int op_show_working_only = 0;
int op_show_context_diff = 0;
int op_show_commands = 0;
int op_show_ignore_edit = 0;
char *op_show_cfg1 = NULL;
char *op_show_cfg2 = NULL;

typedef void (*OpFuncT)(Cstore& cstore, const Cpath& args);

typedef struct {
  const char *op_name;
  const int op_exact_args;
  const char *op_exact_error;
  const int op_min_args;
  const char *op_min_error;
  bool op_use_edit;
  OpFuncT op_func;
} OpT;

/* outputs an environment string to be "eval"ed */
static void
getSessionEnv(Cstore& cstore, const Cpath& args)
{
  // need a "session-specific" cstore so ignore the default one
  string env;
  Cstore *cs = Cstore::createCstore(args[0], env);
  printf("%s", env.c_str());
  delete cs;
}

/* outputs an environment string to be "eval"ed */
static void
getEditEnv(Cstore& cstore, const Cpath& args)
{
  string env;
  if (!cstore.getEditEnv(args, env)) {
    exit(1);
  }
  printf("%s", env.c_str());
}

/* outputs an environment string to be "eval"ed */
static void
getEditUpEnv(Cstore& cstore, const Cpath& args)
{
  string env;
  if (!cstore.getEditUpEnv(env)) {
    exit(1);
  }
  printf("%s", env.c_str());
}

/* outputs an environment string to be "eval"ed */
static void
getEditResetEnv(Cstore& cstore, const Cpath& args)
{
  string env;
  if (!cstore.getEditResetEnv(env)) {
    exit(1);
  }
  printf("%s", env.c_str());
}

static void
editLevelAtRoot(Cstore& cstore, const Cpath& args)
{
  exit(cstore.editLevelAtRoot() ? 0 : 1);
}

/* outputs an environment string to be "eval"ed */
static void
getCompletionEnv(Cstore& cstore, const Cpath& args)
{
  string env;
  if (!cstore.getCompletionEnv(args, env)) {
    exit(1);
  }
  printf("%s", env.c_str());
}

/* outputs a string */
static void
getEditLevelStr(Cstore& cstore, const Cpath& args)
{
  Cpath lvec;
  cstore.getEditLevel(lvec);
  vector<string> vec;
  for (size_t i = 0; i < lvec.size(); i++) {
    vec.push_back(lvec[i]);
  }
  print_vec(vec, " ", "");
}

static void
markSessionUnsaved(Cstore& cstore, const Cpath& args)
{
  if (!cstore.markSessionUnsaved()) {
    exit(1);
  }
}

static void
unmarkSessionUnsaved(Cstore& cstore, const Cpath& args)
{
  if (!cstore.unmarkSessionUnsaved()) {
    exit(1);
  }
}

static void
sessionUnsaved(Cstore& cstore, const Cpath& args)
{
  if (!cstore.sessionUnsaved()) {
    exit(1);
  }
}

static void
sessionChanged(Cstore& cstore, const Cpath& args)
{
  if (!cstore.sessionChanged()) {
    exit(1);
  }
}

static void
teardownSession(Cstore& cstore, const Cpath& args)
{
  if (!cstore.teardownSession()) {
    exit(1);
  }
}

static void
setupSession(Cstore& cstore, const Cpath& args)
{
  if (!cstore.setupSession()) {
    exit(1);
  }
}

static void
inSession(Cstore& cstore, const Cpath& args)
{
  if (!cstore.inSession()) {
    exit(1);
  }
}

/* same as exists() in Perl API */
static void
exists(Cstore& cstore, const Cpath& args)
{
  exit(cstore.cfgPathExists(args, false) ? 0 : 1);
}

/* same as existsOrig() in Perl API */
static void
existsActive(Cstore& cstore, const Cpath& args)
{
  exit(cstore.cfgPathExists(args, true) ? 0 : 1);
}

/* same as isEffective() in Perl API */
static void
existsEffective(Cstore& cstore, const Cpath& args)
{
  exit(cstore.cfgPathEffective(args) ? 0 : 1);
}

/* isMulti */
static void
isMulti(Cstore& cstore, const Cpath& args)
{
  MapT<string, string> tmap;
  cstore.getParsedTmpl(args, tmap, 0);
  string multi = tmap["multi"];
  exit((multi == "1") ? 0 : 1);
}

/* isTag */
static void
isTag(Cstore& cstore, const Cpath& args)
{
  MapT<string, string> tmap;
  cstore.getParsedTmpl(args, tmap, 0);
  string tag = tmap["tag"];
  exit((tag == "1") ? 0 : 1);
}

/* isValue */
static void
isValue(Cstore& cstore, const Cpath& args)
{
  MapT<string, string> tmap;
  cstore.getParsedTmpl(args, tmap, 0);
  string is_value = tmap["is_value"];
  exit((is_value == "1") ? 0 : 1);
}

/* isLeaf */
static void
isLeaf(Cstore& cstore, const Cpath& args)
{
  MapT<string, string> tmap;
  bool is_leaf_typeless = false;
  tmap["type"] = "";
  cstore.getParsedTmpl(args, tmap, 0);
  string is_value = tmap["is_value"];
  string tag = tmap["tag"];
  string type = tmap["type"];
  vector<string> tcnodes;
  cstore.tmplGetChildNodes(args, tcnodes);
  if (tcnodes.size() == 0) {
    // typeless leaf node
    is_leaf_typeless = true;
  }
  exit(((is_value != "1") && (tag != "1") && (type != "" || is_leaf_typeless)) ? 0 : 1);
}

static void getNodeType(Cstore& cstore, const Cpath& args) {
  MapT<string, string> tmap;
  bool is_leaf_typeless = false;
  tmap["type"] = "";
  cstore.getParsedTmpl(args, tmap, 0);
  string is_value = tmap["is_value"];
  string tag = tmap["tag"];
  string type = tmap["type"];
  string multi = tmap["multi"];
  vector<string> tcnodes;
  cstore.tmplGetChildNodes(args, tcnodes);
  if (tcnodes.size() == 0) {
    // typeless leaf node
    is_leaf_typeless = true;
  }
  if (tag == "1") {
    printf("tag");
  } else if (!((is_value != "1") && (tag != "1") && (type != "" || is_leaf_typeless))) {
    printf("non-leaf");
  } else if (multi == "1") {
    printf("multi");
  } else {
    printf("leaf");
  }
  exit(0);
  
}

/* same as listNodes() in Perl API.
 *
 * outputs a string representing multiple nodes. this string MUST be
 * "eval"ed into an array of nodes. e.g.,
 *
 *   values=$(cli-shell-api listNodes interfaces)
 *   eval "nodes=($values)"
 *
 * or a single step:
 *
 *   eval "nodes=($(cli-shell-api listNodes interfaces))"
 */
static void
listNodes(Cstore& cstore, const Cpath& args)
{
  vector<string> cnodes;
  cstore.cfgPathGetChildNodes(args, cnodes, false);
  print_vec(cnodes, " ", "'");
}

/* same as listOrigNodes() in Perl API.
 *
 * outputs a string representing multiple nodes. this string MUST be
 * "eval"ed into an array of nodes. see listNodes above.
 */
static void
listActiveNodes(Cstore& cstore, const Cpath& args)
{
  vector<string> cnodes;
  cstore.cfgPathGetChildNodes(args, cnodes, true);
  print_vec(cnodes, " ", "'");
}

/* same as listEffectiveNodes() in Perl API.
 *
 * outputs a string representing multiple nodes. this string MUST be
 * "eval"ed into an array of nodes. see listNodes above.
 */
static void
listEffectiveNodes(Cstore& cstore, const Cpath& args)
{
  vector<string> cnodes;
  cstore.cfgPathGetEffectiveChildNodes(args, cnodes);
  print_vec(cnodes, " ", "'");
}

/* same as returnValue() in Perl API. outputs a string. */
static void
returnValue(Cstore& cstore, const Cpath& args)
{
  string val;
  if (!cstore.cfgPathGetValue(args, val, false)) {
    exit(1);
  }
  printf("%s", val.c_str());
}

/* same as returnOrigValue() in Perl API. outputs a string. */
static void
returnActiveValue(Cstore& cstore, const Cpath& args)
{
  string val;
  if (!cstore.cfgPathGetValue(args, val, true)) {
    exit(1);
  }
  printf("%s", val.c_str());
}

/* same as returnEffectiveValue() in Perl API. outputs a string. */
static void
returnEffectiveValue(Cstore& cstore, const Cpath& args)
{
  string val;
  if (!cstore.cfgPathGetEffectiveValue(args, val)) {
    exit(1);
  }
  printf("%s", val.c_str());
}

/* same as returnValues() in Perl API.
 *
 * outputs a string representing multiple values. this string MUST be
 * "eval"ed into an array of values. see listNodes above.
 *
 * note that success/failure can be checked using the two-step invocation
 * above. e.g.,
 *
 *   if valstr=$(cli-shell-api returnValues system ntp-server); then
 *     # got the values
 *     eval "values=($valstr)"
 *     ...
 *   else
 *     # failed
 *     ...
 *   fi
 *
 * in most cases, the one-step invocation should be sufficient since a
 * failure would result in an empty array after the eval.
 */
static void
returnValues(Cstore& cstore, const Cpath& args)
{
  vector<string> vvec;
  if (!cstore.cfgPathGetValues(args, vvec, false)) {
    exit(1);
  }
  print_vec(vvec, " ", "'");
}

/* same as returnOrigValues() in Perl API.
 *
 * outputs a string representing multiple values. this string MUST be
 * "eval"ed into an array of values. see returnValues above.
 */
static void
returnActiveValues(Cstore& cstore, const Cpath& args)
{
  vector<string> vvec;
  if (!cstore.cfgPathGetValues(args, vvec, true)) {
    exit(1);
  }
  print_vec(vvec, " ", "'");
}

/* same as returnEffectiveValues() in Perl API.
 *
 * outputs a string representing multiple values. this string MUST be
 * "eval"ed into an array of values. see returnValues above.
 */
static void
returnEffectiveValues(Cstore& cstore, const Cpath& args)
{
  vector<string> vvec;
  if (!cstore.cfgPathGetEffectiveValues(args, vvec)) {
    exit(1);
  }
  print_vec(vvec, " ", "'");
}

/* checks if specified path is a valid "template path" *without* checking
 * the validity of any "tag values" along the path.
 */
static void
validateTmplPath(Cstore& cstore, const Cpath& args)
{
  exit(cstore.validateTmplPath(args, false) ? 0 : 1);
}

/* checks if specified path is a valid "template path", *including* the
 * validity of any "tag values" along the path.
 */
static void
validateTmplValPath(Cstore& cstore, const Cpath& args)
{
  exit(cstore.validateTmplPath(args, true) ? 0 : 1);
}

static void
showCfg(Cstore& cstore, const Cpath& args)
{
  Cpath nargs(args);
  bool active_only = (!cstore.inSession() || op_show_active_only);
  bool working_only = (cstore.inSession() && op_show_working_only);
  cnode::CfgNode aroot(cstore, nargs, true, true);

  if (active_only) {
    // just show the active config (no diff)
    cnode::show_cfg(aroot, op_show_show_defaults, op_show_hide_secrets);
  } else {
    cnode::CfgNode wroot(cstore, nargs, false, true);
    if (working_only) {
      // just show the working config (no diff)
      cnode::show_cfg(wroot, op_show_show_defaults, op_show_hide_secrets);
    } else {
      Cpath cur_path;
      cstore.getEditLevel(cur_path);
      cnode::show_cfg_diff(aroot, wroot, cur_path, op_show_show_defaults,
                           op_show_hide_secrets, op_show_context_diff);
    }
  }
}

/* new "show" API providing superset of functionality of showCfg above.
 * available command-line options (all are optional):
 *   --show-cfg1 <cfg1> --show-cfg2 <cfg2>
 *       specify the two configs to be diffed (must specify both)
 *       <cfg1>: "@ACTIVE", "@WORKING", or config file name
 *       <cfg2>: "@ACTIVE", "@WORKING", or config file name
 *
 *       if not specified, default is cfg1="@ACTIVE" and cfg2="@WORKING",
 *       i.e., same as "traditional show"
 *   --show-active-only
 *       only show active config (i.e., cfg1="@ACTIVE" and cfg2="@ACTIVE")
 *   --show-working-only
 *       only show working config (i.e., cfg1="@WORKING" and cfg2="@WORKING")
 *   --show-show-defaults
 *       display "default" values
 *   --show-hide-secrets
 *       hide "secret" values when displaying
 *   --show-context-diff
 *       show "context diff" between two configs
 *   --show-commands
 *       show output in "commands"
 *   --show-ignore-edit
 *       don't use the edit level in environment
 *
 * note that when neither cfg1 nor cfg2 specifies a config file, the "args"
 * argument specifies the root path for the show output, and the "edit level"
 * in the environment is used.
 *
 * on the other hand, if either cfg1 or cfg2 specifies a config file, then
 * both "args" and "edit level" are ignored.
 */
static void
showConfig(Cstore& cstore, const Cpath& args)
{
  string cfg1 = cnode::ACTIVE_CFG;
  string cfg2 = cnode::WORKING_CFG;

  if (op_show_active_only) {
    cfg2 = cnode::ACTIVE_CFG;
  } else if (op_show_working_only) {
    cfg1 = cnode::WORKING_CFG;
  } else if (op_show_cfg1 && op_show_cfg2) {
    cfg1 = op_show_cfg1;
    cfg2 = op_show_cfg2;
  } else {
    // default
  }

  cnode::showConfig(cfg1, cfg2, args, op_show_show_defaults,
                    op_show_hide_secrets, op_show_context_diff,
                    op_show_commands, op_show_ignore_edit);
}

static void
loadFile(Cstore& cstore, const Cpath& args)
{
  if (!cstore.loadFile(args[0])) {
    // loadFile failed
    exit(1);
  }
}

static cnode::CfgNode *
_cf_process_args(Cstore& cstore, const Cpath& args, Cpath& path)
{
  for (size_t i = 1; i < args.size(); i++) {
    path.push(args[i]);
  }
  cnode::CfgNode *root = cparse::parse_file(args[0], cstore);
  if (!root) {
    // failed to parse config file
    exit(1);
  }
  return root;
}

// output the "pre-commit hook dir"
static void
getPreCommitHookDir(Cstore& cstore, const Cpath& args)
{
  const char *d = commit::getCommitHookDir(commit::PRE_COMMIT);
  if (d) {
    printf("%s", d);
  }
}

// output the "post-commit hook dir"
static void
getPostCommitHookDir(Cstore& cstore, const Cpath& args)
{
  const char *d = commit::getCommitHookDir(commit::POST_COMMIT);
  if (d) {
    printf("%s", d);
  }
}

/* the following "cf" functions form the "config file" shell API, which
 * allows shell scripts to "query" the "config" represented by a config
 * file in a way similar to how they query the active/working config.
 * usage example:
 *
 *   cli-shell-api cfExists /config/config.boot service ssh allow-root
 *
 * the above command will exit with 0 (success) if the "allow-root" node
 * is present in the specified config file (or exit with 1 if it's not).
 */
static void
cfExists(Cstore& cstore, const Cpath& args)
{
  Cpath path;
  cnode::CfgNode *root = _cf_process_args(cstore, args, path);
  exit(cnode::findCfgNode(root, path) ? 0 : 1);
}

static void
cfReturnValue(Cstore& cstore, const Cpath& args)
{
  Cpath path;
  cnode::CfgNode *root = _cf_process_args(cstore, args, path);
  string value;
  if (!cnode::getCfgNodeValue(root, path, value)) {
    exit(1);
  }
  printf("%s", value.c_str());
}

static void
cfReturnValues(Cstore& cstore, const Cpath& args)
{
  Cpath path;
  cnode::CfgNode *root = _cf_process_args(cstore, args, path);
  vector<string> values;
  if (!cnode::getCfgNodeValues(root, path, values)) {
    exit(1);
  }
  print_vec(values, " ", "'");
}

#define OP(name, exact, exact_err, min, min_err, use_edit) \
  { #name, exact, exact_err, min, min_err, use_edit, &name }

static int op_idx = -1;
static OpT ops[] = {
  OP(getSessionEnv, 1, "Must specify session ID", -1, NULL, true),
  OP(getEditEnv, -1, NULL, 1, "Must specify config path", true),
  OP(getEditUpEnv, 0, "No argument expected", -1, NULL, true),
  OP(getEditResetEnv, 0, "No argument expected", -1, NULL, true),
  OP(editLevelAtRoot, 0, "No argument expected", -1, NULL, true),
  OP(getCompletionEnv, -1, NULL,
     2, "Must specify command and at least one component", true),
  OP(getEditLevelStr, 0, "No argument expected", -1, NULL, true),

  OP(markSessionUnsaved, 0, "No argument expected", -1, NULL, NULL),
  OP(unmarkSessionUnsaved, 0, "No argument expected", -1, NULL, NULL),
  OP(sessionUnsaved, 0, "No argument expected", -1, NULL, NULL),
  OP(sessionChanged, 0, "No argument expected", -1, NULL, NULL),

  OP(teardownSession, 0, "No argument expected", -1, NULL, NULL),
  OP(setupSession, 0, "No argument expected", -1, NULL, NULL),
  OP(inSession, 0, "No argument expected", -1, NULL, NULL),

  OP(exists, -1, NULL, 1, "Must specify config path", NULL),
  OP(existsActive, -1, NULL, 1, "Must specify config path", NULL),
  OP(existsEffective, -1, NULL, 1, "Must specify config path", NULL),

  OP(listNodes, -1, NULL, -1, NULL, NULL),
  OP(listActiveNodes, -1, NULL, -1, NULL, NULL),
  OP(listEffectiveNodes, -1, NULL, 1, "Must specify config path", NULL),

  OP(isMulti, -1, NULL, 1, "Must specify config path", NULL),
  OP(isTag,  -1, NULL, 1, "Must specify config path", NULL),
  OP(isLeaf, -1, NULL, 1, "Must specify config path", NULL),
  OP(isValue, -1, NULL, 1, "Must specify config path", NULL),
  OP(getNodeType, -1, NULL, 1, "Must specify config path", NULL),

  OP(returnValue, -1, NULL, 1, "Must specify config path", NULL),
  OP(returnActiveValue, -1, NULL, 1, "Must specify config path", NULL),
  OP(returnEffectiveValue, -1, NULL, 1, "Must specify config path", NULL),

  OP(returnValues, -1, NULL, 1, "Must specify config path", NULL),
  OP(returnActiveValues, -1, NULL, 1, "Must specify config path", NULL),
  OP(returnEffectiveValues, -1, NULL, 1, "Must specify config path", NULL),

  OP(validateTmplPath, -1, NULL, 1, "Must specify config path", NULL),
  OP(validateTmplValPath, -1, NULL, 1, "Must specify config path", NULL),

  OP(showCfg, -1, NULL, -1, NULL, true),
  OP(showConfig, -1, NULL, -1, NULL, true),
  OP(loadFile, 1, "Must specify config file", -1, NULL, NULL),

  OP(getPreCommitHookDir, 0, "No argument expected", -1, NULL, NULL),
  OP(getPostCommitHookDir, 0, "No argument expected", -1, NULL, NULL),

  OP(cfExists, -1, NULL, 2, "Must specify config file and path", NULL),
  OP(cfReturnValue, -1, NULL, 2, "Must specify config file and path", NULL),
  OP(cfReturnValues, -1, NULL, 2, "Must specify config file and path", NULL),

  {NULL, -1, NULL, -1, NULL, NULL, NULL}
};
#define OP_exact_args  ops[op_idx].op_exact_args
#define OP_min_args    ops[op_idx].op_min_args
#define OP_exact_error ops[op_idx].op_exact_error
#define OP_min_error   ops[op_idx].op_min_error
#define OP_use_edit    ops[op_idx].op_use_edit
#define OP_func        ops[op_idx].op_func

enum {
  SHOW_CFG1 = 1,
  SHOW_CFG2
};

struct option options[] = {
  {"show-active-only", no_argument, &op_show_active_only, 1},
  {"show-show-defaults", no_argument, &op_show_show_defaults, 1},
  {"show-hide-secrets", no_argument, &op_show_hide_secrets, 1},
  {"show-working-only", no_argument, &op_show_working_only, 1},
  {"show-context-diff", no_argument, &op_show_context_diff, 1},
  {"show-commands", no_argument, &op_show_commands, 1},
  {"show-ignore-edit", no_argument, &op_show_ignore_edit, 1},
  {"show-cfg1", required_argument, NULL, SHOW_CFG1},
  {"show-cfg2", required_argument, NULL, SHOW_CFG2},
  {NULL, 0, NULL, 0}
};

int
main(int argc, char **argv)
{
  // handle options first
  int c = 0;
  while ((c = getopt_long(argc, argv, "", options, NULL)) != -1) {
    switch (c) {
      case SHOW_CFG1:
        op_show_cfg1 = strdup(optarg);
        break;
      case SHOW_CFG2:
        op_show_cfg2 = strdup(optarg);
        break;
      default:
        break;
    }
  }
  int nargs = argc - optind - 1;
  char *oname = argv[optind];
  char **nargv = &(argv[optind + 1]);

  int i = 0;
  if (nargs < 0) {
    fprintf(stderr, "Must specify operation\n");
    exit(1);
  }
  while (ops[i].op_name) {
    if (strcmp(oname, ops[i].op_name) == 0) {
      op_idx = i;
      break;
    }
    ++i;
  }
  if (op_idx == -1) {
    fprintf(stderr, "Invalid operation\n");
    exit(1);
  }
  if (OP_exact_args >= 0 && nargs != OP_exact_args) {
    fprintf(stderr, "%s\n", OP_exact_error);
    exit(1);
  }
  if (OP_min_args >= 0 && nargs < OP_min_args) {
    fprintf(stderr, "%s\n", OP_min_error);
    exit(1);
  }

  Cpath args(const_cast<const char **>(nargv), nargs);

  // call the op function
  Cstore *cstore = Cstore::createCstore(OP_use_edit);
  OP_func(*cstore, args);
  delete cstore;
  exit(0);
}


