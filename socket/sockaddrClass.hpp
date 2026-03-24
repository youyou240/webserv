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
	Sockaddr(Sockaddr &);
	~Sockaddr(void);

	Sockaddr& operator=(Sockaddr &);

	struct sockaddr_in&	getStruct(void);
	short unsigned int &	getFamily(void);
	unsigned short int &	getPort(void);
	struct in_addr &	getAddrStruct(void);
	in_addr_t &		getAddr(void);
};

std::ostream &	operator<<(std::ostream &, Sockaddr &);
