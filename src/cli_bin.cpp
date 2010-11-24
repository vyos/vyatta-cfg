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
#include <libgen.h>

#include <cli_cstore.h>
#include <cstore/unionfs/cstore-unionfs.hpp>

static int op_idx = -1;
static const char *op_bin_name[] = {
  "my_set",
  "my_delete",
  "my_activate",
  "my_deactivate",
  "my_rename",
  "my_copy",
  "my_comment",
  "my_discard",
  "my_move",
  NULL
};
static const char *op_Str[] = {
  "Set",
  "Delete",
  "Activate",
  "Deactivate",
  "Rename",
  "Copy",
  "Comment",
  "Discard",
  "Move",
  NULL
};
static const char *op_str[] = {
  "set",
  "delete",
  "activate",
  "deactivate",
  "rename",
  "copy",
  "comment",
  "discard",
  "move",
  NULL
};
static const bool op_need_cfg_node_args[] = {
  true,
  true,
  true,
  true,
  true,
  true,
  true,
  false,
  true,
  false // dummy
};
#define OP_Str op_Str[op_idx]
#define OP_str op_str[op_idx]
#define OP_need_cfg_node_args op_need_cfg_node_args[op_idx]

static void
doSet(Cstore& cstore, const vector<string>& path_comps)
{
  if (!cstore.validateSetPath(path_comps)) {
    bye("invalid set path\n");
  }
  if (!cstore.setCfgPath(path_comps)) {
    bye("set cfg path failed\n");
  }
}

static void
doDelete(Cstore& cstore, const vector<string>& path_comps)
{
  vtw_def def;
  if (!cstore.validateDeletePath(path_comps, def)) {
    bye("invalid delete path");
  }
  if (!cstore.deleteCfgPath(path_comps, def)) {
    bye("delete failed\n");
  }
}

static void
doActivate(Cstore& cstore, const vector<string>& path_comps)
{
  if (!cstore.validateActivatePath(path_comps)) {
    bye("%s validate failed", OP_str);
  }
  if (!cstore.unmarkCfgPathDeactivated(path_comps)) {
    bye("%s failed", OP_str);
  }
}

static void
doDeactivate(Cstore& cstore, const vector<string>& path_comps)
{
  if (!cstore.validateDeactivatePath(path_comps)) {
    bye("%s validate failed", OP_str);
  }
  if (!cstore.markCfgPathDeactivated(path_comps)) {
    bye("%s failed", OP_str);
  }
}

static void
doRename(Cstore& cstore, const vector<string>& path_comps)
{
  if (!cstore.validateRenameArgs(path_comps)) {
    bye("invalid rename args\n");
  }
  if (!cstore.renameCfgPath(path_comps)) {
    bye("rename cfg path failed\n");
  }
}

static void
doCopy(Cstore& cstore, const vector<string>& path_comps)
{
  if (!cstore.validateCopyArgs(path_comps)) {
    bye("invalid copy args\n");
  }
  if (!cstore.copyCfgPath(path_comps)) {
    bye("copy cfg path failed\n");
  }
}

static void
doComment(Cstore& cstore, const vector<string>& path_comps)
{
  vtw_def def;
  if (!cstore.validateCommentArgs(path_comps, def)) {
    bye("invalid comment args\n");
  }
  if (!cstore.commentCfgPath(path_comps, def)) {
    bye("comment cfg path failed\n");
  }
}

static void
doDiscard(Cstore& cstore, const vector<string>& args)
{
  if (args.size() > 0) {
    OUTPUT_USER("Invalid discard command\n");
    bye("invalid discard command\n");
  }
  if (!cstore.discardChanges()) {
    bye("discard failed\n");
  }
}

static void
doMove(Cstore& cstore, const vector<string>& path_comps)
{
  if (!cstore.validateMoveArgs(path_comps)) {
    bye("invalid move args\n");
  }
  if (!cstore.moveCfgPath(path_comps)) {
    bye("move cfg path failed\n");
  }
}

typedef void (*OpFuncT)(Cstore& cstore,
                        const vector<string>& path_comps);
OpFuncT OpFunc[] = {
  &doSet,
  &doDelete,
  &doActivate,
  &doDeactivate,
  &doRename,
  &doCopy,
  &doComment,
  &doDiscard,
  &doMove,
  NULL
};

int
main(int argc, char **argv)
{
  int i = 0;
  while (op_bin_name[i]) {
    if (strcmp(basename(argv[0]), op_bin_name[i]) == 0) {
      op_idx = i;
      break;
    }
    ++i;
  }
  if (op_idx == -1) {
    printf("Invalid operation\n");
    exit(1);
  }

  if (initialize_output(OP_Str) == -1) {
    bye("can't initialize output\n");
  }
  if (OP_need_cfg_node_args && argc < 2) {
    fprintf(out_stream, "Need to specify the config node to %s\n", OP_str);
    bye("nothing to %s\n", OP_str);
  }

  // actual CLI operations use the edit levels from environment, so pass true.
  UnionfsCstore cstore(true);
  vector<string> path_comps;
  for (int i = 1; i < argc; i++) {
    path_comps.push_back(argv[i]);
  }

  // call the op function
  OpFunc[op_idx](cstore, path_comps);
  exit(0);
}

