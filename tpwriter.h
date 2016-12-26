#ifndef REPLICATOR_TPWRITER_H
#define REPLICATOR_TPWRITER_H

#include <string>
#include <map>
#include <vector>
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
		const std::string &insert_call = "", const std::string &update_call = "", const std::string &delete_call = "") = 0;

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
};

}

#endif // REPLICATOR_TPWRITER_H
