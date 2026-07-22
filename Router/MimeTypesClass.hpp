# pragma once

# include <string>

class MimeTypes {

	MimeTypes(void);

public:

	static std::string	forPath(std::string const & path);
};
