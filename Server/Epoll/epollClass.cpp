#include "epollClass.hpp"
#include <sys/epoll.h>

Epoll::Epoll(void) : _events(MIN_MAX)
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
}

Epoll & Epoll::operator=(Epoll const & other)
{
	(void)other;
	return (*this);
}

int	Epoll::getFD(void) const
{
	return (this->_fd);
}

std::vector<struct epoll_event> const &	Epoll::getEvents(void) const
{
	return (this->_events);
}

void	Epoll::closeFD(void)
{
	close(_fd);
}

int	Epoll::wait(void)
{
	int	events = epoll_wait(_fd, _events.data(), MIN_MAX, 60000);
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

std::ostream &	operator<<(std::ostream & o, Epoll const & other)
{
	o << other.getFD();
	return (o);
}
