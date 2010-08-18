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

#ifndef _CSTORE_VARREF_H_
#define _CSTORE_VARREF_H_
#include <vector>
#include <string>
#include <map>

#include <cstore/cstore.hpp>

using namespace std;

class Cstore::VarRef {
public:
  VarRef(Cstore *cstore, const string& ref_str, bool active);
  ~VarRef() {};

  bool getValue(string& value, vtw_type_e& def_type);
  bool getSetPath(vector<string>& path_comps);

private:
  Cstore *_cstore;
  bool _active;
  bool _absolute;
  string _at_string;
  vector<string> _orig_path_comps;
  vector<pair<vector<string>, vtw_type_e> > _paths;

  void process_ref(const vector<string>& ref_comps,
                   const vector<string>& cur_path_comps, vtw_type_e def_type);
};

#endif /* _CSTORE_VARREF_H_ */

