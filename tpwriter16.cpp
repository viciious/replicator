#include <sstream>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/any.hpp>
#include <boost/function.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include <zmq.h>
#include <zmq_utils.h>

#include "tpwriter16.h"

namespace replicator {

TPWriter16::TPWriter16(const std::string &dsn,
		uint32_t binlog_key_space, uint32_t binlog_key, unsigned connect_retry, unsigned sync_retry,
		bool disconnect_on_error) :
dsn(dsn),
binlog_key_space(binlog_key_space), binlog_key(binlog_key),
connect_retry(connect_retry), sync_retry(sync_retry), disconnect_on_error(disconnect_on_error),
next_connect_attempt(0), next_sync_attempt(0), next_ping_attempt(0) {
	struct ::timeval tmout_connect;
	memset(&tmout_connect, 0, sizeof(tmout_connect));

	tmout_connect.tv_sec = 3;
	tmout_connect.tv_usec = 0;

	tnt = ::tnt_net(NULL); // Allocating stream
	::tnt_set(tnt, TNT_OPT_URI, dsn.c_str()); // Setting URI
	::tnt_set(tnt, TNT_OPT_SEND_BUF, TPWriter16::SND_BUFSIZE);
	::tnt_set(tnt, TNT_OPT_RECV_BUF, TPWriter16::RCV_BUFSIZE);
	::tnt_set(tnt, TNT_OPT_TMOUT_CONNECT, &tmout_connect);
}

TPWriter16::~TPWriter16()
{
	::tnt_stream_free(tnt);
}

bool TPWriter16::Connect()
{
	// connect to tarantool
	if (::time(NULL) < next_connect_attempt)
	{
		::sleep(1);
		return false;
	}

	std::cout << "Connecting to Tarantool 1.6" << std::endl;

	int rc = ::tnt_connect(tnt); // Initialize stream and connect to Tarantool
	if (rc == -1)
	{
		std::cout << "Could not connect to Tarantool" << std::endl;
		next_connect_attempt = ::time(NULL) + connect_retry;
		return false;
	}

	std::cout << "Connected to Tarantool" << std::endl;
	next_sync_attempt = 0;
	next_connect_attempt = ::time(NULL) + connect_retry;
	return true;
}

void TPWriter16::Disconnect()
{
	::tnt_close(tnt);
}

void TPWriter16::Ping()
{
	::tnt_ping(tnt); // Send ping request
}

bool TPWriter16::ReadBinlogPos(std::string &binlog_name, unsigned long &binlog_pos)
{
	int rc;
	struct tnt_reply r;
	::tnt_reply_init(&r);

	rc = ::tnt_flush(tnt);
	if (rc == -1) {
		//std::cout << "Reading binlog position from Tarantool failed: " << reply_error_msg << std::endl;
		next_connect_attempt = ::time(NULL) + connect_retry;
		return false;
	}

	struct tnt_stream *key = ::tnt_object(NULL);
	::tnt_object_add_array(key, 1);
	::tnt_object_add_int(key, binlog_key);

	rc = ::tnt_select(tnt, binlog_key_space, 0, 1, 0, 0, key);
	rc = ::tnt_flush(tnt);

	rc = tnt->read_reply(tnt, &r);

	std::cout << r.buf << std::endl;

	tnt_reply_free(&r);

	::tnt_stream_free(key);
	return true;
}

}
