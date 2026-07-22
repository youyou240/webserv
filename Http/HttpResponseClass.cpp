# include "HttpResponseClass.hpp"
# include <sstream>
# include <ctime>

static std::string	httpDate(void)
{
	char		buf[64];
	time_t		now = time(NULL);
	struct tm *	gmt = gmtime(&now);

	if (gmt == NULL || strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmt) == 0)
		return ("");
	return (std::string(buf));
}

HttpResponse::HttpResponse(void) : _code(200) {}

void	HttpResponse::setStatus(int code) {_code = code;}
int	HttpResponse::getCode(void) const {return (_code);}

void	HttpResponse::setHeader(std::string const & key, std::string const & value)
{
	_headers[key] = value;
}

void	HttpResponse::setBody(std::string const & body, std::string const & contentType)
{
	_body = body;
	_headers["Content-Type"] = contentType;
}

std::string	HttpResponse::serialize(void) const
{
	std::ostringstream	oss;

	oss << "HTTP/1.1 " << _code << " " << statusText(_code) << "\r\n";
	oss << "Server: webserv\r\n";
	oss << "Date: " << httpDate() << "\r\n";
	if (_code != 204)
		oss << "Content-Length: " << _body.size() << "\r\n";
	for (std::map<std::string, std::string>::const_iterator it = _headers.begin(); it != _headers.end(); ++it)
		oss << it->first << ": " << it->second << "\r\n";
	oss << "\r\n";
	if (_code != 204)
		oss << _body;
	return (oss.str());
}

std::string	HttpResponse::statusText(int code)
{
	switch (code)
	{
		case 200: return ("OK");
		case 201: return ("Created");
		case 204: return ("No Content");
		case 301: return ("Moved Permanently");
		case 302: return ("Found");
		case 400: return ("Bad Request");
		case 403: return ("Forbidden");
		case 404: return ("Not Found");
		case 405: return ("Method Not Allowed");
		case 408: return ("Request Timeout");
		case 411: return ("Length Required");
		case 413: return ("Payload Too Large");
		case 414: return ("URI Too Long");
		case 500: return ("Internal Server Error");
		case 501: return ("Not Implemented");
		case 504: return ("Gateway Timeout");
		case 505: return ("HTTP Version Not Supported");
	}
	return ("Unknown");
}

std::string	HttpResponse::defaultErrorPage(int code)
{
	std::ostringstream	oss;

	oss << "<html>\r\n<head><title>" << code << " " << statusText(code) << "</title></head>\r\n";
	oss << "<body>\r\n<center><h1>" << code << " " << statusText(code) << "</h1></center>\r\n";
	oss << "<hr><center>webserv</center>\r\n</body>\r\n</html>\r\n";
	return (oss.str());
}
