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
doGetSessionEnv(const vector<string>& args)
{
  string env;
  UnionfsCstore cstore(args[0], env);
  printf("%s", env.c_str());
}

static void
doGetEditEnv(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  string env;
  if (!cstore.getEditEnv(args, env)) {
    exit(-1);
  }
  printf("%s", env.c_str());
}

static void
doGetEditUpEnv(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  string env;
  if (!cstore.getEditUpEnv(env)) {
    exit(-1);
  }
  printf("%s", env.c_str());
}

static void
doGetEditResetEnv(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  string env;
  if (!cstore.getEditResetEnv(env)) {
    exit(-1);
  }
  printf("%s", env.c_str());
}

static void
doEditLevelAtRoot(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.editLevelAtRoot() ? 0 : 1);
}

static void
doGetCompletionEnv(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  string env;
  if (!cstore.getCompletionEnv(args, env)) {
    exit(-1);
  }
  printf("%s", env.c_str());
}

static void
doGetEditLevelStr(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  vector<string> lvec;
  cstore.getEditLevel(lvec);
  string level;
  for (unsigned int i = 0; i < lvec.size(); i++) {
    if (level.length() > 0) {
      level += " ";
    }
    level += lvec[i];
  }
  printf("%s", level.c_str());
}

static void
doMarkSessionUnsaved(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.markSessionUnsaved()) {
    exit(-1);
  }
}

static void
doUnmarkSessionUnsaved(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.unmarkSessionUnsaved()) {
    exit(-1);
  }
}

static void
doSessionUnsaved(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.sessionUnsaved()) {
    exit(-1);
  }
}

static void
doSessionChanged(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.sessionChanged()) {
    exit(-1);
  }
}

static void
doTeardownSession(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.teardownSession()) {
    exit(-1);
  }
}

static void
doSetupSession(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.setupSession()) {
    exit(-1);
  }
}

static void
doInSession(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  if (!cstore.inSession()) {
    exit(-1);
  }
}

static void
doExists(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.cfgPathExists(args, false) ? 0 : 1);
}

static void
doExistsActive(const vector<string>& args)
{
  UnionfsCstore cstore(true);
  exit(cstore.cfgPathExists(args, true) ? 0 : 1);
}

static int op_idx = -1;
static OpT ops[] = {
  {"getSessionEnv",
   1, "Must specify session ID", -1, NULL,
   &doGetSessionEnv},

  {"getEditEnv",
   -1, NULL, 1, "Must specify config path",
   &doGetEditEnv},

  {"getEditUpEnv",
   0, "No argument expected", -1, NULL,
   &doGetEditUpEnv},

  {"getEditResetEnv",
   0, "No argument expected", -1, NULL,
   &doGetEditResetEnv},

  {"editLevelAtRoot",
   0, "No argument expected", -1, NULL,
   &doEditLevelAtRoot},

  {"getCompletionEnv",
   -1, NULL, 2, "Must specify command and at least one component",
   &doGetCompletionEnv},

  {"getEditLevelStr",
   0, "No argument expected", -1, NULL,
   &doGetEditLevelStr},

  {"markSessionUnsaved",
   0, "No argument expected", -1, NULL,
   &doMarkSessionUnsaved},

  {"unmarkSessionUnsaved",
   0, "No argument expected", -1, NULL,
   &doUnmarkSessionUnsaved},

  {"sessionUnsaved",
   0, "No argument expected", -1, NULL,
   &doSessionUnsaved},

  {"sessionChanged",
   0, "No argument expected", -1, NULL,
   &doSessionChanged},

  {"teardownSession",
   0, "No argument expected", -1, NULL,
   &doTeardownSession},

  {"setupSession",
   0, "No argument expected", -1, NULL,
   &doSetupSession},

  {"inSession",
   0, "No argument expected", -1, NULL,
   &doInSession},

  {"exists",
   -1, NULL, 1, "Must specify config path",
   &doExists},

  {"existsActive",
   -1, NULL, 1, "Must specify config path",
   &doExistsActive},

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
    printf("Must specify operation\n");
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
    printf("Invalid operation\n");
    exit(-1);
  }
  if (OP_exact_args >= 0 && (argc - 2) != OP_exact_args) {
    printf("%s\n", OP_exact_error);
    exit(-1);
  }
  if (OP_min_args >= 0 && (argc - 2) < OP_min_args) {
    printf("%s\n", OP_min_error);
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

