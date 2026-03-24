# include "AroleClass.hpp"

Client(int fd) : Arole(fd), _bytes_send(0), _complete(false), _lastActivity(time(NULL))
{
	this->_client += 1;
	_activeClient.push_back(*this);
}

~Client(void)
{
	this->_client -= 1;
	for(std::vector<Client &>::iterator it = _activeClient.begin(); it != _activeClient.end(); ++it) {
		if (*it == this)
			_activeClient.erase(it);
	}
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
		_repone = "error 404 request=" + _request;
	_bytes_send = 0;
	this->cleanRequest();
}
	
void	Client::handle_event(int event, Epoll & manager)
{
	if (event & EPOLLIN)
	{
		char buffer[4096];
		int bytes_read = recv(this->fd, buffer, sizeof(buffer), 0);
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
			_ev.data.ptr = this;
			manager.mod(this->fd, &_ev);
		}
	}
	else if (event & EPOLLOUT)
	{
		const char*	data_to_send = _response.c_str() + _bytes_send;
		int		bytes_sent_now = send(this->fd, data_to_send, bytes_left_to_send, 0);

		if (bytes_sent_now == -1)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return ;
			this->closeConnection(manager);
			return ;
		}
		_bytes_send += bytes_sent_now;
		if (_bytes_sent == _response.length())
		{
			this->cleanResponse();
			this->_lastActivity = time(NULL);
			_ev.events = EPOLLIN;
			_ev.data.ptr = this;
			manager.mod(this->fd, &_ev);
		}
	}
}
