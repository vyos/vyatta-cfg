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
#include <cerrno>
#include <vector>
#include <string>
#include <libgen.h>
#include <sys/mount.h>

#include <cli_cstore.h>
#include <cstore/unionfs/cstore-unionfs.hpp>

static void
print_vec(const vector<string>& vec, const char *sep, const char *quote)
{
  for (unsigned int i = 0; i < vec.size(); i++) {
    printf("%s%s%s%s", ((i > 0) ? sep : ""), quote, vec[i].c_str(), quote);
  }
}

typedef void (*OpFuncT)(const vector<string>& args);

typedef struct {
  const char *op_name;
  const int op_exact_args;
  const char *op_exact_error;
  const int op_min_args;
  const char *op_min_error;
  OpFuncT op_func;
} OpT;

static void
getSessionEnv(const vector<string>& args)
{
  string env;
  UnionfsCstore cstore(args[0], env);
  printf("%s", env.c_str());
}

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

static void
exists(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.cfgPathExists(args, false) ? 0 : 1);
}

static void
existsActive(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.cfgPathExists(args, true) ? 0 : 1);
}

static void
listNodes(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  vector<string> cnodes;
  cstore.cfgPathGetChildNodes(args, cnodes, false);
  print_vec(cnodes, " ", "'");
}

static void
listActiveNodes(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  vector<string> cnodes;
  cstore.cfgPathGetChildNodes(args, cnodes, true);
  print_vec(cnodes, " ", "'");
}

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
  OP(listNodes, -1, NULL, -1, NULL),
  OP(listActiveNodes, -1, NULL, -1, NULL),
  OP(returnValue, -1, NULL, 1, "Must specify config path"),
  OP(returnActiveValue, -1, NULL, 1, "Must specify config path"),
  OP(returnValues, -1, NULL, 1, "Must specify config path"),
  OP(returnActiveValues, -1, NULL, 1, "Must specify config path"),

  {NULL, -1, NULL, -1, NULL, NULL}
};
#define OP_exact_args  ops[op_idx].op_exact_args
#define OP_min_args    ops[op_idx].op_min_args
#define OP_exact_error ops[op_idx].op_exact_error
#define OP_min_error   ops[op_idx].op_min_error
#define OP_func        ops[op_idx].op_func

int
main(int argc, char **argv)
{
  int i = 0;
  if (argc < 2) {
    fprintf(stderr, "Must specify operation\n");
    exit(-1);
  }
  while (ops[i].op_name) {
    if (strcmp(argv[1], ops[i].op_name) == 0) {
      op_idx = i;
      break;
    }
    ++i;
  }
  if (op_idx == -1) {
    fprintf(stderr, "Invalid operation\n");
    exit(-1);
  }
  if (OP_exact_args >= 0 && (argc - 2) != OP_exact_args) {
    fprintf(stderr, "%s\n", OP_exact_error);
    exit(-1);
  }
  if (OP_min_args >= 0 && (argc - 2) < OP_min_args) {
    fprintf(stderr, "%s\n", OP_min_error);
    exit(-1);
  }

  vector<string> args;
  for (int i = 2; i < argc; i++) {
    args.push_back(argv[i]);
  }

  // call the op function
  OP_func(args);
  exit(0);
}

