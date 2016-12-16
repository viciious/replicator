#include <sstream>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/any.hpp>
#include <boost/function.hpp>
#include <boost/algorithm/string/join.hpp>

#include "dbreader.h"
#include "serializable.h"

namespace replicator {

static SerializableRow SlaveRowToSerializableRow(const slave::Row &row)
{
	SerializableRow srow;
	srow.reserve(row.size());
	for (slave::Row::const_iterator i = row.begin(); i != row.end(); ++i) {
		srow.push_back((*i).second);
	}
	return srow;
}

DBReader::DBReader(const std::string &host, const std::string &user, const std::string &password, unsigned int port, unsigned connect_retry) :
masterinfo(host, port, user, password, connect_retry), state(), slave(masterinfo, state), stopped(false), last_event_when(0)
{

}

DBReader::~DBReader()
{
	slave.close_connection();
}

void DBReader::AddTable(const std::string &db, const std::string &table, const std::vector<std::string> &columns)
{
	tables.push_back(DBTable(db, table, columns));
}

void DBReader::AddFilterPredicate(const std::string &db, const std::string &tbl, const SimplePredicate &pred)
{
	sfilter.AddPredicate(db, tbl, pred);
}

void DBReader::DumpTables(std::string &binlog_name, BinlogPos &binlog_pos, BinlogEventCallback cb)
{
	slave::callback dummycallback = boost::bind(&DBReader::DummyEventCallback, boost::ref(*this), _1);

	// start temp slave to read DB structure
	slave::Slave tempslave(masterinfo, state);
	for (TableList::const_iterator i = tables.begin(); i != tables.end(); ++i) {
		tempslave.setCallback(i->name.first, i->name.second, dummycallback);
	}
	
	tempslave.init();
	tempslave.createDatabaseStructure();

	last_event_when = ::time(NULL);
	
	slave::Slave::binlog_pos_t bp = tempslave.getLastBinlog();
	binlog_name = bp.first;
	binlog_pos = bp.second;

	state.setMasterLogNamePos(bp.first, bp.second);

	// dump tables
	nanomysql::Connection conn(masterinfo.host.c_str(), masterinfo.user.c_str(),
		masterinfo.password.c_str(), "", masterinfo.port);

	conn.query("SET NAMES utf8");

	for (TableList::const_iterator t = tables.begin(); t != tables.end(); ++t) {
		slave::RelayLogInfo rli = tempslave.getRli();
		if (stopped) {
			break;
		}

		// build field_name -> field_ptr map for filtered columns
		const boost::shared_ptr<slave::Table> rtable = rli.getTable(t->name);

		std::map<std::string, std::pair<unsigned, slave::PtrField>> filtered_fields;
		for (std::vector<slave::PtrField>::const_iterator f = rtable->fields.begin(); f != rtable->fields.end(); ++f)  {
			slave::PtrField field = *f;
			const auto j = find(t->filter.begin(), t->filter.end(), field->getFieldName());
			if (j != t->filter.end()) {
				unsigned index = std::distance(t->filter.begin(), j);
				filtered_fields[field->getFieldName()] = std::pair<unsigned, slave::PtrField>(index, field);
			}
		}

		conn.query(std::string("USE ") + t->name.first);
		conn.query(std::string("SELECT ") + boost::algorithm::join(t->filter, ",")  + " FROM " + t->name.second);
		conn.use(boost::bind(&DBReader::DumpTablesCallback, boost::ref(*this), boost::ref(rli), boost::cref(t->name.first), boost::cref(t->name.second), 
			boost::ref(conn), boost::ref(filtered_fields), _1, cb));
	}

	// send binlog position update event
	if (!stopped) {
		SerializableBinlogEvent ev;
		ev.binlog_name = binlog_name;
		ev.binlog_pos = binlog_pos;
		ev.seconds_behind_master = GetSecondsBehindMaster();
		ev.unix_timestamp = long(time(NULL));
		ev.event = "IGNORE";
		stopped = cb(ev);
	}

	tempslave.close_connection();
}

void DBReader::ReadBinlog(const std::string &binlog_name, BinlogPos binlog_pos, BinlogEventCallback cb)
{
	stopped = false;

	slave::callback callback = boost::bind(&DBReader::EventCallback, boost::ref(*this), _1, cb);

	state.setMasterLogNamePos(binlog_name, binlog_pos);
	for (TableList::const_iterator t = tables.begin(); t != tables.end(); ++t) {
		slave.setCallback(t->name.first, t->name.second, callback, t->filter);
	}
	slave.setXidCallback(boost::bind(&DBReader::XidEventCallback, boost::ref(*this), _1, cb));
	slave.init();
	slave.createDatabaseStructure();

	slave.get_remote_binlog(boost::bind(&DBReader::ReadBinlogCallback, boost::ref(*this)));
}

void DBReader::Stop()
{
	stopped = true;
	slave.close_connection();
}

void DBReader::EventCallback(const slave::RecordSet& event, BinlogEventCallback cb)
{
	last_event_when = event.when;
	
	SerializableBinlogEvent ev;
	ev.binlog_name = state.getMasterLogName();
	ev.binlog_pos = state.getMasterLogPos();
	ev.seconds_behind_master = GetSecondsBehindMaster();
	ev.unix_timestamp = long(time(NULL));
	ev.event = "IGNORE";

	if (sfilter.PassEvent(event.db_name, event.tbl_name, event.m_row)) {
		ev.database = event.db_name;
		ev.table = event.tbl_name;
		switch (event.type_event) {
			case slave::RecordSet::Update: ev.event = "UPDATE"; break;
			case slave::RecordSet::Delete: ev.event = "DELETE"; break;
			case slave::RecordSet::Write:  ev.event = "INSERT"; break;
			default: break;
		}
		ev.row = SlaveRowToSerializableRow(event.m_row);
	}
	else {
		// TEST: do not pass filtered events to ZMQ/TPWriter, this will not update binlog position
		return;
	}

	stopped = cb(ev);
}

void DBReader::XidEventCallback(unsigned int server_id, BinlogEventCallback cb)
{
	last_event_when = ::time(NULL);
	
	// send binlog position update event
	SerializableBinlogEvent ev;
	ev.binlog_name = state.getMasterLogName();
	ev.binlog_pos = state.getMasterLogPos();
	ev.seconds_behind_master = GetSecondsBehindMaster();
	ev.unix_timestamp = long(time(NULL));
	ev.event = "IGNORE";
	stopped = cb(ev);
}

bool DBReader::ReadBinlogCallback()
{
	return stopped != 0;
}

void DBReader::DumpTablesCallback(slave::RelayLogInfo &rli, const std::string &db_name, const std::string &tbl_name, 
	nanomysql::Connection &conn, std::map<std::string, std::pair<unsigned, slave::PtrField>> &filter, const nanomysql::fields_t &f, BinlogEventCallback cb)
{
	SerializableBinlogEvent ev;
	ev.binlog_name = "";
	ev.binlog_pos = 0;
	ev.database = db_name;
	ev.table = tbl_name;
	ev.event = "INSERT";
	ev.seconds_behind_master = GetSecondsBehindMaster();
	ev.unix_timestamp = long(time(NULL));
	ev.row.resize(f.size());

	for (auto i = filter.begin(); i != filter.end(); ++i)  {
		if (stopped) {
			break;
		}

		unsigned index = i->second.first;
		slave::PtrField field = i->second.second;

		std::map<std::string, nanomysql::field>::const_iterator z = f.find(field->getFieldName());
		field->unpacka(z->second.data);

		ev.row[index] = field->getFieldData();
	}

	if (!stopped && sfilter.PassEvent(db_name, tbl_name, ev.row)) {
		if (!stopped && cb(ev)) {
			stopped = true;
		}
	}

	if (stopped) {
		conn.close();
	}
}

unsigned DBReader::GetSecondsBehindMaster() const
{
	::time_t now = ::time(NULL);
	if (last_event_when >= now) {
		return 0;
	}
	return now - last_event_when;
}

} // replicator
