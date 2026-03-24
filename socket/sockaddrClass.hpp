# pragma once

# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <strings.h>
# include <ostream>

class Sockaddr {

	struct sockaddr_in _addr;

public:
	Sockaddr(void);
	Sockaddr(Sockaddr const &);
	~Sockaddr(void);

	Sockaddr& operator=(Sockaddr const &);

	short int &		getFamily(void) const;
	unsigned short int &	getPort(void) const;
	struct in_addr &	getAddrStruct(void) const;
	in_addr_t &		getAddr(void) const;
};

std::ostream &	operator<<(std::ostream &, Sockaddr const &);
