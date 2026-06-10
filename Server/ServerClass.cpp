#include "ServerClass.hpp"
#include "socket/socketClass.hpp"
#include "AroleClass/AroleClass.hpp"
#include "../Epoll/epollClass.hpp"
#include <exception>
#include <netinet/in.h>
#include <unistd.h>

Server& Server::operator=(Server const & other) {(void)other; return(*this);}


Server::Server(Epoll & epoll) : _epoll(epoll)
{
	Socket *sock = new Socket;
	_sockets.insert(std::pair<unsigned short int, Socket *>(ntohs(sock->getAddr().getPort()), sock));
	sock->start(_epoll);
}

Server::Server(int nbSocket, std::vector<unsigned short int> & ports, Epoll & epoll) : _epoll(epoll)
{
	if (nbSocket <= 0 || nbSocket > 10)
		throw toMuchSocket();

	for (int i = 0; i < nbSocket; ++i)
	{
		try {
			Socket *sock;
			if (ports.empty() || static_cast<long unsigned int>(i) >= ports.size())
				sock = new Socket;
			else
				sock = new Socket(ports[i]);
			_sockets.insert(std::pair<unsigned short int, Socket *>(ntohs(sock->getAddr().getPort()), sock));
			sock->start(_epoll);
		} catch (std::exception &e) {
			std::cout << e.what() << std::endl;
		}
	}
}

Server::~Server(void)
{
	std::map<unsigned short int, Socket *>::iterator it = _sockets.begin();
	while (it != _sockets.end())
	{
		Socket *sock = it->second;
		it++;
		_sockets.erase(ntohs(sock->getAddr().getPort()));
		sock->stop(_epoll);
		delete sock;
	}
}

int		Server::alreadyUsePort(unsigned short int port)
{
	std::map<unsigned int short, Socket *>::iterator it = _sockets.find(port);
	if (it == _sockets.end())
		return (0);
	return (1);
}

size_t		Server::getOpenSocket(void) const {return (this->_sockets.size());}


void	Server::start(unsigned short int port)
{
	std::map<unsigned int short, Socket *>::iterator it = _sockets.find(port);
	if (it == _sockets.end())
	{		
		try {
			Socket *sock = new Socket(port);
			_sockets.insert(std::pair<unsigned short int, Socket *>(ntohs(sock->getAddr().getPort()), sock));
			sock->start(_epoll);
		} catch (std::exception &e) {
			std::cout << e.what() << std::endl;
		}
	}
}

void	Server::stop(unsigned short int port)
{
	std::map<unsigned int short, Socket *>::iterator it = _sockets.find(port);
	if (it != _sockets.end())
	{
		Socket *sock = it->second;
		sock->stop(_epoll);
		delete sock;
		_sockets.erase(port);
	}
}

void	Server::allSocketDebug(void) const
{
	for (std::map<unsigned short int, Socket *>::const_iterator it = _sockets.begin(); it != _sockets.end(); ++it)
			std::cout << *(it->second) << "\tactive" << std::endl;
	std::cout << std::endl;
}


std::ostream &	operator<<(std::ostream & o, Server const &serv)
{
	o << serv.getOpenSocket() << " socket(s) ouvert(s) sur ce serveur" << std::endl;
	serv.allSocketDebug();
	return (o);
}

