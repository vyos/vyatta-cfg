/*
 * Copyright (C) 2011 Vyatta, Inc.
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

#ifndef _UTIL_H_
#define _UTIL_H_
#include <tr1/unordered_map>

namespace cstore { // begin namespace cstore

template<class K, class V, class H = std::tr1::hash<K> >
  class MapT : public std::tr1::unordered_map<K, V, H> {};

template<int v>
struct Int2Type {
  enum {
    value = v
  };
};

} // end namespace cstore

#endif /* _UTIL_H_ */

