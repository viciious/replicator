#ifndef REPLICATOR_DBREADER_H
#define REPLICATOR_DBREADER_H

#include <vector>
#include <string>
#include <utility>

#include <boost/function.hpp>

#include <Slave.h>
#include <DefaultExtState.h>
#include <nanomysql.h>

#include "serializable.h"
#include "simplefilter.h"

namespace replicator {

typedef unsigned long BinlogPos;
typedef boost::function<bool (const SerializableBinlogEvent &ev)> BinlogEventCallback;

struct DBTable
{
	DBTable()
		{
		};

	DBTable(const std::string db_name, const std::string tbl_name, std::vector<std::string> filter) : 
		name(db_name, tbl_name), filter(filter)
		{
		};

	std::pair<std::string, std::string> name;
	std::vector<std::string> filter;
};

class DBReader
{
public:
	DBReader (const std::string &host, const std::string &user, const std::string &password, unsigned int port = 3306, unsigned int connect_retry = 60);
	~DBReader();

	void AddTable(const std::string &db, const std::string &table, const std::vector<std::string> &columns);
	void AddFilterPredicate(const std::string &db, const std::string &tbl, const SimplePredicate &pred);
	void DumpTables(std::string &binlog_name, BinlogPos &binlog_pos, BinlogEventCallback f);
	void ReadBinlog(const std::string &binlog_name, BinlogPos binlog_pos, BinlogEventCallback cb);
	void Stop();

	void EventCallback(const slave::RecordSet& event, BinlogEventCallback f);
	void DummyEventCallback(const slave::RecordSet& event) {};
	bool ReadBinlogCallback();
	void XidEventCallback(unsigned int server_id, BinlogEventCallback cb);
	void DumpTablesCallback(slave::RelayLogInfo &rli, const std::string &db_name, const std::string &tbl_name, 
		nanomysql::Connection &conn, std::map<std::string, std::pair<unsigned, slave::PtrField>> &filter, const nanomysql::fields_t &f, BinlogEventCallback cb);

	unsigned GetSecondsBehindMaster() const;

private:
	typedef std::vector<DBTable> TableList;

	slave::MasterInfo masterinfo;
	slave::DefaultExtState state;
	slave::Slave slave;
	TableList tables;
	SimpleFilter sfilter;
	bool stopped;

	::time_t last_event_when;
};

 } // replicator

#endif // REPLICATOR_DBREADER_H
