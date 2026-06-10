# include "sockaddrClass.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/socket.h>

Sockaddr::Sockaddr(void)
{
	_addr.sin_family = AF_INET;
	_addr.sin_port = htons(0);
	_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bzero(&(_addr.sin_zero), 8);
}

Sockaddr::Sockaddr(Sockaddr const & other)
{
	_addr.sin_family = other.getFamily();
	_addr.sin_port = other.getPort();
	_addr.sin_addr.s_addr = other.getAddr();
	bzero(&(_addr.sin_zero), 8);
}

Sockaddr::~Sockaddr(void) {}

Sockaddr& Sockaddr::operator=(Sockaddr const & other)
{
	if (this == &other)
		return (*this);
	_addr.sin_family = other.getFamily();
	_addr.sin_port = other.getPort();
	_addr.sin_addr.s_addr = other.getAddr();
	bzero(&(_addr.sin_zero), 8);
	return (*this);
}

struct sockaddr_in const &	Sockaddr::getStruct(void) const {return(_addr);}
unsigned short int		Sockaddr::getFamily(void) const {return(_addr.sin_family);}
unsigned short int		Sockaddr::getPort(void) const {return(_addr.sin_port);}
struct in_addr const &		Sockaddr::getAddrStruct(void) const {return(_addr.sin_addr);}
in_addr_t const &		Sockaddr::getAddr(void) const {return(_addr.sin_addr.s_addr);}

void			Sockaddr::setPort(unsigned short int Hport) {_addr.sin_port = htons(Hport);}

std::ostream &	operator<<(std::ostream & o, Sockaddr const & addr)
{
	o << "Family=" << addr.getFamily() << "\tPort=" << ntohs(addr.getPort()) << "\tAdresse=" << inet_ntoa(addr.getAddrStruct());
	return (o);
}

