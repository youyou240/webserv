# include "sockaddrClass.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <strings.h>

Sockaddr::Sockaddr(void)
{
	_addr.sin_family = AF_INET;
	_addr.sin_port = htons(0);
	_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bzero(&(_addr.sin_zero), 8);
}

Sockaddr::Sockaddr(Sockaddr & other)
{
	_addr.sin_family = other.getFamily();
	_addr.sin_port = other.getPort();
	_addr.sin_addr.s_addr = other.getAddr();
	bzero(&(_addr.sin_zero), 8);
}

Sockaddr::~Sockaddr(void) {}

Sockaddr& Sockaddr::operator=(Sockaddr & other)
{
	if (this == &other)
		return (*this);
	_addr.sin_family = other.getFamily();
	_addr.sin_port = other.getPort();
	_addr.sin_addr.s_addr = other.getAddr();
	bzero(&(_addr.sin_zero), 8);
	return (*this);
}

short unsigned int &	Sockaddr::getFamily(void) {return(_addr.sin_family);}
unsigned short int &	Sockaddr::getPort(void) {return(_addr.sin_port);}
struct in_addr &	Sockaddr::getAddrStruct(void) {return(_addr.sin_addr);}
in_addr_t &		Sockaddr::getAddr(void) {return(_addr.sin_addr.s_addr);}

std::ostream &	operator<<(std::ostream & o, Sockaddr & addr)
{
	o << "Family=" << addr.getFamily() << "\tPort=" << addr.getPort() << "\tAdresse=" << inet_ntoa(addr.getAddrStruct()) << std::endl;
	return (o);
}

