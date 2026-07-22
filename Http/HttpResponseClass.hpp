# pragma once

# include <string>
# include <map>

class HttpResponse {

	int					_code;
	std::map<std::string, std::string>	_headers;
	std::string				_body;

public:

	HttpResponse(void);

	void	setStatus(int code);
	void	setHeader(std::string const & key, std::string const & value);
	void	setBody(std::string const & body, std::string const & contentType);

	int	getCode(void) const;

	std::string	serialize(void) const;

	static std::string	statusText(int code);
	static std::string	defaultErrorPage(int code);
};
