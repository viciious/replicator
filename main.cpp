#include <time.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <signal.h>

#include <boost/bind.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include <zmq.h>
#include <zmq_utils.h>

#include <libconfig.h++>

#include "dbreader.h"
#include "tpwfactory.h"
#include "serializable.h"
#include "logger.h"
#include "remotemon.h"

// =========

namespace replicator {

static const char *default_pid_filename = "/var/run/replicatord.pid";
static const char *default_log_filename = "/var/log/replicatord.log";
static const char *default_config_filename = "/usr/local/etc/replicatord.cfg";

static volatile bool is_halted = false;
static volatile bool is_term = false;
static TPWriter *tpwriter = NULL;
static DBReader *dbreader = NULL;
static Graphite *graphite = NULL;

static void *ZMQContext = NULL;
static void *ZMQTpSocket = NULL;
static void *ZMQTpThread = NULL;

static void *ZMQWdSocket = NULL;
static void *ZMQWdThread = NULL;

static void tpwrite_main(void *arg);
static void watchdog_main(void *arg);
static void halt(void);

static bool start_zmq(unsigned watchdog_timeout)
{
	int opti;
	int rc;

	ZMQContext = zmq_ctx_new();
	if (ZMQContext == NULL) {
		return false;
	}

	// tarantool
	//
	ZMQTpSocket = zmq_socket(ZMQContext, ZMQ_PAIR);

	// set high water mark
	opti = 10000;
	zmq_setsockopt(ZMQTpSocket, ZMQ_SNDHWM, &opti, sizeof(opti));
	rc = zmq_bind(ZMQTpSocket, "inproc://tp");
	if (rc) {
		return false;
	}

	// spawn tp thread
	ZMQTpThread = zmq_threadstart(tpwrite_main, NULL);
	if (ZMQTpThread == NULL) {
		return false;
	}

	// watchdog
	//
	ZMQWdSocket = zmq_socket(ZMQContext, ZMQ_PAIR);
	rc = zmq_bind(ZMQWdSocket, "inproc://wd");
	if (rc) {
		return false;
	}
	ZMQWdThread = zmq_threadstart(watchdog_main, ((void*)((intptr_t)watchdog_timeout)));

	return true;
}

static void send_zmq_event(void *socket, const SerializableBinlogEvent &ev)
{
	std::ostringstream oss;
	boost::archive::binary_oarchive oa(oss);
	oa << ev;
	zmq_send(socket, oss.str().c_str(), oss.str().length()+1, 0);
}

static bool poll_zmq_event(void *socket, unsigned timeout, BinlogEventCallback f)
{
	zmq_pollitem_t items [] = {
		{ socket, 0, ZMQ_POLLIN, 0 },
	};

	int polled = zmq_poll(items, 1, timeout);
	if (polled < 0) {
		// error polling
		return false;
	}

	bool done = false;

	if (polled > 0 && (items[0].revents & ZMQ_POLLIN)) {
		while (polled-- > 0) {
			// restart binlog
			zmq_msg_t msg;
			zmq_msg_init(&msg);

			if (zmq_msg_recv(&msg, socket, 0) < 0) {
				zmq_msg_close(&msg);
				return false;
			}

			std::string buf;
			buf.append((char *)zmq_msg_data(&msg), zmq_msg_size(&msg));
			zmq_msg_close(&msg);

			SerializableBinlogEvent ev;
			std::istringstream iss(buf);
			boost::archive::binary_iarchive ia(iss);
			ia >> ev;
			done = done || f(ev);
			if (done) break;
		}
	}

	return done;
}

static void close_zmq()
{
	if (ZMQTpThread != NULL) {
		zmq_threadclose(ZMQTpThread);
		ZMQTpThread = NULL;
	}
	if (ZMQWdThread != NULL) {
		zmq_threadclose(ZMQWdThread);
		ZMQWdThread = NULL;
	}


	if (ZMQTpSocket != NULL) {
		zmq_close(ZMQTpSocket);
		ZMQTpSocket = NULL;
	}
	if (ZMQWdSocket != NULL) {
		zmq_close(ZMQWdSocket);
		ZMQWdSocket = NULL;
	}

	if (ZMQContext != NULL) {
		zmq_ctx_term(ZMQContext);
		ZMQContext = NULL;
	}
}

// ===============

static void tpwrite_run(void *ZMQTpSocket)
{
	SerializableBinlogEvent ev_connect;
	SerializableBinlogEvent ev_disconnect;

	ev_connect.event = "CONNECT";
	ev_disconnect.event = "DISCONNECT";

	bool connected = true;

	while (!is_term && connected) {
		if (!tpwriter->Connect()) {
			continue;
		}

		// send initial binlog position to the main thread

		try {
			if (!tpwriter->ReadBinlogPos(ev_connect.binlog_name, ev_connect.binlog_pos)) {
				tpwriter->Disconnect();
				continue;
			}

			send_zmq_event(ZMQTpSocket, ev_connect);

			while(true) {
				if (is_term || !connected) {
					break;
				}

				connected = poll_zmq_event(ZMQTpSocket, 100, boost::bind(&TPWriter::BinlogEventCallback, boost::ref(*tpwriter), _1)) == false;
				if (connected) {
					connected = tpwriter->Sync();
				}

				while (!is_term && connected) {
					int r = tpwriter->ReadReply();
					if (r == 0) {
						break;
					}
					if (r < 0) {
						connected = false;
						break;
					}
					int code = tpwriter->GetReplyCode();
					if (code) {
						std::cerr << "Tarantool error: " << tpwriter->GetReplyErrorMessage() << " (code: " << code << ")" << std::endl;
						connected = !tpwriter->DisconnectOnError();
					}
				}
			}
		}
		catch (std::range_error& ex) {
			connected = false;
			std::cout << ex.what() << std::endl;
			// loop exit
		}
		catch (std::exception& ex) {
			std::cout << ex.what() << std::endl;
			tpwriter->Disconnect();
			send_zmq_event(ZMQTpSocket, ev_disconnect);
			// reconnect
		}
	}

	tpwriter->Disconnect();
	send_zmq_event(ZMQTpSocket, ev_disconnect);
}

static void tpwrite_main(void *arg)
{
	void *ZMQTpSocket = zmq_socket(ZMQContext, ZMQ_PAIR);

	if (!ZMQTpSocket) {
		kill(getpid(), SIGTERM);
		return;
	}

	// set high water mark
	int opti = 10000;
	zmq_setsockopt(ZMQTpSocket, ZMQ_RCVHWM, &opti, sizeof(opti));

	if (zmq_connect(ZMQTpSocket, "inproc://tp")) {
		kill(getpid(), SIGTERM);
		return;
	}

	tpwrite_run(ZMQTpSocket);

	zmq_close(ZMQTpSocket);
}

// ====================

static unsigned seconds_behind_master;
static unsigned max_seconds_behind_master;

#ifdef ZMQ_ENABLE_RB
static unsigned zalloc_count;
static unsigned max_zalloc_count;
#endif

static void ping_watchdog()
{
	SerializableBinlogEvent ev;
	ev.event = "PING";
	send_zmq_event(ZMQWdSocket, ev);
}

static void update_stats()
{
	time_t now;

	if (!dbreader) {
		return;
	}

	ping_watchdog();

	now = ::time(NULL);

	seconds_behind_master = dbreader->GetSecondsBehindMaster();
	if (seconds_behind_master > max_seconds_behind_master) max_seconds_behind_master = seconds_behind_master; 

#ifdef ZMQ_ENABLE_RB
	zalloc_count = zmq_get_alloc_count();
	if (zalloc_count > max_zalloc_count) max_zalloc_count = zalloc_count;
#endif

	if (graphite) {
		if (now > graphite->GetLastPacketTime() + graphite->GetInterval()) {
			graphite->SendStat("seconds_behind_master", seconds_behind_master);
			graphite->SendStat("max_seconds_behind_master", max_seconds_behind_master);
			max_seconds_behind_master = seconds_behind_master;

#ifdef ZMQ_ENABLE_RB
			graphite->SendStat("zmq_allocs_total", zalloc_count);
			graphite->SendStat("zmq_allocs_total_max", max_zalloc_count);
			max_zalloc_count = zalloc_count;
#endif
		}
	}
}

// ====================
// watchdog

static time_t last_event_timestamp;

static bool watchdog_ev_callback(const SerializableBinlogEvent &ev)
{
	last_event_timestamp = ::time(NULL);
	return false;
}

static void watchdog_main(void *arg)
{
	unsigned timeout = (intptr_t)arg;
	void *ZMQWdSocket = zmq_socket(ZMQContext, ZMQ_PAIR);

	if (!ZMQWdSocket || zmq_connect(ZMQWdSocket, "inproc://wd")) {
		kill(getpid(), SIGTERM);
		return;
	}

	last_event_timestamp = ::time(NULL);

	while (!is_term) {
		poll_zmq_event(ZMQWdSocket, 1000, watchdog_ev_callback);

		if (last_event_timestamp + timeout < ::time(NULL)) {
			std::cerr << "Ping timeout detected by watchdog: committing suicide now. Restarting." << std::endl;
			kill(getpid(), SIGKILL);
			break;
		}
	}

	zmq_close(ZMQWdSocket);
}

// ====================

static bool tpread_zmq_callback(const SerializableBinlogEvent &ev, std::string &TpBinlogName, unsigned long &TpBinlogPos, 
	bool &disconnect, bool &read)
{
	read = true;
	disconnect = ev.event == "DISCONNECT";
	TpBinlogName = ev.binlog_name;
	TpBinlogPos = ev.binlog_pos;
	return false;
}

static bool tpread_get_binlogpos(unsigned timeout, std::string &TpBinlogName, unsigned long &TpBinlogPos, bool &disconnect)
{
	bool read = false;
	poll_zmq_event(ZMQTpSocket, timeout, boost::bind(tpread_zmq_callback, _1, boost::ref(TpBinlogName), boost::ref(TpBinlogPos), 
		boost::ref(disconnect), boost::ref(read))); 
	return read;
}

static bool dbread_callback(const SerializableBinlogEvent &ev, std::string &TpBinlogName, unsigned long &TpBinlogPos, bool &disconnect)
{
	if (is_term) {
		return true;
	}

	update_stats();

	if (tpread_get_binlogpos(0, TpBinlogName, TpBinlogPos, disconnect)) {
		return true;
	}
	send_zmq_event(ZMQTpSocket, ev);
	return false;
}

static void init(libconfig::Config &cfg)
{
	unsigned watchdog_timeout = 60;

	try
	{
		const libconfig::Setting& root = cfg.getRoot();

		// read Mysql settings
		{
			const libconfig::Setting &mysql = root["mysql"];

			unsigned port = 3306;
			unsigned connect_retry = 15;
			mysql.lookupValue("port", port);
			mysql.lookupValue("connect_retry", connect_retry);
			mysql.lookupValue("watchdog_timeout", watchdog_timeout);

			dbreader = new DBReader((const char *)mysql["host"], (const char *)mysql["user"], (const char *)mysql["password"], 
				port, connect_retry);
		}

		// read Tarantool config
		{
			const libconfig::Setting &tarantool = root["tarantool"];

			unsigned port = 33013;
			unsigned connect_retry = 15;
			unsigned sync_retry = 1000;
			bool disconnect_on_error = false;
			tarantool.lookupValue("port", port);
			tarantool.lookupValue("connect_retry", connect_retry);
			tarantool.lookupValue("sync_retry", sync_retry);
			tarantool.lookupValue("disconnect_on_error", disconnect_on_error);

			tpwriter = TPWFactory::NewTPWriter(15, (const char *)tarantool["host"], port, (unsigned)tarantool["binlog_pos_space"],
				(unsigned)tarantool["binlog_pos_key"], connect_retry, sync_retry, disconnect_on_error);
		}

		// read Mysql to Tarantool mappings (each table maps to a single Tarantool space)
		{
			const libconfig::Setting &mappings = root["mappings"];
			int count = mappings.getLength();

			for (int i = 0; i < count; i++) {
				const libconfig::Setting &mapping = mappings[i];
				const std::string database((const char *)mapping["database"]);
				const std::string table((const char *)mapping["table"]);
				std::string insert_call = "";
				std::string update_call = "";
				std::string delete_call = "";
				unsigned space((unsigned)mapping["space"]);
				std::vector<std::string> columns;
				TPWriter::Tuple tuple, keys;

				// read columns tuple
				{
					const libconfig::Setting &columns_ = mapping["columns"];
					int count = columns_.getLength();
					for (int i = 0; i < count; i++) {
						columns.push_back((const char *)columns_[i]);
						tuple.push_back(i);
					}
				}

				// read key Tarantool fields we'll use for DELETE requests
				{
					const libconfig::Setting &keys_ = mapping["key_fields"];
					int count = keys_.getLength();
					for (int i = 0; i < count; i++) {
						unsigned k = keys_[i];
						if (k >= columns.size()) {
							std::cerr << "Bad key field id: " << k << " (should be less than " << columns.size() << ")" << std::endl;
							exit(EXIT_FAILURE);
						}
						keys.push_back(k);
					}
				}

				// lookup LUA procedures we can call instead of issuing DML requests
				{
					mapping.lookupValue("insert_call", insert_call);
					mapping.lookupValue("update_call", update_call);
					mapping.lookupValue("delete_call", delete_call);
				}

				if (mapping.exists("simple_filter"))
				{
					const libconfig::Setting &columns = mapping["columns"];
					const libconfig::Setting &simple_filter = mapping["simple_filter"];
					const std::string column((const char *)simple_filter["column"]);

					bool negate;
					simple_filter.lookupValue("negate", negate);

					std::vector<int64_t> values;

					const libconfig::Setting &values_ = simple_filter["values"];
					for (int i = 0; i < values_.getLength(); i++) {
						values.push_back((long long)values_[i]);
					}

					unsigned column_num = columns.getLength();
					for (int i = 0; i < columns.getLength(); i++) {
						if (column == (const char *)columns[i]) {
							column_num = i;
							break;
						}
					}

					if (column_num == columns.getLength()) {
						std::cerr << "Bad filter field: " << column << std::endl;
						exit(EXIT_FAILURE);
					}

					SimplePredicate pred(column_num, negate, values);
					dbreader->AddFilterPredicate(database, table, pred);
				}

				dbreader->AddTable(database, table, columns);
				tpwriter->AddTable(database, table, space, tuple, keys, insert_call, update_call, delete_call);
			}
		}

		// read graphite config
		{
			std::string host("graphite.i");
			unsigned port = 2003;
			std::string prefix("prefix.");

			if (root.exists("graphite"))
			{
				const libconfig::Setting &graphite_ = root["graphite"];
				graphite_.lookupValue("host", host);
				graphite_.lookupValue("port", port);
				graphite_.lookupValue("prefix", prefix);
			}

			graphite = new Graphite(host, port, prefix);
		}
	}
	catch(const libconfig::SettingNotFoundException &nfex)
	{
		std::cerr << nfex.what() << " :: " << nfex.getPath() << std::endl;
		exit(EXIT_FAILURE);
	}

	start_zmq(watchdog_timeout);

	ping_watchdog();
}

static void main_loop()
{
	std::string TpBinlogName;
	unsigned long TpBinlogPos;
	bool disconnected;

	// read initial binlog pos from Tarantool
	while (!is_term) {
		tpread_get_binlogpos(100, TpBinlogName, TpBinlogPos, disconnected);
		if (disconnected) {
			ping_watchdog();
			::sleep(1);
			continue;
		}

		if (is_term) {
			break;
		}

		try {
			BinlogEventCallback cb = boost::bind(dbread_callback, _1,
				boost::ref(TpBinlogName), boost::ref(TpBinlogPos), boost::ref(disconnected));

			if (TpBinlogName == "") {
				std::cout << "Tarantool reported null binlog position. Dumping tables..." << std::endl;
				dbreader->DumpTables(TpBinlogName, TpBinlogPos, cb);
			}

			std::cout << "Reading binlogs (" << TpBinlogName << ", " << TpBinlogPos << ")..." << std::endl;

			dbreader->ReadBinlog(TpBinlogName, TpBinlogPos, cb);
		} catch (std::exception& ex) {
			std::cerr << "Error in reading binlogs: " << ex.what() << std::endl;

			halt();
		}
	}
}

static void shutdown()
{
	close_zmq();

	if (dbreader) {
		// sighandler protection
		DBReader *dbreader_ = dbreader;
		dbreader = NULL;
		delete dbreader_;
	}

	if (graphite) {
		delete graphite;
		graphite = NULL;
	}
}

static void sighandler(int sig)
{
	is_halted = false;
	is_term = true;
	if (dbreader) {
		dbreader->Stop();
	}
}

static void halt(void)
{
	std::cerr << "Terminating" << std::endl;

	is_halted = false;
	is_term = true;
	if (dbreader) {
		dbreader->Stop();
	}
}

}

static replicator::Logger *ol, *el;
static std::streambuf *ol_sink, *el_sink;
static std::ofstream *flog;

static std::string log_filename(replicator::default_log_filename);
static std::string pid_filename(replicator::default_pid_filename);

static void writepidtofile()
{
	// write pid to file
	std::ofstream fpid(pid_filename);
	fpid << getpid();
	fpid.flush();
	fpid.close();
}

static void removepidfile()
{
	unlink(pid_filename.c_str());
}

static void openlogfile()
{
	flog = new std::ofstream(log_filename, std::ofstream::app);

	// redirect cout and cerr streams, appending timestamps and log levels
	ol = new replicator::Logger(std::cout, 'I');
	el = new replicator::Logger(std::cerr, 'E');

	ol_sink = ol->rdsink();
	el_sink = el->rdsink();

	// redirect loggers to file
	ol->rdsink(flog->rdbuf());
	el->rdsink(flog->rdbuf());
}

static void closelogfile()
{
	if (flog == NULL) {
		return;
	}

	flog->flush();
	flog->close();

	delete flog;
	flog = NULL;

	// restore streams
	ol->rdsink(ol_sink);
	el->rdsink(el_sink);

	delete ol;
	delete el;

	ol = NULL;
	el = NULL;
}

static void sighup_handler(int sig)
{
	closelogfile();
	openlogfile();
	std::cout << "Caught SIGHUP, continuing..." << std::endl;
}

int main(int argc, char** argv)
{
	bool print_usage = false;
	std::string config_name(replicator::default_config_filename);

	int c;
	while (-1 != (c = ::getopt(argc, argv, "c:l:i:zp")))
	{
		switch (c)
		{
			case 'c': config_name = optarg; break;
			case 'p': print_usage = true; break;
			case 'l': log_filename = optarg; break;
			case 'i': pid_filename = optarg; break;
			default: print_usage = true; break;
		}
	}

	if (print_usage) {
		std::cout 
			<< "Usage: " << argv[0] << " [-c <config_name>]" << " [-l <log_name>]"<< " [-i <pid_name>]" << " [-p]" << std::endl
			<< " -c configuration file (" << config_name << ")" << std::endl
			<< " -p print usage" << std::endl
			<< " -l log filename (" << log_filename << ")" << std::endl
			<< " -i pid filename (" << pid_filename << ")" << std::endl
			;
		return 1;
	}

	writepidtofile();
	atexit(removepidfile);

	openlogfile();
	atexit(closelogfile);

	libconfig::Config cfg;

	// Read the file. If there is an error, report it and exit.
	try
	{
		cfg.readFile(config_name.c_str());
	}
	catch(const libconfig::FileIOException &fioex)
	{
		std::cerr << "I/O error while reading file " << config_name << std::endl;
		return EXIT_FAILURE;
	}
	catch(const libconfig::ParseException &pex)
	{
		std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
			<< " - " << pex.getError() << std::endl;
		return EXIT_FAILURE;
	}

	signal(SIGINT, replicator::sighandler);
	signal(SIGTERM, replicator::sighandler);
	signal(SIGHUP, sighup_handler);

	replicator::init(cfg);

	replicator::main_loop();

	replicator::shutdown();

	while (replicator::is_halted) ::sleep(1);

	return 0;
}
