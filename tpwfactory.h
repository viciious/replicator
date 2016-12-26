#ifndef REPLICATOR_TPWFACTORY_H
#define REPLICATOR_TPWFACTORY_H

#include "tpwriter.h"

namespace replicator {

class TPWFactory {
public:
	static TPWriter *NewTPWriter(int version, const std::string &host, unsigned int port, unsigned binlog_key_space, unsigned binlog_key,
		unsigned connect_retry = 15, unsigned sync_retry = 1000, bool disconnect_on_error = false);
};

}

#endif
