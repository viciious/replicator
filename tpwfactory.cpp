#include "tpwfactory.h"
#include "tpwriter15.h"
#include "tpwriter16.h"

namespace replicator {

TPWriter *TPWFactory::NewTPWriter(int version, const std::string &dsn, unsigned int port, unsigned binlog_key_space, unsigned binlog_key,
	unsigned connect_retry, unsigned sync_retry, bool disconnect_on_error) {
	if (version == 15) {
		return new TPWriter15(dsn, port, binlog_key_space, binlog_key, connect_retry, sync_retry, disconnect_on_error);
	}
	if (version == 16) {
		return new TPWriter16(dsn, binlog_key_space, binlog_key, connect_retry, sync_retry, disconnect_on_error);
	}
	return NULL;
}

}

