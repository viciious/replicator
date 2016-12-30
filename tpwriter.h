#ifndef REPLICATOR_TPWRITER_H
#define REPLICATOR_TPWRITER_H

#include <string>
#include <map>
#include <vector>
#include <sys/time.h>
#include "serializable.h"

namespace replicator {

class TPWriter
{
public:
	virtual ~TPWriter() {};
	virtual bool Connect() = 0;
	virtual void Disconnect() = 0;
	virtual bool ReadBinlogPos(std::string &binlog_name, unsigned long &binlog_pos) = 0;
	virtual bool Sync(bool force = false) = 0;
	virtual bool BinlogEventCallback(const SerializableBinlogEvent &ev) = 0;
	virtual void Ping() = 0;

	// return values:
	// -1 for error
	// otherwise returns number of complete replies read from socket
	virtual int ReadReply() = 0;
	virtual int GetReplyCode() const = 0;
	virtual const char *GetReplyErrorMessage() const = 0;
	virtual bool DisconnectOnError() const = 0;

	typedef std::vector<unsigned> Tuple;

	virtual void AddTable(const std::string &db, const std::string &table, unsigned space, const Tuple &tuple, const Tuple &keys,
		const std::string &insert_call = "", const std::string &update_call = "", const std::string &delete_call = "") {
		TableMap &d = dbs[db];
		TableSpace &s = d[table];
		s.space = space;
		s.tuple = tuple;
		s.keys = keys;
		s.insert_call = insert_call;
		s.update_call = update_call;
		s.delete_call = delete_call;
	}

	virtual uint64_t Milliseconds() {
		struct timeval tp;
		::gettimeofday( &tp, NULL );
		if (!secbase) {
			secbase = tp.tv_sec;
			return tp.tv_usec / 1000;
		}
		return (uint64_t)(tp.tv_sec - secbase)*1000 + tp.tv_usec / 1000;
	}

protected:
	class TableSpace
	{
	public:
		TableSpace() : space(0), insert_call(""), update_call(""), delete_call("") {}
		unsigned space;
		Tuple tuple;
		Tuple keys;
		std::string insert_call;
		std::string update_call;
		std::string delete_call;
	};

	uint64_t secbase;

	typedef std::map<std::string, TableSpace> TableMap;
	typedef std::map<std::string, TableMap> DBMap;
	DBMap dbs;
};

}

#endif // REPLICATOR_TPWRITER_H
