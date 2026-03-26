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

	struct sockaddr_in const&	getStruct(void) const;
	short unsigned int &	getFamily(void) const;
	unsigned short int &	getPort(void) const;
	struct in_addr const &	getAddrStruct(void) const;
	in_addr_t const &	getAddr(void) const;

	void			setPort(unsigned int short);
};

std::ostream &	operator<<(std::ostream &, Sockaddr &);
