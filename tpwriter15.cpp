#include <sstream>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/any.hpp>
#include <boost/function.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <lib/tp.1.5.h>
#include <lib/session.h>

#include <zmq.h>
#include <zmq_utils.h>

#include <sys/time.h>

#include "tpwriter15.h"
#include "serializable.h"

namespace replicator {

TPWriter15::TPWriter15(const std::string &host, unsigned int port,
	uint32_t binlog_key_space, uint32_t binlog_key, unsigned connect_retry, unsigned sync_retry,
	bool disconnect_on_error) :
host(host), port(port), binlog_key_space(binlog_key_space), binlog_key(binlog_key),
binlog_name(""), binlog_pos(0), seconds_behind_master(0), last_unix_timestamp(0),
connect_retry(connect_retry), sync_retry(sync_retry),
next_connect_attempt(0), next_sync_attempt(0), next_ping_attempt(0),
last_synced_binlog_name(""), last_synced_binlog_pos(0), disconnect_on_error(disconnect_on_error),
reply_bytes(0), reply_server_code(0), reply_error_msg("")
{

}

bool TPWriter15::Connect()
{
	// connect to tarantool
	if (::time(NULL) < next_connect_attempt) {
		::sleep(1);
		return false;
	}

	::tbses *s = &sess;
	::tb_sesinit(s);
	::tb_sesset(s, TB_HOST, host.c_str());
	::tb_sesset(s, TB_PORT, port);
	::tb_sesset(s, TB_SENDBUF, TPWriter15::SND_BUFSIZE);
	::tb_sesset(s, TB_READBUF, 0);
	::tb_sesset(s, TB_RECVTM, 10);
	::tb_sesset(s, TB_SENDTM, 10000);
	::tb_sesset(s, TB_NOBLOCK, 1);

	int rc = ::tb_sesconnect(s);
	if (rc == -1)
	{
		std::cout << "Could not connect to Tarantool" << std::endl;
		::tb_sesfree(&sess);
		next_connect_attempt = ::time(NULL) + connect_retry;
		return false;
	}

	std::cout << "Connected to Tarantool at " << host << ":" << port << std::endl;
	next_sync_attempt = 0;

	return true;
}

TPWriter15::~TPWriter15()
{
	::tb_sesfree(&sess);
}

bool TPWriter15::ReadBinlogPos(std::string &binlog_name, unsigned long &binlog_pos)
{
	::tbses *s = &sess;

	// read initial binlog pos
	char buf[256];

	::tp req;
	::tp_init(&req, buf, sizeof(buf), NULL, NULL);
	::tp_select(&req, binlog_key_space, 0, 0, 1);
	::tp_tuple(&req);
	::tp_field(&req, (const char *)&binlog_key, sizeof(binlog_key));
	Send(buf, ::tp_used(&req));
	Sync();

	int64_t r = 0;
	while ((r = ReadReply()) == 0) { ::usleep(5000); }

	if (r < 0) {
		std::cout << "Reading binlog position from Tarantool failed (errno: " << s->errno_ << ")" << std::endl;
		next_connect_attempt = ::time(NULL) + connect_retry;
		return false;
	}

	if (reply_server_code) {
		std::cout << "Reading binlog position from Tarantool failed: " << reply_error_msg << std::endl;
		next_connect_attempt = ::time(NULL) + connect_retry;
		return false;
	}

	next_ping_attempt = Milliseconds() + TPWriter15::PING_TIMEOUT;

	// send initial binlog position to the main thread
	SerializableBinlogEvent ev;
	if (::tp_next(&reply) <= 0 || ::tp_tuplecount(&reply) < 3) {
		binlog_name = "";
		binlog_pos = 0;
		this->binlog_name = "";
		this->binlog_pos = 0;
		return true;
	}

	std::string tmp;

	::tp_nextfield(&reply);

	::tp_nextfield(&reply);
	tmp = "";
	tmp.append(::tp_getfield(&reply), ::tp_getfieldsize(&reply));
	binlog_name = tmp;

	::tp_nextfield(&reply);
	tmp = "";
	tmp.append(::tp_getfield(&reply), ::tp_getfieldsize(&reply));
	std::istringstream iss(tmp);
	iss >> binlog_pos;

	if (::tp_nextfield(&reply)) {
		tmp = "";
		tmp.append(::tp_getfield(&reply), ::tp_getfieldsize(&reply));
		std::istringstream iss(tmp);
		iss >> this->seconds_behind_master;
	}

	this->binlog_name = binlog_name;
	this->binlog_pos = binlog_pos;
	this->last_unix_timestamp = time(NULL);

	return true;
}

void TPWriter15::Disconnect()
{
	::tb_sesfree(&sess);
}

void TPWriter15::Ping()
{
	char buf[100];
	::tp req;
	::tp_init(&req, buf, sizeof(buf), NULL, NULL);
	::tp_ping(&req);
	Send(buf, ::tp_used(&req));
}

void TPWriter15::AddTable(const std::string &db, const std::string &table, unsigned space, const Tuple &tuple, const Tuple &keys,
	const std::string &insert_call, const std::string &update_call, const std::string &delete_call)
{
	TableMap &d = dbs[db];
	TableSpace &s = d[table];
	s.space = space;
	s.tuple = tuple;
	s.keys = keys;
	s.insert_call = insert_call;
	s.update_call = update_call;
	s.delete_call = delete_call;
}

void TPWriter15::SaveBinlogPos()
{
	char buf[1024];
	std::ostringstream oss1, oss2, oss3;

	if (last_synced_binlog_name == binlog_name && last_synced_binlog_pos == binlog_pos) {
		return;
	}
	if (binlog_name == "") {
		return;
	}

	// query
	::tp req;
	::tp_init(&req, buf, sizeof(buf), NULL, NULL);
	::tp_insert(&req, binlog_key_space, 0);
	::tp_tuple(&req);
	::tp_field(&req, (const char *)&binlog_key, sizeof(binlog_key));
	::tp_field(&req, binlog_name.c_str(), binlog_name.length());

	oss1 << binlog_pos;
	::tp_field(&req, oss1.str().c_str(), oss1.str().length());

	oss2 << seconds_behind_master;
	::tp_field(&req, oss2.str().c_str(), oss2.str().length());

	oss3 << last_unix_timestamp;
	::tp_field(&req, oss3.str().c_str(), oss3.str().length());
	Send(buf, ::tp_used(&req));

	last_synced_binlog_name = binlog_name;
	last_synced_binlog_pos = binlog_pos;
}

bool TPWriter15::BinlogEventCallback(const SerializableBinlogEvent &ev)
{
	char buf[TPWriter15::SND_BUFSIZE];
	::tp req;

	// spacial case event "IGNORE", which only updates binlog position
	// but doesn't modify any table data

	DBMap::const_iterator i = dbs.find(ev.database);
	if (ev.event != "IGNORE" && i != dbs.end()) {
		const TableMap &d = i->second;
		TableMap::const_iterator j = d.find(ev.table);
		if (j != d.end()) {
			const TableSpace &s = j->second;
			const Tuple &t = ev.event == "DELETE" ? s.keys : s.tuple;

			// add Tarantool request
			::tp_init(&req, buf, sizeof(buf), NULL, NULL);
			if (ev.event == "DELETE") {
				if (s.delete_call.empty()) {
					::tp_delete(&req, s.space, 0);
				}
				else {
					::tp_call(&req, 0, s.delete_call.c_str(), s.delete_call.length());
				}
			} else if (ev.event == "INSERT") {
				if (s.insert_call.empty()) {
					::tp_insert(&req, s.space, 0);
				}
				else {
					::tp_call(&req, 0, s.insert_call.c_str(), s.insert_call.length());
				}
			} else if (ev.event == "UPDATE") {
				if (s.update_call.empty()) {
					::tp_insert(&req, s.space, 0);
				}
				else {
					::tp_call(&req, 0, s.update_call.c_str(), s.update_call.length());
				}
			} else {
				throw std::range_error("Uknown binlog event: " + ev.event);
				return false;
			}

			::tp_tuple(&req);

			for (Tuple::const_iterator it = t.begin(); it != t.end(); ++it) {
				unsigned col = *it;
				const SerializableValue &v = ev.row[col];
				const boost::any &a = *v;
				const std::string &vs = v.value_string();

				try {
					if (a.type() == typeid(int)) {
						int32_t ival = boost::any_cast<int>(a);
						::tp_field(&req, (const char *)&ival, sizeof(ival));
					} else if (a.type() == typeid(unsigned int)) {
						uint32_t ival = boost::any_cast<unsigned int>(a);
						::tp_field(&req, (const char *)&ival, sizeof(ival));
					} else if (a.type() == typeid(long long)) {
						int64_t ival = int64_t(boost::any_cast<long long>(a));
						::tp_field(&req, (const char *)&ival, sizeof(ival));
					} else if (a.type() == typeid(unsigned long long)) {
						uint64_t ival = uint64_t(boost::any_cast<unsigned long long>(a));
						::tp_field(&req, (const char *)&ival, sizeof(ival));
					} else 	if (a.type() == typeid(long)) {
						uint32_t ival = uint32_t(boost::any_cast<long>(a));
						::tp_field(&req, (const char *)&ival, sizeof(ival));
					} else if (a.type() == typeid(float) || a.type() == typeid(double)) {
						union {
							uint32_t i;
							float f;
						} uiv;
						uiv.f = a.type() == typeid(float) ? boost::any_cast<float>(a) : float(boost::any_cast<double>(a));
						::tp_field(&req, (const char *)&uiv.i, sizeof(uiv.i));
					}
					else if (a.type() == typeid(void)) {
						::tp_field(&req, "", 0);
					}
					else {
						::tp_field(&req, vs.c_str(), vs.length());
					}
				}
				catch (boost::bad_any_cast &ex) {
					throw std::range_error(std::string("Typecasting error for column: ") + ex.what());
					return true;
				}
			}

			Send(buf, ::tp_used(&req));
		}
	}

	if (ev.binlog_name != "") {
		binlog_name = ev.binlog_name;
		binlog_pos = ev.binlog_pos;
	}
	last_unix_timestamp = time(NULL);
	seconds_behind_master = ev.seconds_behind_master + last_unix_timestamp - ev.unix_timestamp;

#if 0
	std::ostringstream oss;
	boost::archive::binary_oarchive oa(oss);
	oa << ev;
	std::cout << oss.str() << std::endl;
#endif

	return false;
}

// blocking send
ssize_t TPWriter15::Send(void *buf, ssize_t bytes)
{
	int64_t r;
	ssize_t total = 0;
	do {
		r = ::tb_sessend(&sess, static_cast<char *>(buf), bytes);
		if (r > 0) {
			total += r;
		}
	} while (r == -1 && (sess.errno_ == EWOULDBLOCK || sess.errno_ == EAGAIN));

	if (r == -1) {
		return -1;
	}

	return total;
}

// non-blocking receive
ssize_t TPWriter15::Recv(void *buf, ssize_t bytes)
{
	int64_t r = ::tb_sesrecv(&sess, static_cast<char *>(buf), bytes, 0);
	if (r == -1 && (sess.errno_ == EWOULDBLOCK || sess.errno_ == EAGAIN)) {
		return 0;
	}
	if (r == -1) {
		throw std::runtime_error("Lost connection to Tarantool");
		return -1;
	}
	return r;
}

bool TPWriter15::Sync(bool force)
{
	int64_t r = 0;

	if (next_ping_attempt == 0 || Milliseconds() > next_ping_attempt) {
		force = true;
		next_ping_attempt = Milliseconds() + TPWriter15::PING_TIMEOUT;
		Ping();
	}

	if (force || next_sync_attempt == 0 || Milliseconds() >= next_sync_attempt) {
		SaveBinlogPos();

		next_sync_attempt = Milliseconds() + sync_retry;
		do {
			r = ::tb_sessync(&sess);
		} while (r == -1 && (sess.errno_ == EWOULDBLOCK || sess.errno_ == EAGAIN));
	}

	if (r == -1) {
		throw std::runtime_error("Lost connection to Tarantool");
	}
	return r != -1;
}

int TPWriter15::ReadReply(void)
{
	ssize_t len = ::tp_reqbuf(reply_buf, reply_bytes);
	if (len > 0) {
		if (reply_bytes + len > ssize_t(sizeof(reply_buf))) {
			throw std::runtime_error("TPWriter::ReadReply: increase reply_buf size at least up to: " + (reply_bytes + len));
			return -1;
		}
		len = Recv(reply_buf + reply_bytes, len);
		if (len < 0) {
			return -1;
		}
		if (len == 0) {
			return 0;
		}
		reply_bytes += len;
		len = ::tp_reqbuf(reply_buf, reply_bytes);
	}

	// got at least one full reply
	if (len <= 0) {
		ssize_t reply_len = reply_bytes + len;
		if (reply_len == 0) {
			return 0;
		}

		::memcpy(reply_copy, reply_buf, reply_len);
		::memmove(reply_buf, reply_buf + reply_len, sizeof(reply_buf) - reply_len);
		reply_bytes -= reply_len;

		::tp_init(&reply, reply_copy, reply_len, NULL, NULL);
		reply_server_code = ::tp_reply(&reply);
		reply_error_msg = reply_server_code != 0 ? ::tp_replyerror(&reply) : "";
		return 1;
	}

	return 0;
}

int TPWriter15::GetReplyCode() const
{
	return reply_server_code;
}

const char *TPWriter15::GetReplyErrorMessage() const
{
	return reply_error_msg;
}

uint64_t TPWriter15::Milliseconds()
{
	struct timeval tp;
	::gettimeofday( &tp, NULL );
	if (!secbase) {
		secbase = tp.tv_sec;
		return tp.tv_usec / 1000;
	}
	return (uint64_t)(tp.tv_sec - secbase)*1000 + tp.tv_usec / 1000;
}

}
