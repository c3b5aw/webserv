
# webserv

School 42 Project - Build HTTP/1.1 resilient server.

**Score:** 125/100


## Authors

- [@c3b5aw](https://www.github.com/c3b5aw)
- [@gmarcha](https://www.github.com/gmarcha)


## Build status

[![Unit Test](https://github.com/c3b5aw/webserv/actions/workflows/unit_test.yml/badge.svg?branch=main)](https://github.com/c3b5aw/webserv/actions/workflows/unit_test.yml)
[![Linting](https://github.com/c3b5aw/webserv/actions/workflows/cpplint.yml/badge.svg?branch=main)](https://github.com/c3b5aw/webserv/actions/workflows/cpplint.yml)
[![Docs](https://github.com/c3b5aw/webserv/actions/workflows/docs.yml/badge.svg?branch=main)](https://github.com/c3b5aw/webserv/actions/workflows/docs.yml)
[![Stress Test](https://github.com/c3b5aw/webserv/actions/workflows/stress_test.yml/badge.svg?branch=main)](https://github.com/c3b5aw/webserv/actions/workflows/stress_test.yml)

## Tech Stack

**Core:** C++ / C

**Tests:** Python


## Features
- HTTP/1.1 Support
- 100.00% availability using epoll()
- Support GET, POST, DELETE
- Mimic official HTTP responses
- Listen multiple ports
- VHosts
- Fully configurable (view https://github.com/c3b5aw/webserv/blob/config/docs/config_file.md)
- Support Cookies and Session
- Support CGI

## Sessions
```
// Enabled by default

make SESSION=disable
```

## Optimizations

```
make MODE=benchmark
make MODE=benchmark SESSION=disable
```
## Running Tests

To run tests, run the following command

```bash
make tests
```


## Usefull Links

- https://www.nginx.com/resources/wiki/start/topics/examples/full/
- https://developer.mozilla.org/en-US/docs/Web/HTTP
