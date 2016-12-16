#ifndef REPLICATOR_REMOTEMON_H
#define REPLICATOR_REMOTEMON_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string>
#include <sstream>

namespace replicator {

class Graphite
{
public:
	Graphite(const std::string &host, unsigned port, const std::string &prefix) : host(host), port(port), prefix(prefix), 
		initialized(false), sock(-1), last_packet_time(0) {}

	~Graphite() { if (sock >= 0) close(sock); }

	template<typename T>
	void SendStat(const std::string &graph, T value)
	{
		Init();

		if (sock < 0) {
			return;
		}

		std::ostringstream oss;
		oss  << prefix << graph << " " << value << " " << ::time(NULL) << std::endl;

		sendto(sock, static_cast<const void *>(oss.str().c_str()), oss.str().length(), 0, (struct sockaddr*)&server, sizeof(server));

		last_packet_time = ::time(NULL);
	}

	::time_t GetLastPacketTime() const { return last_packet_time; }

	unsigned GetInterval() const { return 60; }

private:
	std::string host;
	unsigned port;
	std::string prefix;
	bool initialized;
	int sock;
	struct sockaddr_in server;
	::time_t last_packet_time;

	void Init()
	{
		if (initialized) {
			return;
		}
		initialized = true;

		InitSocket();
		InitPrefix();
	}

	void InitPrefix()
	{
		char hostname[256];

		gethostname(hostname, sizeof(hostname));
		const char *p = strchr(hostname, '.');
		if (p) {
			hostname[p - hostname] = '\0';
		}
		
		prefix.append(hostname);
		prefix.append(".replicatord.");
	}

	void InitSocket()
	{
		struct sockaddr_in client;

		sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock < 0) {
			return;
		}

		memset((char *) &client, 0, sizeof(client));
		client.sin_family = AF_INET;
		client.sin_addr.s_addr = htonl(INADDR_ANY);
		client.sin_port = 0;

		server.sin_family = AF_INET;
		server.sin_addr.s_addr = GetIpV4Addr(host);
		server.sin_port = htons(port);
		if (server.sin_addr.s_addr == 0) {
			std::cerr << "Host " << host << " not found";
			sock = -1;
			return;
		}

		if (bind(sock, (struct sockaddr*)&client, sizeof(client)) < 0) {
			std::cerr << "Could not bind socket: " << strerror(errno);
			sock = -1;
			return;
		}
	}

	unsigned GetIpV4Addr(const std::string &hostname) const
	{
		struct addrinfo hints = { 0 }, *servinfo;
		unsigned res = 0;

		hints.ai_family = AF_INET; // use AF_INET6 to force IPv6
		if (getaddrinfo(hostname.c_str(), NULL, &hints, &servinfo) == 0) {
			if (servinfo) {
				res = ((struct sockaddr_in *)servinfo->ai_addr)->sin_addr.s_addr;
			}
			freeaddrinfo(servinfo);
		}

		return res;
	}
};

} // replicator

#endif // REPLICATOR_REMOTEMON_H
