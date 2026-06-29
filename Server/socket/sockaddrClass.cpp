# include "sockaddrClass.hpp"
# include <arpa/inet.h>
# include <netinet/in.h>
# include <netdb.h>
# include <string.h>
# include <sstream>
# include <sys/socket.h>

Sockaddr::Sockaddr(void)
{
	memset(&_addr, 0, sizeof(_addr));
	_addr.sin_family = AF_INET;
	_addr.sin_port = htons(0);
	_addr.sin_addr.s_addr = htonl(INADDR_ANY);
}

Sockaddr::Sockaddr(Sockaddr const & other)
{
	memset(&_addr, 0, sizeof(_addr));
	_addr.sin_family = other.getFamily();
	_addr.sin_port = other.getPort();
	_addr.sin_addr.s_addr = other.getAddr();
}

Sockaddr::~Sockaddr(void) {}

Sockaddr& Sockaddr::operator=(Sockaddr const & other)
{
	if (this == &other)
		return (*this);
	memset(&_addr, 0, sizeof(_addr));
	_addr.sin_family = other.getFamily();
	_addr.sin_port = other.getPort();
	_addr.sin_addr.s_addr = other.getAddr();
	return (*this);
}

struct sockaddr_in const &	Sockaddr::getStruct(void) const {return(_addr);}
unsigned short int		Sockaddr::getFamily(void) const {return(_addr.sin_family);}
unsigned short int		Sockaddr::getPort(void) const {return(_addr.sin_port);}
struct in_addr const &		Sockaddr::getAddrStruct(void) const {return(_addr.sin_addr);}
in_addr_t const &		Sockaddr::getAddr(void) const {return(_addr.sin_addr.s_addr);}

void			Sockaddr::setPort(unsigned short int Hport) {_addr.sin_port = htons(Hport);}

void			Sockaddr::resolve(std::string const & host, uint16_t port)
{
	struct addrinfo		hints;
	struct addrinfo		*res = NULL;
	std::ostringstream	portStr;
	const char		*node = host.empty() ? NULL : host.c_str();
	int			ret;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (host.empty())
		hints.ai_flags = AI_PASSIVE;
	portStr << port;
	ret = getaddrinfo(node, portStr.str().c_str(), &hints, &res);
	if (ret != 0 || res == NULL)
		throw badAddr(std::string("getaddrinfo: ") + gai_strerror(ret));
	memcpy(&_addr, res->ai_addr, sizeof(struct sockaddr_in));
	freeaddrinfo(res);
}

Sockaddr::badAddr::badAddr(std::string const & msg) throw() : _msg(msg) {}
Sockaddr::badAddr::~badAddr(void) throw() {}
const char *	Sockaddr::badAddr::what(void) const throw() {return (_msg.c_str());}

std::ostream &	operator<<(std::ostream & o, Sockaddr const & addr)
{
	uint32_t	ip = ntohl(addr.getAddr());

	o << "Family=" << addr.getFamily() << "\tPort=" << ntohs(addr.getPort()) << "\tAdresse=" << ((ip >> 24) & 0xFF) << "." << ((ip >> 16) & 0xFF) << "." << ((ip >> 8) & 0xFF) << "." << (ip & 0xFF);
	return (o);
}
