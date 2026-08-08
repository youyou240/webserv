*This project has been created as part of the 42 curriculum by shvalma, jrighi, alrighi.*

# webserv

An HTTP/1.1 server written from scratch in C++98, configured with an NGINX-inspired
configuration file.

## Description

`webserv` accepts connections on any number of `interface:port` endpoints, parses HTTP
requests incrementally, and answers them by serving static files, storing uploads,
deleting resources or running CGI programs — all from a **single I/O multiplexer**.

### Features

| Area | What is supported |
|---|---|
| Configuration | NGINX-like `server` blocks, `location` blocks, strict fail-fast parser reporting the offending line |
| Networking | Multiple `interface:port` endpoints, virtual hosts selected by the `Host` header, a socket shared by several virtual hosts |
| I/O model | One single `epoll` instance for every socket and pipe, all file descriptors non-blocking, read and write watched simultaneously |
| Methods | `GET`, `POST`, `DELETE` (any other method is answered `501`) |
| Static site | MIME type per extension, `index` resolution, directory listing (`autoindex`), HTTP redirections, custom `error_page` with a built-in fallback |
| Request bodies | `Content-Length` and `Transfer-Encoding: chunked` (de-chunked by the parser), `Expect: 100-continue`, `client_max_body_size` enforced per location |
| Uploads | `multipart/form-data` and raw bodies, written into the configured `upload_store` |
| CGI | Per-extension CGI, `GET` and `POST`, C (compiled binary), Python and PHP (interpreter given in the configuration), full CGI/1.1 environment, timeout and reaping |
| Bonus | Cookies and server-side sessions demonstrated by a CGI script |
| Robustness | Idle and per-request timeouts, slowloris protection, graceful handling of client disconnections, no leaked memory or file descriptors |

### How it works

The server follows the **Reactor** pattern. Every file descriptor registered in `epoll`
is an *actor* deriving from the abstract class `Arole`, which knows how to handle its own
event:

- `Acceptor` — a listening socket; accepts new connections and creates clients.
- `Client` — a connected client; reads the request, produces the response, writes it back.
- `CgiIn` / `CgiOut` — the two ends of the pipes talking to a CGI process.

The main loop does nothing but wait on `epoll` and dispatch each ready event to its actor.
Because the CGI pipes are actors too, running a CGI never blocks the server: other clients
keep being served while a script is still writing its output.

### Layout

```
main.cpp            entry point: parse the configuration, build the servers, run the loop
Server/             ServerManager (orchestrator), Server (one config block = one virtual host)
Server/socket/      Socket and Sockaddr (an endpoint and its address)
Server/AroleClass/  Arole (abstract actor), Acceptor, Client
Epoll/              the single I/O multiplexer
Config/             configuration lexer, parser and memory model (server / location)
Http/               incremental request parser and response builder
Router/             location matching, path mapping, static files, MIME types, uploads
Cgi/                CGI engine: fork, pipes, environment, output parsing, timeout
myzod/              input validation helpers
www/, www-b/        example websites, error pages and CGI scripts
configs/            example configuration files
tester/             test scripts
```

## Instructions

### Requirements

- A C++ compiler supporting `-std=c++98`
- `make`
- Linux — the server relies on `epoll`
- `python3` and `php-cgi` if you want to run the Python and PHP CGI examples

### Build

```sh
make
```

This produces the `Webserv` binary and compiles the C CGI example
(`www/cgi-bin/hello.cgi`). The build uses `-Wall -Wextra -Werror -std=c++98`.

Available rules: `all`, `clean`, `fclean`, `re`.

### Run

```sh
./Webserv [configuration file]
```

Without an argument, `configs/default.conf` is used.

**Start the server from the repository root**: the example configuration uses relative
paths (`root ./www`), which are resolved from the current working directory.

The example configuration exposes:

| Endpoint | Root | Notes |
|---|---|---|
| `localhost:8080` | `./www` | main site, uploads, CGI, a redirection |
| `site-b.local:8080` | `./www-b` | second virtual host on the same socket |
| `0.0.0.0:9090` | `./www` | same files, directory listing enabled |

Once running, open `http://localhost:8080/` in a browser: the index page is a dashboard
that exercises every feature and shows the status code expected for each one.

### Interactive menu

Pressing `Ctrl+C` does not kill the server, it opens a small management menu:

```
SHOW | START | STOP | EXIT
```

- `SHOW` — print the configured servers, the open endpoints, their virtual hosts and their number of clients
- `START` — open a new endpoint, or attach a virtual host to an existing one
- `STOP` — close an endpoint, or detach a virtual host from it
- `EXIT` — shut the server down

### Quick check

```sh
# static file, MIME type, and the custom 404 page
curl -i http://localhost:8080/
curl -i http://localhost:8080/style.css
curl -i http://localhost:8080/does-not-exist

# directory listing, upload, and deletion
curl -i http://localhost:8080/uploads/
curl -i -F "file=@Makefile" http://localhost:8080/uploads
curl -i -X DELETE http://localhost:8080/uploads/Makefile

# CGI in GET and POST
curl -i "http://localhost:8080/cgi-bin/hello.py?name=42"
curl -i -d "hello from a body" http://localhost:8080/cgi-bin/hello.py

# redirection, method not allowed, virtual host
curl -i http://localhost:8080/old
curl -i -X POST http://localhost:8080/
curl -i -H "Host: site-b.local" http://localhost:8080/
```

### Configuration

A configuration file is a list of `server` blocks, each containing `location` blocks.
Directives are terminated by `;`, blocks by `{ }`, and `#` starts a comment.

| Directive | Level | Meaning |
|---|---|---|
| `listen` | server | `port` or `host:port`, may be repeated |
| `server_name` | server | names used to select the virtual host from the `Host` header |
| `root` | server, location | directory the URL is mapped to |
| `index` | server, location | files tried when the target is a directory |
| `error_page` | server | custom page for one or more status codes |
| `client_max_body_size` | server, location | maximum request body size (`1m`, `512k`, ...) |
| `methods` | location | allowed HTTP methods; anything else answers `405` with an `Allow` header |
| `autoindex` | location | `on` or `off`, directory listing |
| `return` | location | status code and target of an HTTP redirection |
| `upload_store` | location | destination directory; its presence enables uploads |
| `cgi` | location | file extension, plus an optional interpreter path |

Locations inherit from their server block, and anything unknown or misplaced is a fatal
error reported with its line number. A commented example is available in
`configs/default.conf`.

### Testing

The `tester/` directory contains Python 3 scripts (standard library only) used during
development:

| Script | Purpose |
|---|---|
| `mine_functional.py` | status codes, headers and bodies of every feature |
| `mine_robustness.py` | malformed input, slowloris, server stays available |
| `mine_security.py` | traversal, smuggling, overflow, XSS in the directory listing |
| `mine_cgi_stress.py` | concurrent CGI, timeouts, large outputs, large bodies |
| `mine_fuzz.py` | mutation fuzzing of the request parser |
| `mine_stress.py` | concurrent load and file descriptor usage |
| `mine_leakcheck.sh` | valgrind wrapper for memory and descriptor leaks |

Start the server, then run a script against it:

```sh
python3 tester/mine_functional.py
```

## Resources

### Specifications

- RFC 7230 — HTTP/1.1: Message Syntax and Routing
- RFC 7231 — HTTP/1.1: Semantics and Content
- RFC 3875 — The Common Gateway Interface (CGI) Version 1.1
- RFC 7578 and RFC 2046 — `multipart/form-data` and MIME multipart bodies
- RFC 6265 — HTTP State Management Mechanism (cookies)

### Documentation and references

- NGINX documentation and observed behaviour, used as the reference implementation whenever the RFCs left a choice open
- Manual pages: `epoll`, `socket`, `bind`, `listen`, `accept`, `fcntl`, `pipe`, `fork`, `execve`, `waitpid`, `stat`, `opendir`
- Beej's Guide to Network Programming
- MDN Web Docs, for HTTP headers and browser-side behaviour

### Use of AI

An AI assistant (Claude, by Anthropic) was used to write this README file. Everything
else — the server implementation, the configuration parser, the router, the CGI engine,
the example website and the test scripts — was written by the team.
