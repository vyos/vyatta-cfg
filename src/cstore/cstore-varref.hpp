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

namespace cstore { // begin namespace cstore

class Cstore::VarRef {
public:
  VarRef(Cstore *cstore, const string& ref_str, bool active);
  ~VarRef() {};

  bool getValue(string& value, vtw_type_e& def_type);
  bool getSetPath(Cpath& path_comps);

private:
  Cstore *_cstore;
  bool _active;
  bool _absolute;
  string _at_string;
  Cpath _orig_path_comps;
  vector<pair<Cpath, vtw_type_e> > _paths;

  void process_ref(const Cpath& ref_comps,
                   const Cpath& cur_path_comps, vtw_type_e def_type);
};

} // end namespace cstore

#endif /* _CSTORE_VARREF_H_ */

