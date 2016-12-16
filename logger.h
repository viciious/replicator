#ifndef REPLICATOR_LOGGER_H
#define REPLICATOR_LOGGER_H

#include <unistd.h>
#include <time.h>
#include <iostream>
#include <sstream>

namespace replicator {

// originally found on stackoverflow

class Logger : public std::streambuf
{
public:
	Logger( std::basic_ios< char >& out, char level ) : out(out), sink(), newline(true), level(level)
	{
		sink = out.rdbuf(this);
	}
	~Logger()
	{
		out.rdbuf(sink);
	}
	std::streambuf *rdsink() const {
		return sink;
	}
	void rdsink(std::streambuf *sink) {
		this->sink = sink;
	}

protected:
	int_type overflow( int_type m = traits_type::eof() )
	{
		if( traits_type::eq_int_type( m, traits_type::eof() ) )
			return sink->pubsync() == -1 ? m: traits_type::not_eof(m);

		if( newline )
		{
			std::ostream str( sink );

			char timestr[64];
			::time_t t;
			struct ::tm bdtime;
			t = ::time(NULL);
			localtime_r(&t, &bdtime);
			::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &bdtime);

			if( !(str << "[" << timestr << "] " << level << " " ) ) {
				return traits_type::eof(); // error
			}
		}

		newline = traits_type::to_char_type( m ) == '\n';

		int_type res = sink->sputc( m );
		if (newline) {
			sink->pubsync();
		}
		return res;
	}

private:
	Logger( const Logger& );
	Logger& operator=( const Logger& ); // not copyable

	std::basic_ios< char >& out;
	std::streambuf* sink;
	bool newline;
	char level;
};

} // replicator

#endif // REPLICATOR_LOGGER_H
