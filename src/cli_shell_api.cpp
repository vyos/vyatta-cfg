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
#include <cstring>
#include <vector>
#include <string>
#include <getopt.h>

#include <cli_cstore.h>
#include <cstore/unionfs/cstore-unionfs.hpp>
#include <cnode/cnode.hpp>
#include <cnode/cnode-algorithm.hpp>

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

typedef void (*OpFuncT)(const vector<string>& args);

typedef struct {
  const char *op_name;
  const int op_exact_args;
  const char *op_exact_error;
  const int op_min_args;
  const char *op_min_error;
  OpFuncT op_func;
} OpT;

/* outputs an environment string to be "eval"ed */
static void
getSessionEnv(const vector<string>& args)
{
  string env;
  UnionfsCstore cstore(args[0], env);
  printf("%s", env.c_str());
}

/* outputs an environment string to be "eval"ed */
static void
getEditEnv(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  string env;
  if (!cstore.getEditEnv(args, env)) {
    exit(-1);
  }
  printf("%s", env.c_str());
}

/* outputs an environment string to be "eval"ed */
static void
getEditUpEnv(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  string env;
  if (!cstore.getEditUpEnv(env)) {
    exit(-1);
  }
  printf("%s", env.c_str());
}

/* outputs an environment string to be "eval"ed */
static void
getEditResetEnv(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  string env;
  if (!cstore.getEditResetEnv(env)) {
    exit(-1);
  }
  printf("%s", env.c_str());
}

static void
editLevelAtRoot(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.editLevelAtRoot() ? 0 : 1);
}

/* outputs an environment string to be "eval"ed */
static void
getCompletionEnv(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  string env;
  if (!cstore.getCompletionEnv(args, env)) {
    exit(-1);
  }
  printf("%s", env.c_str());
}

/* outputs a string */
static void
getEditLevelStr(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  vector<string> lvec;
  cstore.getEditLevel(lvec);
  print_vec(lvec, " ", "");
}

static void
markSessionUnsaved(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.markSessionUnsaved()) {
    exit(-1);
  }
}

static void
unmarkSessionUnsaved(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.unmarkSessionUnsaved()) {
    exit(-1);
  }
}

static void
sessionUnsaved(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.sessionUnsaved()) {
    exit(-1);
  }
}

static void
sessionChanged(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.sessionChanged()) {
    exit(-1);
  }
}

static void
teardownSession(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.teardownSession()) {
    exit(-1);
  }
}

static void
setupSession(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.setupSession()) {
    exit(-1);
  }
}

static void
inSession(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.inSession()) {
    exit(-1);
  }
}

/* same as exists() in Perl API */
static void
exists(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.cfgPathExists(args, false) ? 0 : 1);
}

/* same as existsOrig() in Perl API */
static void
existsActive(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.cfgPathExists(args, true) ? 0 : 1);
}

/* same as isEffective() in Perl API */
static void
existsEffective(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.cfgPathEffective(args) ? 0 : 1);
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
listNodes(const vector<string>& args)
{
  UnionfsCstore cstore(true);
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
listActiveNodes(const vector<string>& args)
{
  UnionfsCstore cstore(true);
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
listEffectiveNodes(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  vector<string> cnodes;
  cstore.cfgPathGetEffectiveChildNodes(args, cnodes);
  print_vec(cnodes, " ", "'");
}

/* same as returnValue() in Perl API. outputs a string. */
static void
returnValue(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  string val;
  if (!cstore.cfgPathGetValue(args, val, false)) {
    exit(1);
  }
  printf("%s", val.c_str());
}

/* same as returnOrigValue() in Perl API. outputs a string. */
static void
returnActiveValue(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  string val;
  if (!cstore.cfgPathGetValue(args, val, true)) {
    exit(1);
  }
  printf("%s", val.c_str());
}

/* same as returnEffectiveValue() in Perl API. outputs a string. */
static void
returnEffectiveValue(const vector<string>& args)
{
  UnionfsCstore cstore(true);
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
returnValues(const vector<string>& args)
{
  UnionfsCstore cstore(true);
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
returnActiveValues(const vector<string>& args)
{
  UnionfsCstore cstore(true);
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
returnEffectiveValues(const vector<string>& args)
{
  UnionfsCstore cstore(true);
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
validateTmplPath(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.validateTmplPath(args, false) ? 0 : 1);
}

/* checks if specified path is a valid "template path", *including* the
 * validity of any "tag values" along the path.
 */
static void
validateTmplValPath(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.validateTmplPath(args, true) ? 0 : 1);
}

static void
showCfg(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  vector<string> nargs(args);
  bool active_only = (!cstore.inSession() || op_show_active_only);
  bool working_only = (cstore.inSession() && op_show_working_only);
  cnode::CfgNode aroot(cstore, nargs, true, true);

  if (active_only) {
    // just show the active config
    cnode::show_diff(aroot, aroot, op_show_show_defaults,
                     op_show_hide_secrets);
  } else if (working_only) {
    // just show the working config without diff markers
    cnode::CfgNode wroot(cstore, nargs, false, true);
    cnode::show_diff(wroot, wroot, op_show_show_defaults,
                     op_show_hide_secrets);
  } else {
    cnode::CfgNode wroot(cstore, nargs, false, true);
    cnode::show_diff(aroot, wroot, op_show_show_defaults,
                     op_show_hide_secrets);
  }
}

#define OP(name, exact, exact_err, min, min_err) \
  { #name, exact, exact_err, min, min_err, &name }

static int op_idx = -1;
static OpT ops[] = {
  OP(getSessionEnv, 1, "Must specify session ID", -1, NULL),
  OP(getEditEnv, -1, NULL, 1, "Must specify config path"),
  OP(getEditUpEnv, 0, "No argument expected", -1, NULL),
  OP(getEditResetEnv, 0, "No argument expected", -1, NULL),
  OP(editLevelAtRoot, 0, "No argument expected", -1, NULL),
  OP(getCompletionEnv, -1, NULL,
     2, "Must specify command and at least one component"),
  OP(getEditLevelStr, 0, "No argument expected", -1, NULL),

  OP(markSessionUnsaved, 0, "No argument expected", -1, NULL),
  OP(unmarkSessionUnsaved, 0, "No argument expected", -1, NULL),
  OP(sessionUnsaved, 0, "No argument expected", -1, NULL),
  OP(sessionChanged, 0, "No argument expected", -1, NULL),

  OP(teardownSession, 0, "No argument expected", -1, NULL),
  OP(setupSession, 0, "No argument expected", -1, NULL),
  OP(inSession, 0, "No argument expected", -1, NULL),

  OP(exists, -1, NULL, 1, "Must specify config path"),
  OP(existsActive, -1, NULL, 1, "Must specify config path"),
  OP(existsEffective, -1, NULL, 1, "Must specify config path"),

  OP(listNodes, -1, NULL, -1, NULL),
  OP(listActiveNodes, -1, NULL, -1, NULL),
  OP(listEffectiveNodes, -1, NULL, 1, "Must specify config path"),

  OP(returnValue, -1, NULL, 1, "Must specify config path"),
  OP(returnActiveValue, -1, NULL, 1, "Must specify config path"),
  OP(returnEffectiveValue, -1, NULL, 1, "Must specify config path"),

  OP(returnValues, -1, NULL, 1, "Must specify config path"),
  OP(returnActiveValues, -1, NULL, 1, "Must specify config path"),
  OP(returnEffectiveValues, -1, NULL, 1, "Must specify config path"),

  OP(validateTmplPath, -1, NULL, 1, "Must specify config path"),
  OP(validateTmplValPath, -1, NULL, 1, "Must specify config path"),

  OP(showCfg, -1, NULL, -1, NULL),

  {NULL, -1, NULL, -1, NULL, NULL}
};
#define OP_exact_args  ops[op_idx].op_exact_args
#define OP_min_args    ops[op_idx].op_min_args
#define OP_exact_error ops[op_idx].op_exact_error
#define OP_min_error   ops[op_idx].op_min_error
#define OP_func        ops[op_idx].op_func

struct option options[] = {
  {"show-active-only", no_argument, &op_show_active_only, 1},
  {"show-show-defaults", no_argument, &op_show_show_defaults, 1},
  {"show-hide-secrets", no_argument, &op_show_hide_secrets, 1},
  {"show-working-only", no_argument, &op_show_working_only, 1},
  {NULL, 0, NULL, 0}
};

int
main(int argc, char **argv)
{
  // handle options first
  int c = 0;
  while ((c = getopt_long(argc, argv, "", options, NULL)) != -1) {
    // nothing for now
  }
  int nargs = argc - optind - 1;
  char *oname = argv[optind];
  char **nargv = &(argv[optind + 1]);

  int i = 0;
  if (nargs < 0) {
    fprintf(stderr, "Must specify operation\n");
    exit(-1);
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
    exit(-1);
  }
  if (OP_exact_args >= 0 && nargs != OP_exact_args) {
    fprintf(stderr, "%s\n", OP_exact_error);
    exit(-1);
  }
  if (OP_min_args >= 0 && nargs < OP_min_args) {
    fprintf(stderr, "%s\n", OP_min_error);
    exit(-1);
  }

  vector<string> args;
  for (int i = 0; i < nargs; i++) {
    args.push_back(nargv[i]);
  }

  // call the op function
  OP_func(args);
  exit(0);
}

