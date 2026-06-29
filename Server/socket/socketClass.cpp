# include "socketClass.hpp"
# include <netinet/in.h>
# include <sys/epoll.h>
# include <sys/socket.h>

Socket::Socket(std::string const & host, uint16_t port)
{
	int	opt = 1;

	_addr.resolve(host, port);
	_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (_sockfd == -1)
		throw badExec();
	if (fcntl(_sockfd, F_SETFL, O_NONBLOCK) == -1)
	{
		close(_sockfd);
		throw badExec();
	}
	if (setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
	{
		close(_sockfd);
		throw badExec();
	}
	if (bind(_sockfd, (struct sockaddr *)&_addr.getStruct(), sizeof(struct sockaddr)) == -1)
	{
		close(_sockfd);
		throw badExec();
	}
	_acceptor.setFD(_sockfd);
	if (listen(_sockfd, SOMAXCONN) == -1)
	{
		close(_sockfd);
		throw badExec();
	}
}

Socket::Socket(Socket const & other) {(void)other;}

Socket::~Socket(void) {}

Socket& Socket::operator=(Socket const & other)
{
	if (this == &other)
		return (*this);
	return (*this);
}

void	Socket::addServerConfig(const ServerConfig * cfg) {_acceptor.addServerConfig(cfg);}

int			Socket::getSockfd(void) const {return(_sockfd);}
Sockaddr const &	Socket::getAddr(void) const {return(_addr);}
Acceptor const &	Socket::getAcceptor(void) const {return (_acceptor);}

void 		Socket::start(Epoll & manager)
{
	_acceptor.start(manager);
	std::cout << "Le socket sur le port " << ntohs(_addr.getPort()) << " est ouvert !" << std::endl;
}

void 		Socket::stop(Epoll & manager)
{
	_acceptor.closeAll(manager);
	std::cout << "Le socket sur le port " << ntohs(_addr.getPort()) << " est ferme !" << std::endl;
}

std::ostream &	operator<<(std::ostream & o, Socket const & other)
{
	o << other.getAddr() << "\t" << other.getAcceptor();
	return (o);
}
