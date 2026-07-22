# include "HttpRequestClass.hpp"

static std::string	toLowerCopy(std::string const & s)
{
	std::string	out(s);

	for (size_t i = 0; i < out.size(); ++i)
		if (out[i] >= 'A' && out[i] <= 'Z')
			out[i] = out[i] - 'A' + 'a';
	return (out);
}

static std::string	trimCopy(std::string const & s)
{
	size_t	start = 0;
	size_t	end = s.size();

	while (start < end && (s[start] == ' ' || s[start] == '\t'))
		++start;
	while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t'))
		--end;
	return (s.substr(start, end - start));
}

static int	hexVal(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

static bool	parseSizeValue(std::string const & s, size_t & out)
{
	out = 0;
	if (s.empty())
		return (false);
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] < '0' || s[i] > '9')
			return (false);
		size_t	digit = s[i] - '0';
		if (out > ((size_t)-1 - digit) / 10)
			return (false);
		out = out * 10 + digit;
	}
	return (true);
}

static bool	parseHexValue(std::string const & s, size_t & out)
{
	out = 0;
	if (s.empty())
		return (false);
	for (size_t i = 0; i < s.size(); ++i)
	{
		int	v = hexVal(s[i]);

		if (v < 0)
			return (false);
		if (out > ((size_t)-1 - v) / 16)
			return (false);
		out = out * 16 + v;
	}
	return (true);
}

static bool	percentDecode(std::string const & in, std::string & out)
{
	for (size_t i = 0; i < in.size(); ++i)
	{
		if (in[i] == '%')
		{
			if (i + 2 >= in.size())
				return (false);
			int	hi = hexVal(in[i + 1]);
			int	lo = hexVal(in[i + 2]);
			if (hi < 0 || lo < 0)
				return (false);
			out += static_cast<char>(hi * 16 + lo);
			i += 2;
		}
		else
			out += in[i];
	}
	return (true);
}

HttpRequest::HttpRequest(void) : _state(S_REQUEST_LINE), _contentLength(0), _chunked(false), _chunkSize(0), _maxBodySize(NO_BODY_LIMIT), _headerBytes(0), _headersParsed(false), _errorCode(0) {}

void	HttpRequest::reset(void)
{
	*this = HttpRequest();
}

void	HttpRequest::fail(int code)
{
	_state = S_ERROR;
	_errorCode = code;
}

void	HttpRequest::append(char const * data, size_t len)
{
	if (_state == S_COMPLETE || _state == S_ERROR)
		return ;
	_buffer.append(data, len);
	bool	progress = true;
	while (progress && _state != S_COMPLETE && _state != S_ERROR)
	{
		if (_state == S_REQUEST_LINE)
			progress = this->stepRequestLine();
		else if (_state == S_HEADERS)
			progress = this->stepHeaders();
		else if (_state == S_BODY)
			progress = this->stepBody();
		else if (_state == S_CHUNK_SIZE)
			progress = this->stepChunkSize();
		else if (_state == S_CHUNK_DATA)
			progress = this->stepChunkData();
		else if (_state == S_CHUNK_CRLF)
			progress = this->stepChunkCrlf();
		else if (_state == S_TRAILER)
			progress = this->stepTrailer();
	}
}

void	HttpRequest::setMaxBodySize(size_t limit)
{
	_maxBodySize = limit;
	if (_state == S_ERROR)
		return ;
	if (_headersParsed && !_chunked && _contentLength > limit)
	{
		this->fail(413);
		return ;
	}
	if (_body.size() > limit)
		this->fail(413);
}

bool	HttpRequest::getLine(std::string & line)
{
	size_t	pos = _buffer.find('\n');

	if (pos == std::string::npos)
		return (false);
	line = _buffer.substr(0, pos);
	_buffer.erase(0, pos + 1);
	if (!line.empty() && line[line.size() - 1] == '\r')
		line.erase(line.size() - 1);
	return (true);
}

bool	HttpRequest::stepRequestLine(void)
{
	std::string	line;

	if (!this->getLine(line))
	{
		if (_buffer.size() > MAX_REQUEST_LINE)
			this->fail(414);
		return (false);
	}
	if (line.empty())
		return (true);
	if (line.size() > MAX_REQUEST_LINE)
	{
		this->fail(414);
		return (false);
	}
	this->parseRequestLine(line);
	if (_state != S_ERROR)
		_state = S_HEADERS;
	return (true);
}

void	HttpRequest::parseRequestLine(std::string const & line)
{
	size_t	sp1 = line.find(' ');
	size_t	sp2 = (sp1 == std::string::npos) ? std::string::npos : line.find(' ', sp1 + 1);

	if (sp1 == std::string::npos || sp2 == std::string::npos || line.find(' ', sp2 + 1) != std::string::npos)
	{
		this->fail(400);
		return ;
	}
	_method = line.substr(0, sp1);
	_uri = line.substr(sp1 + 1, sp2 - sp1 - 1);
	_version = line.substr(sp2 + 1);
	if (_method.empty() || _uri.empty() || _version.empty())
	{
		this->fail(400);
		return ;
	}
	for (size_t i = 0; i < _method.size(); ++i)
	{
		if (_method[i] < 'A' || _method[i] > 'Z')
		{
			this->fail(400);
			return ;
		}
	}
	for (size_t i = 0; i < _uri.size(); ++i)
	{
		if (static_cast<unsigned char>(_uri[i]) < 32 || _uri[i] == 127)
		{
			this->fail(400);
			return ;
		}
	}
	if (_version != "HTTP/1.1" && _version != "HTTP/1.0")
	{
		this->fail(505);
		return ;
	}
	if (_method != "GET" && _method != "POST" && _method != "DELETE")
	{
		this->fail(501);
		return ;
	}
	if (_uri[0] != '/')
	{
		this->fail(400);
		return ;
	}
	size_t		q = _uri.find('?');
	std::string	rawPath = (q == std::string::npos) ? _uri : _uri.substr(0, q);

	_query = (q == std::string::npos) ? "" : _uri.substr(q + 1);
	_path.clear();
	if (!percentDecode(rawPath, _path))
	{
		this->fail(400);
		return ;
	}
	for (size_t i = 0; i < _path.size(); ++i)
	{
		if (static_cast<unsigned char>(_path[i]) < 32 || _path[i] == 127)
		{
			this->fail(400);
			return ;
		}
	}
}

bool	HttpRequest::stepHeaders(void)
{
	std::string	line;

	if (!this->getLine(line))
	{
		if (_buffer.size() > MAX_HEADER_BYTES)
			this->fail(400);
		return (false);
	}
	_headerBytes += line.size() + 2;
	if (_headerBytes > MAX_HEADER_BYTES)
	{
		this->fail(400);
		return (false);
	}
	if (line.empty())
	{
		this->finishHeaders();
		return (true);
	}
	this->parseHeaderLine(line);
	return (true);
}

void	HttpRequest::parseHeaderLine(std::string const & line)
{
	size_t	colon = line.find(':');

	if (colon == 0 || colon == std::string::npos)
	{
		this->fail(400);
		return ;
	}
	std::string	key = line.substr(0, colon);
	std::string	value = trimCopy(line.substr(colon + 1));

	for (size_t i = 0; i < key.size(); ++i)
	{
		if (static_cast<unsigned char>(key[i]) <= 32 || key[i] == 127)
		{
			this->fail(400);
			return ;
		}
	}
	for (size_t i = 0; i < value.size(); ++i)
	{
		if ((static_cast<unsigned char>(value[i]) < 32 && value[i] != '\t') || value[i] == 127)
		{
			this->fail(400);
			return ;
		}
	}
	key = toLowerCopy(key);
	std::map<std::string, std::string>::iterator	it = _headers.find(key);

	if (it == _headers.end())
		_headers[key] = value;
	else if (key == "host" || key == "content-length" || key == "transfer-encoding")
		this->fail(400);
	else
		it->second += ", " + value;
}

void	HttpRequest::finishHeaders(void)
{
	_headersParsed = true;
	if (_version == "HTTP/1.1" && !this->hasHeader("host"))
	{
		this->fail(400);
		return ;
	}
	if (this->hasHeader("transfer-encoding"))
	{
		if (toLowerCopy(this->getHeader("transfer-encoding")) != "chunked")
		{
			this->fail(501);
			return ;
		}
		if (this->hasHeader("content-length"))
		{
			this->fail(400);
			return ;
		}
		_chunked = true;
		_state = S_CHUNK_SIZE;
		return ;
	}
	if (this->hasHeader("content-length"))
	{
		if (!parseSizeValue(this->getHeader("content-length"), _contentLength))
		{
			this->fail(400);
			return ;
		}
		if (_contentLength > _maxBodySize)
		{
			this->fail(413);
			return ;
		}
		if (_contentLength == 0)
		{
			_state = S_COMPLETE;
			return ;
		}
		_state = S_BODY;
		return ;
	}
	_state = S_COMPLETE;
}

bool	HttpRequest::stepBody(void)
{
	if (_buffer.empty())
		return (false);
	size_t	need = _contentLength - _body.size();
	size_t	take = _buffer.size() < need ? _buffer.size() : need;

	_body.append(_buffer, 0, take);
	_buffer.erase(0, take);
	if (_body.size() == _contentLength)
		_state = S_COMPLETE;
	return (true);
}

bool	HttpRequest::stepChunkSize(void)
{
	std::string	line;

	if (!this->getLine(line))
	{
		if (_buffer.size() > MAX_REQUEST_LINE)
			this->fail(400);
		return (false);
	}
	size_t		semi = line.find(';');
	std::string	hex = trimCopy(semi == std::string::npos ? line : line.substr(0, semi));

	if (!parseHexValue(hex, _chunkSize))
	{
		this->fail(400);
		return (false);
	}
	if (_chunkSize == 0)
	{
		_state = S_TRAILER;
		return (true);
	}
	if (_body.size() + _chunkSize < _body.size() || _body.size() + _chunkSize > _maxBodySize)
	{
		this->fail(413);
		return (false);
	}
	_state = S_CHUNK_DATA;
	return (true);
}

bool	HttpRequest::stepChunkData(void)
{
	if (_buffer.empty())
		return (false);
	size_t	take = _buffer.size() < _chunkSize ? _buffer.size() : _chunkSize;

	_body.append(_buffer, 0, take);
	_buffer.erase(0, take);
	_chunkSize -= take;
	if (_chunkSize == 0)
	{
		_state = S_CHUNK_CRLF;
		return (true);
	}
	return (false);
}

bool	HttpRequest::stepChunkCrlf(void)
{
	if (_buffer.empty())
		return (false);
	if (_buffer[0] == '\n')
	{
		_buffer.erase(0, 1);
		_state = S_CHUNK_SIZE;
		return (true);
	}
	if (_buffer[0] == '\r')
	{
		if (_buffer.size() < 2)
			return (false);
		if (_buffer[1] != '\n')
		{
			this->fail(400);
			return (false);
		}
		_buffer.erase(0, 2);
		_state = S_CHUNK_SIZE;
		return (true);
	}
	this->fail(400);
	return (false);
}

bool	HttpRequest::stepTrailer(void)
{
	std::string	line;

	if (!this->getLine(line))
	{
		if (_buffer.size() > MAX_HEADER_BYTES)
			this->fail(400);
		return (false);
	}
	if (line.empty())
		_state = S_COMPLETE;
	return (true);
}

bool	HttpRequest::isComplete(void) const {return (_state == S_COMPLETE);}
bool	HttpRequest::isError(void) const {return (_state == S_ERROR);}
bool	HttpRequest::headersDone(void) const {return (_headersParsed);}
int	HttpRequest::getErrorCode(void) const {return (_errorCode);}

bool	HttpRequest::isKeepAlive(void) const
{
	std::string	con = toLowerCopy(this->getHeader("connection"));

	if (_version == "HTTP/1.0")
		return (con == "keep-alive");
	return (con != "close");
}

std::string const &	HttpRequest::getMethod(void) const {return (_method);}
std::string const &	HttpRequest::getUri(void) const {return (_uri);}
std::string const &	HttpRequest::getPath(void) const {return (_path);}
std::string const &	HttpRequest::getQuery(void) const {return (_query);}
std::string const &	HttpRequest::getVersion(void) const {return (_version);}
std::string const &	HttpRequest::getBody(void) const {return (_body);}
std::map<std::string, std::string> const &	HttpRequest::getHeaders(void) const {return (_headers);}

std::string	HttpRequest::getHeader(std::string const & key) const
{
	std::map<std::string, std::string>::const_iterator	it = _headers.find(toLowerCopy(key));

	if (it == _headers.end())
		return ("");
	return (it->second);
}

bool	HttpRequest::hasHeader(std::string const & key) const
{
	return (_headers.find(toLowerCopy(key)) != _headers.end());
}
