#include "ServerClass.hpp"
#include <netinet/in.h>
#include <unistd.h>

Server::Server(Server const & other) {(void)other;}
Server& Server::operator=(Server const & other) {(void)other; return(*this);}


Server::Server(void)
{
	Socket *sock = new Socket;
	_sockets.insert(std::pair<unsigned short int, Socket *>(ntohs(sock->getAddr().getPort()), sock));
	sock->start(_epoll);
}

Server::Server(int nbSocket, std::vector<unsigned short int> & ports)
{
	if (nbSocket <= 0 || nbSocket > 10)
		throw toMuchSocket();

	for (int i = 0; i < nbSocket; ++i)
	{
		Socket *sock;
		if (ports.empty() || static_cast<long unsigned int>(i) >= ports.size())
			sock = new Socket;
		else
			sock = new Socket(ports[i]);
		_sockets.insert(std::pair<unsigned short int, Socket *>(ntohs(sock->getAddr().getPort()), sock));
		sock->start(_epoll);
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
		sock->closeAll(_epoll);
		delete sock;
	}
}

size_t	Server::getOpenSocket(void) const {return (this->_sockets.size());}
Epoll const &	Server::getEpoll(void) const {return (this->_epoll);}

void	Server::run(void)
{
	std::cout << *this << std::endl;
	while (true)
	{
		int num_events = _epoll.wait();
		for (int i = 0; i < num_events; ++i)
		{
			Arole *role = static_cast<Arole *>(_epoll.getEvents()[i].data.ptr);
			if (role == NULL)
				continue ;
			role->handle_event(_epoll);
		}
		Arole::launchActiveRoutine(_epoll);
	}
}

void	Server::start(void)
{
	for (std::map<unsigned short int, Socket *>::iterator it = _sockets.begin(); it != _sockets.end(); ++it)
	{	
		if (!it->second->getActiveStatus())
			it->second->start(_epoll);
	}
}


void	Server::start(unsigned short int port)
{
	if (!_sockets[port]->getActiveStatus())
		_sockets[port]->start(_epoll);
}

void	Server::stop(void)
{
	for (std::map<unsigned short int, Socket *>::iterator it = _sockets.begin(); it != _sockets.end(); ++it)
	{	
		if (it->second->getActiveStatus())
			it->second->stop(_epoll);
	}
}

void	Server::stop(unsigned short int port)
{
	if (_sockets[port]->getActiveStatus())
		_sockets[port]->stop(_epoll);
}

void	Server::exit(unsigned short int port)
{
	_sockets[port]->closeAll(_epoll);
	_sockets.erase(port);
}

std::ostream &	operator<<(std::ostream & o, Server const &serv)
{
	o << serv.getOpenSocket() << " socket(s) ouvert(s) sur ce serveur" << std::endl \
		<< "FD du Epoll manager = " << serv.getEpoll();
	return (o);
}
