#include "socketClass.hpp"
#include <sys/epoll.h>

Socket::Socket(void)
{
	_sockfd = socket(AF_INET, SOCK_STREAM, SOCK_NONBLOCK);
	if (_sockfd == -1)
		throw badExec();
	if (bind(_sockfd, (struct sockaddr *)&_addr.getStruct(), sizeof(struct sockaddr)) == -1)
		throw badExec();
	_serv.getFD() = _sockfd;
	_serv.getStructEvent().data.fd = _sockfd;
	_serv.setFdTable(_fdTable);
	_manager.add();
}

Socket::Socket(unsigned short int port)
{
	_sockfd = socket(AF_INET, SOCK_STREAM, SOCK_NONBLOCK);
	if (_sockfd == -1)
		throw badExec();
	_addr.getPort() = htons(port);
	if (bind(_sockfd, (struct sockaddr *)&_addr.getStruct(), sizeof(struct sockaddr)) == -1)
		throw badExec();
	_serv.getFD() = _sockfd;
	_serv.getStructEvent().data.fd = _sockfd;
	_serv.setFdTable(_fdTable);
	_manager.add();
	
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
