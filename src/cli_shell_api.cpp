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

static int op_idx = -1;
static const char *op_name[] = {
  "getSessionEnv",
  "getEditEnv",
  "getEditUpEnv",
  "getEditResetEnv",
  "editLevelAtRoot",
  "getCompletionEnv",
  "getEditLevelStr",
  "markSessionUnsaved",
  "unmarkSessionUnsaved",
  "sessionUnsaved",
  "sessionChanged",
  "teardownSession",
  "setupSession",
  "inSession",
  "exists",
  "existsActive",
  NULL
};
static const int op_exact_args[] = {
  1,
  -1,
  0,
  0,
  0,
  -1,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  -1,
  -1,
  -1
};
static const char *op_exact_error[] = {
  "Must specify session ID",
  NULL,
  "No argument expected",
  "No argument expected",
  "No argument expected",
  NULL,
  "No argument expected",
  "No argument expected",
  "No argument expected",
  "No argument expected",
  "No argument expected",
  "No argument expected",
  "No argument expected",
  "No argument expected",
  NULL,
  NULL,
  NULL
};
static const int op_min_args[] = {
  -1,
  1,
  -1,
  -1,
  -1,
  2,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  1,
  1,
  -1
};
static const char *op_min_error[] = {
  NULL,
  "Must specify config path to edit",
  NULL,
  NULL,
  NULL,
  "Must specify command and at least one component",
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  "Must specify config path",
  "Must specify config path",
  NULL
};
#define OP_exact_args op_exact_args[op_idx]
#define OP_min_args op_min_args[op_idx]
#define OP_exact_error op_exact_error[op_idx]
#define OP_min_error op_min_error[op_idx]

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

typedef void (*OpFuncT)(const vector<string>& args);
OpFuncT OpFunc[] = {
  &doGetSessionEnv,
  &doGetEditEnv,
  &doGetEditUpEnv,
  &doGetEditResetEnv,
  &doEditLevelAtRoot,
  &doGetCompletionEnv,
  &doGetEditLevelStr,
  &doMarkSessionUnsaved,
  &doUnmarkSessionUnsaved,
  &doSessionUnsaved,
  &doSessionChanged,
  &doTeardownSession,
  &doSetupSession,
  &doInSession,
  &doExists,
  &doExistsActive,
  NULL
};

int
main(int argc, char **argv)
{
  int i = 0;
  if (argc < 2) {
    printf("Must specify operation\n");
    exit(-1);
  }
  while (op_name[i]) {
    if (strcmp(argv[1], op_name[i]) == 0) {
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
  OpFunc[op_idx](args);
  exit(0);
}

