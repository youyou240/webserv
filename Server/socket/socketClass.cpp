#include "socketClass.hpp"
#include <netinet/in.h>
#include <sys/epoll.h>

Socket::Socket(void) : _active(false)
{
	_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (_sockfd == -1)
		throw badExec();
	if (fcntl(_sockfd, F_SETFL, O_NONBLOCK) == -1)
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

Socket::Socket(unsigned short int port) : _active(false)
{
	_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (_sockfd == -1)
		throw badExec();
	if (fcntl(_sockfd, F_SETFL, O_NONBLOCK) == -1)
    		throw badExec();
	_addr.setPort(port);
	if (bind(_sockfd, (struct sockaddr *)&_addr.getStruct(), sizeof(struct sockaddr)) == -1)
		throw badExec();
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

int			Socket::getSockfd(void) const {return(_sockfd);}
bool			Socket::getActiveStatus(void) const {return (_active);}
Sockaddr const &	Socket::getAddr(void) const {return(_addr);}
Acceptor const &	Socket::getAcceptor(void) const {return (_acceptor);}

void 		Socket::start(Epoll & manager)
{
	_acceptor.start(manager);
	_active = true;
	std::cout << "Le socket sur le port " << ntohs(_addr.getPort()) << " est ouvert !" << std::endl;
}

void 		Socket::stop(Epoll & manager)
{
	_acceptor.stop(manager);
	_active = false;
	std::cout << "Le socket sur le port " << ntohs(_addr.getPort()) << " est ferme !";
}

void		Socket::closeAll(Epoll & manager) {_acceptor.closeAll(manager);}

std::ostream &	operator<<(std::ostream & o, Socket const & other)
{
	o << other.getAddr() << std::endl << other.getAcceptor() << std::endl;
	return (o);
}
