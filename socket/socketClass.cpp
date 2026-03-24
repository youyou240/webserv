#include "socketClass.hpp"
#include <sys/epoll.h>

Socket::Socket(void) : _fdTable(500)
{
	_sockfd = socket(AF_INET, SOCK_STREAM, SOCK_NONBLOCK);
	if (_sockfd == -1)
		throw badExec();
	if (bind(_sockfd, (struct sockaddr *)&_addr.getStruct(), sizeof(struct sockaddr)) == -1)
		throw badExec();
	_serv.getFD() = _sockfd;
	_serv.getStructEvent().data.fd = _sockfd;
	_manager.add(_serv);
}

Socket::Socket(unsigned short int port) : _fdTable(500)
{
	_sockfd = socket(AF_INET, SOCK_STREAM, SOCK_NONBLOCK);
	if (_sockfd == -1)
		throw badExec();
	_addr.getPort() = htons(port);
	if (bind(_sockfd, (struct sockaddr *)&_addr.getStruct(), sizeof(struct sockaddr)) == -1)
		throw badExec();
	_serv.getFD() = _sockfd;
	_serv.getStructEvent().data.fd = _sockfd;
	_manager.add(_serv);
}

Socket::Socket(Socket const & other) {(void)other;}

Socket::~Socket(void) {this->closeAll();}

Socket& Socket::operator=(Socket const & other)
{
	if (*this == other)
		return (*this);
	this->closeAll();
	_sockfd = other.getSockfd();
	_addr = other.getAddr();
	_manager = other.getManager();
	return (*this);
}

int		Socket::getSockfd(void) const {return(_sockfd);}
Sockaddr&	Socket::getAddr(void) const {return(_addr);}
Epoll&		Socket::getManager(void) const {return (_manager);}

void Socket::run(void)
{
	std::cout << "Le socket sur le port " << ntohs(_addr.getPort()) << "est ouvert" << std::endl;

	while (true)
	{
		int num_events = _manager.wait();
		for (int i = 0; i < num_events; ++i)
		{
			Arole *role = static_cast<Arole *>(_manager.getEvents()[i].data.ptr);
			if (role != NULL)
				continue ;
			role->handle_event(_manager);
		}
	}
}
	
void	Socket::closeAll(void)
{
	_serv.closeAll(_manager);
	_manager.close();
}

std::ostream &	operator<<(std::ostream & o, Socket const & other)
{
	o << "sockFd=" << other.getSockfd() << std::endl << other.getAddr() << std::endl << other.getManager() << std::endl;
	return (o);
}
