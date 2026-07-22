# pragma once

# include <string>
# include <map>
# include <cstddef>

# define MAX_REQUEST_LINE 8192
# define MAX_HEADER_BYTES 32768
# define NO_BODY_LIMIT ((size_t)-1)

class HttpRequest {

public:

	enum State {
		S_REQUEST_LINE,
		S_HEADERS,
		S_BODY,
		S_CHUNK_SIZE,
		S_CHUNK_DATA,
		S_CHUNK_CRLF,
		S_TRAILER,
		S_COMPLETE,
		S_ERROR
	};

private:

	State					_state;
	std::string				_buffer;
	std::string				_method;
	std::string				_uri;
	std::string				_path;
	std::string				_query;
	std::string				_version;
	std::map<std::string, std::string>	_headers;
	std::string				_body;
	size_t					_contentLength;
	bool					_chunked;
	size_t					_chunkSize;
	size_t					_maxBodySize;
	size_t					_headerBytes;
	bool					_headersParsed;
	int					_errorCode;

	bool	getLine(std::string & line);
	void	fail(int code);
	void	parseRequestLine(std::string const & line);
	void	parseHeaderLine(std::string const & line);
	void	finishHeaders(void);
	bool	stepRequestLine(void);
	bool	stepHeaders(void);
	bool	stepBody(void);
	bool	stepChunkSize(void);
	bool	stepChunkData(void);
	bool	stepChunkCrlf(void);
	bool	stepTrailer(void);

public:

	HttpRequest(void);

	void	append(char const * data, size_t len);
	void	setMaxBodySize(size_t limit);
	void	reset(void);

	bool	isComplete(void) const;
	bool	isError(void) const;
	bool	headersDone(void) const;
	bool	isKeepAlive(void) const;
	int	getErrorCode(void) const;

	std::string const &	getMethod(void) const;
	std::string const &	getUri(void) const;
	std::string const &	getPath(void) const;
	std::string const &	getQuery(void) const;
	std::string const &	getVersion(void) const;
	std::string const &	getBody(void) const;
	std::map<std::string, std::string> const &	getHeaders(void) const;
	std::string	getHeader(std::string const & key) const;
	bool	hasHeader(std::string const & key) const;
};
