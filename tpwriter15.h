#ifndef REPLICATOR_TPWRITER15_H
#define REPLICATOR_TPWRITER15_H

#include <string>
#include <map>
#include <vector>
#include <lib/tp.1.5.h>
#include <lib/session.h>
#include "tpwriter.h"
#include "serializable.h"

namespace replicator {

class TPWriter15: public TPWriter
{
public:
	TPWriter15(const std::string &host, unsigned int port, unsigned binlog_key_space, unsigned binlog_key,
		unsigned connect_retry, unsigned sync_retry, bool disconnect_on_error);
	virtual ~TPWriter15();

	bool Connect();
	void Disconnect();
	bool ReadBinlogPos(std::string &binlog_name, unsigned long &binlog_pos);
	bool Sync(bool force = false);
	bool BinlogEventCallback(const SerializableBinlogEvent &ev);
	void Ping();

	int ReadReply();
	int GetReplyCode() const;
	const char *GetReplyErrorMessage() const;
	bool DisconnectOnError() const { return disconnect_on_error; }

	void AddTable(const std::string &db, const std::string &table, unsigned space, const Tuple &tuple, const Tuple &keys,
		const std::string &insert_call = "", const std::string &update_call = "", const std::string &delete_call = "");

private:
	static const unsigned int BINLOG_POS_KEY = 1;
	static const unsigned int PING_TIMEOUT = 5000;

	static const unsigned int SND_BUFSIZE = 102400;
	static const unsigned int RCV_BUFSIZE = 10240;

	std::string host;
	uint32_t binlog_key_space;
	uint32_t binlog_key;
	std::string binlog_name;
	unsigned long binlog_pos;
	unsigned long seconds_behind_master;
	unsigned long last_unix_timestamp;
	unsigned port;
	unsigned connect_retry;
	unsigned sync_retry;
	::time_t next_connect_attempt; /* seconds */
	uint64_t next_sync_attempt; /* milliseconds */
	uint64_t next_ping_attempt; /* milliseconds */
	std::string last_synced_binlog_name;
	unsigned long last_synced_binlog_pos;
	::tbses sess;
	bool disconnect_on_error;

	// blocking send
	ssize_t Send(void *buf, ssize_t bytes);

	// non-blocking receive
	ssize_t Recv(void *buf, ssize_t bytes);

	void SaveBinlogPos();

	uint64_t Milliseconds();

	char reply_buf[RCV_BUFSIZE];
	char reply_copy[sizeof(reply_buf)];
	size_t reply_bytes;
	::tp reply;
	int reply_server_code;
	const char *reply_error_msg;
	uint64_t secbase;

	typedef std::map<std::string, TableSpace> TableMap;
	typedef std::map<std::string, TableMap> DBMap;
	DBMap dbs;

};

}

#endif // REPLICATOR_TPWRITER15_H
