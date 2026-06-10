#include "epollClass.hpp"
#include "../Server/ServerClass.hpp"
#include "../Server/AroleClass/AroleClass.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

volatile sig_atomic_t isRunning = 1;

Epoll::Epoll(void) : _events(MIN_MAX), _active(1)
{
	_fd = epoll_create(1);
	if (_fd == -1)
		throw epollError();
}

Epoll::Epoll(Epoll const & other)
{
	(void)other;
}

Epoll::~Epoll(void)
{
	close(_fd);
	std::vector<Server*>::iterator it = _servers.begin();
	while (it != _servers.end())
	{
		delete *it;
		it = _servers.erase(it);
	}
}

Epoll & Epoll::operator=(Epoll const & other)
{
	(void)other;
	return (*this);
}

Epoll& Epoll::createEpoll(void)
{
	static Epoll epoll;
	return epoll;
}

int	Epoll::getFD(void) const
{
	return (this->_fd);
}

std::vector<struct epoll_event> const &	Epoll::getEvents(void) const
{
	return (this->_events);
}

int	Epoll::wait(void)
{
	int	events = epoll_wait(_fd, _events.data(), _events.size(), 60000);
	if (events == -1)
	{
		if (errno == EINTR)
			return (0);
		throw epollError();
	}
	return (events);
}

void	Epoll::add(Arole * role)
{
	Client *conv;
	
	if (epoll_ctl(_fd, EPOLL_CTL_ADD, role->getFD(), &role->getStructEvent()) == -1)
	{	
		if ((conv = dynamic_cast<Client *> (role)))
			conv->closeConnection(*this);
	}
}

void	Epoll::mod(Arole * role)
{
	Client *conv;
	
	if (epoll_ctl(_fd, EPOLL_CTL_MOD, role->getFD(), &role->getStructEvent()) == -1)
	{	
		if ((conv = dynamic_cast<Client *> (role)))
			conv->closeConnection(*this);
	}
}

void	Epoll::del(Arole * role)
{
	Client *conv;
	
	if (epoll_ctl(_fd, EPOLL_CTL_DEL, role->getFD(), NULL) == -1)
	{	
		if ((conv = dynamic_cast<Client *> (role)))
			conv->closeConnection(*this);
	}
}

void	Epoll::addServer(Server * serv)
{
	for (std::vector<Server*>::iterator it = _servers.begin(); it != _servers.end(); it++) {
		if (*it == serv)
			return ;
	}
	_servers.push_back(serv);
}

void	Epoll::run(void)
{
	std::cout << *this << std::endl;
	while (this->_active)
	{
		if (isRunning)
		{
			int num_events = wait();
			for (int i = 0; i < num_events; ++i)
			{
				Arole *role = static_cast<Arole *>(_events[i].data.ptr);
				if (role == NULL)
					continue ;
				role->handle_event(*this, _events[i].events);
			}
			Arole::launchActiveRoutine(*this);
			show();
		}
		else
			sigManager();
	}
}

void	Epoll::handleSigEvent(int signum)
{
	(void)signum;
	isRunning = 0;
}

void	Epoll::show(void)
{
	for (size_t i = 0; i < _servers.size(); i++)
	{
		std::cout << "Server " << i + 1 << ":" << std::endl;
		_servers[i]->allSocketDebug();
	}
}

void	Epoll::sigManager(void)
{
	do {
		std::cout << "SHOW | START | STOP | EXIT | exit" << std::endl;
		std::string event = safeInputClass::safeInput();
		if (event == "SHOW")
			show();
		else if (event == "EXIT")
		{
			this->_active = 0;
			return ;
		}
		else if (event == "START")
		{
			int numServ = 0;
			do {
				std::cout << "On wich Server you want to open a port" << std::endl;
				numServ = safeInputClass::safeInputNumber();
				if (numServ == -1)
					break ;
				if (numServ > 0 && (size_t)numServ <= _servers.size())
				{
					int numServUsed = 0;
					do {
						std::cout << "Enter the new port you want to open" << std::endl;
						int port = safeInputClass::safeInputPort();
						if (port == -1)
							break ;
						numServUsed = alreadyUsePort(port);
						if (numServUsed)
							std::cout << "The port " << port << " is already open on the Server number " << numServ << std::endl;
						else {
							_servers[numServ - 1]->start(port);
							std::cout << "The port " << port << " is now open on the Server " << numServ << std::endl;
						}
					} while (numServUsed);
					break ;
				}
				else
					std::cout << "Entrer a valid server number" << std::endl;
			} while (numServ != -1);
		}
		else if (event == "STOP")
		{
			int numServ = 0;
			do {
				std::cout << "On wich Server you want to stop a port" << std::endl;
				numServ = safeInputClass::safeInputNumber();
				if (numServ == -1)
					break ;
				if (numServ > 0 && (size_t)numServ <= _servers.size())
				{
					int numServUsed = 0;
					do {
						_servers[numServ - 1]->allSocketDebug();
						std::cout << "Enter the port you want to stop" << std::endl;
						int port = safeInputClass::safeInputPort();
						if (port == -1)
							break ;
						numServUsed = alreadyUsePort(port, numServ);
						if (!numServUsed)
							std::cout << "The port " << port << " is not open yet on the server" << std::endl;
						else {
							_servers[numServ - 1]->stop(port);
							std::cout << "The port " << port << " stoped" << std::endl;
						}
					} while (!numServUsed);
					break;
				}
				else
					std::cout << "Entrer a valid server number" << std::endl;
			} while (numServ != -1);
		}
		else if (event == "exit")
				isRunning = 1;
	} while (!isRunning);
	std::cout << "Le serveur reprend !" << std::endl;
}
	
int	Epoll::alreadyUsePort(unsigned short int port, int serv)
{
	if (serv == 0) {
		for (size_t i = 0; i < _servers.size(); i++) {
			if (_servers[i]->alreadyUsePort(port))
				return (i + 1);
		}
		return (0);
	}
	else {
		if (_servers[serv - 1]->alreadyUsePort(port))
			return (serv);
	}
	return (0);
}

std::ostream &	operator<<(std::ostream & o, Epoll const & other)
{
	o << other.getFD();
	return (o);
}
