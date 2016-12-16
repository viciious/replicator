/* Copyright 2011 ZAO "Begun".
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __SLAVE_TABLE_H_
#define __SLAVE_TABLE_H_


#include <string>
#include <vector>
#include <map>

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>

#include "field.h"
#include "recordset.h"
#include "SlaveStats.h"


namespace slave
{

typedef boost::shared_ptr<Field> PtrField;
typedef boost::function<void (RecordSet&)> callback;


class Table {

public:

    std::vector<PtrField> fields;
    std::vector<unsigned char> filter;
    std::vector<unsigned> filter_fields;
    unsigned n_filter_count;

    callback m_callback;

    void call_callback(slave::RecordSet& _rs, ExtStateIface &ext_state) {

        // Some stats
        ext_state.incTableCount(full_name);
        ext_state.setLastFilteredUpdateTime();

        m_callback(_rs);
    }

    void set_callback_filter(const std::vector<std::string> &_filter) {
        if (_filter.empty()) {
            filter.clear();
            filter_fields.clear();
            n_filter_count = 0;
            return;
        }

        n_filter_count = _filter.size();
        filter_fields.resize(fields.size());
        filter.resize((fields.size() + 7)/8);

        for (unsigned i = 0; i < filter.size(); i++) {
            filter[i] = 0;
            filter_fields[i] = 0;
        }

        // FIXME: this loop has complexity factor of O(N*M)
        for (auto i = _filter.begin(); i != _filter.end(); ++i) {
            const auto &field_name = *i;
            for (auto j = fields.begin(); j != fields.end(); ++j) {
                const auto &field = *j;
                if (field->getFieldName() == field_name) {
                    const int index = j - fields.begin();
                    filter[index>>3] |= (1<<(index&7));
                    filter_fields[index] = i - _filter.begin();
                    break;
                }
            }
        }
    }

    const std::string table_name;
    const std::string database_name;

    std::string full_name;
    std::string pk_field;

    Table(const std::string& db_name, const std::string& tbl_name) :
        n_filter_count(0),
        table_name(tbl_name), database_name(db_name),
        full_name(database_name + "." + table_name),
        pk_field("")
        {}

    Table() {}

};

}

#endif
