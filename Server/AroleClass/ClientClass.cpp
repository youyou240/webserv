# include "AroleClass.hpp"
#include <sys/epoll.h>

std::map<int, std::map<int, Client *> > Arole::_activeClient;
unsigned int Arole::_client = 0;

Client::Client(int fd, int servfd) : Arole(fd), _acceptorfd(servfd), _bytes_send(0), _complete(false), _lastActivity(time(NULL))
{
	this->_client += 1;
	_activeClient[_acceptorfd].insert(std::pair<int, Client *>(_fd, this));
	_ev.events = EPOLLIN;
	_ev.data.ptr = this;
}

Client::~Client(void)
{
	this->_client -= 1;
	_activeClient[this->_acceptorfd].erase(this->_fd);
}

void	Client::cleanRequest(void)
{
	_request.clear();
	_complete = false;
}

void	Client::cleanResponse(void)
{
	_response.clear();
	_bytes_send = 0;
}

void	Client::genResponse(void)
{
	if (_request.find("name") != std::string::npos)
		_response = "Shan";
	else if (_request.find("age") != std::string::npos)
		_response = "24";
	else if (_request.find("style") != std::string::npos)
		_response = "mec de cite simple";
	else
		_response = "error 404 request=" + _request;
	_bytes_send = 0;
	this->cleanRequest();
}
	
void	Client::handle_event(Epoll & manager)
{
	if (this->_ev.events & EPOLLIN)
	{
		char buffer[4096];
		int bytes_read = recv(this->_fd, buffer, sizeof(buffer), 0);
		if (bytes_read == -1)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			this->closeConnection(manager);
			return ;
		}
		if (bytes_read == 0)
		{
			this->closeConnection(manager);
			return ;
		}
		else if (bytes_read > 0)
		{
			_request.append(buffer, bytes_read);
			if (_request.find("\tfin\t") != std::string::npos)
                    		_complete = true;
		}
		if (_complete)
		{
			this->genResponse();
			_ev.events = EPOLLOUT;
			manager.mod(this);
		}
	}
	else if (this->_ev.events & EPOLLOUT)
	{
		const char*	data_to_send = _response.c_str() + _bytes_send;
		int 		bytes_left_to_send = _response.length() - _bytes_send;
		int		bytes_sent_now = send(this->_fd, data_to_send, bytes_left_to_send, 0);

		if (bytes_sent_now == -1)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			this->closeConnection(manager);
			return ;
		}
		_bytes_send += bytes_sent_now;
		if (_bytes_send == _response.length())
		{
			this->cleanResponse();
			this->_lastActivity = time(NULL);
			_ev.events = EPOLLIN;
			manager.mod(this);
		}
	}
}

bool	Client::getInactiveTime(time_t now) const
{
	if ((now - _lastActivity) >= 60)
		return true;
	return false;
}

void	Client::closeConnection(Epoll & manager)
{
	manager.del(this);
	close(this->_fd);
	delete this;
}
