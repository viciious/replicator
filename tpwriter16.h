#ifndef REPLICATOR_TPWRITER16_H
#define REPLICATOR_TPWRITER16_H

#include <string>
#include <map>
#include <vector>
#include <tarantool/tarantool.h>
#include <tarantool/tnt_net.h>
#include <tarantool/tnt_opt.h>
#include "tpwriter.h"

namespace replicator {

class TPWriter16: public TPWriter
{
public:
	TPWriter16(const std::string &dsn, unsigned binlog_key_space, unsigned binlog_key,
		unsigned connect_retry, unsigned sync_retry, bool disconnect_on_error);
	virtual ~TPWriter16();

	bool Connect();
	void Disconnect();
	bool ReadBinlogPos(std::string &binlog_name, unsigned long &binlog_pos);
	bool Sync(bool force = false) { return true; };
	bool BinlogEventCallback(const SerializableBinlogEvent &ev) { return true; };
	void Ping();

	int ReadReply() { return 0; };
	int GetReplyCode() const { return reply_server_code; };
	const char *GetReplyErrorMessage() const { return reply_error_msg; };
	bool DisconnectOnError() const { return disconnect_on_error; }

private:
    static const unsigned int SND_BUFSIZE = 102400;
    static const unsigned int RCV_BUFSIZE = 10240;

	std::string dsn;
	uint32_t binlog_key_space;
	uint32_t binlog_key;
    unsigned connect_retry;
    unsigned sync_retry;
	bool disconnect_on_error;

    ::time_t next_connect_attempt; /* seconds */
    uint64_t next_sync_attempt; /* milliseconds */
    uint64_t next_ping_attempt; /* milliseconds */

	struct tnt_stream *tnt;

	int reply_server_code;
	const char *reply_error_msg;

};

}

#endif // REPLICATOR_TPWRITER16_H
