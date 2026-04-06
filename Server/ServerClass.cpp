#include "ServerClass.hpp"
#include <exception>
#include <netinet/in.h>
#include <unistd.h>

volatile sig_atomic_t isRunning = 1;

Server::Server(Server const & other) {(void)other;}
Server& Server::operator=(Server const & other) {(void)other; return(*this);}


Server::Server(void) : _active(1)
{
	Socket *sock = new Socket;
	_sockets.insert(std::pair<unsigned short int, Socket *>(ntohs(sock->getAddr().getPort()), sock));
	sock->start(_epoll);
}

Server::Server(int nbSocket, std::vector<unsigned short int> & ports) : _active(1)
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

size_t		Server::getOpenSocket(void) const {return (this->_sockets.size());}
Epoll const &	Server::getEpoll(void) const {return (this->_epoll);}
int		Server::getIsActive(void) const {return (this->_active);}

void	Server::run(void)
{
	std::cout << *this << std::endl;
	while (this->_active)
	{
		if (isRunning)
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
			allSocketDebug();
		}
		else
			sigManager();
	}
}

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
		_sockets[port]->stop(_epoll);
		_sockets.erase(port);
	}
}	

void	Server::allSocketDebug(void) const
{
	for (std::map<unsigned short int, Socket *>::const_iterator it = _sockets.begin(); it != _sockets.end(); ++it)
	{
		if (it->second->getActiveStatus())
			std::cout << *(it->second) << "\tactive" << std::endl;
		else
			std::cout << *(it->second) << "\tinactive" << std::endl;
	}
	std::cout << std::endl;
}

void	Server::handleSigEvent(int signum)
{
	(void)signum;
	isRunning = 0;
}

std::ostream &	operator<<(std::ostream & o, Server const &serv)
{
	o << serv.getOpenSocket() << " socket(s) ouvert(s) sur ce serveur" << std::endl \
		<< "FD du Epoll manager = " << serv.getEpoll();
	return (o);
}

void	Server::sigManager(void)
{
	std::string event;

	std::cout << "SHOW | START | STOP | EXIT" << std::endl;
	getline(std::cin, event);
	if (event == "SHOW")
		this->allSocketDebug();
	else if (event == "EXIT")
	{
		this->_active = 0;
		return ;
	}
	else if (event == "START")
	{
		std::cout << "Enter the new port you want to open" << std::endl;
		getline(std::cin, event);
		this->start(atoi(event.c_str()));
	}
	else if (event == "STOP")
	{	
		std::cout << "Enter the port you want to stop" << std::endl;
		getline(std::cin, event);
		this->stop(atoi(event.c_str()));
	}
	isRunning = 1;
	std::cout << "Le serveur reprend !" << std::endl;
}
