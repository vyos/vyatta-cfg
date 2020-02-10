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

#ifndef _CNODE_ALGORITHM_HPP_
#define _CNODE_ALGORITHM_HPP_

#include <vector>
#include <string>

#include <cstore/cpath.hpp>
#include <cnode/cnode.hpp>

namespace cnode {

enum DiffState {
  DIFF_ADD,
  DIFF_DEL,
  DIFF_UPD,
  DIFF_NONE,
  DIFF_NULL
};

bool cmp_multi_values(const CfgNode *cfg1, const CfgNode *cfg2,
                      std::vector<std::string>& values,
                      std::vector<DiffState>& pfxs);
void cmp_non_leaf_nodes(const CfgNode *cfg1, const CfgNode *cfg2,
                        std::vector<CfgNode *>& rcnodes1,
                        std::vector<CfgNode *>& rcnodes2,
                        bool& not_tag_node, bool& is_value,
                        bool& is_leaf_typeless, std::string& name,
                        std::string& value);

int show_cfg_diff(const CfgNode& cfg1, const CfgNode& cfg2,
                   cstore::Cpath& cur_path, bool show_def = false,
                   bool hide_secret = false, bool context_diff = false);
int show_cfg(const CfgNode& cfg, bool show_def = false,
              bool hide_secret = false);

void show_cmds_diff(const CfgNode& cfg1, const CfgNode& cfg2);
void show_cmds(const CfgNode& cfg);

void get_cmds_diff(const CfgNode& cfg1, const CfgNode& cfg2,
                   std::vector<cstore::Cpath>& del_list,
                   std::vector<cstore::Cpath>& set_list,
                   std::vector<cstore::Cpath>& com_list);
void get_cmds(const CfgNode& cfg, std::vector<cstore::Cpath>& set_list,
              std::vector<cstore::Cpath>& com_list);

extern const std::string ACTIVE_CFG;
extern const std::string WORKING_CFG;

int showConfig(const std::string& cfg1, const std::string& cfg2,
               const cstore::Cpath& path, bool show_def = false,
               bool hide_secret = false, bool context_diff = false,
               bool show_cmds = false, bool ignore_edit = false);

/* these functions provide the functionality necessary for the "config
 * file" shell API. basically the API uses the "cparse" interface to
 * parse a config file into a CfgNode tree structure, and then these
 * functions can be used to access the nodes in the tree.
 */
CfgNode *findCfgNode(CfgNode *root, const cstore::Cpath& path,
                     bool& is_value);
CfgNode *findCfgNode(CfgNode *root, const cstore::Cpath& path);
bool getCfgNodeValue(CfgNode *root, const cstore::Cpath& path,
                     std::string& value);
bool getCfgNodeValues(CfgNode *root, const cstore::Cpath& path,
                      std::vector<std::string>& values);

} // namespace cnode

#endif /* _CNODE_ALGORITHM_HPP_ */

